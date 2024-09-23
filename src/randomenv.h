// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developerts
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#ifndef RANDOMENV_H
#define RANDOMENV_H

#include <crypto/sha512.h>


/** Gather non-cryptographic environment data that changes over time. */
void RandAddDynamicEnv(CSHA512 &hasher);

/** Gather non-cryptographic environment data that does not change over time. */
void RandAddStaticEnv(CSHA512 &hasher);

#endif