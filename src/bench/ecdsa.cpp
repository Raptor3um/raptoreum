// Copyright (c) 2018 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>

#include <key.h>

static void ECDSASign(benchmark::State& state)
{
    std::vector<CKey> keys;
    std::vector<uint256> hashes;
    for (size_t i = 0; i < 100; i++) {
        CKey k;
        k.MakeNewKey(false);
        keys.emplace_back(k);
        hashes.emplace_back(::SerializeHash((int)i));
    }

    // Benchmark.
    size_t i = 0;
    while (state.KeepRunning()) {
        std::vector<unsigned char> sig;
        keys[i].Sign(hashes[i], sig);
        i = (i + 1) % keys.size();
    }
}

static void ECDSAVerify(benchmark::State& state)
{
    std::vector<CPubKey> keys;
    std::vector<uint256> hashes;
    std::vector<std::vector<unsigned char>> sigs;
    for (size_t i = 0; i < 100; i++) {
        CKey k;
        k.MakeNewKey(false);
        keys.emplace_back(k.GetPubKey());
        hashes.emplace_back(::SerializeHash((int)i));
        std::vector<unsigned char> sig;
        k.Sign(hashes[i], sig);
        sigs.emplace_back(sig);
    }

    // Benchmark.
    size_t i = 0;
    while (state.KeepRunning()) {
        keys[i].Verify(hashes[i], sigs[i]);
        i = (i + 1) % keys.size();
    }
}

static void ECDSAVerify_LargeBlock(benchmark::State& state)
{
    std::vector<CPubKey> keys;
    std::vector<uint256> hashes;
    std::vector<std::vector<unsigned char>> sigs;
    for (size_t i = 0; i < 1000; i++) {
        CKey k;
        k.MakeNewKey(false);
        keys.emplace_back(k.GetPubKey());
        hashes.emplace_back(::SerializeHash((int)i));
        std::vector<unsigned char> sig;
        k.Sign(hashes[i], sig);
        sigs.emplace_back(sig);
    }

    // Benchmark.
    while (state.KeepRunning()) {
        for (size_t i = 0; i < keys.size(); i++) {
            keys[i].Verify(hashes[i], sigs[i]);
        }
    }
}

BENCHMARK(ECDSASign, 22 * 1000)
BENCHMARK(ECDSAVerify, 15 * 1000)
BENCHMARK(ECDSAVerify_LargeBlock, 15)
