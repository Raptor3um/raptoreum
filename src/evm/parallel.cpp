// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/parallel.h>

#include <evm/balance.h>
#include <evm/evmtx.h>
#include <evm/process.h>
#include <evm/state_cache.h>

#include <evo/specialtx.h>
#include <primitives/block.h>
#include <primitives/transaction.h>

#include <algorithm>
#include <cstring>
#include <future>
#include <thread>
#include <vector>

namespace evm {

namespace {

// ----------------------------------------------------------------------
// Pre-flight intermediate: a successfully-deserialized payload + the
// per-tx execution context that the serial phase will use.
// Exactly one payload variant is populated, matching txType.
// ----------------------------------------------------------------------
struct PreflightedTx
{
    int blockIndex{-1};
    int txType{0};
    bool deserOk{false};

    CEvmDeployTx deployPayload;
    CEvmCallTx callPayload;
    CEvmSpendTx spendPayload;
};

uint64_t BaseFeeUint64(const uint256& baseFee)
{
    for (int i = 0; i < 24; ++i) {
        if (*(baseFee.begin() + i) != 0) return 0;
    }
    return Uint256ToLowUint64(baseFee);
}

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

ExecutionContext PerTxContext(const ExecutionContext& tmpl,
                              const uint256& senderHash,
                              uint64_t effectiveGasPrice)
{
    ExecutionContext c = tmpl;
    std::memcpy(c.txOrigin.begin(), senderHash.begin() + 12, 20);
    c.txGasPrice = Uint256FromUint64(effectiveGasPrice);
    return c;
}

// Worker task: deserialize the payload for a single EVM tx.
PreflightedTx PreflightOne(const CTransaction& tx, int blockIndex)
{
    PreflightedTx out;
    out.blockIndex = blockIndex;
    out.txType = tx.nType;
    out.deserOk = false;

    if (tx.nType == TRANSACTION_EVM_DEPLOY) {
        if (GetTxPayload(tx, out.deployPayload)) out.deserOk = true;
    } else if (tx.nType == TRANSACTION_EVM_CALL) {
        if (GetTxPayload(tx, out.callPayload)) out.deserOk = true;
    } else if (tx.nType == TRANSACTION_EVM_SPEND) {
        if (GetTxPayload(tx, out.spendPayload)) out.deserOk = true;
    } else {
        // Caller filters out non-EVM txs before queuing — defensive.
        out.deserOk = false;
    }
    return out;
}

} // anonymous namespace

BlockProcessResult ParallelProcessEvmTransactionsInBlock(
    const CBlock& block,
    const CBlockIndex* /*pindex*/,
    CEvmStateCache& cache,
    const ExecutionContext& contextTemplate,
    const ParallelOptions& opts)
{
    BlockProcessResult out;
    const uint64_t baseFee = BaseFeeUint64(contextTemplate.baseFee);

    // -- Stage 1: collect EVM-typed tx indices. ----------------------
    std::vector<int> evmIndices;
    for (size_t i = 0; i < block.vtx.size(); ++i) {
        const int t = block.vtx[i]->nType;
        if (t == TRANSACTION_EVM_DEPLOY || t == TRANSACTION_EVM_CALL ||
            t == TRANSACTION_EVM_SPEND)
        {
            evmIndices.push_back(static_cast<int>(i));
        }
    }
    if (evmIndices.empty()) {
        return out; // trivially ok; no EVM work in this block
    }

    // -- Stage 2: parallel pre-flight. -------------------------------
    // Dispatch one std::async per tx with launch::async to force a
    // worker thread per task. For very large block.vtx counts a real
    // bounded pool would be cheaper; std::async is good enough at the
    // block-size scale we're targeting (≤ a few hundred EVM txs).
    // The numWorkers option is currently advisory — std::async with
    // launch::async picks its own scheduling; we keep the field for
    // when we replace it with a CCheckQueue-based pool (next pass).
    (void)opts;

    std::vector<std::future<PreflightedTx>> futures;
    futures.reserve(evmIndices.size());
    for (int idx : evmIndices) {
        const CTransaction& tx = *block.vtx[idx];
        futures.push_back(std::async(std::launch::async,
            [&tx, idx]() { return PreflightOne(tx, idx); }));
    }
    std::vector<PreflightedTx> preflighted;
    preflighted.reserve(futures.size());
    for (auto& f : futures) preflighted.push_back(f.get());

    // -- Stage 3: serial execution against the shared cache. --------
    // Identical control flow to the serial ProcessEvmTransactionsIn-
    // Block — the only difference is that deserialization already
    // happened in workers. Same conflict-free invariant: state
    // mutations stay single-threaded.
    for (const auto& pf : preflighted) {
        if (!pf.deserOk) {
            out.ok = false;
            out.failedTxIndex = pf.blockIndex;
            return out;
        }

        ProcessResult result;
        if (pf.txType == TRANSACTION_EVM_DEPLOY) {
            const auto& p = pf.deployPayload;
            const uint64_t eff = EffectiveGasPriceForCtx(
                baseFee, p.maxFeePerGas, p.maxPriorityFeePerGas);
            const auto ctx = PerTxContext(contextTemplate,
                                          p.senderHash, eff);
            result = ProcessEvmDeployTx(p, cache, ctx);
        } else if (pf.txType == TRANSACTION_EVM_CALL) {
            const auto& p = pf.callPayload;
            const uint64_t eff = EffectiveGasPriceForCtx(
                baseFee, p.maxFeePerGas, p.maxPriorityFeePerGas);
            const auto ctx = PerTxContext(contextTemplate,
                                          p.senderHash, eff);
            result = ProcessEvmCallTx(p, cache, ctx);
        } else { // SPEND
            const auto& p = pf.spendPayload;
            const uint64_t eff = EffectiveGasPriceForCtx(
                baseFee, p.maxFeePerGas, p.maxPriorityFeePerGas);
            const auto ctx = PerTxContext(contextTemplate,
                                          p.fromAddress, eff);
            result = ProcessEvmSpendTx(p, cache, ctx);
        }

        if (result.preflightFailed) {
            out.ok = false;
            out.failedTxIndex = pf.blockIndex;
            return out;
        }

        out.totalCoinbaseTip += result.fee.coinbaseTip;
        out.totalBurned += result.fee.burned;
        for (auto& credit : result.apply.utxoCredits) {
            out.utxoCredits.push_back(credit);
        }
        out.txResults.push_back(std::move(result));
    }

    return out;
}

} // namespace evm
