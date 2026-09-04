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

/**
 * Process a CEvmFundTx (AAL: UTXO -> EVM funding).
 *
 * FUND is funded by the transaction's UTXO inputs, not by an EVM
 * account, so it has NO gas/EIP-1559 pre-flight and NO fee split: it
 * simply credits the destination EVM account (ApplyEvmFundTx) under a
 * snapshot. A failure (unreachable for a tx that passed CheckEvmFundTx
 * — precision is pre-validated, overflow is impossible for real
 * amounts) is reported as preflightFailed so ConnectBlock rejects the
 * block. The UTXO-side accounting (removing the funded amount from the
 * miner-claimable fee) lives in checkSpecialTxFee.
 */
ProcessResult ProcessEvmFundTx(const CEvmFundTx& payload,
                               CEvmStateCache& cache,
                               const ExecutionContext& context);

/**
 * Process a CWrapAssetTx / CUnwrapAssetTx (D4 Smart-Asset mirror).
 *
 * Like FUND, these execute no EVM code: no gas, no EIP-1559 pre-flight, no
 * fee split. They credit (wrap) or debit (unwrap) the asset's EVM-side
 * ERC-20 ledger under a snapshot via ApplyWrap/UnwrapAssetTx. A failure is
 * reported as preflightFailed so ConnectBlock rejects the whole block:
 *   - wrap: balance/supply overflow (unreachable for real supply);
 *   - unwrap: the sender does not hold enough wrapped units — rejecting the
 *     block is mandatory, since the UTXO-side mint was already validated and
 *     must never stand without its matching EVM burn.
 */
ProcessResult ProcessEvmWrapAssetTx(const CWrapAssetTx& payload,
                                    CEvmStateCache& cache,
                                    const ExecutionContext& context);

ProcessResult ProcessEvmUnwrapAssetTx(const CUnwrapAssetTx& payload,
                                      CEvmStateCache& cache,
                                      const ExecutionContext& context);

// ---------------------------------------------------------------------
// EIP-1559 base-fee adjustment (D2).
// ---------------------------------------------------------------------
//
// Standard Ethereum constants. NOTE (plan review A9): the 1/8
// max-change-per-block was tuned for Ethereum's ~12s blocks; RTM's
// ~2-min block time means far fewer blocks per unit wall-clock, so
// the effective responsiveness differs. These are consensus
// constants the team finalises before the EVM_COMMIT mainnet vote —
// increment 5 pins the *mechanism* (this deterministic function);
// the exact denominators remain a documented, single-point tunable.
static constexpr uint64_t kEvmElasticityMultiplier = 2;
static constexpr uint64_t kEvmBaseFeeMaxChangeDenominator = 8;
// First committed (activation) block's base fee, in weis — 1 gwei,
// matching Ethereum's INITIAL_BASE_FEE.
static constexpr uint64_t kInitialEvmBaseFee = 1000000000ULL;

/**
 * Canonical EIP-1559 base fee for a block, computed from the PARENT
 * block's committed base fee, gas used and gas limit. Pure,
 * deterministic, overflow-safe integer math (EIP-1559 reference).
 *
 * Used to (a) recompute-and-validate the committed cbTx.evmBaseFee
 * in ConnectBlock and (b) — later, increment 6 — let the miner
 * produce it. Saturates at uint64 max on the way up and floors at 0
 * on the way down; below/at target never increases, above target
 * increases by at least 1 (per the spec).
 */
uint64_t ComputeNextBaseFee(uint64_t parentBaseFee,
                            uint64_t parentGasUsed,
                            uint64_t parentGasLimit);

} // namespace evm

#endif // RAPTOREUM_EVM_PROCESS_H
