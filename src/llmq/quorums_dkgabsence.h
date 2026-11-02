// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LLMQ_QUORUMS_DKGABSENCE_H
#define BITCOIN_LLMQ_QUORUMS_DKGABSENCE_H

#include <serialize.h>
#include <llmq/quorums_parameters.h>
#include <evo/deterministicmns.h>
#include <bls/bls.h>
#include <univalue.h>

#include <map>
#include <optional>
#include <set>

class CBlockIndex;

namespace llmq {

// One session member's own, locally-observed view, built at session end, of
// which OTHER members never sent a valid Contribute-phase message. A member
// never sets a bit here for anyone it personally received a valid
// contribution from -- that omission is the override that protects a real
// node from a false absence claim: no number of OTHER members' votes can
// mark someone absent if even one HONEST VOTE IN THE BUNDLE saw otherwise,
// because CDKGAbsenceRecord only keeps the intersection of all bundled votes
// (see Verify() below). This does not protect against an honest vote that
// exists but was never included in the bundle in the first place -- that's
// a property of whoever builds/relays the bundle, not of Verify() itself,
// and is exactly the gap the still-undecided K (minimum vote count) is for.
class CDKGAbsenceVote {
public:
    Consensus::LLMQType llmqType{Consensus::LLMQ_NONE};
    uint256 quorumHash;
    uint256 proTxHash; // the reporting member
    std::vector<bool> absentMembers;
    CBLSSignature sig; // reporter's own sig over SerializeWithoutSig()

    CDKGAbsenceVote() = default;

    explicit CDKGAbsenceVote(const Consensus::LLMQParams &params) : absentMembers((size_t) params.size) {}

    [[nodiscard]] int CountAbsentMembers() const {
        return int(std::count(absentMembers.begin(), absentMembers.end(), true));
    }

    template<typename Stream>
    inline void SerializeWithoutSig(Stream &s) const {
        s << llmqType;
        s << quorumHash;
        s << proTxHash;
        s << DYNBITSET(absentMembers);
    }

    SERIALIZE_METHODS(CDKGAbsenceVote, obj)
    {
        READWRITE(obj.llmqType, obj.quorumHash, obj.proTxHash, DYNBITSET(obj.absentMembers), obj.sig);
    }

    [[nodiscard]] uint256 GetSignHash() const;
};

// A bundle of independent CDKGAbsenceVotes for the same failed session.
// confirmedAbsent is NOT itself a new claim -- it is always exactly the
// intersection of every vote in `votes`, recomputed and checked by Verify().
// A member appears here only if every single one of the bundled reporters
// independently agreed it sent nothing; one dissenting honest vote removes
// that member from confirmedAbsent regardless of how many others voted
// against them.
//
// This is a record of PER-SESSION evidence, not yet a ban decision: how many
// votes are required to bundle one (K), and what happens with confirmedAbsent
// once mined, are deliberately left to the caller / to a later consensus
// rule, not decided in this file.
class CDKGAbsenceRecord {
public:
    static const uint16_t CURRENT_VERSION = 1;

    uint16_t nVersion{CURRENT_VERSION};
    Consensus::LLMQType llmqType{Consensus::LLMQ_NONE};
    uint256 quorumHash;
    std::vector<bool> confirmedAbsent;
    std::vector<CDKGAbsenceVote> votes;

    CDKGAbsenceRecord() = default;

    [[nodiscard]] int CountConfirmedAbsent() const {
        return int(std::count(confirmedAbsent.begin(), confirmedAbsent.end(), true));
    }

    SERIALIZE_METHODS(CDKGAbsenceRecord, obj)
    {
        READWRITE(obj.nVersion, obj.llmqType, obj.quorumHash, DYNBITSET(obj.confirmedAbsent), obj.votes);
    }

