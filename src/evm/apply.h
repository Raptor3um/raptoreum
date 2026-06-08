// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_APPLY_H
#define RAPTOREUM_EVM_APPLY_H

#include <amount.h>
#include <evm/evmtx.h>
#include <evm/host.h>
#include <script/script.h>
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

    /** Gas refund accumulated during execution (mostly from SSTORE
     *  clearing non-zero slots to zero, and historically from
     *  SELFDESTRUCT). The Phase 2.4 fee accounting layer is expected
     *  to apply this refund — capped at `gasUsed / 5` per EIP-3529
     *  (London+) — when settling the sender's gas debit. ApplyEvmTx
     *  itself does not deduct the refund from `gasUsed`; it just
     *  surfaces evmone's raw `gas_refund` for the caller to consume. */
    int64_t gasRefund{0};

    /** Bytes returned by the contract via RETURN. */
    std::vector<uint8_t> returnData;

    /** Logs (LOG0..LOG4) emitted during execution. Caller drains these
     *  into the surrounding block's receipt set on success. */
    std::vector<CEvmHost::Log> logs;

    /** Addresses that issued SELFDESTRUCT. Caller processes the
     *  account deletion + beneficiary balance transfer after a
     *  successful execution. */
    std::set<evmc::address> selfdestructs;

    /** Addresses CREATEd / CREATE2'd during this transaction. Per
     *  EIP-6780 (Cancun), only the intersection
     *  `selfdestructs ∩ sameTxCreated` actually deletes accounts;
     *  everything else in `selfdestructs` is balance-transfer-only. */
    std::set<evmc::address> sameTxCreated;

    /** Set by ApplyEvmDeployTx on EVMC_SUCCESS: the 20-byte address
     *  the new contract was created at. Zero-initialized for CALL
     *  and SPEND results. */
    uint160 deployedAddress;

    /** A UTXO output that the caller must add to the surrounding
     *  block, produced by ApplyEvmSpendTx on success. Empty for
     *  CALL and DEPLOY results. The script is the destination
     *  output script (typically a P2PKH); amount is in RTM satoshis. */
    struct UtxoCredit
    {
        CScript script;
        CAmount amount{0};
    };
    std::vector<UtxoCredit> utxoCredits;
};

/**
 * Conversion factor between EVM weis and UTXO-side satoshis.
 *
 * RTM uses Bitcoin-style 1 RTM = 10^8 satoshis. EVM accounting uses
 * Ethereum-style 1 RTM = 10^18 weis. Therefore 1 satoshi = 10^10 weis,
 * and any amount transferred between the two sides must be an exact
 * multiple of this factor — otherwise we either round (losing money)
 * or refuse (we refuse, returning EVMC_FAILURE so the user knows to
 * round the value themselves rather than the chain silently truncating).
 */
constexpr uint64_t kWeisPerSatoshi = 10'000'000'000ULL;

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

/**
 * Execute a CEvmDeployTx against the cache via evmone (Phase 2.3b).
 *
 *   1. Derive the contract address from the payload's sender and
 *      nonce using the standard Ethereum CREATE formula:
 *
 *          address = last 20 bytes of keccak256(rlp([sender, nonce]))
 *
 *      An address derived this way is byte-identical to what geth /
 *      erigon / nethermind would produce for the same (sender, nonce)
 *      pair. The chosen address is reflected in
 *      ApplyResult.deployedAddress for the caller to record in a
 *      transaction receipt.
 *
 *   2. CREATE-collision check (matches A11): refuse to overwrite a
 *      pre-existing account that already has code (codeHash !=
 *      EmptyCodeHash) or a non-zero nonce. Refusal returns
 *      EVMC_FAILURE in the result.
 *
 *   3. Dispatch the init bytecode (payload.code) through evmone with
 *      EVMC_CREATE kind. evmone executes the constructor and the
 *      RETURN output of the init code becomes the runtime code we
 *      persist.
 *
 *   4. On EVMC_SUCCESS: write the runtime code (keyed by its
 *      keccak256 hash) and a fresh CEvmAccount(nonce=1, balance=value,
 *      codeHash=hash, storageRoot=empty) for the new address. The
 *      caller (Phase 2.4 ConnectBlock) is responsible for the
 *      sender-side bookkeeping (nonce increment, balance debit for
 *      gas + value).
 *
 *   5. On revert or any non-success status: the cache is left as
 *      evmone wrote it; the caller Discard()s.
 *
 * Same scope notes as ApplyEvmCallTx — no gas accounting, no sender
 * nonce/balance updates in this commit; those live in Phase 2.4.
 */
ApplyResult ApplyEvmDeployTx(const CEvmDeployTx& payload,
                             CEvmStateCache& cache,
                             const ExecutionContext& context);

/**
 * Execute a CEvmSpendTx against the cache (Phase 2.3c).
 *
 * Moves RTM weis from an EVM account to a UTXO output:
 *
 *   1. Verify payload.amount is an exact multiple of kWeisPerSatoshi
 *      (10^10). Non-multiples are rejected with EVMC_FAILURE — the
 *      chain never silently rounds funds away.
 *
 *   2. Verify payload.outputScript is non-empty (the destination
 *      UTXO output script). Empty is rejected.
 *
 *   3. Load the source EVM account from the cache. Account must
 *      exist (no implicit zero-balance accounts on the EVM side).
 *
 *   4. Verify account.balance >= payload.amount (no overdraft).
 *
 *   5. Debit the EVM account's balance and write the updated record
 *      back through the cache.
 *
 *   6. Record the satoshi-amount UTXO output in ApplyResult.utxoCredits.
 *      The caller (Phase 2.4 ConnectBlock) creates the actual CTxOut
 *      in the block and emits a coinbase-style "EVM credit" output
 *      with this script + amount.
 *
 * Same scope notes as the other Apply* functions: nonce increment
 * and gas accounting happen at the surrounding ConnectBlock layer
 * (Phase 2.4), not here. ApplyEvmSpendTx records an intrinsic
 * gasUsed of 21000 (matches the Ethereum baseline for a simple
 * value transfer) so the caller has a reasonable lower bound.
 */
ApplyResult ApplyEvmSpendTx(const CEvmSpendTx& payload,
                            CEvmStateCache& cache,
                            const ExecutionContext& context);

/**
 * Execute a CEvmFundTx against the cache (AAL: UTXO -> EVM funding).
 *
 * The inverse of ApplyEvmSpendTx. The transaction's real UTXO inputs
 * are consumed by the normal UTXO machinery; the funded amount is
 * removed from the miner-claimable fee in checkSpecialTxFee (it leaves
 * the UTXO money supply). Here we credit it to the EVM side:
 *
 *   1. Verify payload.amount is an exact multiple of kWeisPerSatoshi
 *      (10^10) so it round-trips against the satoshi amount removed
 *      from the UTXO side.
 *
 *   2. Load (or force-create, EIP-161 style) the destination EVM
 *      account — funding a brand-new address is the whole point, so an
 *      absent account is created with nonce 0, empty code/storage.
 *
 *   3. Credit account.balance += payload.amount (overflow-checked)
 *      and write it back through the cache.
 *
 * No gas, no nonce, no UTXO credit — FUND does not execute EVM code.
 * The credit lands in the EVM state cache and is committed in the
 * block's evmStateRoot (D2) like any other state change.
 */
ApplyResult ApplyEvmFundTx(const CEvmFundTx& payload,
                           CEvmStateCache& cache,
                           const ExecutionContext& context);

} // namespace evm

#endif // RAPTOREUM_EVM_APPLY_H
