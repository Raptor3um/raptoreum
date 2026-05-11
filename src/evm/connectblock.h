// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_CONNECTBLOCK_H
#define RAPTOREUM_EVM_CONNECTBLOCK_H

#include <evm/apply.h>
#include <evm/host.h>
#include <evm/process.h>

#include <cstdint>
#include <vector>

class CBlock;
class CBlockIndex;

namespace evm {

class CEvmStateCache;

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

} // namespace evm

#endif // RAPTOREUM_EVM_CONNECTBLOCK_H
