// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <support/cleanse.h>

#include <cstring>

#if defined(_MSC_VER)
#include <Windows.h> // For SecureZeroMemory.
#endif

void memory_cleanse(void *ptr, size_t len) {
#if defined(_MSC_VER)
    SecureZeroMemory(ptr, len);
#else
    std::memset(ptr, 0, len);

    __asm__ __volatile__("" : : "r"(ptr) : "memory");
#endif
}
