// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LLMQ_QUORUMS_DKGABSENCE_H
#define BITCOIN_LLMQ_QUORUMS_DKGABSENCE_H

#include <serialize.h>
#include <llmq/quorums_parameters.h>
#include <bls/bls.h>
#include <univalue.h>

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
    [[nodiscard]] bool Verify(const CBlockIndex *pQuorumBaseBlockIndex, const Consensus::LLMQParams &params) const;

    void ToJson(UniValue &obj) const;
};

using CDKGAbsenceRecordPtr = std::unique_ptr<CDKGAbsenceRecord>;

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
