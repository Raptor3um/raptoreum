// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_PROCESS_H
#define RAPTOREUM_EVM_PROCESS_H

#include <evm/apply.h>
#include <evm/evmtx.h>
#include <evm/host.h>

#include <cstdint>

namespace evm {

class CEvmStateCache;

/**
 * EIP-1559 fee split for a single EVM transaction.
 *
 *   total_paid_by_sender = gas_used * effective_gas_price
 *                        = burned + coinbaseTip
 *
 *   effective_gas_price  = min(maxFeePerGas,
 *                              baseFee + maxPriorityFeePerGas)
 *   priority_per_gas     = effective_gas_price - baseFee  (>= 0)
 *
 *   burned       = gas_used * baseFee
 *   coinbaseTip  = gas_used * priority_per_gas
 *
 * The caller (Phase 2.4 ConnectBlock integration) sums `coinbaseTip`
 * across all EVM txs in the block and credits the miner's coinbase
 * output. `burned` is removed from circulation (no recipient) —
 * matches the EIP-1559 deflationary mechanism.
 */
struct ProcessFee
{
    uint64_t burned{0};      // gas_used * baseFee, in weis
    uint64_t coinbaseTip{0}; // gas_used * priority_per_gas, in weis
};

/**
 * Result of fully processing an EVM transaction, including fee
 * accounting and sender-side state mutations (nonce bump, balance
 * debit / refund).
 */
struct ProcessResult
{
    /** Inner Apply* result with status, gasUsed, logs, returnData,
     *  and any UTXO credits (spend txs only). */
    ApplyResult apply;

    /** Fee split computed from gasUsed and the EIP-1559 parameters. */
    ProcessFee fee;

    /** True if the upfront balance / nonce check failed BEFORE any
     *  execution. In this case the tx is rejected from the block;
     *  callers (Phase 2.4 ConnectBlock) treat this as a block
     *  validation error rather than a charged failure. */
    bool preflightFailed{false};
};

/**
 * Process an EVM CALL transaction with full fee accounting (Phase 2.4).
 *
 *   1. Pre-flight checks against current sender state:
 *      - Account exists.
 *      - account.nonce == payload.nonce  (replay protection).
 *      - account.balance >= gasLimit * maxFeePerGas + value
 *        (worst-case upfront cost; refund of the unused portion
 *        happens post-execution).
 *      - payload.maxPriorityFeePerGas <= payload.maxFeePerGas.
 *      - context.baseFee fits in uint64 AND <= payload.maxFeePerGas.
 *      Any failure here returns preflightFailed=true; the caller
 *      rejects the block.
 *
 *   2. Compute effective_gas_price = min(maxFeePerGas,
 *      baseFee + maxPriorityFeePerGas). Reserve the upfront cost
 *      (gasLimit * effective_gas_price + value) from the sender's
 *      balance. Bump sender's nonce.
 *
 *   3. Open a savepoint, dispatch through ApplyEvmCallTx.
 *
 *   4. Post-execution:
 *      - On EVMC_SUCCESS: commit savepoint. Refund
 *        (gasLimit - gasUsed) * effective_gas_price to sender.
 *      - On EVMC_REVERT or any failure: revert savepoint (rolls back
 *        all storage / log / created-account effects). Refund
 *        (gasLimit - gasUsed) * effective_gas_price to sender —
 *        the sender keeps the value (no value transfer happened
 *        from sender's perspective at this layer; the inner
 *        snapshot/revert handles the EVM-side debit if the call
 *        attempted it).
 *
 *   5. Split the consumed gas fee into burned + coinbaseTip and
 *      record both in the ProcessFee.
 *
 * Note: nonce remains bumped on every outcome (success or charged
 * failure), matching Ethereum semantics. Only a preflight failure
 * leaves the nonce untouched (because that tx was rejected from the
 * block entirely).
 */
ProcessResult ProcessEvmCallTx(const CEvmCallTx& payload,
                               CEvmStateCache& cache,
                               const ExecutionContext& context);

/**
 * Process an EVM DEPLOY transaction with full fee accounting.
 *
 * Same control flow as ProcessEvmCallTx but the inner dispatch is
 * ApplyEvmDeployTx. CEvmDeployTx has no `value` field today (born
 * with zero balance), so the upfront reserve is gasLimit *
 * maxFeePerGas only.
 */
ProcessResult ProcessEvmDeployTx(const CEvmDeployTx& payload,
                                 CEvmStateCache& cache,
                                 const ExecutionContext& context);

/**
 * Process an EVM SPEND transaction with full fee accounting.
 *
 * Spends use a fixed gas of 21000 (matching Ethereum's intrinsic
 * value-transfer cost). The sender pre-debit accounts for both the
 * gas reservation AND the spent amount.
 */
ProcessResult ProcessEvmSpendTx(const CEvmSpendTx& payload,
                                CEvmStateCache& cache,
                                const ExecutionContext& context);

} // namespace evm

#endif // RAPTOREUM_EVM_PROCESS_H
