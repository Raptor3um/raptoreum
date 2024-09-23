// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c)      2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php

#include <shutdown.h>

#include <config/raptoreum-config.h>

#include <assert.h>
#include <atomic>

#ifdef WIN32
#include <condition_variable>
#else

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#endif

static std::atomic<bool> fRequestShutdown{false};
static std::atomic<bool> fRequestRestart{false};
#ifdef WIN32
/** On windows it is possible to simply use a condition variable. */
std::mutex g_shutdown_mutex;
std::condition_variable g_shutdown_cv;
#else
/** On UNIX-like operating systems use the self-pipe trick.
 * Index 0 will be the read end of the pipe, index 1 the write end.
 */
static int g_shutdown_pipe[2] = {-1, -1};
#endif

bool InitShutdownState() {
#ifndef WIN32
#if HAVE_O_CLOEXEC && HAVE_DECL_PIPE2
    // If we can, make sure that the file descriptors are closed on exec()
    // to prevent interference.
  if (pipe2(g_shutdown_pipe, O_CLOEXEC) != 0) {
    return false;
  }
#else
    if (pipe(g_shutdown_pipe) != 0) {
        return false;
    }
#endif
#endif
    return true;
}

void StartShutdown() {
#ifdef WIN32
    std::unique_lock<std::mutex> lk(g_shutdown_mutex);
      fRequestShutdown = true;
      g_shutdown_cv.notify_one();
#else
    if (!fRequestShutdown.exchange(true)) {
        const char token = 'x';
        while (true) {
            int result = write(g_shutdown_pipe[1], &token, 1);
            if (result < 0) {
                assert(errno == EINTR);
            } else {
                assert(result == 1);
                break;
            }
        }
    }
#endif
}

void StartRestart() {
    fRequestShutdown = fRequestRestart = true;
}

void AbortShutdown() {
    if (fRequestShutdown) {
        WaitForShutdown();
    }
    fRequestShutdown = false;
}

bool ShutdownRequested() {
    return fRequestShutdown;
}

void WaitForShutdown() {
#ifdef WIN32
    std::unique_lock<std::mutex> lk(g_shutdown_mutex);
    g_shutdown_cv.wait(lk, [] { return fRequestShutdown.load(); });
#else
    char token;
    while (true) {
        int result = read(g_shutdown_pipe[0], &token, 1);
        if (result < 0) {
            // Failure. Check if the read was interrupted by a signal.
            // Other errors are unexpected here.
            assert(errno == EINTR);
        } else {
            assert(result == 1);
            break;
        }
    }
#endif
}

bool RestartRequested() {
    return fRequestRestart;
}