// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_PARALLEL_H
#define RAPTOREUM_EVM_PARALLEL_H

#include <evm/connectblock.h>

#include <cstdint>

class CBlock;
class CBlockIndex;

namespace evm {

class CEvmStateCache;
struct ExecutionContext;

/**
 * Tuning knobs for ParallelProcessEvmTransactionsInBlock.
 */
struct ParallelOptions
{
    /** Number of pre-flight worker threads. Zero (default) means
     *  std::thread::hardware_concurrency(), clamped to a minimum of 1. */
    int numWorkers{0};
};

/**
 * Worker-pool variant of ProcessEvmTransactionsInBlock (D1, Phase 2.5).
 *
 * Phase 2.5 ships the worker-pool API surface with a conservative
 * MVP implementation that runs *pre-flight* (payload deserialization
 * + structural validation) in parallel and *execution* serially.
 * The result is byte-identical to ProcessEvmTransactionsInBlock —
 * verified by paired tests — so the serial baseline remains the
 * source of truth while we wire the parallelism foundation in.
 *
 * Why pre-flight only, for now:
 *
 *   - Pre-flight is pure: it reads the tx bytes, deserializes a
 *     payload struct, and validates structural invariants. No
 *     CEvmStateCache mutations happen at this phase, so parallel
 *     workers are trivially safe.
 *   - Execution mutates the cache. True parallel execution requires
 *     read/write-set tracking + optimistic-concurrency conflict
 *     detection + retry — substantial design work that should be
 *     informed by empirical workload data, not pre-launch
 *     speculation. The plan's D1 acknowledges this (+3-4 weeks of
 *     Phase 2 budget).
 *   - The pre-flight win is real on blocks with many EVM txs:
 *     deserialization of a 24 KiB CEvmDeployTx is non-trivial,
 *     and amortising it across cores cuts wall-clock latency on
 *     the critical ConnectBlock path.
 *
 * The Qtum problem (cs_main held during EVM execution → P2P
 * starves) remains future work: it requires releasing cs_main
 * during EVM execution and re-acquiring for merge, which is a
 * deeper refactor of ConnectBlock. This function does NOT yet
 * address that; it is single-thread-equivalent for execution.
 *
 * Caller contract identical to ProcessEvmTransactionsInBlock:
 * pass an active per-block cache, get a BlockProcessResult.
 */
BlockProcessResult ParallelProcessEvmTransactionsInBlock(
    const CBlock& block,
    const CBlockIndex* pindex,
    CEvmStateCache& cache,
    const ExecutionContext& contextTemplate,
    const ParallelOptions& opts = {});

} // namespace evm

#endif // RAPTOREUM_EVM_PARALLEL_H
