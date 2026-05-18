// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <evm/account.h>
#include <evm/balance.h>
#include <evm/hashing.h>
#include <evm/mpt.h>
#include <evm/rlp.h>
#include <evm/state_cache.h>
#include <evm/state_db.h>

#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <vector>

/**
 * Sanity tests for our Merkle Patricia Trie port. The point is to pin
 * down the canonical edge cases against pre-computed reference values
 * — if these drift the whole postStateHash verification path breaks
 * silently, so we catch it here first.
 */

BOOST_FIXTURE_TEST_SUITE(evm_mpt_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(empty_trie_hash_matches_canonical)
{
    // Per yellow paper Appendix D, the empty Merkle-Patricia-Trie root
    // is keccak256(RLP("")) = 0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421.
    evm::MPT trie;
    BOOST_CHECK(trie.Hash() == evm::CEvmAccount::EmptyStorageRoot());
}

BOOST_AUTO_TEST_CASE(single_leaf_round_trip)
{
    // A single (key, value) insertion produces a single-leaf trie
    // whose encoding is RLP(rlp([compact_path, value])). We don't
    // pin the exact hash here (would require hand-computing keccak),
    // but we DO assert that the hash is NOT the empty-trie hash and
    // is non-zero.
    evm::MPT trie;
    std::vector<uint8_t> key{0xAA};
    std::vector<uint8_t> value{0x01, 0x02, 0x03};
    trie.Insert(key, value);
    const uint256 root = trie.Hash();
    BOOST_CHECK(root != evm::CEvmAccount::EmptyStorageRoot());
    BOOST_CHECK(root != uint256{});
}

BOOST_AUTO_TEST_CASE(order_independence)
{
    // The MPT is content-addressable: inserting the same set of
    // (key, value) pairs in any order MUST produce the same root.
    evm::MPT trieA, trieB;
    std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> pairs = {
        {{0x10, 0x20, 0x30}, {0xAA}},
        {{0x10, 0x20, 0x40}, {0xBB}},
        {{0x10, 0x30, 0x50}, {0xCC}},
        {{0x40, 0x50, 0x60}, {0xDD}},
    };

    for (const auto& [k, v] : pairs) trieA.Insert(k, v);

    // Reverse order.
    for (auto it = pairs.rbegin(); it != pairs.rend(); ++it) trieB.Insert(it->first, it->second);

    BOOST_CHECK(trieA.Hash() == trieB.Hash());
}

BOOST_AUTO_TEST_CASE(state_root_empty_accounts_collapses_to_empty_trie)
{
    // No accounts → state root must equal the canonical empty MPT.
    std::vector<evm::StateRootAccount> accounts;
    BOOST_CHECK(evm::ComputeStateRoot(accounts) == evm::CEvmAccount::EmptyStorageRoot());
}

BOOST_AUTO_TEST_CASE(state_root_single_eoa)
{
    // A single EOA with nonce=1 and a small balance, no storage, no
    // code. The expected root must differ from empty-trie hash.
    evm::StateRootAccount acc;
    std::memset(acc.address.begin(), 0xa1, 20);
    acc.nonce = 1;
    // balance = 1 (high byte zero, byte[31]=0x01).
    *(acc.balance.begin() + 31) = 0x01;
    // No storage, no code — codeHash defaults to EmptyCodeHash via
    // ComputeStateRoot's logic.

    std::vector<evm::StateRootAccount> accounts{acc};
    uint256 root = evm::ComputeStateRoot(accounts);
    BOOST_CHECK(root != evm::CEvmAccount::EmptyStorageRoot());
    BOOST_CHECK(root != uint256{});
}

// D2 increment 3: the committed-evmStateRoot consensus check
// recomputes ComputeStateRoot(CollectAccountsForStateRoot(cache)) on
// the post-block cache and compares it to cbTx.evmStateRoot. That is
// only sound if the recompute is (a) deterministic for identical
// state and (b) sensitive to any state change. Pin both — exactly
// the cache-driven path ConnectBlock uses (not the bare vector form
// the cases above cover).
BOOST_AUTO_TEST_CASE(state_root_over_cache_is_deterministic_and_sensitive)
{
    auto mkAddr = [](uint8_t fill) {
        std::vector<unsigned char> r(20, fill);
        return uint160(r);
    };

    evm::CEvmStateDB db(1 << 20, /*fMemory=*/true);
    evm::CEvmStateCache cache(db);
    cache.SetAccount(mkAddr(0xA1), evm::CEvmAccount(
        1, evm::Uint256FromUint64(1000),
        evm::CEvmAccount::EmptyCodeHash(),
        evm::CEvmAccount::EmptyStorageRoot()));
    cache.SetAccount(mkAddr(0xB2), evm::CEvmAccount(
        0, evm::Uint256FromUint64(42),
        evm::CEvmAccount::EmptyCodeHash(),
        evm::CEvmAccount::EmptyStorageRoot()));

    const uint256 r1 =
        evm::ComputeStateRoot(evm::CollectAccountsForStateRoot(cache));
    const uint256 r2 =
        evm::ComputeStateRoot(evm::CollectAccountsForStateRoot(cache));
    BOOST_CHECK(r1 == r2);                       // deterministic
    BOOST_CHECK(r1 != uint256{});

    // A one-wei balance change must move the root (else a tampered
    // post-state could pass the committed-root check).
    cache.SetAccount(mkAddr(0xB2), evm::CEvmAccount(
        0, evm::Uint256FromUint64(43),
        evm::CEvmAccount::EmptyCodeHash(),
        evm::CEvmAccount::EmptyStorageRoot()));
    const uint256 r3 =
        evm::ComputeStateRoot(evm::CollectAccountsForStateRoot(cache));
    BOOST_CHECK(r3 != r1);

    // Reverting the change restores the exact original root
    // (path-independence — the committed root depends only on state).
    cache.SetAccount(mkAddr(0xB2), evm::CEvmAccount(
        0, evm::Uint256FromUint64(42),
        evm::CEvmAccount::EmptyCodeHash(),
        evm::CEvmAccount::EmptyStorageRoot()));
    const uint256 r4 =
        evm::ComputeStateRoot(evm::CollectAccountsForStateRoot(cache));
    BOOST_CHECK(r4 == r1);
}

BOOST_AUTO_TEST_SUITE_END()
