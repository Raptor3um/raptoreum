// Copyright (c) 2018-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LLMQ_QUORUMS_DKGSESSIONMGR_H
#define BITCOIN_LLMQ_QUORUMS_DKGSESSIONMGR_H

#include <llmq/quorums_dkgsessionhandler.h>
#include <llmq/quorums_dkgsession.h>
#include <llmq/quorums_parameters.h>
#include <bls/bls.h>
#include <bls/bls_worker.h>

#include <functional>

class UniValue;

class CBlockIndex;

class CNode;

namespace llmq {

    class CDKGSessionManager {
        static const int64_t MAX_CONTRIBUTION_CACHE_TIME = 60 * 1000;

    private:
        mutable Mutex cs_db;
        std::unique_ptr <CDBWrapper> db{nullptr};
        CBLSWorker &blsWorker;
        CConnman &connman;

        std::map <Consensus::LLMQType, CDKGSessionHandler> dkgSessionHandlers;

        mutable RecursiveMutex contributionsCacheCs;

        struct ContributionsCacheKey {
            Consensus::LLMQType llmqType;
            uint256 quorumHash;
            uint256 proTxHash;

            bool operator<(const ContributionsCacheKey &r) const {
                if (llmqType != r.llmqType) return llmqType < r.llmqType;
                if (quorumHash != r.quorumHash) return quorumHash < r.quorumHash;
                return proTxHash < r.proTxHash;
            }
        };

        struct ContributionsCacheEntry {
            int64_t entryTime;
            uint32_t nVersion;
            BLSVerificationVectorPtr vvec;
            CBLSSecretKey skContribution;
        };
        mutable std::map <ContributionsCacheKey, ContributionsCacheEntry> contributionsCache
        GUARDED_BY(contributionsCacheCs);

        // Records relayed network-wide via QABSENCERECORD (see ValidateAndRelayAbsenceRecord),
        // keyed by the record's own SerializeHash -- doubles as both the dedup seen-set (if
        // it's in here, it's already been validated and relayed once, so a repeat is a no-op)
        // and the GETDATA-serving store for peers that haven't seen it yet. Deliberately
        // separate from GetAbsenceRecord(llmqType, ...) below, which is a different lookup:
        // this node's own current record for a type it's a member of, not an arbitrary
        // record received from the wider network for any quorum.
        mutable RecursiveMutex cs_relayedAbsenceRecords;
        std::map <uint256, CDKGAbsenceRecord> relayedAbsenceRecords GUARDED_BY(cs_relayedAbsenceRecords);

    public:
        CDKGSessionManager(CConnman &_connman, CBLSWorker &_blsWorker, bool unitTests, bool fWipe);

        ~CDKGSessionManager() = default;

        void StartThreads();

        void StopThreads();

        void UpdatedBlockTip(const CBlockIndex *pindexNew, bool fInitialDownload);

        void ProcessMessage(CNode *pfrom, const std::string &strCommand, CDataStream &vRecv);

        bool AlreadyHave(const CInv &inv) const;

        bool GetContribution(const uint256 &hash, CDKGContribution &ret) const;

        bool GetComplaint(const uint256 &hash, CDKGComplaint &ret) const;

        bool GetJustification(const uint256 &hash, CDKGJustification &ret) const;

        bool GetPrematureCommitment(const uint256 &hash, CDKGPrematureCommitment &ret) const;

        bool GetAbsenceVote(const uint256 &hash, CDKGAbsenceVote &ret) const;

        // Unlike GetAbsenceVote (one specific message, looked up by hash,
        // for serving GETDATA), this returns the CURRENT session's own
        // already-built, already-self-verified record for the given type --
        // set once by HandleDKGRound, see quorums_dkgsessionhandler.cpp.
        // False if this node isn't running that type, hasn't reached
        // Finalize yet this round, or was never a member of this session.
        bool GetAbsenceRecord(Consensus::LLMQType llmqType, CDKGAbsenceRecord &ret) const;

        // GETDATA lookup for a record relayed over QABSENCERECORD -- by the record's own hash,
        // for any quorum, not just ones this node is a member of. See relayedAbsenceRecords.
        bool GetRelayedAbsenceRecord(const uint256 &hash, CDKGAbsenceRecord &ret) const;

