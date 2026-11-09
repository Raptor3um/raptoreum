// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LLMQ_QUORUMS_GHOSTSTATS_H
#define BITCOIN_LLMQ_QUORUMS_GHOSTSTATS_H

#include <consensus/params.h>
#include <primitives/transaction.h>
#include <uint256.h>

#include <map>

class CBlockIndex;

namespace llmq {

struct CGhostStats {
    int selected{0};
    int missed{0};
};

// Trigger type and scope types are per-network, set in chainparams.cpp
// (Consensus::Params::llmqTypeGhostBanTrigger / llmqTypesGhostBanScope).

static constexpr int GHOST_THRESHOLD_DEFAULT_PERCENT = 51;
static constexpr int GHOST_THRESHOLD_DISABLED = 255;

// Keeps each chunk's payload comfortably under MAX_TX_EXTRA_PAYLOAD (10000 bytes);
// 250 entries is ~8.1KB. Worst case across all scope types in a day is ~2400
// candidates -- 10 chunks, bounded by registration, never unbounded.
static constexpr size_t GHOST_BAN_CHUNK_MAX_ENTRIES = 250;

inline bool IsGhost(const CGhostStats &stats, int thresholdPercent) {
    return stats.missed * 100 >= stats.selected * thresholdPercent;
}

std::map<uint256, CGhostStats> ComputeGhostStats(const CBlockIndex *pindexPrev);

int GetGhostBanSlotStart(int candidateHeight, const Consensus::LLMQParams &params24h);

// Chunks of GHOST_BAN_CHUNK_MAX_ENTRIES; always at least 1, even for 0 ghosts
// (mandatory presence still requires exactly one tx with an empty list).
size_t GhostBanChunkCount(size_t totalGhosts);

bool IsGhostBanTxRequired(const CBlockIndex *pindexPrev);

} // namespace llmq

#endif // BITCOIN_LLMQ_QUORUMS_GHOSTSTATS_H
