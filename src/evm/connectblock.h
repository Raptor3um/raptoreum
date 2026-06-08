// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_CONNECTBLOCK_H
#define RAPTOREUM_EVM_CONNECTBLOCK_H

#include <evm/apply.h>
#include <evm/host.h>
#include <evm/process.h>

#include <uint256.h>

#include <cstdint>
#include <vector>

class CBlock;
class CBlockIndex;
namespace Consensus { struct Params; }

namespace evm {

class CEvmStateCache;
class CEvmStateDB;

/**
 * Aggregate outcome of processing all EVM-typed transactions in a single
 * block. Produced by ProcessEvmTransactionsInBlock and consumed by the
 * caller (Phase 2.4e validation.cpp wiring) to:
 *
 *   - Verify the coinbase output credits the expected coinbaseTip sum.
 *   - Add the UTXO credits from SPEND txs to the block's output set.
 *   - Discard the cache and reject the block on `ok=false`.
 *
 * The per-tx `txResults` mirror the order of EVM-typed txs in the block,
 * not the order of all block.vtx entries. Non-EVM txs are skipped.
 */
struct BlockProcessResult
{
    /** True if every EVM tx in the block validated and executed at
     *  least to a "charged failure" status (REVERT / OOG / FAILURE
     *  with charged gas). False on any pre-flight failure: pre-flight
     *  failures invalidate the block per consensus rules — a tx that
     *  cannot be billed cannot be in a block. */
    bool ok{true};

    /** Sum of priority-fee credits across all EVM txs (in weis).
     *  The caller credits this to the miner's coinbase output. */
    uint64_t totalCoinbaseTip{0};

    /** Sum of base-fee burns across all EVM txs (in weis).
     *  No recipient — EIP-1559 deflationary mechanism. */
    uint64_t totalBurned{0};

    /** Per-EVM-tx results, in the order they appeared in block.vtx. */
    std::vector<ProcessResult> txResults;

    /** Parallel to `txResults`: the index in `block.vtx` of the
     *  CTransaction each ProcessResult came from. Phase 3.6 receipt
     *  generation uses this to key receipts by the wrapper's
     *  Raptoreum sha256d hash (block.vtx[blockIndex]->GetHash()). */
    std::vector<int> txBlockIndices;

    /** UTXO credits collected from all SPEND txs in the block. The
     *  caller adds these as outputs alongside the coinbase. */
    std::vector<ApplyResult::UtxoCredit> utxoCredits;

    /** Index into block.vtx of the first failing EVM tx (for error
     *  reporting). -1 if ok is true. */
    int failedTxIndex{-1};
};

/**
 * Iterate `block.vtx`, dispatching every EVM-typed transaction
 * (TRANSACTION_EVM_DEPLOY / CALL / SPEND) through the matching
 * ProcessEvm*Tx with the supplied execution context.
 *
 * The function maintains a single CEvmStateCache instance across all
 * txs in the block: state changes from tx N are visible to tx N+1,
 * matching Ethereum's sequential-execution semantics within a block.
 * (Phase 2.5 will introduce a worker pool that executes txs in
 * parallel against private caches and merges serially — same external
 * result, faster.)
 *
 * Non-EVM transactions (nType == TRANSACTION_NORMAL, asset txs,
 * ProRegTx, etc.) are skipped without effect on the cache.
 *
 * On pre-flight failure of any EVM tx, processing stops and `ok`
 * is set to false with `failedTxIndex` pointing at the offending tx.
 * The caller is expected to discard the cache and reject the block.
 *
 * On charged failure (REVERT / OOG / FAILURE with gas debited), the
 * tx is included in `txResults` with its `apply.statusCode` reflecting
 * the EVM result; processing continues with the next tx. This matches
 * Ethereum semantics: a reverted tx is a valid block inclusion.
 *
 * `contextTemplate` supplies the block-level fields (chainId, height,
 * timestamp, baseFee, coinbase, gasLimit). Per-tx fields (origin,
 * gasPrice) are filled by the dispatcher.
 */
BlockProcessResult ProcessEvmTransactionsInBlock(
    const CBlock& block,
    const CBlockIndex* pindex,
    CEvmStateCache& cache,
    const ExecutionContext& contextTemplate);

/**
 * Phase 2.4 — verify a block's coinbase realises every EVM_SPEND
 * UTXO credit, and return their consensus-recomputed sum.
 *
 * `credits` is the authoritative set the validator recomputed from
 * re-executing the block's EVM_SPEND txs (never trusted from the
 * block). Each credit must match a DISTINCT, not-yet-consumed
 * coinbase output by EXACT (scriptPubKey, nValue) — multiset
 * containment, so N identical credits require N matching outputs.
 *
 * Returns false (and leaves `total` unspecified) if any credit is
 * missing from the coinbase, or any credit amount / the aggregate is
 * out of MoneyRange. On true, `total` is the sum the caller adds to
 * the allowed coinbase value: non-inflationary, because the EVM side
 * already destroyed exactly that much balance. This is the security
 * boundary that stops a miner inflating (cap rises only by the
 * destroyed amount) or redirecting a SPEND to itself (the credit
 * must pay its own destination script).
 */
bool CheckCoinbaseRealisesSpendCredits(
    const std::vector<ApplyResult::UtxoCredit>& credits,
    const CTransaction& coinbase,
    CAmount& total);

/**
 * D2 — the five EVM consensus values a v3 coinbase commits, computed
 * over a fully-assembled block. Single source of truth shared by the
 * miner (CreateNewBlock) and the test harness (CreateBlock) so a
 * coinbase is always built over the SAME block the validator will
 * re-execute — miner output == validator recompute.
 */
struct EvmCoinbaseCommitment
{
    bool ok{false};
    uint256 stateRoot;
    uint256 receiptsRoot;
    uint64_t baseFee{0};
    uint64_t gasUsed{0};
    uint64_t execTime{0};
    uint64_t totalCoinbaseTip{0};  // weis (caller converts to sat)
};

/**
 * Compute the v3 EVM coinbase commitment for `block` built on
 * `pindexPrev`, executing the block's EVM txs against a THROWAWAY
 * cache over `db` (never flushed). Derives execTime = max(parent MTP
 * + 1, block.nTime), the EIP-1559 base fee from the parent's
 * committed value, and the state/receipts roots + gas used + tip from
 * a deterministic ProcessEvmTransactionsInBlock pass. `ok=false` if
 * the EVM pass fails (the block cannot be built/validated).
 */
EvmCoinbaseCommitment ComputeCoinbaseEvmCommitment(
    const CBlock& block, const CBlockIndex* pindexPrev,
    CEvmStateDB& db, const Consensus::Params& consensus);

} // namespace evm

#endif // RAPTOREUM_EVM_CONNECTBLOCK_H
