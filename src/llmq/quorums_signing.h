// Copyright (c) 2018-2020 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_QUORUMS_SIGNING_H
#define RAPTOREUM_QUORUMS_SIGNING_H

#include "llmq/quorums.h"

#include "net.h"
#include "chainparams.h"
#include "saltedhasher.h"
#include "univalue.h"
#include "unordered_lru_cache.h"

#include <unordered_map>

namespace llmq
{

class CRecoveredSig
{
public:
    Consensus::LLMQType llmqType;
    uint256 quorumHash;
    uint256 id;
    uint256 msgHash;
    CBLSLazySignature sig;

    // only in-memory
    uint256 hash;

public:

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(llmqType);
        READWRITE(quorumHash);
        READWRITE(id);
        READWRITE(msgHash);
        READWRITE(sig);
        if (ser_action.ForRead()) {
            UpdateHash();
        }
    }

    void UpdateHash()
    {
        hash = ::SerializeHash(*this);
    }

    const uint256& GetHash() const
    {
        assert(!hash.IsNull());
        return hash;
    }

    UniValue ToJson() const;
};

class CRecoveredSigsDb
{
private:
    CDBWrapper& db;

    CCriticalSection cs;
    unordered_lru_cache<std::pair<Consensus::LLMQType, uint256>, bool, StaticSaltedHasher, 30000> hasSigForIdCache;
    unordered_lru_cache<uint256, bool, StaticSaltedHasher, 30000> hasSigForSessionCache;
    unordered_lru_cache<uint256, bool, StaticSaltedHasher, 30000> hasSigForHashCache;

public:
    CRecoveredSigsDb(CDBWrapper& _db);

    void ConvertInvalidTimeKeys();
    void AddVoteTimeKeys();

    bool HasRecoveredSig(Consensus::LLMQType llmqType, const uint256& id, const uint256& msgHash);
    bool HasRecoveredSigForId(Consensus::LLMQType llmqType, const uint256& id);
    bool HasRecoveredSigForSession(const uint256& signHash);
    bool HasRecoveredSigForHash(const uint256& hash);
    bool GetRecoveredSigByHash(const uint256& hash, CRecoveredSig& ret);
    bool GetRecoveredSigById(Consensus::LLMQType llmqType, const uint256& id, CRecoveredSig& ret);
    void WriteRecoveredSig(const CRecoveredSig& recSig);
    void RemoveRecoveredSig(Consensus::LLMQType llmqType, const uint256& id);
    void TruncateRecoveredSig(Consensus::LLMQType llmqType, const uint256& id);

    void CleanupOldRecoveredSigs(int64_t maxAge);

    // votes are removed when the recovered sig is written to the db
    bool HasVotedOnId(Consensus::LLMQType llmqType, const uint256& id);
    bool GetVoteForId(Consensus::LLMQType llmqType, const uint256& id, uint256& msgHashRet);
    void WriteVoteForId(Consensus::LLMQType llmqType, const uint256& id, const uint256& msgHash);

    void CleanupOldVotes(int64_t maxAge);

private:
    bool ReadRecoveredSig(Consensus::LLMQType llmqType, const uint256& id, CRecoveredSig& ret);
    void RemoveRecoveredSig(CDBBatch& batch, Consensus::LLMQType llmqType, const uint256& id, bool deleteHashKey, bool deleteTimeKey);
};

class CRecoveredSigsListener
{
public:
    virtual ~CRecoveredSigsListener() {}

    virtual void HandleNewRecoveredSig(const CRecoveredSig& recoveredSig) = 0;
};

class CSigningManager
{
    friend class CSigSharesManager;
    static const int64_t DEFAULT_MAX_RECOVERED_SIGS_AGE = 60 * 60 * 24 * 7; // keep them for a week

    // when selecting a quorum for signing and verification, we use CQuorumManager::SelectQuorum with this offset as
    // starting height for scanning. This is because otherwise the resulting signatures would not be verifiable by nodes
    // which are not 100% at the chain tip.
    static const int SIGN_HEIGHT_OFFSET = 8;

private:
    CCriticalSection cs;

    CRecoveredSigsDb db;

