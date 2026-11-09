// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/quorums_ghoststats.h>

#include <llmq/quorums_blockprocessor.h>
#include <llmq/quorums_commitment.h>
#include <llmq/quorums_utils.h>
#include <evo/deterministicmns.h>
#include <chain.h>
#include <chainparams.h>
#include <update/update.h>

#include <algorithm>

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

} // namespace llmq
