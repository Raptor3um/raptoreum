// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LLMQ_QUORUMS_DKGABSENCEESTIMATE_H
#define BITCOIN_LLMQ_QUORUMS_DKGABSENCEESTIMATE_H

#include <llmq/quorums_dkgabsence.h>
#include <llmq/quorums_dkgabsencetracker.h>
#include <llmq/quorums_parameters.h>
#include <sync.h>
#include <uint256.h>

#include <map>
#include <set>
#include <vector>

namespace llmq {

// CDKGAbsenceTracker (quorums_dkgabsencetracker.h) is fed by a quorum MEMBER's own
// HandleDKGRound: exactly one CDKGAbsenceRecord per round, from a session this node actually
// ran. A non-member has no such session -- what it has instead is whatever CDKGAbsenceRecords
// happen to reach it over the QABSENCERECORD relay (quorums_dkgsessionmgr.h), which can be
// *several, independently built by different members, for the same round*: that relay
// deliberately has no "keep only the best" dedup, since every independent record is legitimate
// evidence (see the design doc's lone-liar-gap section). Feeding each one straight into
// CDKGAbsenceTracker::RecordRound would double- or triple-count roundsSelected for a round that
// only happened once.
//
// This class is the non-member equivalent: it merges every record received for one round into
// a single running view -- recomputing the intersection over the union of every vote seen so
// far for that round, via FoldAbsenceVotes, the exact same fold CDKGAbsenceRecord::Verify()
// itself uses -- and only then updates counters, once per round, diffing against the round's
// previous merged view rather than blindly incrementing (a later, independently-relayed record
// can carry an honest "I heard from them" vote that retracts an earlier record's claim; the
// override property has to keep working across the merge, not just within one record).
//
// This is explicitly a non-member's ESTIMATE, not CDKGAbsenceTracker's any less honest
// "roundsSelected" count: it depends entirely on what this node happened to receive over P2P
// relay, which the design doc's own regtest pass already measured as incomplete in a
// severe-dropout scenario. Same caveat #501's own functional test documents for the member
// case (local to the observer, not network-wide truth) -- just one hop further removed, since
// this node was never a member running the actual DKG round at all.
//
// In-memory, per-process, not persisted -- same deliberate scope as CDKGAbsenceTracker. No
// threshold, no window, no consequence here either.
class CDKGAbsenceEstimate {
public:
    // Merges one newly-relayed, already-validated CDKGAbsenceRecord into this node's running
    // estimate. `candidateMembers` must be the exact member list CDKGAbsenceRecord::Verify()
    // already resolved to check this same record's signatures -- the caller (
    // CDKGSessionManager::ValidateAndRelayAbsenceRecord) passes it straight through rather
    // than this class re-deriving quorum membership itself, which would mean paying for
    // CLLMQUtils::GetAllQuorumMembers a second time for the same (llmqType, quorumHash) --
    // exactly the cache-thrashing cost the QABSENCERECORD relay's own staleness gate exists to
    // narrow down in the first place.
    void MergeRecord(const std::vector<uint256> &candidateMembers, const CDKGAbsenceRecord &record);

    // Zero-valued if the member has never appeared in any merged round under this type.
    [[nodiscard]] CDKGAbsenceStats GetStats(Consensus::LLMQType llmqType, const uint256 &proTxHash) const;

    // Every member this estimate has any data for under one quorum type.
    [[nodiscard]] std::map<uint256, CDKGAbsenceStats> GetAllStats(Consensus::LLMQType llmqType) const;

private:
    struct RoundState {
        bool countedSelected{false}; // true once roundsSelected has been credited for this
                                     // round -- an explicit flag rather than inferring "first
                                     // sighting" from an empty vector, since a quorum whose
                                     // resolved candidate list is itself empty would make
                                     // lastConfirmedAbsent stay empty too and look uncounted
                                     // forever
        std::vector<CDKGAbsenceVote> mergedVotes;
        std::set<uint256> votedReporters; // dedup: a reporter's vote re-arriving via a second,
                                           // independently-relayed record for the same round is
                                           // an expected, benign re-delivery, not new evidence
        std::vector<bool> lastConfirmedAbsent; // the round's merged view as of the last MergeRecord
                                                // call, so the next one can diff rather than
                                                // blindly re-increment
    };

    mutable RecursiveMutex cs;
    std::map<Consensus::LLMQType, std::map<uint256, RoundState>> roundsByType GUARDED_BY(cs);
    std::map<Consensus::LLMQType, std::map<uint256, CDKGAbsenceStats>> statsByType GUARDED_BY(cs);
};

extern CDKGAbsenceEstimate *quorumDKGAbsenceEstimate;

} // namespace llmq

#endif // BITCOIN_LLMQ_QUORUMS_DKGABSENCEESTIMATE_H
