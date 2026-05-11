// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_APPLY_H
#define RAPTOREUM_EVM_APPLY_H

#include <evm/evmtx.h>
#include <evm/host.h>
#include <uint256.h>

#include <evmc/evmc.h>

#include <cstdint>
#include <set>
#include <vector>

namespace evm {

class CEvmStateCache;
struct ExecutionContext;

/**
 * Result of applying a single EVM transaction to a state cache.
 *
 * The caller inspects this struct and either:
 *   - on EVMC_SUCCESS: flushes the cache and records the side effects
 *     (logs / selfdestructs) into the surrounding block,
 *   - on EVMC_REVERT or any failure: discards the cache and the side
 *     effects (per EVM semantics — a reverted tx leaves no state trace
 *     beyond the gas paid).
 *
 * Gas refunds at the effective EIP-1559 gas price are not yet modeled
 * in Phase 2.3a; the caller computes them from `gasUsed` and the
 * payload's fee parameters externally. Phase 2.4 wires the full fee
 * accounting into ConnectBlock.
 */
struct ApplyResult
{
    /** evmone's status code: EVMC_SUCCESS, EVMC_REVERT, EVMC_OUT_OF_GAS,
     *  EVMC_INVALID_INSTRUCTION, EVMC_STACK_UNDERFLOW, etc. */
    evmc_status_code statusCode{EVMC_FAILURE};

    /** Gas consumed by the call (gasLimit - r.gas_left). Used by the
     *  caller for fee accounting. */
    int64_t gasUsed{0};

    /** Bytes returned by the contract via RETURN. */
    std::vector<uint8_t> returnData;

    /** Logs (LOG0..LOG4) emitted during execution. Caller drains these
     *  into the surrounding block's receipt set on success. */
    std::vector<CEvmHost::Log> logs;

    /** Addresses that issued SELFDESTRUCT. Caller processes the
     *  account deletion + beneficiary balance transfer after a
     *  successful execution. */
    std::set<evmc::address> selfdestructs;
};

/**
 * Execute a CEvmCallTx against the cache via evmone.
 *
 * Phase 2.3a scope:
 *   - Decodes the payload (caller has already validated it via
 *     CheckEvmCallTx).
 *   - Reads recipient's contract code from the cache (if any).
 *   - Builds an evmc_message with the payload's sender/recipient/value/
 *     calldata/gasLimit and dispatches it through CEvmHost.
 *   - Returns the result. The caller decides whether to Flush() or
 *     Discard() the cache based on the status code.
 *
 * Phase 2.3a deliberately does NOT do:
 *   - Sender balance check / debit-for-gas / refund. These require
 *     coordinated bookkeeping with the cache and the EIP-1559 fee
 *     parameters; Phase 2.4 ConnectBlock integration handles it.
 *   - Nonce increment on the sender. Same reason; needs to be atomic
 *     with the rest of the tx accounting.
 *   - Value transfer at the EVM level. evmone handles the in-call
 *     value via the message, but a value > 0 currently requires the
 *     sender to have been pre-credited. Pre-credit logic lands in
 *     Phase 2.4.
 *
 * Phase 2.3 successors:
 *   - 2.3b ApplyEvmDeployTx: contract creation + address derivation.
 *   - 2.3c ApplyEvmSpendTx: move RTM from an EVM account to a UTXO.
 *
 * Thread safety: like CEvmStateCache, NOT thread-safe. Each EVM
 * transaction in a block gets its own (cache, host) pair during the
 * worker-pool phase per design decision D1.
 */
ApplyResult ApplyEvmCallTx(const CEvmCallTx& payload,
                           CEvmStateCache& cache,
                           const ExecutionContext& context);

} // namespace evm

#endif // RAPTOREUM_EVM_APPLY_H
