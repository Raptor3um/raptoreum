// Copyright (c) 2018-2021 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LLMQ_QUORUMS_DKGSESSIONMGR_H
#define BITCOIN_LLMQ_QUORUMS_DKGSESSIONMGR_H

#include <llmq/quorums_dkgsessionhandler.h>

#include <validation.h>

#include <ctpl.h>

class UniValue;

namespace llmq
{

class CDKGSessionManager
{
    static const int64_t MAX_CONTRIBUTION_CACHE_TIME = 60 * 1000;

private:
    CDBWrapper& llmqDb;
    CBLSWorker& blsWorker;

    std::map<Consensus::LLMQType, CDKGSessionHandler> dkgSessionHandlers;

    CCriticalSection contributionsCacheCs;
    struct ContributionsCacheKey {
        Consensus::LLMQType llmqType;
        uint256 quorumHash;
        uint256 proTxHash;
        bool operator<(const ContributionsCacheKey& r) const
        {
            if (llmqType != r.llmqType) return llmqType < r.llmqType;
            if (quorumHash != r.quorumHash) return quorumHash < r.quorumHash;
            return proTxHash < r.proTxHash;
        }
    };
    struct ContributionsCacheEntry {
        int64_t entryTime;
        BLSVerificationVectorPtr vvec;
        CBLSSecretKey skContribution;
    };
    std::map<ContributionsCacheKey, ContributionsCacheEntry> contributionsCache;

public:
    CDKGSessionManager(CDBWrapper& _llmqDb, CBLSWorker& _blsWorker);
    ~CDKGSessionManager();

    void StartThreads();
    void StopThreads();

    void UpdatedBlockTip(const CBlockIndex *pindexNew, bool fInitialDownload);

    void ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv);
    bool AlreadyHave(const CInv& inv) const;
    bool GetContribution(const uint256& hash, CDKGContribution& ret) const;
    bool GetComplaint(const uint256& hash, CDKGComplaint& ret) const;
    bool GetJustification(const uint256& hash, CDKGJustification& ret) const;
    bool GetPrematureCommitment(const uint256& hash, CDKGPrematureCommitment& ret) const;

    // Contributions are written while in the DKG
    void WriteVerifiedVvecContribution(Consensus::LLMQType llmqType, const CBlockIndex* pindexQuorum, const uint256& proTxHash, const BLSVerificationVectorPtr& vvec);
    void WriteVerifiedSkContribution(Consensus::LLMQType llmqType, const CBlockIndex* pindexQuorum, const uint256& proTxHash, const CBLSSecretKey& skContribution);
    bool GetVerifiedContributions(Consensus::LLMQType llmqType, const CBlockIndex* pindexQuorum, const std::vector<bool>& validMembers, std::vector<uint16_t>& memberIndexesRet, std::vector<BLSVerificationVectorPtr>& vvecsRet, BLSSecretKeyVector& skContributionsRet);
    /// Write encrypted (unverified) DKG contributions for the member with the given proTxHash to the llmqDb
    void WriteEncryptedContributions(Consensus::LLMQType llmqType, const CBlockIndex* pindexQuorum, const uint256& proTxHash, const CBLSIESMultiRecipientObjects<CBLSSecretKey>& contributions);
    /// Read encrypted (unverified) DKG contributions for the member with the given proTxHash from the llmqDb
    bool GetEncryptedContributions(Consensus::LLMQType llmqType, const CBlockIndex* pindexQuorum, const std::vector<bool>& validMembers, const uint256& proTxHash, std::vector<CBLSIESEncryptedObject<CBLSSecretKey>>& vecRet);

private:
    void CleanupCache();
};

bool IsQuorumDKGEnabled();

extern CDKGSessionManager* quorumDKGSessionManager;

} // namespace llmq

#endif // BITCOIN_LLMQ_QUORUMS_DKGSESSIONMGR_H