    // Incoming and not verified yet
    std::unordered_map<NodeId, std::list<CRecoveredSig>> pendingRecoveredSigs;
    std::list<std::pair<CRecoveredSig, CQuorumCPtr>> pendingReconstructedRecoveredSigs;

    // must be protected by cs
    FastRandomContext rnd;

    int64_t lastCleanupTime{0};

    std::vector<CRecoveredSigsListener*> recoveredSigsListeners;

public:
    CSigningManager(CDBWrapper& llmqDb, bool fMemory);

    bool AlreadyHave(const CInv& inv);
    bool GetRecoveredSigForGetData(const uint256& hash, CRecoveredSig& ret);

    void ProcessMessage(CNode* pnode, const std::string& strCommand, CDataStream& vRecv, CConnman& connman);

    // This is called when a recovered signature was was reconstructed from another P2P message and is known to be valid
    // This is the case for example when a signature appears as part of InstantSend or ChainLocks
    void PushReconstructedRecoveredSig(const CRecoveredSig& recoveredSig, const CQuorumCPtr& quorum);

    // This is called when a recovered signature can be safely removed from the DB. This is only safe when some other
    // mechanism prevents possible conflicts. As an example, ChainLocks prevent conflicts in confirmed TXs InstantSend votes
    // This won't completely remove all traces of the recovered sig but instead leave the hash entry in the DB. This
    // allows AlreadyHave to keep returning true. Cleanup will later remove the remains
    void TruncateRecoveredSig(Consensus::LLMQType llmqType, const uint256& id);

private:
    void ProcessMessageRecoveredSig(CNode* pfrom, const CRecoveredSig& recoveredSig, CConnman& connman);
    bool PreVerifyRecoveredSig(NodeId nodeId, const CRecoveredSig& recoveredSig, bool& retBan);

    void CollectPendingRecoveredSigsToVerify(size_t maxUniqueSessions,
            std::unordered_map<NodeId, std::list<CRecoveredSig>>& retSigShares,
            std::unordered_map<std::pair<Consensus::LLMQType, uint256>, CQuorumCPtr, StaticSaltedHasher>& retQuorums);
    void ProcessPendingReconstructedRecoveredSigs();
    bool ProcessPendingRecoveredSigs(CConnman& connman); // called from the worker thread of CSigSharesManager
    void ProcessRecoveredSig(NodeId nodeId, const CRecoveredSig& recoveredSig, const CQuorumCPtr& quorum, CConnman& connman);
    void Cleanup(); // called from the worker thread of CSigSharesManager

public:
    // public interface
    void RegisterRecoveredSigsListener(CRecoveredSigsListener* l);
    void UnregisterRecoveredSigsListener(CRecoveredSigsListener* l);

    bool AsyncSignIfMember(Consensus::LLMQType llmqType, const uint256& id, const uint256& msgHash, bool allowReSign = false);
    bool HasRecoveredSig(Consensus::LLMQType llmqType, const uint256& id, const uint256& msgHash);
    bool HasRecoveredSigForId(Consensus::LLMQType llmqType, const uint256& id);
    bool HasRecoveredSigForSession(const uint256& signHash);
    bool GetRecoveredSigForId(Consensus::LLMQType llmqType, const uint256& id, CRecoveredSig& retRecSig);
    bool IsConflicting(Consensus::LLMQType llmqType, const uint256& id, const uint256& msgHash);

    bool HasVotedOnId(Consensus::LLMQType llmqType, const uint256& id);
    bool GetVoteForId(Consensus::LLMQType llmqType, const uint256& id, uint256& msgHashRet);

    std::vector<CQuorumCPtr> GetActiveQuorumSet(Consensus::LLMQType llmqType, int signHeight);
    CQuorumCPtr SelectQuorumForSigning(Consensus::LLMQType llmqType, int signHeight, const uint256& selectionHash);

    // Verifies a recovered sig that was signed while the chain tip was at signedAtTip
    bool VerifyRecoveredSig(Consensus::LLMQType llmqType, int signedAtHeight, const uint256& id, const uint256& msgHash, const CBLSSignature& sig);
};

extern CSigningManager* quorumSigningManager;

} // namespace llmq

#endif //RAPTOREUM_QUORUMS_SIGNING_H
