// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/connectblock.h>

#include <evm/balance.h>
#include <evm/evmtx.h>
#include <evm/state_cache.h>

#include <evo/specialtx.h>
#include <amount.h>
#include <primitives/block.h>
#include <primitives/transaction.h>

#include <cstring>
#include <vector>

namespace evm {

namespace {

// Per-tx execution context: clones the block-level template and fills
// in the sender-derived fields from the tx payload.
ExecutionContext PerTxContext(const ExecutionContext& tmpl,
                              const uint256& senderHash,
                              uint64_t effectiveGasPrice)
{
    ExecutionContext c = tmpl;
    std::memcpy(c.txOrigin.begin(), senderHash.begin() + 12, 20);
    c.txGasPrice = Uint256FromUint64(effectiveGasPrice);
    return c;
}

// Compute the same effectiveGasPrice that process.cpp uses, so the
// context handed to evmone matches what the tx is being billed at.
// Returns 0 if the EIP-1559 invariants are violated (in which case
// pre-flight will reject the tx — we only use this value to populate
// the context, never to bill).
uint64_t EffectiveGasPriceForCtx(uint64_t baseFee,
                                 uint64_t maxFee,
                                 uint64_t priorityFee)
{
    if (priorityFee > maxFee) return 0;
    if (baseFee > maxFee) return 0;
    const uint64_t cap = baseFee + priorityFee;
    if (cap < baseFee) return 0; // overflow
    return std::min(maxFee, cap);
}

// Read the low 64 bits of an unsigned 256-bit baseFee field as uint64,
// returning 0 if the high 192 bits are non-zero (a value we don't
// support today and which pre-flight will also reject).
uint64_t BaseFeeUint64(const uint256& baseFee)
{
    for (int i = 0; i < 24; ++i) {
        if (*(baseFee.begin() + i) != 0) return 0;
    }
    return Uint256ToLowUint64(baseFee);
}

} // anonymous namespace

BlockProcessResult ProcessEvmTransactionsInBlock(
    const CBlock& block,
    const CBlockIndex* /*pindex*/,
    CEvmStateCache& cache,
    const ExecutionContext& contextTemplate)
{
    BlockProcessResult out;
    const uint64_t baseFee = BaseFeeUint64(contextTemplate.baseFee);

    for (size_t i = 0; i < block.vtx.size(); ++i) {
        // Defensive: the block-template path (miner v3 precompute,
        // D2 inc 6b) runs this before the coinbase slot is
        // materialised, so vtx[0] can be a null placeholder. A null
        // tx is never a valid EVM tx (and never occurs in a real
        // connected block, where vtx[0] is the coinbase) — skip it.
        if (!block.vtx[i]) {
            continue;
        }
        const CTransaction& tx = *block.vtx[i];
        const int txType = tx.nType;
        if (txType != TRANSACTION_EVM_DEPLOY &&
            txType != TRANSACTION_EVM_CALL &&
            txType != TRANSACTION_EVM_SPEND) {
            continue; // not an EVM tx — caller handles it via UpdateCoins etc.
        }

        ProcessResult result;
        if (txType == TRANSACTION_EVM_DEPLOY) {
            CEvmDeployTx payload;
            if (!GetTxPayload(tx, payload)) {
                out.ok = false;
                out.failedTxIndex = static_cast<int>(i);
                return out;
            }
            const uint64_t effective = EffectiveGasPriceForCtx(
                baseFee, payload.maxFeePerGas, payload.maxPriorityFeePerGas);
            const auto perTxCtx = PerTxContext(contextTemplate,
                                              payload.senderHash, effective);
            result = ProcessEvmDeployTx(payload, cache, perTxCtx);
        } else if (txType == TRANSACTION_EVM_CALL) {
            CEvmCallTx payload;
            if (!GetTxPayload(tx, payload)) {
                out.ok = false;
                out.failedTxIndex = static_cast<int>(i);
                return out;
            }
            const uint64_t effective = EffectiveGasPriceForCtx(
                baseFee, payload.maxFeePerGas, payload.maxPriorityFeePerGas);
            const auto perTxCtx = PerTxContext(contextTemplate,
                                              payload.senderHash, effective);
            result = ProcessEvmCallTx(payload, cache, perTxCtx);
        } else { // TRANSACTION_EVM_SPEND
            CEvmSpendTx payload;
            if (!GetTxPayload(tx, payload)) {
                out.ok = false;
                out.failedTxIndex = static_cast<int>(i);
                return out;
            }
            const uint64_t effective = EffectiveGasPriceForCtx(
                baseFee, payload.maxFeePerGas, payload.maxPriorityFeePerGas);
            const auto perTxCtx = PerTxContext(contextTemplate,
                                              payload.fromAddress, effective);
            result = ProcessEvmSpendTx(payload, cache, perTxCtx);
        }

        if (result.preflightFailed) {
            // A pre-flight failure means the tx is malformed at the
            // EVM-fee-accounting layer (bad nonce / insufficient
            // balance / bad EIP-1559 params). Such a tx cannot legally
            // be included in a block; reject the block.
            out.ok = false;
            out.failedTxIndex = static_cast<int>(i);
            return out;
        }

        // Aggregate fee + utxo credits.
        out.totalCoinbaseTip += result.fee.coinbaseTip;
        out.totalBurned += result.fee.burned;
        for (auto& credit : result.apply.utxoCredits) {
            out.utxoCredits.push_back(credit);
        }
        out.txResults.push_back(std::move(result));
        out.txBlockIndices.push_back(static_cast<int>(i));
    }

    return out;
}

bool CheckCoinbaseRealisesSpendCredits(
    const std::vector<ApplyResult::UtxoCredit>& credits,
    const CTransaction& coinbase,
    CAmount& total)
{
    total = 0;
    if (credits.empty()) {
        return true;  // nothing to realise; cap unchanged
    }
    // Multiset containment: each credit consumes one distinct,
    // exactly-matching coinbase output.
    std::vector<bool> used(coinbase.vout.size(), false);
    for (const auto& credit : credits) {
        if (credit.amount < 0 || !MoneyRange(credit.amount)) {
            return false;
        }
        bool matched = false;
        for (size_t v = 0; v < coinbase.vout.size(); ++v) {
            if (used[v]) continue;
            if (coinbase.vout[v].nValue == credit.amount &&
                coinbase.vout[v].scriptPubKey == credit.script) {
                used[v] = true;
                matched = true;
                break;
            }
        }
        if (!matched) {
            return false;
        }
        total += credit.amount;
        if (!MoneyRange(total)) {
            return false;
        }
    }
    return true;
}

} // namespace evm
