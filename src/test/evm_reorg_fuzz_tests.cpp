// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <evm/account.h>
#include <evm/balance.h>
#include <evm/hashing.h>
#include <evm/state_cache.h>
#include <evm/state_db.h>
#include <evm/undo.h>

#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <map>
#include <random>
#include <utility>
#include <vector>

/**
 * Phase 2.6 — reorg-safety property fuzz (test gap T3).
 *
 * The plan flags reorg state-revert as a HIGH-severity, under-tested
 * consensus area (issue A10) and the reorg-fuzzing property as CRITICAL
 * (T3): "for any sequence of blocks B1..BN with random reorgs, replay
 * produces identical state". `evm_undo_tests.cpp` only covers single-
 * block round-trips; this drives long random chains with deep random
 * reorgs (up to 100 blocks, per A10) against the real
 * BuildUndoFromCache / cache.Flush() / ApplyUndoToDB pipeline.
 *
 * Method: a reference model is mutated in lockstep with a
 * CEvmStateCache for every block, mirroring the *observable* effect of
 * Flush() (a zero SSTORE erases the slot; v46 DeleteAccount purges the
 * account's whole storage footprint). After every connect AND every
 * disconnect the on-disk DB must be byte-identical to the model. A
 * disconnect that fails to restore pre-block state — the classic
 * "funds locked / state divergence after a reorg" bug — fails here.
 *
 * The op mix deliberately uses a tiny address/slot pool so create →
 * delete → recreate and set → zero → restore collisions (the paths
 * the v44–v47 semantics changes touched) happen constantly.
 */

namespace {

using AcctMap = std::map<uint160, evm::CEvmAccount>;
using StoreMap = std::map<std::pair<uint160, uint256>, uint256>;
using CodeMap = std::map<uint256, std::vector<uint8_t>>;

struct Model {
    AcctMap accts;
    // Only non-zero values are kept (zero == absent, mirroring the DB
    // EraseStorage-on-zero-flush convention).
    StoreMap store;
    // Code blobs currently resolvable in the DB.
    CodeMap code;
};

uint160 Addr(uint8_t i)
{
    std::vector<unsigned char> r(20, 0);
    r[19] = i;
    return uint160(r);
}

uint256 Slot(uint8_t i)
{
    std::vector<unsigned char> r(32, 0);
    r[31] = i;
    return uint256(r);
}

bool AcctEq(const evm::CEvmAccount& a, const evm::CEvmAccount& b)
{
    return a.nonce == b.nonce && a.balance == b.balance &&
           a.codeHash == b.codeHash && a.storageRoot == b.storageRoot;
}

const std::vector<uint8_t> kAddrs = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
const std::vector<uint8_t> kSlots = {0, 1, 2, 3};

// Apply one random op to BOTH the cache and the model so the model
// tracks exactly what the DB will hold once the block is flushed.
void ApplyRandomOp(std::mt19937_64& rng, evm::CEvmStateCache& cache,
                   Model& m)
{
    const uint160 a = Addr(kAddrs[rng() % kAddrs.size()]);
    const int op = static_cast<int>(rng() % 10);

    if (op <= 4) {
        // SetAccount: create or modify. Preserve an existing
        // codeHash/storageRoot so SetCode references stay coherent.
        evm::CEvmAccount acc(
            rng() % 8,
            evm::Uint256FromUint64(rng() % 1000000),
            evm::CEvmAccount::EmptyCodeHash(),
            evm::CEvmAccount::EmptyStorageRoot());
        auto it = m.accts.find(a);
        if (it != m.accts.end()) {
            acc.codeHash = it->second.codeHash;
            acc.storageRoot = it->second.storageRoot;
        }
        cache.SetAccount(a, acc);
        m.accts[a] = acc;
    } else if (op == 5) {
        // DeleteAccount — v46: also purges the account's storage.
        cache.DeleteAccount(a);
        m.accts.erase(a);
        for (auto it = m.store.begin(); it != m.store.end();) {
            if (it->first.first == a) {
                it = m.store.erase(it);
            } else {
                ++it;
            }
        }
    } else if (op <= 8) {
        // SetStorage, ~1/3 of which writes zero (=> erase on flush).
        const uint256 s = Slot(kSlots[rng() % kSlots.size()]);
        const bool zero = (rng() % 3 == 0);
        const uint256 v =
            zero ? uint256() : evm::Uint256FromUint64(1 + rng() % 1000000);
        cache.SetStorage(a, s, v);
        if (v.IsNull()) {
            m.store.erase({a, s});
        } else {
            m.store[{a, s}] = v;
        }
    } else {
        // SetCode + point an account at it.
        std::vector<uint8_t> blob(1 + rng() % 8);
        for (auto& b : blob) {
            b = static_cast<uint8_t>(rng() & 0xFF);
        }
        const uint256 h = evm::Keccak256(blob);
        cache.SetCode(h, blob);
        evm::CEvmAccount acc(
            0, uint256(),
            evm::CEvmAccount::EmptyCodeHash(),
            evm::CEvmAccount::EmptyStorageRoot());
        auto it = m.accts.find(a);
        if (it != m.accts.end()) {
            acc = it->second;
        }
        acc.codeHash = h;
        cache.SetAccount(a, acc);
        m.accts[a] = acc;
        m.code[h] = blob;
    }
}

void AssertDbEqualsModel(evm::CEvmStateDB& db, const Model& m,
                         const char* where, unsigned long long seed,
                         int blocks)
{
    // model ⊆ db, equal.
    for (const auto& kv : m.accts) {
        evm::CEvmAccount got;
        const bool ok = db.ReadAccount(kv.first, got);
        BOOST_REQUIRE_MESSAGE(
            ok, where << " seed=" << seed << " blocks=" << blocks
                      << ": account missing in DB");
        BOOST_CHECK_MESSAGE(
            AcctEq(got, kv.second),
            where << " seed=" << seed << ": account field mismatch");
    }
    // db ⊆ model, equal (no resurrected / extra accounts).
    size_t dbAccts = 0;
    db.ForEachAccount(
        [&](const uint160& ad, const evm::CEvmAccount& ac) {
            ++dbAccts;
            auto it = m.accts.find(ad);
            BOOST_CHECK_MESSAGE(
                it != m.accts.end() && AcctEq(it->second, ac),
                where << " seed=" << seed
                      << ": DB has an account the model does not");
        });
    BOOST_CHECK_MESSAGE(
        dbAccts == m.accts.size(),
        where << " seed=" << seed << ": account count "
              << dbAccts << " != model " << m.accts.size());

    // Storage: the address pool is closed, so iterating it covers
    // every slot either side could hold.
    for (uint8_t ai : kAddrs) {
        const uint160 ad = Addr(ai);
        std::map<uint256, uint256> dbSlots;
        db.ForEachStorage(ad, [&](const uint256& s, const uint256& v) {
            dbSlots[s] = v;
        });
        std::map<uint256, uint256> mSlots;
        for (const auto& kv : m.store) {
            if (kv.first.first == ad) {
                mSlots[kv.first.second] = kv.second;
            }
        }
        BOOST_CHECK_MESSAGE(
            dbSlots == mSlots,
            where << " seed=" << seed << " blocks=" << blocks
                  << ": storage mismatch (DB " << dbSlots.size()
                  << " vs model " << mSlots.size() << " slots)");
    }

    // Every live account's code must still be resolvable.
    for (const auto& kv : m.accts) {
        const uint256& ch = kv.second.codeHash;
        if (ch == evm::CEvmAccount::EmptyCodeHash()) {
            continue;
        }
        std::vector<uint8_t> cb;
        const bool ok = db.ReadCode(ch, cb);
        BOOST_CHECK_MESSAGE(
            ok, where << " seed=" << seed
                      << ": code blob for a live account was erased");
        auto mit = m.code.find(ch);
        if (ok && mit != m.code.end()) {
            BOOST_CHECK_MESSAGE(
                cb == mit->second,
                where << " seed=" << seed << ": code bytes differ");
        }
    }
}

struct BlockRec {
    evm::CEvmStateUndo undo;
    Model before;  // model snapshot the undo must restore to
};

// One self-contained fuzz episode for a given seed: build an initial
// chain, then do several deep random reorgs, asserting DB==model after
// every connect and disconnect. maxReorg caps reorg depth (set to 100
// for the A10 deep-reorg requirement).
void RunEpisode(unsigned long long seed, int initialBlocks,
                int reorgRounds, int maxReorg)
{
    std::mt19937_64 rng(seed);
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/true);
    Model model;
    std::vector<BlockRec> chain;

