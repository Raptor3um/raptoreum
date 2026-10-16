// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Adapted from evmone's test/state/mpt.{cpp,hpp} — Apache-2.0,
// Copyright 2022 The evmone Authors.

#ifndef RAPTOREUM_EVM_MPT_H
#define RAPTOREUM_EVM_MPT_H

#include <uint256.h>

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

class uint160;

namespace evm {

class CEvmStateCache;
class MPTNode;
struct CEvmReceipt;

/**
 * A compact in-memory snapshot of one account, used by ComputeStateRoot
 * to feed the state MPT. We collect (address, account, storage) tuples
 * from a cache and hand them in here. The MPT consumes them in a
 * canonical order.
 */
struct StateRootAccount
{
    uint160 address;
    uint64_t nonce{0};
    uint256 balance;
    std::vector<uint8_t> code;
    std::map<uint256, uint256> storage; // slot -> value; zero slots are skipped
};

/**
 * Compute the canonical Ethereum-style state root hash for a list of
 * accounts. For each account, builds a per-account storage trie keyed
 * by keccak256(slot_u256) → RLP(trim(value)), then assembles the
 * state trie keyed by keccak256(address_u160) → RLP(nonce, balance,
 * storage_root, code_hash). Returns keccak256 of the assembled trie.
 *
 * Matches the format ethereum/tests fixtures use in their
 * `postStateHash` field.
 */
uint256 ComputeStateRoot(const std::vector<StateRootAccount>& accounts);

/**
 * Convenience: walk a CEvmStateCache and build the StateRootAccount
 * list. Pulls each dirty account, its storage entries from the cache's
 * dirty storage map, and its code via GetCode(codeHash). Skips
 * addresses present in the cache's deleted set. Order-independent.
 *
 * Defined separately from MPT itself so the trie code stays free of
 * cache headers; this lives in mpt.cpp because it's still a
 * computation on cache contents that no other caller needs.
 */
std::vector<StateRootAccount> CollectAccountsForStateRoot(CEvmStateCache& cache);

/**
 * D2 — canonical receipts-trie root committed in CCbTx v3.
 *
 * MPT keyed by keccak256(8-byte big-endian receipt index, in block
 * EVM-tx order) → a deterministic encoding of ONLY the
 * execution-result fields: status, cumulativeGasUsed, logs. The
 * chain/lookup-derived fields (ethTxHash, blockHash, rtmTxHash,
 * txIndex, effectiveGasPrice, sender/to/contractAddress) are
 * deliberately excluded — they can differ per node (ethTxHash comes
 * from a local cross-index) or are redundant, and committing them
 * would risk a receiptsRoot divergence. This mirrors Ethereum, whose
 * receipt commits status / cumulativeGas / bloom / logs only.
 *
 * Empty receipt set → the canonical empty-trie hash (same convention
 * as ComputeStateRoot), so a block with no EVM txs commits a stable
 * well-known value.
 */
uint256 ComputeReceiptsRoot(const std::vector<CEvmReceipt>& receipts);


/**
 * Insert-only Merkle Patricia Trie for computing the canonical state
 * root hash that ethereum/tests fixtures expect.
 *
 * Same constraints as evmone's reference implementation:
 *   - A key must not be longer than 32 bytes.
 *   - A key must not be a prefix of another key (yellow paper App. D).
 *   - A key must be unique (no updates by re-insertion).
 *   - No erasures.
 *
 * Two callers in this codebase:
 *   - Storage MPT: one per contract, keyed by keccak256(slot_u256),
 *     value = RLP(trim(slot_value_u256)).
 *   - State MPT: one per fixture, keyed by keccak256(address_u160),
 *     value = RLP(nonce, balance, storage_root, code_hash).
 */
class MPT
{
public:
    MPT() noexcept;
    ~MPT() noexcept;

    /** Insert a (key, value) pair. Key is the 32-byte keccak hash;
     *  value is the RLP-encoded leaf payload. */
    void Insert(const std::vector<uint8_t>& key, std::vector<uint8_t> value);

    /** Returns keccak256 of the trie's root node encoding (or the
     *  canonical empty-trie hash if the trie has no entries). */
    uint256 Hash() const;

private:
    std::unique_ptr<MPTNode> mRoot;
};

} // namespace evm

#endif // RAPTOREUM_EVM_MPT_H
