// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/quorums_dkgabsencetracker.h>

#include <univalue.h>

namespace llmq {

CDKGAbsenceTracker *quorumDKGAbsenceTracker;

void CDKGAbsenceStats::ToJson(UniValue &obj) const {
    obj.setObject();
    obj.pushKV("roundsSelected", int(roundsSelected));
    obj.pushKV("roundsReportedAbsent", int(roundsReportedAbsent));
}

void CDKGAbsenceTracker::RecordRound(Consensus::LLMQType llmqType, const std::vector<uint256> &selectedMembers,
                                      const CDKGAbsenceRecord *record) {
    // confirmedAbsent is sized to the quorum's nominal params.size
    // (CDKGAbsenceVote's constructor, BuildAbsenceRecord's own assign call)
    // -- not to the actual candidate list's size, which is members.size()
    // in CDKGSession and can genuinely be smaller (CDKGSession::Init does
    // members.resize(mns.size()), and mns can come up short of params.size
    // when fewer than that many valid candidates exist, e.g. one already
    // PoSe-banned). selectedMembers (from GetMemberProTxHashes(), which
    // mirrors members.size()) can therefore be shorter than confirmedAbsent
    // -- those extra trailing bits are not real candidate slots and carry
    // no information either way. Only refuse if selectedMembers is somehow
    // *longer* than confirmedAbsent: that would mean indexing confirmedAbsent
    // out of bounds below, a real caller error, not this size difference.
    if (record != nullptr && selectedMembers.size() > record->confirmedAbsent.size()) {
        return;
    }

    LOCK(cs);
    auto &statsForType = statsByType[llmqType];
    for (size_t i = 0; i < selectedMembers.size(); i++) {
        auto &stats = statsForType[selectedMembers[i]];
        stats.roundsSelected++;
        if (record != nullptr && record->confirmedAbsent[i]) {
            stats.roundsReportedAbsent++;
        }
    }
}

CDKGAbsenceStats CDKGAbsenceTracker::GetStats(Consensus::LLMQType llmqType, const uint256 &proTxHash) const {
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

std::map<uint256, CDKGAbsenceStats> CDKGAbsenceTracker::GetAllStats(Consensus::LLMQType llmqType) const {
    LOCK(cs);
    auto typeIt = statsByType.find(llmqType);
    if (typeIt == statsByType.end()) {
        return {};
    }
    return typeIt->second;
}

} // namespace llmq