    auto connect = [&]() {
        evm::CEvmStateCache cache(db);
        Model before = model;
        const int nops = 1 + static_cast<int>(rng() % 8);
        for (int i = 0; i < nops; ++i) {
            ApplyRandomOp(rng, cache, model);
        }
        evm::CEvmStateUndo undo = evm::BuildUndoFromCache(cache, db);
        BOOST_REQUIRE(cache.Flush());
        chain.push_back({std::move(undo), std::move(before)});
        AssertDbEqualsModel(db, model, "connect", seed,
                            static_cast<int>(chain.size()));
    };
    auto disconnect = [&]() {
        BOOST_REQUIRE(!chain.empty());
        BlockRec rec = std::move(chain.back());
        chain.pop_back();
        BOOST_REQUIRE(evm::ApplyUndoToDB(rec.undo, db));
        model = rec.before;
        AssertDbEqualsModel(db, model, "disconnect", seed,
                            static_cast<int>(chain.size()));
    };

    for (int i = 0; i < initialBlocks; ++i) {
        connect();
    }
    for (int r = 0; r < reorgRounds; ++r) {
        if (chain.empty()) {
            connect();
            continue;
        }
        const int cap = std::min<int>(
            static_cast<int>(chain.size()), maxReorg);
        const int depth = 1 + static_cast<int>(rng() % cap);
        for (int i = 0; i < depth; ++i) {
            disconnect();
        }
        // Reconnect a different number of fresh blocks (the competing
        // branch); sometimes longer than what we rolled back.
        const int rebuild = 1 + static_cast<int>(rng() % (depth + 2));
        for (int i = 0; i < rebuild; ++i) {
            connect();
        }
    }
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(evm_reorg_fuzz_tests, BasicTestingSetup)

// Many independent seeds, moderate chains + frequent shallow/medium
// reorgs. Each asserts DB==model at every connect/disconnect.
BOOST_AUTO_TEST_CASE(reorg_property_many_seeds)
{
    for (unsigned long long seed = 1; seed <= 40; ++seed) {
        RunEpisode(seed, /*initialBlocks=*/30, /*reorgRounds=*/12,
                   /*maxReorg=*/15);
    }
}

// A10: explicitly exercise a deep (up to 100-block) reorg.
BOOST_AUTO_TEST_CASE(reorg_property_deep_100_blocks)
{
    for (unsigned long long seed = 1001; seed <= 1006; ++seed) {
        RunEpisode(seed, /*initialBlocks=*/120, /*reorgRounds=*/4,
                   /*maxReorg=*/100);
    }
}

BOOST_AUTO_TEST_SUITE_END()
