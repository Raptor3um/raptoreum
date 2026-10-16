#!/usr/bin/env python3
"""Reproduce the final-data-plus-FIN race fixed alongside the busy-loop.

Self-contained: standard library only, no test framework, no build flags.

BACKGROUND

SocketRecvData() used to clear a node's receive-readiness on any read shorter
than its 64K buffer -- the normal case for almost every real read, not
evidence the peer is done. Only a 0 return (closed) or EWOULDBLOCK (drained
for now) actually means that.

Under an edge-triggered backend -- kqueue with EV_CLEAR specifically -- a
peer that sends its last bytes and closes at essentially the same moment can
get read once, have its readiness cleared by that old heuristic, and then
never get asked again. The terminating recv()==0 that would actually close
the connection never runs; the connection lingers in getpeerinfo until
something unrelated notices (an outbound send drawing an RST, or the
inactivity timeout), rather than closing when the peer actually said
goodbye, which could be minutes.

This script never reproduced the lingering connection, on Linux or on real
macOS/arm64 hardware, across five separate live attempts (with the heuristic
reverted alone, and with it and the kqueue EV_EOF-routing fix below both
reverted together) -- not because the bug isn't real, but because sendall()
immediately followed by close() leaves the OS free to transmit the final
data and the FIN as two separate segments, and a second, independent
notification covering the FIN gives SocketRecvData() another chance
regardless of any of these fixes. Root-caused by example, not by guessing:
repro-socket-eof-race.c, a lower-level companion using TCP_NOPUSH/TCP_CORK
to force the final write and the close into one segment instead of leaving
it to transmit-scheduling luck, reproduced a clean, consistent ~3.3s
lingering close on Linux with just the heuristic reverted (0.0s with it
fixed) across three repeated runs each way. Use that one for an actual
before/after comparison; this script is kept for the simpler setup
(no C toolchain needed) where forcing the exact race isn't the point.

A second, kqueue-only bug lived alongside the heuristic one: read-side
EV_EOF was routed to error_set, which only called SocketRecvData() once for
that event regardless of the heuristic -- fixed by routing it to recv_set
instead, like any other readable event. This script cannot isolate that half
from the heuristic half; both are kqueue questions this run cannot answer.

HOW TO RUN

  raptoreumd -regtest -datadir=/tmp/eofrace -listen=1 -port=19941
  contrib/devtools/repro-socket-eof-race.py --port 19941 \
      --cli /path/to/raptoreum-cli --datadir /tmp/eofrace

Run it against an otherwise idle node -- this script tells peers apart by
counting getpeerinfo, not by identity, so another connection coming or going
during the run would read as a false result.

WHAT TO LOOK FOR

  peer still listed after --seconds   -> bug present (lingering close)
  peer gone within about one --poll   -> fixed

This is a real race, not a guaranteed one: whether the final bytes and the
FIN land in the same TCP segment -- the exact condition the fix addresses --
is up to the OS's own send coalescing, not something this script controls
directly. Sending one small message and closing immediately after, with no
read, delay or explicit flush in between, gives the kernel its best chance to
pack them together on loopback. The buggy case doesn't need to be the common
case to matter -- an occasional few-minutes-long stuck connection is still a
real cost -- so if the first run doesn't show the gap, running it again is
worth it before concluding the fix isn't doing anything.
"""
import argparse
import hashlib
import json
import socket
import struct
import subprocess
import sys
import time

MAGIC = {"regtest": b"\xfc\xc1\xb7\xdc", "main": b"\x72\x74\x6d\x2e", "test": b"\x74\x72\x74\x6d"}


def frame(command, payload, net="regtest"):
    checksum = hashlib.sha256(hashlib.sha256(payload).digest()).digest()[:4]
    return MAGIC[net] + command.ljust(12, b"\x00") + struct.pack("<I", len(payload)) + checksum + payload


def version_payload():
    return (struct.pack("<iQq", 70220, 0, int(time.time()))
            + b"\x00" * 26 + b"\x00" * 26
            + struct.pack("<Q", 0) + b"\x00" + struct.pack("<i", 0) + b"\x00")


NET_FLAG = {"regtest": "-regtest", "test": "-testnet", "main": None}


def peer_count(cli, datadir, net):
    cmd = [cli]
    net_flag = NET_FLAG[net]
    if net_flag:
        cmd.append(net_flag)
    cmd += ["-datadir=%s" % datadir, "getpeerinfo"]
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
    if out.returncode != 0:
        sys.exit("getpeerinfo failed: %s" % (out.stderr.strip() or out.stdout.strip()))
    return len(json.loads(out.stdout))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--cli", required=True, help="path to raptoreum-cli")
    ap.add_argument("--datadir", required=True, help="the node's datadir, for -datadir on the cli")
    ap.add_argument("--net", default="regtest", choices=sorted(MAGIC))
    ap.add_argument("--seconds", type=float, default=15.0, help="how long to keep polling after closing")
    ap.add_argument("--poll", type=float, default=0.5, help="poll interval in seconds")
    a = ap.parse_args()

    before = peer_count(a.cli, a.datadir, a.net)

    sock = socket.create_connection((a.host, a.port), timeout=20)
    sock.sendall(frame(b"version", version_payload(), a.net))
    reply = sock.recv(65536)
    if not reply:
        sys.exit("handshake failed: peer closed the connection before replying to "
                  "version (wrong --net for this node?)")
    sock.sendall(frame(b"verack", b"", a.net))

    after_handshake = peer_count(a.cli, a.datadir, a.net)
    if after_handshake <= before:
        sys.exit("peer count did not increase after handshake (%d -> %d) -- "
                  "connection never registered, this run tested nothing" % (before, after_handshake))

    # The node sends its own unprompted post-handshake messages (sendheaders,
    # sendcmpct, senddsq, a ping) within the same instant as verack, and this
    # script never reads any of them. Closing a socket that still has that
    # unread data sitting in its own receive buffer sends an RST, not a FIN
    # -- recv() on the node's side then fails outright (ECONNRESET), which
    # disconnects unconditionally regardless of the heuristic this script
    # means to test. Draining our own side until it goes quiet first makes
    # the eventual close a real, clean FIN instead.
    sock.settimeout(1.0)
    try:
        while True:
            if not sock.recv(65536):
                sys.exit("node closed the connection while draining -- nothing left to test")
    except socket.timeout:
        pass
    sock.settimeout(20)

    # The actual race: one small valid message that gets no reply (so
    # nothing is left for the node to write back to us), then close
    # immediately -- no further read, no delay, no explicit flush -- giving
    # the OS its best shot at coalescing the final bytes and the FIN into
    # one segment. sendheaders is a one-way preference announcement in both
    # directions; the node never replies to it.
    sock.sendall(frame(b"sendheaders", b"", a.net))
    sock.close()

    t0 = time.time()
    gone_at = None
    while time.time() - t0 < a.seconds:
        if peer_count(a.cli, a.datadir, a.net) < after_handshake:
            gone_at = time.time() - t0
            break
        time.sleep(a.poll)

    if gone_at is None:
        print("peer still listed %.1fs after close -> bug present (lingering close)" % a.seconds)
        return 1
    print("peer gone %.2fs after close -> fixed" % gone_at)
    return 0


if __name__ == "__main__":
    sys.exit(main())
