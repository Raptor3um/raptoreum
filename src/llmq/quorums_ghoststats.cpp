// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/quorums_ghoststats.h>

#include <llmq/quorums_blockprocessor.h>
#include <llmq/quorums_commitment.h>
#include <llmq/quorums_utils.h>
#include <evo/deterministicmns.h>
#include <evo/specialtx.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <primitives/block.h>
#include <spork.h>
#include <validation.h>

#include <algorithm>
#include <set>

namespace llmq {

namespace {

bool Get24hParams(Consensus::LLMQParams &out) {
    const auto &consensusLlmqs = Params().GetConsensus().llmqs;
    auto it = consensusLlmqs.find(Params().GetConsensus().llmqTypeGhostBanTrigger);
    if (it == consensusLlmqs.end()) {
        return false;
    }
    out = it->second;
    return true;
}

uint8_t ReadLiveGhostThreshold() {
    int64_t liveSpork = sporkManager.GetSporkValue(SPORK_26_GHOST_BAN_THRESHOLD);
    if (liveSpork < 1 || liveSpork > 255) {
        return GHOST_THRESHOLD_DISABLED;
    }
    return (uint8_t) liveSpork;
}

// Sorted, deduplicated -- the canonical ordering both chunk construction and
// chunk validation slice against.
std::vector<uint256> ComputeExpectedGhosts(const CBlockIndex *pindexPrev, int thresholdPercent) {
    std::set<uint256> ghostsSet;
    for (const auto &p : ComputeGhostStats(pindexPrev)) {
        if (IsGhost(p.second, thresholdPercent)) {
            ghostsSet.insert(p.first);
        }
    }
    return std::vector<uint256>(ghostsSet.begin(), ghostsSet.end());
}

} // anonymous namespace

size_t GhostBanChunkCount(size_t totalGhosts) {
    return std::max<size_t>(
            1, (totalGhosts + GHOST_BAN_CHUNK_MAX_ENTRIES - 1) / GHOST_BAN_CHUNK_MAX_ENTRIES);
}

int GetGhostBanSlotStart(int candidateHeight, const Consensus::LLMQParams &params24h) {
    int anchorHeight = candidateHeight - 1;
    if (anchorHeight < params24h.dkgMiningWindowEnd) {
        return -1;
    }
    int slotStart = anchorHeight - params24h.dkgMiningWindowEnd;
    if (slotStart % params24h.dkgInterval != 0) {
        return -1;
    }
    return slotStart;
}

std::map<uint256, CGhostStats> ComputeGhostStats(const CBlockIndex *pindexPrev) {
    std::map<uint256, CGhostStats> result;
    if (!pindexPrev) {
        return result;
    }

    const auto &consensus = Params().GetConsensus();
    const auto &consensusLlmqs = consensus.llmqs;
    auto it24h = consensusLlmqs.find(consensus.llmqTypeGhostBanTrigger);
    if (it24h == consensusLlmqs.end()) {
        return result;
    }
    const int windowBlocks = it24h->second.dkgInterval;

    for (const auto &type : consensus.llmqTypesGhostBanScope) {
        auto itParams = consensusLlmqs.find(type);
        if (itParams == consensusLlmqs.end()) {
            continue;
        }
        const Consensus::LLMQParams &params = itParams->second;

        int referenceHeight = pindexPrev->nHeight - params.dkgMiningWindowEnd;
        if (referenceHeight < 0) {
            continue;
        }
        int latestSlotStart = referenceHeight - (referenceHeight % params.dkgInterval);

        // Bound against referenceHeight, not nHeight: on regtest (trigger == scope type) nHeight
        // would exclude the slot latestSlotStart was just computed to find.
        for (int slotStart = latestSlotStart;
             slotStart >= 0 && slotStart > referenceHeight - windowBlocks;
             slotStart -= params.dkgInterval) {
            const CBlockIndex *pQuorumBaseBlockIndex = pindexPrev->GetAncestor(slotStart);
            if (!pQuorumBaseBlockIndex) {
                continue;
            }

            auto members = CLLMQUtils::GetAllQuorumMembers(params, pQuorumBaseBlockIndex);
            if (members.empty()) {
                continue;
            }

            uint256 minedBlockHash;
            CFinalCommitmentPtr qc = quorumBlockProcessor->GetMinedCommitment(
                    type, pQuorumBaseBlockIndex->GetBlockHash(), minedBlockHash);
            if (!qc || qc->validMembers.size() != members.size()) {
                // params for this type were swapped (live MN-count-driven variant change)
                // since this quorum actually formed -- members[] no longer lines up with
                // the historical commitment's real bitset. Skip rather than guess; every
                // node computes the same mismatch-or-not verdict from public data.
                continue;
            }
            bool hasRealCommitment = !qc->IsNull();

            for (size_t i = 0; i < members.size(); i++) {
                auto &stats = result[members[i]->proTxHash];
                stats.selected++;
                if (!hasRealCommitment || !qc->validMembers[i]) {
                    stats.missed++;
                }
            }
        }
    }

    return result;
}

bool IsGhostBanTxRequired(const CBlockIndex *pindexPrev) {
    if (!Updates().IsActive(EUpdate::GHOST_BAN_LIST, pindexPrev)) {
        return false;
    }
    Consensus::LLMQParams params24h;
    if (!Get24hParams(params24h)) {
        return false;
    }
    return GetGhostBanSlotStart(pindexPrev->nHeight + 1, params24h) >= 0;
}

int GetAppliedGhostThreshold(const CBlockIndex *pindexPrev) {
    Consensus::LLMQParams params24h;
    if (!Get24hParams(params24h)) {
        return -1;
    }
    int slotStart = GetGhostBanSlotStart(pindexPrev->nHeight + 1, params24h);
    if (slotStart < 0) {
        return -1;
    }

    int prevSlotStart = slotStart - params24h.dkgInterval;
    if (prevSlotStart < 0) {
        // No prior cycle exists -- not "couldn't determine", genuinely nothing to read.
        // Cycle one is always OFF, regardless of what the spork is set to right now.
        return GHOST_THRESHOLD_DISABLED;
    }
    int prevTriggerHeight = prevSlotStart + params24h.dkgMiningWindowEnd + 1;
    const CBlockIndex *pPrevTrigger = pindexPrev->GetAncestor(prevTriggerHeight);
    if (!pPrevTrigger) {
        return GHOST_THRESHOLD_DISABLED;
    }

    CBlock block;
    if (!ReadBlockFromDisk(block, pPrevTrigger, Params().GetConsensus())) {
        return GHOST_THRESHOLD_DISABLED;
    }
    for (const auto &tx : block.vtx) {
        if (tx->nVersion == 3 && tx->nType == TRANSACTION_GHOST_BAN_LIST) {
            CGhostBanTxPayload payload;
            // nSporkValue only lives on chunk 0 -- other chunks don't carry it.
            if (GetTxPayload(*tx, payload) && payload.nChunkIndex == 0) {
                return payload.nSporkValue;
            }
        }
    }
    // Mandatory presence guarantees a valid tx here past cycle one; reaching this
    // point means something else is already wrong. Fail toward OFF, not the live spork.
    return GHOST_THRESHOLD_DISABLED;
}

bool CheckGhostBanTx(const CTransaction &tx, const CBlockIndex *pindexPrev, CValidationState &state) {
    if (!Updates().IsActive(EUpdate::GHOST_BAN_LIST, pindexPrev)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-ghostban-not-activated");
    }

    CGhostBanTxPayload payload;
    if (!GetTxPayload(tx, payload)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-ghostban-payload");
    }

    if (payload.nVersion == 0 || payload.nVersion > CGhostBanTxPayload::CURRENT_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-ghostban-version");
    }

    if (payload.nHeight != uint32_t(pindexPrev->nHeight + 1)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-ghostban-height");
    }

    // Unauthenticated: whoever mines chunk 0 sets next cycle's value, not checked
    // against the real spork signature. Known, accepted limitation -- not fixed.
    if (payload.nChunkIndex == 0 && payload.nSporkValue == 0) {
        return state.DoS(100, false, REJECT_INVALID, "bad-ghostban-sporkvalue");
    }

    // Cheap bound before the real (ComputeGhostStats-backed) chunk-count check below --
    // a flood of distinct wildly-out-of-range indices gets rejected without ever paying
    // for the 27-lookup recompute.
    if (payload.nChunkIndex >= GHOST_BAN_MAX_POSSIBLE_CHUNKS) {
        return state.DoS(100, false, REJECT_INVALID, "bad-ghostban-chunk-index");
    }

    Consensus::LLMQParams params24h;
    int slotStart = -1;
    if (Get24hParams(params24h)) {
        slotStart = GetGhostBanSlotStart(pindexPrev->nHeight + 1, params24h);
    }
    if (slotStart < 0) {
        return state.DoS(100, false, REJECT_INVALID, "bad-ghostban-not-allowed");
    }

    const CBlockIndex *pQuorumBaseBlockIndex = pindexPrev->GetAncestor(slotStart);
    if (!pQuorumBaseBlockIndex || pQuorumBaseBlockIndex->GetBlockHash() != payload.quorumHash) {
        return state.DoS(100, false, REJECT_INVALID, "bad-ghostban-quorum-hash");
    }

    int expectedThreshold = GetAppliedGhostThreshold(pindexPrev);
    if (expectedThreshold < 0) {
        return state.DoS(100, false, REJECT_INVALID, "bad-ghostban-threshold");
    }

    std::vector<uint256> expectedGhosts = ComputeExpectedGhosts(pindexPrev, expectedThreshold);
    size_t chunkCount = GhostBanChunkCount(expectedGhosts.size());
    if (payload.nChunkIndex >= chunkCount) {
        return state.DoS(100, false, REJECT_INVALID, "bad-ghostban-chunk-index");
    }

    size_t start = (size_t) payload.nChunkIndex * GHOST_BAN_CHUNK_MAX_ENTRIES;
    size_t end = std::min(start + GHOST_BAN_CHUNK_MAX_ENTRIES, expectedGhosts.size());
    std::vector<uint256> expectedSlice(expectedGhosts.begin() + start, expectedGhosts.begin() + end);

    // Exact ordered-vector equality, not size+membership: a size+membership check
    // can't see duplicates, so e.g. [A,A] for expected {A,B} would pass.
    if (payload.ghostProTxHashes != expectedSlice) {
        return state.DoS(100, false, REJECT_INVALID, "bad-ghostban-list-mismatch");
    }

    return true;
}

bool GetGhostBanTxs(const CBlockIndex *pindexPrev, std::vector<CTransactionRef> &ret) {
    ret.clear();
    if (!IsGhostBanTxRequired(pindexPrev)) {
        return false;
    }

    Consensus::LLMQParams params24h;
    Get24hParams(params24h);
    int slotStart = GetGhostBanSlotStart(pindexPrev->nHeight + 1, params24h);
    const CBlockIndex *pQuorumBaseBlockIndex = pindexPrev->GetAncestor(slotStart);
    uint256 quorumHash = pQuorumBaseBlockIndex->GetBlockHash();

    int threshold = GetAppliedGhostThreshold(pindexPrev);
    std::vector<uint256> expectedGhosts = ComputeExpectedGhosts(pindexPrev, threshold);
    size_t chunkCount = GhostBanChunkCount(expectedGhosts.size());
    uint8_t liveSpork = ReadLiveGhostThreshold();

    for (size_t i = 0; i < chunkCount; i++) {
        CGhostBanTxPayload payload;
        payload.nHeight = pindexPrev->nHeight + 1;
        payload.quorumHash = quorumHash;
        payload.nChunkIndex = (uint16_t) i;

        size_t start = i * GHOST_BAN_CHUNK_MAX_ENTRIES;
        size_t end = std::min(start + GHOST_BAN_CHUNK_MAX_ENTRIES, expectedGhosts.size());
        payload.ghostProTxHashes.assign(expectedGhosts.begin() + start, expectedGhosts.begin() + end);

        if (i == 0) {
            payload.nSporkValue = liveSpork;
        }

        CMutableTransaction tx;
        tx.nVersion = 3;
        tx.nType = TRANSACTION_GHOST_BAN_LIST;
        SetTxPayload(tx, payload);
        ret.push_back(MakeTransactionRef(tx));
    }

    return true;
}

bool CheckGhostBanTxPresence(const CBlock &block, const CBlockIndex *pindex, CValidationState &state) {
    bool isGhostBanTxRequired = IsGhostBanTxRequired(pindex->pprev);

    size_t expectedChunkCount = 0;
    std::vector<bool> chunksSeen;
    if (isGhostBanTxRequired) {
        int threshold = GetAppliedGhostThreshold(pindex->pprev);
        size_t totalGhosts = threshold < 0 ? 0 : ComputeExpectedGhosts(pindex->pprev, threshold).size();
        expectedChunkCount = GhostBanChunkCount(totalGhosts);
        chunksSeen.assign(expectedChunkCount, false);
    }

    for (const auto &ptr_tx : block.vtx) {
        if (ptr_tx->nVersion != 3 || ptr_tx->nType != TRANSACTION_GHOST_BAN_LIST) {
            continue;
        }
        if (!isGhostBanTxRequired) {
            return state.DoS(100, false, REJECT_INVALID, "bad-ghostban-not-allowed");
        }
        CGhostBanTxPayload payload;
        if (!GetTxPayload(*ptr_tx, payload) || payload.nChunkIndex >= expectedChunkCount) {
            return state.DoS(100, false, REJECT_INVALID, "bad-ghostban-chunk-index");
        }
        // Duplicates are already rejected earlier, in ProcessSpecialTxsInBlock's per-tx
        // loop, before a repeat ever reaches the expensive per-chunk check.
        chunksSeen[payload.nChunkIndex] = true;
    }

    for (bool seen : chunksSeen) {
        if (!seen) {
            return state.DoS(100, false, REJECT_INVALID, "bad-ghostban-missing");
        }
    }

    return true;
}

} // namespace llmq
