// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/process.h>

#include <evm/account.h>
#include <evm/balance.h>
#include <evm/state_cache.h>

#include <cstring>
#include <limits>

namespace evm {

// ----------------------------------------------------------------------
// EIP-1559 fee math
// ----------------------------------------------------------------------

namespace {

// uint64 multiplication with overflow detection.
// Returns false (and leaves `out` unspecified) on overflow.
bool MulU64(uint64_t a, uint64_t b, uint64_t& out)
{
    if (a == 0 || b == 0) {
        out = 0;
        return true;
    }
    if (a > std::numeric_limits<uint64_t>::max() / b) {
        return false;
    }
    out = a * b;
    return true;
}

// uint64 addition with overflow detection.
bool AddU64(uint64_t a, uint64_t b, uint64_t& out)
{
    const uint64_t sum = a + b;
    if (sum < a) return false;
    out = sum;
    return true;
}

// Read the low 64 bits of context.baseFee. Reject (returns false) if
// the high 192 bits are non-zero — a baseFee that big would dwarf any
// imaginable RTM market price.
bool BaseFeeAsUint64(const uint256& baseFee, uint64_t& out)
{
    for (int i = 0; i < 24; ++i) {
        if (*(baseFee.begin() + i) != 0) return false;
    }
    out = Uint256ToLowUint64(baseFee);
    return true;
}

// Extract the 160-bit EVM address from a uint256 (low 20 bytes).
uint160 EvmAddrFromUint256(const uint256& u)
{
    uint160 addr;
    std::memcpy(addr.begin(), u.begin() + 12, 20);
    return addr;
}

// Common pre-flight result. We zero-fill on the failure path so any
// downstream code that inspects the result before checking
// preflightFailed sees innocuous values.
ProcessResult MakePreflightFailure()
{
    ProcessResult r;
    r.preflightFailed = true;
    r.apply.statusCode = EVMC_FAILURE;
    r.apply.gasUsed = 0;
    return r;
}

// Per EIP-1559: effective_gas_price = min(maxFeePerGas, baseFee + priorityFee).
// Returns false on overflow in the inner add.
bool EffectiveGasPrice(uint64_t baseFee,
                      uint64_t maxFeePerGas,
                      uint64_t maxPriorityFeePerGas,
                      uint64_t& outEffective)
{
    uint64_t cap;
    if (!AddU64(baseFee, maxPriorityFeePerGas, cap)) return false;
    outEffective = std::min(maxFeePerGas, cap);
    return true;
}

// Split the consumed-gas fee into burned and coinbase tip.
//   burned = gasUsed * baseFee
//   tip    = gasUsed * (effective - baseFee)
bool ComputeFeeSplit(uint64_t gasUsed,
                    uint64_t baseFee,
                    uint64_t effectiveGasPrice,
                    ProcessFee& outFee)
{
    if (!MulU64(gasUsed, baseFee, outFee.burned)) return false;
    const uint64_t tipPerGas = effectiveGasPrice - baseFee; // effective >= base
    return MulU64(gasUsed, tipPerGas, outFee.coinbaseTip);
}

} // anonymous namespace

// ----------------------------------------------------------------------
// Shared core: parameters + control flow that ProcessEvmCallTx and
// ProcessEvmDeployTx both use. ProcessEvmSpendTx has its own path
// because it doesn't dispatch through evmone.
// ----------------------------------------------------------------------

namespace {

// Bundle the fee/nonce/balance pre-flight data the inner step needs.
struct PreFlight
{
    uint64_t baseFee;
    uint64_t effectiveGasPrice;
    uint64_t upfrontCost;       // gasLimit * effective + value
    uint64_t gasReservation;    // gasLimit * effective
};

// Run pre-flight checks common to CALL and DEPLOY. On success:
//   - debits upfrontCost from sender's balance,
//   - bumps sender's nonce,
//   - writes the updated sender account back to the cache,
//   - populates `outPF`.
// Returns false on any pre-flight violation (caller returns
// preflightFailed=true).
bool PreFlightCallOrDeploy(const uint256& senderHash,
                          uint64_t payloadNonce,
                          uint64_t gasLimit,
                          uint64_t maxFeePerGas,
                          uint64_t maxPriorityFeePerGas,
                          uint64_t value,
                          CEvmStateCache& cache,
                          const ExecutionContext& context,
                          PreFlight& outPF)
{
    // Tip cap must not exceed the absolute cap.
    if (maxPriorityFeePerGas > maxFeePerGas) return false;
    // Pull baseFee as uint64 (reject absurdly large values).
    if (!BaseFeeAsUint64(context.baseFee, outPF.baseFee)) return false;
    // base fee must be coverable by the user's cap.
    if (outPF.baseFee > maxFeePerGas) return false;
    if (!EffectiveGasPrice(outPF.baseFee, maxFeePerGas, maxPriorityFeePerGas,
                           outPF.effectiveGasPrice)) return false;
    // Worst-case upfront cost is gasLimit * maxFeePerGas (so the refund
    // path always has the right capacity). Bill the user against that
    // here and refund the difference once gas_used is known.
    if (!MulU64(gasLimit, maxFeePerGas, outPF.gasReservation)) return false;
    if (!AddU64(outPF.gasReservation, value, outPF.upfrontCost)) return false;

    // Load sender, check nonce and balance.
    const uint160 senderAddr = EvmAddrFromUint256(senderHash);
    CEvmAccount sender;
    if (!cache.GetAccount(senderAddr, sender)) return false;
    if (sender.nonce != payloadNonce) return false;
    if (!Uint256GreaterOrEqualUint64(sender.balance, outPF.upfrontCost)) return false;

    // Debit upfront cost and bump nonce immediately. If the inner
    // dispatch fails or reverts we'll refund the unused gas portion;
    // the nonce bump stays (matches Ethereum: a charged failure still
    // increments the nonce so the same payload cannot be replayed).
    if (!Uint256SubUint64(sender.balance, outPF.upfrontCost)) return false;
    sender.nonce += 1;
    cache.SetAccount(senderAddr, sender);
    return true;
}

// Refund the unused gas portion to the sender's balance (post-execution).
//   refund = (gasLimit - gasUsed) * maxFeePerGas
//
// We pre-debited gasLimit * maxFeePerGas (the worst case). Now that
// we know gasUsed, we refund the unused portion at the SAME rate.
// The effective spend at the EIP-1559 rate is gasUsed *
// effective_gas_price (computed by the caller for the fee split).
// The difference between maxFeePerGas and effective_gas_price for
// the consumed portion goes back to the sender too — that's just
// (gasLimit*maxFeePerGas) - upfront + (gasUsed*effective - gasUsed*
// maxFeePerGas) which collapses algebraically into "refund the cap-
// vs-effective delta on consumed gas as part of the unused refund".
//
// Concretely we compute the post-execution credit as:
//   credit = gasReservation - gasUsed * effective_gas_price
// since gasReservation = gasLimit * maxFeePerGas (the upfront billing).
//
// This is the textbook EIP-1559 implementation.
bool RefundSender(const uint256& senderHash,
                 uint64_t gasUsed,
                 const PreFlight& pf,
                 CEvmStateCache& cache)
{
    uint64_t spent;
    if (!MulU64(gasUsed, pf.effectiveGasPrice, spent)) return false;
    if (spent > pf.gasReservation) {
        // Shouldn't happen: effective <= maxFee, and gasUsed <= gasLimit.
        // Defensive: cap refund at zero rather than mint money.
        spent = pf.gasReservation;
    }
    const uint64_t credit = pf.gasReservation - spent;
    if (credit == 0) return true;

    const uint160 senderAddr = EvmAddrFromUint256(senderHash);
    CEvmAccount sender;
    if (!cache.GetAccount(senderAddr, sender)) return false;
    if (!Uint256AddUint64(sender.balance, credit)) return false;
    cache.SetAccount(senderAddr, sender);
    return true;
}

// On a charged failure (REVERT / OOG / etc.), the value never moved
// from the sender's POV at the apply layer, so refunding only the
// gas-portion is correct here. Plus we must restore the `value` we
// reserved upfront.
bool RefundSenderOnFailure(const uint256& senderHash,
                          uint64_t gasUsed,
                          uint64_t value,
                          const PreFlight& pf,
                          CEvmStateCache& cache)
{
    uint64_t gasSpent;
    if (!MulU64(gasUsed, pf.effectiveGasPrice, gasSpent)) return false;
    if (gasSpent > pf.gasReservation) gasSpent = pf.gasReservation;
    // Gas refund.
    const uint64_t gasCredit = pf.gasReservation - gasSpent;
    // Value refund — failure means the value never reached the
    // recipient (the apply-layer snapshot rolled it back).
    uint64_t totalCredit;
    if (!AddU64(gasCredit, value, totalCredit)) return false;
    if (totalCredit == 0) return true;

    const uint160 senderAddr = EvmAddrFromUint256(senderHash);
    CEvmAccount sender;
    if (!cache.GetAccount(senderAddr, sender)) return false;
    if (!Uint256AddUint64(sender.balance, totalCredit)) return false;
    cache.SetAccount(senderAddr, sender);
    return true;
}

} // anonymous namespace

// ----------------------------------------------------------------------
// ProcessEvmCallTx
// ----------------------------------------------------------------------

ProcessResult ProcessEvmCallTx(const CEvmCallTx& payload,
                               CEvmStateCache& cache,
                               const ExecutionContext& context)
{
    PreFlight pf{};
    if (!PreFlightCallOrDeploy(payload.senderHash, payload.nonce,
                               payload.gasLimit,
                               payload.maxFeePerGas,
                               payload.maxPriorityFeePerGas,
                               payload.value,
                               cache, context, pf))
    {
        return MakePreflightFailure();
    }

    const int snap = cache.Snapshot();
    ApplyResult inner = ApplyEvmCallTx(payload, cache, context);

    const bool ok = (inner.statusCode == EVMC_SUCCESS);
    if (ok) {
        cache.Commit(snap);
        // Successful tx: the value transfer (if any) was done by
        // evmone via host.call; we only refund the unused gas at
        // the effective rate.
        if (!RefundSender(payload.senderHash, static_cast<uint64_t>(inner.gasUsed),
                          pf, cache)) {
            // Refund failed — treat as a charged failure to preserve
            // the invariant that we never mint funds.
            cache.Revert(snap);
            inner.statusCode = EVMC_FAILURE;
        }
    } else {
        cache.Revert(snap);
        // Charged failure: refund unused gas + the upfront-reserved value.
        if (!RefundSenderOnFailure(payload.senderHash,
                                   static_cast<uint64_t>(inner.gasUsed),
                                   payload.value, pf, cache))
        {
            // Catastrophic; do not produce a result that mints funds.
            // The block this tx is in will be rejected.
            return MakePreflightFailure();
        }
    }

    ProcessResult out;
    out.apply = std::move(inner);
    out.preflightFailed = false;
    if (!ComputeFeeSplit(static_cast<uint64_t>(out.apply.gasUsed),
                         pf.baseFee, pf.effectiveGasPrice, out.fee))
    {
        // Fee math overflowed — the upstream block accounting cannot
        // proceed. Mark as preflight failure (defensive; not expected
        // in practice given the earlier MulU64 in PreFlight).
        return MakePreflightFailure();
    }
    return out;
}

// ----------------------------------------------------------------------
// ProcessEvmDeployTx
// ----------------------------------------------------------------------

ProcessResult ProcessEvmDeployTx(const CEvmDeployTx& payload,
                                 CEvmStateCache& cache,
                                 const ExecutionContext& context)
{
    PreFlight pf{};
    // Deploy tx has no `value` field currently (constructor is born
    // with zero balance). Pass 0 as the value reservation.
    if (!PreFlightCallOrDeploy(payload.senderHash, payload.nonce,
                               payload.gasLimit,
                               payload.maxFeePerGas,
                               payload.maxPriorityFeePerGas,
                               /*value=*/ 0,
                               cache, context, pf))
    {
        return MakePreflightFailure();
    }

    const int snap = cache.Snapshot();
    ApplyResult inner = ApplyEvmDeployTx(payload, cache, context);

    const bool ok = (inner.statusCode == EVMC_SUCCESS);
    if (ok) {
        cache.Commit(snap);
        if (!RefundSender(payload.senderHash,
                          static_cast<uint64_t>(inner.gasUsed),
                          pf, cache)) {
            cache.Revert(snap);
            inner.statusCode = EVMC_FAILURE;
        }
    } else {
        cache.Revert(snap);
        if (!RefundSenderOnFailure(payload.senderHash,
                                   static_cast<uint64_t>(inner.gasUsed),
                                   /*value=*/ 0, pf, cache))
        {
            return MakePreflightFailure();
        }
    }

    ProcessResult out;
    out.apply = std::move(inner);
    out.preflightFailed = false;
    if (!ComputeFeeSplit(static_cast<uint64_t>(out.apply.gasUsed),
                         pf.baseFee, pf.effectiveGasPrice, out.fee))
    {
        return MakePreflightFailure();
    }
    return out;
}

// ----------------------------------------------------------------------
// ProcessEvmSpendTx
// ----------------------------------------------------------------------
//
// Spend has a fixed 21000 gas, no inner evmone execution, and the
// amount-being-spent IS the value (treated as the upfront-reserved
// portion for refund math). We still run the full EIP-1559 fee
// accounting so the burned/tip split is correct.

ProcessResult ProcessEvmSpendTx(const CEvmSpendTx& payload,
                                CEvmStateCache& cache,
                                const ExecutionContext& context)
{
    PreFlight pf{};
    // Spend's pre-flight uses the spend amount as the value reservation.
    // Note: ApplyEvmSpendTx itself also subtracts payload.amount from
    // the sender's balance, so we pass value=0 here and let the inner
    // apply do the spend-side debit. The pre-flight only reserves gas.
    if (!PreFlightCallOrDeploy(payload.fromAddress, payload.nonce,
                               payload.gasLimit,
                               payload.maxFeePerGas,
                               payload.maxPriorityFeePerGas,
                               /*value=*/ 0,
                               cache, context, pf))
    {
        return MakePreflightFailure();
    }

    const int snap = cache.Snapshot();
    ApplyResult inner = ApplyEvmSpendTx(payload, cache, context);

    const bool ok = (inner.statusCode == EVMC_SUCCESS);
    if (ok) {
        cache.Commit(snap);
        if (!RefundSender(payload.fromAddress,
                          static_cast<uint64_t>(inner.gasUsed),
                          pf, cache)) {
            cache.Revert(snap);
            inner.statusCode = EVMC_FAILURE;
        }
    } else {
        cache.Revert(snap);
        if (!RefundSenderOnFailure(payload.fromAddress,
                                   static_cast<uint64_t>(inner.gasUsed),
                                   /*value=*/ 0, pf, cache))
        {
            return MakePreflightFailure();
        }
    }

    ProcessResult out;
    out.apply = std::move(inner);
    out.preflightFailed = false;
    if (!ComputeFeeSplit(static_cast<uint64_t>(out.apply.gasUsed),
                         pf.baseFee, pf.effectiveGasPrice, out.fee))
    {
        return MakePreflightFailure();
    }
    return out;
}

ProcessResult ProcessEvmFundTx(const CEvmFundTx& payload,
                               CEvmStateCache& cache,
                               const ExecutionContext& context)
{
    const int snap = cache.Snapshot();
    ApplyResult inner = ApplyEvmFundTx(payload, cache, context);
    ProcessResult out;
    if (inner.statusCode != EVMC_SUCCESS) {
        // Unreachable for a tx that passed CheckEvmFundTx (precision
        // pre-validated; balance overflow impossible for real amounts).
        // Treat as block-invalidating rather than silently dropping a
        // credit the UTXO side already paid for.
        cache.Revert(snap);
        out.preflightFailed = true;
        return out;
    }
    cache.Commit(snap);
    out.apply = std::move(inner);
    out.preflightFailed = false;
    // No fee: FUND executes no EVM code (fee.burned = coinbaseTip = 0).
    return out;
}

uint64_t ComputeNextBaseFee(uint64_t parentBaseFee,
                            uint64_t parentGasUsed,
                            uint64_t parentGasLimit)
{
    if (parentGasLimit == 0) {
        return parentBaseFee;  // degenerate guard (no EVM gas budget)
    }
    const uint64_t target = parentGasLimit / kEvmElasticityMultiplier;
    if (target == 0 || parentGasUsed == target) {
        return parentBaseFee;  // exactly at target → unchanged
    }
    if (parentGasUsed > target) {
        // Above target: base fee rises by
        //   max( parentBaseFee * (used-target) / target / DENOM , 1 ).
        const uint64_t gasDelta = parentGasUsed - target;
        const __uint128_t num =
            static_cast<__uint128_t>(parentBaseFee) * gasDelta;
        uint64_t change = static_cast<uint64_t>(
            num / target / kEvmBaseFeeMaxChangeDenominator);
        if (change < 1) {
            change = 1;  // EIP-1559: minimum +1 when above target
        }
        if (parentBaseFee >
            std::numeric_limits<uint64_t>::max() - change) {
            return std::numeric_limits<uint64_t>::max();  // saturate
        }
        return parentBaseFee + change;
    }
    // Below target: base fee falls by
    //   parentBaseFee * (target-used) / target / DENOM, floored at 0.
    const uint64_t gasDelta = target - parentGasUsed;
    const __uint128_t num =
        static_cast<__uint128_t>(parentBaseFee) * gasDelta;
    const uint64_t change = static_cast<uint64_t>(
        num / target / kEvmBaseFeeMaxChangeDenominator);
    return change >= parentBaseFee ? 0 : parentBaseFee - change;
}

} // namespace evm
