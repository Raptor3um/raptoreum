// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2018-2020 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <iostream>

#include "bench.h"
#include "bloom.h"
#include "hash.h"
#include "random.h"
#include "uint256.h"
#include "utiltime.h"
#include "crypto/ripemd160.h"
#include "crypto/sha1.h"
#include "crypto/sha256.h"
#include "crypto/sha512.h"

/* Number of bytes to hash per iteration */
static const uint64_t BUFFER_SIZE = 1000*1000;

static void HASH_RIPEMD160(benchmark::State& state)
{
    uint8_t hash[CRIPEMD160::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    while (state.KeepRunning())
        CRIPEMD160().Write(in.data(), in.size()).Finalize(hash);
}

static void HASH_SHA1(benchmark::State& state)
{
    uint8_t hash[CSHA1::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    while (state.KeepRunning())
        CSHA1().Write(in.data(), in.size()).Finalize(hash);
}

static void HASH_SHA256(benchmark::State& state)
{
    uint8_t hash[CSHA256::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    while (state.KeepRunning())
        CSHA256().Write(in.data(), in.size()).Finalize(hash);
}

static void HASH_SHA256_0032b(benchmark::State& state)
{
    std::vector<uint8_t> in(32,0);
    while (state.KeepRunning()) {
        for (int i = 0; i < 1000000; i++) {
            CSHA256().Write(in.data(), in.size()).Finalize(&in[0]);
        }
    }
}

static void HASH_DSHA256(benchmark::State& state)
{
    uint8_t hash[CSHA256::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    while (state.KeepRunning())
        CHash256().Write(in.data(), in.size()).Finalize(hash);
}

static void HASH_DSHA256_0032b(benchmark::State& state)
{
    std::vector<uint8_t> in(32,0);
    while (state.KeepRunning()) {
        for (int i = 0; i < 1000000; i++) {
            CHash256().Write(in.data(), in.size()).Finalize(&in[0]);
        }
    }
}

static void HASH_SHA256D64_1024(benchmark::State& state)
{
    std::vector<uint8_t> in(64 * 1024, 0);
    while (state.KeepRunning()) {
        SHA256D64(in.data(), in.data(), 1024);
    }
}

static void HASH_SHA512(benchmark::State& state)
{
    uint8_t hash[CSHA512::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    while (state.KeepRunning())
        CSHA512().Write(in.data(), in.size()).Finalize(hash);
}

static void HASH_SipHash_0032b(benchmark::State& state)
{
    uint256 x;
    while (state.KeepRunning()) {
        for (int i = 0; i < 1000000; i++) {
            *((uint64_t*)x.begin()) = SipHashUint256(0, i, x);
        }
    }
}

static void FastRandom_32bit(benchmark::State& state)
{
    FastRandomContext rng(true);
    uint32_t x = 0;
    while (state.KeepRunning()) {
        for (int i = 0; i < 1000000; i++) {
            x += rng.rand32();
        }
    }
}

static void FastRandom_1bit(benchmark::State& state)
{
    FastRandomContext rng(true);
    uint32_t x = 0;
    while (state.KeepRunning()) {
        for (int i = 0; i < 1000000; i++) {
            x += rng.randbool();
        }
    }
}

static void HASH_DSHA256_0032b_single(benchmark::State& state)
{
    std::vector<uint8_t> in(32,0);
    while (state.KeepRunning())
        CHash256().Write(in.data(), in.size()).Finalize(&in[0]);
}

static void HASH_DSHA256_0080b_single(benchmark::State& state)
{
    std::vector<uint8_t> in(80,0);
    while (state.KeepRunning())
        CHash256().Write(in.data(), in.size()).Finalize(&in[0]);
}

static void HASH_DSHA256_0128b_single(benchmark::State& state)
{
    std::vector<uint8_t> in(128,0);
    while (state.KeepRunning())
        CHash256().Write(in.data(), in.size()).Finalize(&in[0]);
}

static void HASH_DSHA256_0512b_single(benchmark::State& state)
{
    std::vector<uint8_t> in(512,0);
    while (state.KeepRunning())
        CHash256().Write(in.data(), in.size()).Finalize(&in[0]);
}

static void HASH_DSHA256_1024b_single(benchmark::State& state)
{
    std::vector<uint8_t> in(1024,0);
    while (state.KeepRunning())
        CHash256().Write(in.data(), in.size()).Finalize(&in[0]);
}

static void HASH_DSHA256_2048b_single(benchmark::State& state)
{
    std::vector<uint8_t> in(2048,0);
    while (state.KeepRunning())
        CHash256().Write(in.data(), in.size()).Finalize(&in[0]);
}

static void HASH_GR(benchmark::State& state)
{
    uint256 hash;
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    while (state.KeepRunning())
        hash = HashGR(in.begin(), in.end(), uint256());
}

static void HASH_GR_0032b_single(benchmark::State& state)
{
    uint256 hash;
    std::vector<uint8_t> in(32,0);
    while (state.KeepRunning())
        hash = HashGR(in.begin(), in.end(), uint256());
}

static void HASH_GR_0080b_single(benchmark::State& state)
{
    uint256 hash;
    std::vector<uint8_t> in(80,0);
    while (state.KeepRunning())
        hash = HashGR(in.begin(), in.end(), uint256());
}

static void HASH_GR_0128b_single(benchmark::State& state)
{
    uint256 hash;
    std::vector<uint8_t> in(128,0);
    while (state.KeepRunning())
        hash = HashGR(in.begin(), in.end(), uint256());
}

static void HASH_GR_0512b_single(benchmark::State& state)
{
    uint256 hash;
    std::vector<uint8_t> in(512,0);
    while (state.KeepRunning())
        hash = HashGR(in.begin(), in.end(), uint256());
}

static void HASH_GR_1024b_single(benchmark::State& state)
{
    uint256 hash;
    std::vector<uint8_t> in(1024,0);
    while (state.KeepRunning())
        hash = HashGR(in.begin(), in.end(), uint256());
}

static void HASH_GR_2048b_single(benchmark::State& state)
{
    uint256 hash;
    std::vector<uint8_t> in(2048,0);
    while (state.KeepRunning())
        hash = HashGR(in.begin(), in.end(), uint256());
}

BENCHMARK(HASH_RIPEMD160);
BENCHMARK(HASH_SHA1);
BENCHMARK(HASH_SHA256);
BENCHMARK(HASH_DSHA256);
BENCHMARK(HASH_SHA512);
BENCHMARK(HASH_GR);

BENCHMARK(HASH_SHA256_0032b);
BENCHMARK(HASH_DSHA256_0032b);
BENCHMARK(HASH_SipHash_0032b);
BENCHMARK(HASH_SHA256D64_1024/*, 7400*/);

BENCHMARK(HASH_DSHA256_0032b_single);
BENCHMARK(HASH_DSHA256_0080b_single);
BENCHMARK(HASH_DSHA256_0128b_single);
BENCHMARK(HASH_DSHA256_0512b_single);
BENCHMARK(HASH_DSHA256_1024b_single);
BENCHMARK(HASH_DSHA256_2048b_single);
BENCHMARK(HASH_GR_0032b_single);
BENCHMARK(HASH_GR_0080b_single);
BENCHMARK(HASH_GR_0128b_single);
BENCHMARK(HASH_GR_0512b_single);
BENCHMARK(HASH_GR_1024b_single);
BENCHMARK(HASH_GR_2048b_single);
BENCHMARK(FastRandom_32bit);
BENCHMARK(FastRandom_1bit);
