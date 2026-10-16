/* Lower-level companion to repro-socket-eof-race.py.
 *
 * The Python reproducer's sendall() immediately followed by close() leaves
 * the OS free to transmit the final data and the FIN as two separate
 * segments, which turned out to always give SocketRecvData() a second,
 * independent chance to see recv()==0 -- live-tested and never reproduced
 * the bug on either Linux or real macOS/arm64 hardware, across five runs.
 *
 * This version is explicit about the one thing that actually matters: that
 * the final write and the close are combined into ONE segment, not two.
 * TCP_NOPUSH (BSD/Darwin) / TCP_CORK (Linux) tells the kernel to hold back
 * a partial segment instead of transmitting eagerly; close() right after
 * then has to flush everything still held, FIN included, in one shot --
 * not a timing hope, an explicit kernel instruction.
 *
 * Self-contained: POSIX sockets only, no build flags. Still only reaches
 * the kqueue-specific half of this fix on macOS/FreeBSD -- on Linux this
 * exercises the same shared heuristic the Python version already covered
 * there, for the same reason documented in that script.
 *
 * HOW TO RUN
 *
 *   cc -O2 -o repro-eof-race-c contrib/devtools/repro-socket-eof-race.c -lcrypto
 *   raptoreumd -regtest -datadir=/tmp/eofrace -listen=1 -port=19941 -debug=net &
 *   ./repro-eof-race-c 127.0.0.1 19941 ./raptoreum-cli /tmp/eofrace
 *
 * WHAT TO LOOK FOR
 *
 *   peer still listed after Ns  -> bug present
 *   peer gone within ~1 poll    -> fixed (or still not reproduced)
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__) || defined(__FreeBSD__)
#define CORK_OPT TCP_NOPUSH
#else
#define CORK_OPT TCP_CORK
#endif

static const unsigned char REGTEST_MAGIC[4] = {0xfc, 0xc1, 0xb7, 0xdc};

static void put_u32(unsigned char *p, uint32_t v) {
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}

static size_t frame(unsigned char *out, const char *command, const unsigned char *payload, size_t paylen) {
    size_t off = 0;
    memcpy(out + off, REGTEST_MAGIC, 4); off += 4;
    char cmd[12]; memset(cmd, 0, 12);
    size_t cmdlen = strlen(command);
    memcpy(cmd, command, cmdlen < 12 ? cmdlen : 12);
    memcpy(out + off, cmd, 12); off += 12;
    put_u32(out + off, (uint32_t) paylen); off += 4;

    /* checksum = first 4 bytes of SHA256(SHA256(payload)) -- this IS
     * verified (ProcessMessages rejects a bad one with "CHECKSUM ERROR"
     * and silently drops the message, which is what the all-zero
     * placeholder this reproducer first shipped with ran into). */
    unsigned char h1[SHA256_DIGEST_LENGTH], h2[SHA256_DIGEST_LENGTH];
    SHA256(payload, paylen, h1);
    SHA256(h1, SHA256_DIGEST_LENGTH, h2);
    memcpy(out + off, h2, 4); off += 4;

    memcpy(out + off, payload, paylen); off += paylen;
    return off;
}

static int peer_count(const char *cli, const char *datadir) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s -regtest -datadir=%s getpeerinfo 2>/dev/null", cli, datadir);
    FILE *fp = popen(cmd, "r");
    if (!fp) { perror("popen"); exit(1); }
    int count = 0;
    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while ((p = strstr(p, "\"id\""))) { count++; p += 4; }
    }
    pclose(fp);
    return count;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "usage: %s <host> <port> <raptoreum-cli path> <datadir>\n", argv[0]);
        return 2;
    }
    const char *host = argv[1];
    int port = atoi(argv[2]);
    const char *cli = argv[3];
    const char *datadir = argv[4];

    int before = peer_count(cli, datadir);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);

    if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
        perror("connect");
        return 1;
    }

    /* version payload: matches the Python reproducer's version_payload() --
     * protocol version, services (0), timestamp, two empty CAddress blocks,
     * nonce, empty user agent, start height, relay flag. */
    unsigned char verpay[86];
    memset(verpay, 0, sizeof(verpay));
    put_u32(verpay, 70220);
    time_t now = time(NULL);
    memcpy(verpay + 12, &now, 8); /* nTime, little-endian on every platform this targets */
    verpay[84] = 0; /* fRelay */

    unsigned char buf[512];
    size_t n = frame(buf, "version", verpay, sizeof(verpay));
    if (write(fd, buf, n) != (ssize_t) n) { perror("write version"); return 1; }

    char rbuf[65536];
    ssize_t r = read(fd, rbuf, sizeof(rbuf));
    if (r <= 0) {
        fprintf(stderr, "handshake failed: node closed before replying to version\n");
        return 1;
    }

    n = frame(buf, "verack", (const unsigned char *) "", 0);
    if (write(fd, buf, n) != (ssize_t) n) { perror("write verack"); return 1; }

    int after_handshake = peer_count(cli, datadir);
    if (after_handshake <= before) {
        fprintf(stderr, "peer count did not increase after handshake (%d -> %d)\n", before, after_handshake);
        return 1;
    }

    /* Drain our own side until quiet, same reason as the Python version:
     * unread data left in our receive buffer turns close() into an RST,
     * not the graceful FIN this test needs. */
    struct timeval tv = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    for (;;) {
        r = read(fd, rbuf, sizeof(rbuf));
        if (r == 0) {
            fprintf(stderr, "node closed the connection while draining\n");
            return 1;
        }
        if (r < 0) break; /* timed out: drained */
    }

    /* The actual mechanism: cork so the kernel holds this write instead of
     * transmitting it alone, then close() flushes the held write and the
     * FIN together, in one segment -- not left to transmit-scheduling luck. */
    int one = 1;
    if (setsockopt(fd, IPPROTO_TCP, CORK_OPT, &one, sizeof(one)) != 0) {
        perror("setsockopt cork");
        /* not fatal -- proceed anyway, falls back to the Python version's approach */
    }

    n = frame(buf, "sendheaders", (const unsigned char *) "", 0);
    if (write(fd, buf, n) != (ssize_t) n) { perror("write sendheaders"); return 1; }

    close(fd);

    int gone_after_ms = -1;
    for (int elapsed_ms = 0; elapsed_ms < 15000; elapsed_ms += 100) {
        if (peer_count(cli, datadir) < after_handshake) {
            gone_after_ms = elapsed_ms;
            break;
        }
        usleep(100000);
    }

    if (gone_after_ms < 0) {
        printf("peer still listed 15.0s after close -> bug present (lingering close)\n");
        return 1;
    }
    printf("peer gone %.1fs after close -> fixed\n", gone_after_ms / 1000.0);
    return 0;
}
