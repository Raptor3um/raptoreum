// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <assets/assets.h>
#include <hash.h>

#include <boost/test/unit_test.hpp>

#include <array>
#include <cstring>
#include <random>
#include <string>
#include <vector>

/**
 * FUP-4.1 — CAssetsCache::ResolveAssetIdByTag safety lock.
 *
 * The Smart-Asset ERC-20 precompile resolves a per-asset EVM address back
 * to its assetId every call (during eth_call AND during EVM execution in
 * ConnectBlock — i.e. on the consensus path). The O(N) linear scan was
 * replaced by a self-correcting hint index for O(log N) warm lookups.
 *
 * A reverse index that could silently diverge from mapAsset would be a
 * CONSENSUS bug (different nodes resolving the same address differently →
 * fork). The index is safe by construction: every hint hit is re-verified
 * against mapAsset, with a full-scan fallback, so its result is ALWAYS a
 * pure function of mapAsset — never of the (possibly stale) hint contents.
 *
 * These tests prove that invariant directly: the method's answer must
 * equal an independent linear scan for every tag, under adversarial
 * insert/erase sequences, AND a warmed hint must self-correct the instant
 * its asset disappears from mapAsset (the exact "stale index returns a
 * dead asset" failure that would diverge consensus).
 */

namespace {

// The 12-byte hash160(assetId)[8:20] tag — matches AssetErc20Address and
// CAssetsCache's internal AssetTagOf.
std::array<uint8_t, 12> TagOf(const std::string& id)
{
    const uint160 h = Hash160(std::vector<unsigned char>(id.begin(), id.end()));
    std::array<uint8_t, 12> t{};
    std::memcpy(t.data(), h.begin() + 8, 12);
    return t;
}

// Independent oracle: the authoritative answer by direct scan of mapAsset
// (the same logic the method falls back to — but with NO hint state).
bool ScanResolve(const CAssetsCache& c, const std::array<uint8_t, 12>& tag,
                 std::string& out)
{
    for (const auto& kv : c.mapAsset) {
        if (TagOf(kv.first) == tag) { out = kv.first; return true; }
    }
    return false;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(evm_asset_resolver_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(resolve_basic_and_self_corrects_on_stale_hint)
{
    CAssetsCache cache;
    const std::vector<std::string> ids = {
        "GOLDASSETID00001", "SILVERASSETID002", "BRONZEASSETID003"};
    for (const auto& id : ids) cache.mapAsset[id];  // key is all that matters

    // Each asset resolves to itself.
    for (const auto& id : ids) {
        std::string out;
        BOOST_REQUIRE(cache.ResolveAssetIdByTag(TagOf(id), out));
        BOOST_CHECK_EQUAL(out, id);
    }

    // An unknown tag resolves to nothing.
    {
        std::array<uint8_t, 12> bogus{};
        bogus.fill(0xEE);
        std::string out;
        BOOST_CHECK(!cache.ResolveAssetIdByTag(bogus, out));
    }

    // SELF-CORRECTION (the consensus-safety assertion): warm the hint for
    // ids[0], then remove it from mapAsset directly (as a reorg / cache
    // eviction would). A naive index would still return the dead asset;
    // the verified hint must detect the staleness and report "not found".
    {
        std::string out;
        BOOST_REQUIRE(cache.ResolveAssetIdByTag(TagOf(ids[0]), out));  // warm
        cache.mapAsset.erase(ids[0]);
        BOOST_CHECK(!cache.ResolveAssetIdByTag(TagOf(ids[0]), out));   // gone
        // Unrelated assets are unaffected.
        BOOST_REQUIRE(cache.ResolveAssetIdByTag(TagOf(ids[1]), out));
        BOOST_CHECK_EQUAL(out, ids[1]);
        // Re-inserting brings it back.
        cache.mapAsset[ids[0]];
        BOOST_REQUIRE(cache.ResolveAssetIdByTag(TagOf(ids[0]), out));
        BOOST_CHECK_EQUAL(out, ids[0]);
    }
}

BOOST_AUTO_TEST_CASE(resolve_always_equals_scan_under_random_mutation)
{
    CAssetsCache cache;
    std::mt19937 rng(0xABCDEF12u);  // fixed seed — reproducible

    std::vector<std::string> pool;
    for (int i = 0; i < 40; ++i) {
        pool.push_back("POOLASSET" + std::to_string(100000 + i));
    }

    for (int step = 0; step < 4000; ++step) {
        // Adversarial mutation of mapAsset directly (insert or erase a
        // random pool member), interleaved with lookups that warm/stale
        // the hint.
        const std::string& mut = pool[rng() % pool.size()];
        if (rng() & 1u) {
            cache.mapAsset[mut];
        } else {
            cache.mapAsset.erase(mut);
        }

        // The hint-backed resolver must agree with the authoritative scan
        // for an arbitrary probe tag — found-ness AND the resolved id.
        const std::string& probe = pool[rng() % pool.size()];
        const std::array<uint8_t, 12> tag = TagOf(probe);
        std::string hintOut, scanOut;
        const bool hintFound = cache.ResolveAssetIdByTag(tag, hintOut);
        const bool scanFound = ScanResolve(cache, tag, scanOut);
        BOOST_REQUIRE_EQUAL(hintFound, scanFound);
        if (hintFound) BOOST_REQUIRE_EQUAL(hintOut, scanOut);
    }
}

BOOST_AUTO_TEST_SUITE_END()
