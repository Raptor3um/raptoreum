// Copyright (c) 2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/hmac_sha512.h>

#include <string.h>

CHMAC_SHA512::CHMAC_SHA512(const unsigned char* key, size_t keylen)
{
    unsigned char rkey[512];
    if (keylen <= 512) {
        memcpy(rkey, key, keylen);
        memset(rkey + keylen, 0, 512 - keylen);
    } else {
        CSHA512().Write(key, keylen).Finalize(rkey);
        memset(rkey + 64, 0, 64);
    }

    for (int n = 0; n < 512; n++)
        rkey[n] ^= 0x5c;
    outer.Write(rkey, 512);

    for (int n = 0; n < 512; n++)
        rkey[n] ^= 0x5c ^ 0x36;
    inner.Write(rkey, 512);
}

void CHMAC_SHA512::Finalize(unsigned char hash[OUTPUT_SIZE])
{
    unsigned char temp[64];
    inner.Finalize(temp);
    outer.Write(temp, 64).Finalize(hash);
}
