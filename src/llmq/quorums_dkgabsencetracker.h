// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LLMQ_QUORUMS_DKGABSENCETRACKER_H
#define BITCOIN_LLMQ_QUORUMS_DKGABSENCETRACKER_H

#include <llmq/quorums_dkgabsence.h>
#include <llmq/quorums_parameters.h>
#include <sync.h>
#include <uint256.h>

#include <map>
#include <memory>
#include <vector>

namespace llmq {

// How many DKG rounds a member has been selected into for one quorum type,
// and how many of those rounds this node's own self-verified absence record
// named it as confirmed-absent. In-memory only, per process: a restart loses
// the counts, and that is deliberate for now -- a persisted version needs a
// schema that backfills for a node rebuilding its list from genesis, which
// is a separate, not-yet-settled question (see the design note linked from
// #499/#500). This also records no judgement: there is no threshold, no
// window, no consequence here. It exists so a future decision about those
// has real per-round numbers to look at instead of none.
//
// roundsSelected is the denominator a raw absence count must never be read
// without: a member barely selected into any quorum will have a small,
// uninformative roundsReportedAbsent on its own.
struct CDKGAbsenceStats {
    uint32_t roundsSelected{0};
    uint32_t roundsReportedAbsent{0};

    void ToJson(UniValue &obj) const;
};

class CDKGAbsenceTracker {
public:
    // One round's outcome for one quorum type. `selectedMembers` is every
    // member selected this round, in CDKGAbsenceRecord's own bit order
    // (CDKGSession::GetMemberProTxHashes()) -- every one of them gets
    // roundsSelected incremented. `record` is this node's own built record
    // for the same round, or nullptr if none was built (no votes collected
    // at all); when present, its confirmedAbsent bits -- matched back to
    // `selectedMembers` by position -- additionally increment
    // roundsReportedAbsent. Safe to call once per round per quorum type;
    // calling it more than once for the same round double-counts, since
    // this has no notion of a round identity to deduplicate against.
    void RecordRound(Consensus::LLMQType llmqType, const std::vector<uint256> &selectedMembers,
                      const CDKGAbsenceRecord *record);

    // Zero-valued if the member has never been selected under this type.
    [[nodiscard]] CDKGAbsenceStats GetStats(Consensus::LLMQType llmqType, const uint256 &proTxHash) const;

    // Every member this tracker has any data for under one quorum type, so
    // a caller (the RPC) does not need to already know who to ask about.
    [[nodiscard]] std::map<uint256, CDKGAbsenceStats> GetAllStats(Consensus::LLMQType llmqType) const;

private:
    mutable RecursiveMutex cs;
    std::map<Consensus::LLMQType, std::map<uint256, CDKGAbsenceStats>> statsByType GUARDED_BY(cs);
};

extern CDKGAbsenceTracker *quorumDKGAbsenceTracker;

} // namespace llmq

#endif // BITCOIN_LLMQ_QUORUMS_DKGABSENCETRACKER_H
