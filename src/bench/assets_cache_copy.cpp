// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Microbenchmark for the per-ATMP deep copy of the global asset cache.
//
// Before this PR, AcceptToMemoryPoolWorker (validation.cpp) executed
//     CAssetsCache assetsCache = *passetsCache.get();
// unconditionally, on every transaction whether or not it touched an asset.
// This benchmark isolates the cost of that copy itself, as a function of
// confirmed asset count, to show why paying it unconditionally was wrong.
//
// It does NOT exercise AcceptToMemoryPoolWorker or TxNeedsAssetsCache at all
// -- it constructs the copy directly, every run, regardless of whether the
// real guard is intact, narrowed, or removed entirely. If that guard ever
// regresses, these numbers will not move; they show what the cost WOULD be
// if paid unconditionally, not whether it currently is. The regression check
// for that is src/test/txvalidation_tests.cpp's
// tx_mempool_atmp_accepts_ordinary_tx_with_null_assets_cache, which runs a
// transaction through the real ATMP call site instead.
//
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
//
// The dirty-tracking sets are populated too, at a fixed size independent of
// K: Flush() (assets.cpp) accumulates each processed asset transaction's
// entries into the global cache's own sets until the next
// DumpCacheToDatabase, so a node with asset activity since its last dump
// carries a non-empty, uncommitted batch here -- unlike mapAsset/mapAssetId,
// this cost does not scale with the confirmed-asset count. Because of this,
// AssetCacheCopy_0 no longer measures an empty cache; it now measures this
// fixed dirty-batch floor alone, with zero confirmed assets on top of it.

#include <bench/bench.h>
#include <assets/assets.h>
#include <uint256.h>

#include <string>

// A batch of uncommitted asset ops since the last DumpCacheToDatabase. Not
// meant to be a realistic upper bound, just non-empty -- see file header.
static constexpr size_t DIRTY_BATCH = 20;

static std::string HexId(uint64_t i)
{
    // 64-char hex, like a real asset creation txid.
    char buf[65];
    for (int p = 0; p < 64; ++p) buf[p] = "0123456789abcdef"[(i >> ((p & 15) * 4)) & 0xf];
    buf[64] = '\0';
    return std::string(buf);
}

// Build a CAssetsCache holding K confirmed assets plus a fixed-size dirty
// batch, exactly as the global passetsCache would carry between dumps.
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

    for (size_t i = 0; i < DIRTY_BATCH; ++i) {
        const std::string id = HexId(1000000 + i);
        const std::string name = "PENDING" + std::to_string(i);

        CAssetMetaData meta;
        meta.SetNull();
        meta.assetId = id;
        meta.name = name;
        meta.isRoot = true;
        meta.amount = 100000;
        cache.NewAssetsToAdd.insert(CDatabaseAssetData(meta, 1, uint256()));

        CAssetTransfer transfer(id, 1000);
        cache.NewAssetsTransferToAdd.insert(CAssetTransferEntry(transfer, name, COutPoint(uint256S(id), (uint32_t)i)));
    }

    return cache;
}

// The copy performed at validation.cpp:685, isolated. nanobench times one
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
