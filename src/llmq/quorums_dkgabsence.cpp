// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/quorums_dkgabsence.h>
#include <llmq/quorums_utils.h>
#include <evo/deterministicmns.h>
#include <chain.h>
#include <hash.h>

namespace llmq {

    uint256 CDKGAbsenceVote::GetSignHash() const {
        CHashWriter hw(SER_NETWORK, 0);
        SerializeWithoutSig(hw);
        return hw.GetHash();
    }

    bool VerifyAbsenceRecordCore(Consensus::LLMQType llmqType, const uint256 &quorumHash,
                                  size_t paramsSize, size_t memberCount, const std::set<uint256> &validMembers,
                                  const std::vector<bool> &confirmedAbsent,
                                  const std::vector<CDKGAbsenceVote> &votes) {
        if (confirmedAbsent.size() != paramsSize) {
            return false;
        }
        if (votes.empty()) {
            // Nothing bundled -- confirmedAbsent must be all-false, never a
            // claim manufactured out of zero evidence.
            return std::count(confirmedAbsent.begin(), confirmedAbsent.end(), true) == 0;
        }

        // CalculateQuorum can hand back fewer than paramsSize members when
        // the eligible pool is smaller than that -- bits at or beyond the
        // real member count map to nobody, and a Phase 2 consumer indexing
        // members[i] for each set bit would go out of range. Deliberately
        // redundant with the identical per-vote loop below: given votes is
        // non-empty here, any confirmedAbsent[i] this loop would catch is
        // already unreachable by the time we'd return true anyway (every
        // vote with that same bit set gets rejected below; if instead some
        // vote has it clear, the final intersection != confirmedAbsent
        // comparison catches it) -- kept anyway as defense in depth, since
        // that equivalence depends on the vote-side loop and the final
        // comparison both staying exactly as they are.
        for (size_t i = memberCount; i < paramsSize; i++) {
            if (confirmedAbsent[i]) {
                return false;
            }
        }

        // intersection accumulator, starts all-true and gets AND-ed down
        std::vector<bool> intersection(paramsSize, true);
        std::set<uint256> seenReporters;

        for (const auto &vote: votes) {
            if (vote.llmqType != llmqType || vote.quorumHash != quorumHash) {
                return false;
            }
            if (vote.absentMembers.size() != paramsSize) {
                return false;
            }
            for (size_t i = memberCount; i < paramsSize; i++) {
                if (vote.absentMembers[i]) {
                    return false;
                }
            }
            if (!seenReporters.insert(vote.proTxHash).second) {
                // same reporter counted twice -- reject rather than silently
                // dedupe, so a builder can't inflate its own vote's weight
                return false;
            }
            if (!validMembers.count(vote.proTxHash)) {
                // vote signer is not actually a member of this quorum session
                return false;
            }

            for (size_t i = 0; i < intersection.size(); i++) {
                intersection[i] = intersection[i] && vote.absentMembers[i];
            }
        }

        return intersection == confirmedAbsent;
    }

    bool CDKGAbsenceRecord::Verify(const CBlockIndex *pQuorumBaseBlockIndex,
                                    const Consensus::LLMQParams &params) const {
        if (nVersion == 0 || nVersion > CURRENT_VERSION) {
            return false;
        }
        if (llmqType != params.type) {
            return false;
        }
        if (pQuorumBaseBlockIndex == nullptr || quorumHash != pQuorumBaseBlockIndex->GetBlockHash()) {
            return false;
        }
        if (confirmedAbsent.size() != (size_t) params.size) {
            return false;
        }
        if (votes.empty()) {
            return !CountConfirmedAbsent();
        }

        auto members = CLLMQUtils::GetAllQuorumMembers(params, pQuorumBaseBlockIndex);
        std::set<uint256> validMembers;
        for (const auto &m: members) {
            validMembers.insert(m->proTxHash);
        }

        // Cheap structural checks first (bounds, duplicates, non-members,
        // the intersection itself) -- same cheap-before-expensive order the
        // original, inlined Verify() used, so a malformed record (wrong
        // size, duplicate reporter, etc.) never buys itself N BLS pairings'
        // worth of work before getting rejected for free.
        if (!VerifyAbsenceRecordCore(llmqType, quorumHash, (size_t) params.size, members.size(), validMembers,
                                      confirmedAbsent, votes)) {
            return false;
        }

        // Real signature verification needs the live DML-resolved members
        // above, so it can't move into VerifyAbsenceRecordCore -- it's the
        // one thing that core intentionally leaves to the caller.
        for (const auto &vote: votes) {
            CDeterministicMNCPtr reporterDmn;
            for (const auto &m: members) {
                if (m->proTxHash == vote.proTxHash) {
                    reporterDmn = m;
                    break;
                }
            }
            if (reporterDmn == nullptr) {
                return false;
            }
            if (!vote.sig.VerifyInsecure(reporterDmn->pdmnState->pubKeyOperator.Get(), vote.GetSignHash())) {
                return false;
            }
        }

        return true;
    }

    void CDKGAbsenceRecord::ToJson(UniValue &obj) const {
        obj.setObject();
        obj.pushKV("version", int(nVersion));
        obj.pushKV("llmqType", int(llmqType));
        obj.pushKV("quorumHash", quorumHash.ToString());
        obj.pushKV("confirmedAbsentCount", CountConfirmedAbsent());
        obj.pushKV("confirmedAbsent", CLLMQUtils::ToHexStr(confirmedAbsent));
        obj.pushKV("voteCount", int(votes.size()));
        UniValue votesArr(UniValue::VARR);
        for (const auto &vote: votes) {
            UniValue v(UniValue::VOBJ);
            v.pushKV("proTxHash", vote.proTxHash.ToString());
            v.pushKV("absentCount", vote.CountAbsentMembers());
            votesArr.push_back(v);
        }
        obj.pushKV("votes", votesArr);
    }

} // namespace llmq
