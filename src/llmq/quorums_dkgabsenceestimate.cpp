// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/quorums_dkgabsenceestimate.h>

namespace llmq {

CDKGAbsenceEstimate *quorumDKGAbsenceEstimate;

void CDKGAbsenceEstimate::MergeRecord(const std::vector<uint256> &candidateMembers,
                                       const CDKGAbsenceRecord &record) {
    LOCK(cs);
    auto &round = roundsByType[record.llmqType][record.quorumHash];

    for (const auto &vote: record.votes) {
        if (round.votedReporters.insert(vote.proTxHash).second) {
            round.mergedVotes.push_back(vote);
        }
    }

    std::set<uint256> validMembers(candidateMembers.begin(), candidateMembers.end());
    auto folded = FoldAbsenceVotes(record.llmqType, record.quorumHash, record.confirmedAbsent.size(),
                                    candidateMembers.size(), validMembers, round.mergedVotes);
    if (!folded.has_value()) {
        // The merged union doesn't fold cleanly. Shouldn't happen if every individual record
        // reaching here already passed CDKGAbsenceRecord::Verify() on its own, but the merge
        // is new code this session hasn't had the chance to prove the way Verify() has -- defend
        // by leaving the round's prior state untouched rather than risk corrupting it.
        return;
    }

    auto &stats = statsByType[record.llmqType];
    if (!round.countedSelected) {
        for (const auto &member: candidateMembers) {
            stats[member].roundsSelected++;
        }
        round.lastConfirmedAbsent.assign(candidateMembers.size(), false);
        round.countedSelected = true;
    }

    // Diff against the round's previous merged view: credit members newly absent, retract
    // credit for members an override vote has since cleared. Never a blind increment -- a
    // later, independently-relayed record can carry an honest vote that removes someone the
    // first record claimed, and the count has to follow that, not just accumulate.
    for (size_t i = 0; i < candidateMembers.size() && i < folded->size(); i++) {
        const bool wasAbsent = i < round.lastConfirmedAbsent.size() && round.lastConfirmedAbsent[i];
        const bool isAbsent = (*folded)[i];
        if (isAbsent && !wasAbsent) {
            stats[candidateMembers[i]].roundsReportedAbsent++;
        } else if (!isAbsent && wasAbsent) {
            stats[candidateMembers[i]].roundsReportedAbsent--;
        }
    }
    round.lastConfirmedAbsent = *folded;
}

CDKGAbsenceStats CDKGAbsenceEstimate::GetStats(Consensus::LLMQType llmqType, const uint256 &proTxHash) const {
    LOCK(cs);
    auto typeIt = statsByType.find(llmqType);
    if (typeIt == statsByType.end()) {
        return {};
    }
    auto memberIt = typeIt->second.find(proTxHash);
    if (memberIt == typeIt->second.end()) {
        return {};
    }
    return memberIt->second;
}

std::map<uint256, CDKGAbsenceStats> CDKGAbsenceEstimate::GetAllStats(Consensus::LLMQType llmqType) const {
    LOCK(cs);
    auto typeIt = statsByType.find(llmqType);
    if (typeIt == statsByType.end()) {
        return {};
    }
    return typeIt->second;
}

} // namespace llmq
