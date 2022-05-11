// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2018-2020 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <bench/bench.h>
#include <crypto/ripemd160.h>
#include <crypto/sha1.h>
#include <crypto/sha256.h>
#include <crypto/sha512.h>
#include <hash.h>
#include <random.h>
#include <uint256.h>

/* Number of bytes to hash per iteration */
static const uint64_t BUFFER_SIZE = 1000*1000;

static void HASH_1MB_RIPEMD160(benchmark::Bench& bench)
{
    uint8_t hash[CRIPEMD160::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    bench.batch(in.size()).unit("byte").minEpochIterations(10).run([&] {
        CRIPEMD160().Write(in.data(), in.size()).Finalize(hash);
    });
}

static void HASH_1MB_SHA1(benchmark::Bench& bench)
{
    uint8_t hash[CSHA1::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    bench.batch(in.size()).unit("byte").minEpochIterations(10).run([&] {
        CSHA1().Write(in.data(), in.size()).Finalize(hash);
    });
}

static void HASH_1MB_SHA256(benchmark::Bench& bench)
{
    uint8_t hash[CSHA256::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    bench.batch(in.size()).unit("byte").minEpochIterations(10).run([&] {
        CSHA256().Write(in.data(), in.size()).Finalize(hash);
    });
}

static void HASH_1MB_DSHA256(benchmark::Bench& bench)
{
    uint8_t hash[CSHA256::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    bench.batch(in.size()).unit("byte").minEpochIterations(10).run([&] {
        CHash256().Write(in.data(), in.size()).Finalize(hash);
    });
}

static void HASH_1MB_SHA512(benchmark::Bench& bench)
{
    uint8_t hash[CSHA512::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    bench.batch(in.size()).unit("byte").minEpochIterations(10).run([&] {
        CSHA512().Write(in.data(), in.size()).Finalize(hash);
    });
}

static void HASH_DSHA256_0032b_single(benchmark::Bench& bench)
{
    uint8_t hash[CSHA256::OUTPUT_SIZE];
    std::vector<uint8_t> in(32,0);
    bench.minEpochIterations(100000).run([&] {
      CHash256().Write(in).Finalize(hash);
}

static void HASH_DSHA256_0080b_single(benchmark::Bench& bench)
{
    uint8_t hash[CSHA256::OUTPUT_SIZE];
    std::vector<uint8_t> in(80,0);
    bench.minEpochIterations(100000).run([&] {
      CHash256().Write(in).Finalize(hash);
    });
}

static void HASH_DSHA256_0128b_single(benchmark::Bench& bench)
{
    uint8_t hash[CSHA256::OUTPUT_SIZE];
    std::vector<uint8_t> in(128,0);
    bench.minEpochIterations(100000).run([&] {
      CHash256().Write(in).Finalize(hash);
    });
}

static void HASH_DSHA256_0512b_single(benchmark::Bench& bench)
{
    uint8_t hash[CSHA256::OUTPUT_SIZE];
    std::vector<uint8_t> in(512,0);
    bench.minEpochIterations(100000).run([&] {
      CHash256().Write(in).Finalize(hash);
    });
}

static void HASH_DSHA256_1024b_single(benchmark::Bench& bench)
{
    uint8_t hash[CSHA256::OUTPUT_SIZE];
    std::vector<uint8_t> in(1024,0);
    bench.minEpochIterations(100000).run([&] {
      CHash256().Write(in).Finalize(hash);
    });
}

static void HASH_DSHA256_2048b_single(benchmark::Bench& bench)
{
    uint8_t hash[CSHA256::OUTPUT_SIZE];
    std::vector<uint8_t> in(2048,0);
    bench.minEpochIterations(100000).run([&] {
      CHash256().Write(in).Finalize(hash);
    });
}

static void HASH_1MB_GR(benchmark::Bench& bench)
{
  uint256 hash;
  std::vector<uint8_t> in(BUFFER_SIZE, 0);
  bench.batch(in.size()).unit("byte").minEpochIterations(10).run([&] {
    hash = HashGR(in.begin(), in.end(), uint256());
  });
}

static void HASH_GR_0032b_single(benchmark::Bench& bench)
{
    uint256 hash;
    std::vector<uint8_t> in(32,0);
    bench.minEpochIterations(10000).run([&] {
      hash = HashGR(in.begin(), in.end(), uint256());
    });
}

static void HASH_GR_0080b_single(benchmark::Bench& bench)
{
    uint256 hash;
    std::vector<uint8_t> in(80,0);
    bench.minEpochIterations(10000).run([&] {
      hash = HashGR(in.begin(), in.end(), uint256());
    });
}

static void HASH_GR_0128b_single(benchmark::Bench& bench)
{
    uint256 hash;
    std::vector<uint8_t> in(128,0);
    bench.minEpochIterations(10000).run([&] {
      hash = HashGR(in.begin(), in.end(), uint256());
    });
}

static void HASH_GR_0512b_single(benchmark::Bench& bench)
{
    uint256 hash;
    std::vector<uint8_t> in(512,0);
    bench.minEpochIterations(10000).run([&] {
      hash = HashGR(in.begin(), in.end(), uint256());
    });
}

static void HASH_GR_1024b_single(benchmark::Bench& bench)
{
    uint256 hash;
    std::vector<uint8_t> in(1024,0);
    bench.minEpochIterations(10000).run([&] {
      hash = HashGR(in.begin(), in.end(), uint256());
    });
}

static void HASH_GR_2048b_single(benchmark::Bench& bench)
{
    uint256 hash;
    std::vector<uint8_t> in(2048,0);
    bench.minEpochIterations(10000).run([&] {
      hash = HashGR(in.begin(), in.end(), uint256());
    });
}


static void HashCn(benchmark::Bench& bench, int hashSelection)
{
    uint512 hashIn;
    uint512 hashOut;
    bench.minEpochIterations(10000).run([&] {
      cnHash(&hashIn, &hashOut, 64, hashSelection);
      hashIn = hashOut;
    });
}

static void HASH_CN_cryptonight_dark_hash(benchmark::Bench& bench)
{
    HashCn(bench, 0);
}

static void HASH_CN_cryptonight_darklite_hash(benchmark::Bench& bench)
{
    HashCn(bench, 1);
}

static void HASH_CN_cryptonight_cnfast_hash(benchmark::Bench& bench)
{
    HashCn(bench, 2);
}

static void HASH_CN_cryptonight_cnlite_hash(benchmark::Bench& bench)
{
    HashCn(bench, 3);
}

static void HASH_CN_cryptonight_turtle_hash(benchmark::Bench& bench)
{
    HashCn(bench, 4);
}

static void HASH_CN_cryptonight_turtlelite_hash(benchmark::Bench& bench)
{
    HashCn(bench, 5);
}

/* Hash 32 bytes via SHA */

static void HASH_SHA256_32b(benchmark::Bench& bench)
{
  std::vector<uint8_t> in(32, 0);
  bench.run([&] {
    CSHA256().Write(in.data(), in.size()).Finalize(in.data());
  });
}

/* Hash 1024 blobs 64 bytes each via DSHA256 */

static void HASH_SHA256D64_1024(benchmark::Bench& bench)
{
  std::vector<uint8_t> in(64 * 1024, 0);
  bench.minEpochIterations(1000).run([&] {
    SHA256D64(in.data(), in.data(), 1024);
  });
}

/* FastRandom for uint32_t and bool */

static void FastRandom_32bit(benchmark::Bench& bench)
{
  FastRandomContext rng(true);
  bench.run([&] {
    rng.rand32();
  });
}

static void FastRandom_1bit(benchmark::Bench& bench)
{
  FastRandomContext rng(true);
  bench.run([&] {
    rng.randbool();
  });
}

BENCHMARK(HASH_1MB_RIPEMD160);
BENCHMARK(HASH_1MB_SHA1);
BENCHMARK(HASH_1MB_SHA256);
BENCHMARK(HASH_1MB_DSHA256);
BENCHMARK(HASH_1MB_SHA512);
BENCHMARK(HASH_1MB_GR);

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

BENCHMARK(HASH_CN_cryptonight_dark_hash);
BENCHMARK(HASH_CN_cryptonight_darklite_hash);
BENCHMARK(HASH_CN_cryptonight_cnfast_hash);
BENCHMARK(HASH_CN_cryptonight_cnlite_hash);
BENCHMARK(HASH_CN_cryptonight_turtle_hash);
BENCHMARK(HASH_CN_cryptonight_turtlelite_hash);

BENCHMARK(HASH_SHA256_32b);
BENCHMARK(HASH_SHA256D64_1024);
BENCHMARK(FastRandom_32bit);
BENCHMARK(FastRandom_1bit);