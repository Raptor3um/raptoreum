// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LLMQ_QUORUMS_GHOSTSTATS_H
#define BITCOIN_LLMQ_QUORUMS_GHOSTSTATS_H

#include <consensus/params.h>
#include <primitives/transaction.h>
#include <serialize.h>
#include <uint256.h>
#include <univalue.h>

#include <map>
#include <vector>

class CBlock;
class CBlockIndex;
class CValidationState;

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

// Generous static ceiling (real worst case is 10, see GHOST_BAN_CHUNK_MAX_ENTRIES's own
// comment) checked before any expensive recompute, so a flood of distinct out-of-range
// chunk indices can't force more than this many ComputeGhostStats calls per block.
static constexpr uint16_t GHOST_BAN_MAX_POSSIBLE_CHUNKS = 16;

inline bool IsGhost(const CGhostStats &stats, int thresholdPercent) {
    return stats.missed * 100 >= stats.selected * thresholdPercent;
}

std::map<uint256, CGhostStats> ComputeGhostStats(const CBlockIndex *pindexPrev);

int GetGhostBanSlotStart(int candidateHeight, const Consensus::LLMQParams &params24h);

// Chunks of GHOST_BAN_CHUNK_MAX_ENTRIES; always at least 1, even for 0 ghosts
// (mandatory presence still requires exactly one tx with an empty list).
size_t GhostBanChunkCount(size_t totalGhosts);

bool IsGhostBanTxRequired(const CBlockIndex *pindexPrev);

bool CheckGhostBanTxPresence(const CBlock &block, const CBlockIndex *pindex, CValidationState &state);

int GetAppliedGhostThreshold(const CBlockIndex *pindexPrev);

bool CheckGhostBanTx(const CTransaction &tx, const CBlockIndex *pindexPrev, CValidationState &state);

bool GetGhostBanTxs(const CBlockIndex *pindexPrev, std::vector<CTransactionRef> &ret);

// nSporkValue: one-cycle lag, see GetAppliedGhostThreshold. Only meaningful on
// nChunkIndex == 0 -- the live spork is read once per cycle, not once per chunk.
class CGhostBanTxPayload {
public:
    static const uint16_t CURRENT_VERSION = 1;

    uint16_t nVersion{CURRENT_VERSION};
    uint32_t nHeight{(uint32_t) -1};
    uint256 quorumHash;
    uint16_t nChunkIndex{0};
    std::vector<uint256> ghostProTxHashes;
    uint8_t nSporkValue{0};

    SERIALIZE_METHODS(CGhostBanTxPayload, obj)
    {
        READWRITE(obj.nVersion, obj.nHeight, obj.quorumHash, obj.nChunkIndex, obj.ghostProTxHashes,
                  obj.nSporkValue);
    }

    void ToJson(UniValue &obj) const {
        obj.setObject();
        obj.pushKV("version", int(nVersion));
        obj.pushKV("height", int(nHeight));
        obj.pushKV("quorumHash", quorumHash.ToString());
        obj.pushKV("chunkIndex", int(nChunkIndex));
        UniValue arr(UniValue::VARR);
        for (const auto &h : ghostProTxHashes) {
            arr.push_back(h.ToString());
        }
        obj.pushKV("ghostProTxHashes", arr);
        obj.pushKV("sporkValue", int(nSporkValue));
    }
};

} // namespace llmq

#endif // BITCOIN_LLMQ_QUORUMS_GHOSTSTATS_H
