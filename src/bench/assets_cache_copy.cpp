// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Microbenchmark for the per-ATMP deep copy of the global asset cache.
//
// AcceptToMemoryPoolWorker (validation.cpp:683) executes
//     CAssetsCache assetsCache = *passetsCache.get();
// on every transaction, whether or not the transaction touches an asset.
// The copy duplicates CAssets' three maps (mapAsset, mapAssetId,
// mapAssetAddressAmount) plus CAssetsCache's four dirty-tracking sets.
// mapAsset and mapAssetId grow with the number of confirmed assets, so this
// cost scales O(assets) and is paid by ordinary payment transactions that
// have nothing to do with assets. Regtest has zero assets, so every
// acceptance figure measured on regtest omits this term entirely; this
// benchmark supplies it as a function of confirmed asset count.
//
// Each entry is built conservatively: short asset names and an EMPTY
// referenceHash, so the measured cost is a lower bound on mainnet, where
// referenceHash (e.g. an IPFS hash, ~46 chars) is frequently populated.

#include <bench/bench.h>
#include <assets/assets.h>
#include <uint256.h>

#include <string>

static std::string HexId(uint64_t i)
{
    // 64-char hex, like a real asset creation txid.
    char buf[65];
    for (int p = 0; p < 64; ++p) buf[p] = "0123456789abcdef"[(i >> ((p & 15) * 4)) & 0xf];
    buf[64] = '\0';
    return std::string(buf);
}

// Build a CAssetsCache holding K confirmed assets, exactly as the global
// passetsCache would in steady state (dirty sets empty; those are per-call).
static CAssetsCache BuildCache(size_t K, bool withAddressIndex)
{
    CAssetsCache cache;
    for (size_t i = 0; i < K; ++i) {
        const std::string id = HexId(i);
        const std::string name = "ASSET" + std::to_string(i);

        CAssetMetaData meta;
        meta.SetNull();
        meta.assetId = id;
        meta.name = name;
        meta.referenceHash = "";     // conservative: empty
        meta.isRoot = true;
        meta.circulatingSupply = 0;
        meta.amount = 100000;

        CDatabaseAssetData data;
        data.asset = meta;
        data.blockHeight = 1;
        data.blockHash = uint256();

        cache.mapAsset[id] = data;
        cache.mapAssetId[name] = id;

        if (withAddressIndex) {
            // -assetindex populates mapAssetAddressAmount, keyed
            // (assetId, address). One holder per asset here; real assets
            // have many, so this too is a lower bound.
            cache.mapAssetAddressAmount[std::make_pair(id, name)] = 1;
        }
    }
    return cache;
}

// The copy performed at validation.cpp:683, isolated. nanobench times one
// copy construction + destruction per iteration (both are paid per ATMP: the
// copy at line 683, the destructor when the worker returns).
static void RunCopy(benchmark::Bench& bench, size_t K, bool withAddressIndex, size_t iters)
{
    const CAssetsCache source = BuildCache(K, withAddressIndex);
    bench.batch(1).unit("copy").minEpochIterations(iters).run([&] {
        CAssetsCache copy = const_cast<CAssetsCache&>(source);
        bench.doNotOptimizeAway(copy.mapAsset.size());
    });
}

// No -assetindex (mapAssetAddressAmount empty): the default node.
static void AssetCacheCopy_0(benchmark::Bench& b)      { RunCopy(b, 0, false, 1000); }
static void AssetCacheCopy_100(benchmark::Bench& b)    { RunCopy(b, 100, false, 300); }
static void AssetCacheCopy_1000(benchmark::Bench& b)   { RunCopy(b, 1000, false, 100); }
static void AssetCacheCopy_2500(benchmark::Bench& b)   { RunCopy(b, 2500, false, 60); }
static void AssetCacheCopy_3439(benchmark::Bench& b)   { RunCopy(b, 3439, false, 50); }  // RTM mainnet total, 2026-09-16
static void AssetCacheCopy_10000(benchmark::Bench& b)  { RunCopy(b, 10000, false, 30); }
static void AssetCacheCopy_50000(benchmark::Bench& b)  { RunCopy(b, 50000, false, 15); }
static void AssetCacheCopy_100000(benchmark::Bench& b) { RunCopy(b, 100000, false, 11); }

// With -assetindex (mapAssetAddressAmount populated, one holder per asset).
static void AssetCacheCopyIdx_1000(benchmark::Bench& b)  { RunCopy(b, 1000, true, 60); }
static void AssetCacheCopyIdx_2500(benchmark::Bench& b)  { RunCopy(b, 2500, true, 40); }
static void AssetCacheCopyIdx_10000(benchmark::Bench& b) { RunCopy(b, 10000, true, 20); }

BENCHMARK(AssetCacheCopy_0);
BENCHMARK(AssetCacheCopy_100);
BENCHMARK(AssetCacheCopy_1000);
BENCHMARK(AssetCacheCopy_2500);
BENCHMARK(AssetCacheCopy_3439);
BENCHMARK(AssetCacheCopy_10000);
BENCHMARK(AssetCacheCopy_50000);
BENCHMARK(AssetCacheCopy_100000);
BENCHMARK(AssetCacheCopyIdx_1000);
BENCHMARK(AssetCacheCopyIdx_2500);
BENCHMARK(AssetCacheCopyIdx_10000);