    // Checks that:
    //  - every vote's signer is a distinct, genuine member of this specific
    //    quorum session (looked up against pQuorumBaseBlockIndex), with a
    //    valid signature over its own SerializeWithoutSig() content;
    //  - every vote's llmqType/quorumHash/absentMembers.size() match this
    //    record's own and the quorum's size;
    //  - confirmedAbsent is exactly the intersection of all bundled votes --
    //    not a superset, not a subset.
    // Does not enforce a minimum vote count; the caller decides what K is
    // acceptable for its purpose (local collection vs. eventual mining).
    //
    // Resolves quorum membership itself via GetAllQuorumMembers() -- the right choice for a
    // caller that doesn't need that member list for anything else afterward. A caller that
    // does (e.g. feeding a local accumulator the same candidate list this record was checked
    // against) should resolve it once and call the three-argument overload below instead,
    // rather than pay for the same lookup twice.
    [[nodiscard]] bool Verify(const CBlockIndex *pQuorumBaseBlockIndex, const Consensus::LLMQParams &params) const;

    // Same check, taking an already-resolved member list instead of resolving one itself.
    // The two-argument overload above is a thin wrapper around this one.
    [[nodiscard]] bool Verify(const CBlockIndex *pQuorumBaseBlockIndex, const Consensus::LLMQParams &params,
                               const std::vector<CDeterministicMNCPtr> &members) const;

    void ToJson(UniValue &obj) const;
};

using CDKGAbsenceRecordPtr = std::unique_ptr<CDKGAbsenceRecord>;

// True if a received CDKGAbsenceRecord should be rejected before paying for
// the membership-dependent work in Verify() -- either its claimed quorumHash
// isn't a real block at all (blockFound=false), or it's older than this
// quorum type's own retention window. Pulled out as a pure function of plain
// values (no live chain, no CBlockIndex) so this ordering is testable
// directly: a malformed or stale claim must never reach GetAllQuorumMembers().
bool ShouldRejectAbsenceRecordAsStale(bool blockFound, int nTipHeight, int nQuorumHeight,
                                       const Consensus::LLMQParams &params);

// True if `hash` was newly inserted into `store` -- meaning this is the first time this
// exact record has been seen, so it should be relayed onward. False if it was already
// present -- already validated and relayed once, nothing new to do, must not be relayed
// again. Pulled out of CDKGSessionManager::ValidateAndRelayAbsenceRecord so the dedup
// decision is testable with a plain map: no chain, network, or BLS signature needed.
bool TryMarkAbsenceRecordRelayed(std::map<uint256, CDKGAbsenceRecord> &store, const uint256 &hash,
                                  const CDKGAbsenceRecord &record);

// Folds `votes` into the intersection of their absentMembers bitsets, after checking each
// vote's structural validity (right llmqType/quorumHash, right size, no out-of-range bit, one
// vote per reporter, reporter is a genuine member of validMembers). Returns nullopt if any
// vote fails those checks. An empty `votes` is not a failure -- it folds to all-false,
// matching the "no claim from zero evidence" rule used throughout this file.
//
// This is the single shared place the intersection itself gets computed. VerifyAbsenceRecordCore
// below calls it to check a given confirmedAbsent against what the votes actually fold to.
// A caller that instead needs the computed intersection itself -- merging several independently
// -relayed records for the same round into one running view, say -- calls this directly rather
// than reimplementing the fold.
std::optional<std::vector<bool>> FoldAbsenceVotes(Consensus::LLMQType llmqType, const uint256 &quorumHash,
                                                    size_t paramsSize, size_t memberCount,
                                                    const std::set<uint256> &validMembers,
                                                    const std::vector<CDKGAbsenceVote> &votes);

// Pure core of CDKGAbsenceRecord::Verify(), extracted so the single most
// important invariant in this file -- that confirmedAbsent is exactly the
// intersection of every bundled vote, with no duplicate or non-member
// reporter and no out-of-range bit -- is testable directly with plain
// values. No live chain, DML, or real BLS signature needed: llmqType/
// quorumHash here are the record's own fields, taken as given (Verify()
// has already checked them against params/the chain before calling this),
// memberCount/validMembers are what Verify()'s own GetAllQuorumMembers()
// call hands off, and every vote's signature must already have been
// checked for real by the caller -- this only re-checks the fields that
// matter for the intersection itself.
bool VerifyAbsenceRecordCore(Consensus::LLMQType llmqType, const uint256 &quorumHash,
                              size_t paramsSize, size_t memberCount, const std::set<uint256> &validMembers,
                              const std::vector<bool> &confirmedAbsent, const std::vector<CDKGAbsenceVote> &votes);

} // namespace llmq

#endif // BITCOIN_LLMQ_QUORUMS_DKGABSENCE_H