        // Validates `record` (the cheap quorumHash-plausibility gate, then the full
        // membership/signature/intersection check in Verify()) and, if new and valid, stores
        // it for GETDATA and floods it to every connected peer via QABSENCERECORD -- the
        // second relay hop past the quorum-member-only mesh QABSENCEVOTE stays within.
        //
        // `pfrom` is the peer the record arrived from, for misbehavior scoring on a real
        // verification failure; pass nullptr for a record this node itself just built (already
        // trusted, never misbehavior-scored, but still runs through the same single path so
        // there is exactly one way this ever gets relayed, not two that could drift apart).
        //
        // Returns true if the record is valid (whether newly relayed or already seen before),
        // false if it was rejected.
        bool ValidateAndRelayAbsenceRecord(CNode *pfrom, const CDKGAbsenceRecord &record);

        // Extension seam for a future non-member local accumulator (see the design doc) --
        // called once, after a record is confirmed new and about to be relayed. Empty/unset
        // by default. Deliberately just a callback, not a listener interface: there is exactly
        // one planned consumer so far, and this is the smallest thing that unblocks it.
        std::function<void(const CDKGAbsenceRecord &)> onAbsenceRecordRelayed;

        // Contributions are written while in the DKG
        void WriteVerifiedVvecContribution(Consensus::LLMQType llmqType, const CBlockIndex *pQuorumBaseBlockIndex,
                                           const uint256 &proTxHash, const BLSVerificationVectorPtr &vvec);

        void WriteVerifiedSkContribution(Consensus::LLMQType llmqType, const CBlockIndex *pQuorumBaseBlockIndex,
                                         const uint256 &proTxHash, const CBLSSecretKey &skContribution);

        bool GetVerifiedContributions(Consensus::LLMQType llmqType, const CBlockIndex *pQuorumBaseBlockIndex,
                                      const std::vector<bool> &validMembers, std::vector <uint16_t> &memberIndexesRet,
                                      std::vector <BLSVerificationVectorPtr> &vvecsRet,
                                      BLSSecretKeyVector &skContributionsRet,
                                      Consensus::CQuorumUpdateVoteVec &updateVotesRet) const;

        /// Write encrypted (unverified) DKG contributions for the member with the given proTxHash to the llmqDb
        void WriteEncryptedContributions(Consensus::LLMQType llmqType, const CBlockIndex *pQuorumBaseBlockIndex,
                                         const uint256 &proTxHash,
                                         const CBLSIESMultiRecipientObjects <CBLSSecretKey> &contributions);

        /// Read encrypted (unverified) DKG contributions for the member with the given proTxHash from the llmqDb
        bool GetEncryptedContributions(Consensus::LLMQType llmqType, const CBlockIndex *pQuorumBaseBlockIndex,
                                       const std::vector<bool> &validMembers, const uint256 &proTxHash,
                                       std::vector <CBLSIESEncryptedObject<CBLSSecretKey>> &vecRet) const;

        // Write UpdateVote for the member with the given proTxHash to the llmqDb
        void WriteUpdateVote(Consensus::LLMQType llmqType,
                                const CBlockIndex *pQuorumBaseBlockIndex,
                                const uint256 &proTxHash,
                                const uint32_t& updateVote);

        void CleanupOldContributions() const;

    private:
        void MigrateDKG();

        void CleanupCache() const;
    };

    bool IsQuorumDKGEnabled();

    // Pure predicate extracted from CleanupOldContributions()'s own DB-iterating
    // loop: does a contribution recorded for a quorum at nQuorumHeight count as
    // too old to keep, given the active chain's current tip height? Testable
    // directly with plain integers -- no DB, chain, or quorum membership needed.
    bool IsContributionExpired(int nTipHeight, int nQuorumHeight, const Consensus::LLMQParams &params);

    extern CDKGSessionManager *quorumDKGSessionManager;

} // namespace llmq

#endif // BITCOIN_LLMQ_QUORUMS_DKGSESSIONMGR_H
