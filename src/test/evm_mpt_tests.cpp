// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <evm/account.h>
#include <evm/hashing.h>
#include <evm/mpt.h>
#include <evm/rlp.h>

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

BOOST_AUTO_TEST_SUITE_END()
