// Copyright (c) 2021 The Bitcoin Core developers
// Copyright (c) 2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef BITCOIN_THREAD_H
#define BITCOIN_THREAD_H

#include <functional>

namespace util {

void TraceThread(const std::string thread_name, std::function<void()> thread_func);

}

#endif // BITCOIN_THREAD_H