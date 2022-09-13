// Copyright (c) 2019-2021 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LLMQ_QUORUMS_INSTANTSEND_H
#define BITCOIN_LLMQ_QUORUMS_INSTANTSEND_H

#include <llmq/quorums_signing.h>
#include <unordered_lru_cache.h>

#include <chain.h>
#include <coins.h>
#include <dbwrapper.h>
#include <primitives/transaction.h>
#include <threadinterrupt.h>
#include <txmempool.h>

#include <unordered_map>
#include <unordered_set>

namespace llmq
{

struct CInstantSendLock
{
    // This is the old format of instant send lock, it must be 0
    static constexpr uint8_t islock_version{0};
    // This is the new format of instant send deterministic lock, this should be incremented for new isdlock versions
    static constexpr uint8_t isdlock_version{1};

    uint8_t nVersion;
    std::vector<COutPoint> inputs;
    uint256 txid;
    uint256 cycleHash;
    CBLSLazySignature sig;

    CInstantSendLock() : CInstantSendLock(islock_version) {}
    explicit CInstantSendLock(const uint8_t desiredVersion) : nVersion(desiredVersion) {}

    SERIALIZE_METHODS(CInstantSendLock, obj)
    {
        if (s.GetVersion() >= ISDLOCK_PROTO_VERSION && obj.IsDeterministic()) {
            READWRITE(obj.nVersion);
        }
        READWRITE(obj.inputs);
        READWRITE(obj.txid);
        if (s.GetVersion() >= ISDLOCK_PROTO_VERSION && obj.IsDeterministic()) {
            READWRITE(obj.cycleHash);
        }
        READWRITE(obj.sig);
    }

    uint256 GetRequestId() const;
    bool IsDeterministic() const { return nVersion != islock_version; }
};

using CInstantSendLockPtr = std::shared_ptr<CInstantSendLock>;

class CInstantSendDb
{
private:
    mutable Mutex cs_db;

    static const int CURRENT_VERSION{1};

    int best_confirmed_height GUARDED_BY(cs_db) {0};

    std::unique_ptr<CDBWrapper> db GUARDED_BY(cs_db) {nullptr};
    mutable unordered_lru_cache<uint256, CInstantSendLockPtr, StaticSaltedHasher, 10000> islockCache GUARDED_BY(cs_db);
    mutable unordered_lru_cache<uint256, uint256, StaticSaltedHasher, 10000> txidCache GUARDED_BY(cs_db);

    mutable unordered_lru_cache<COutPoint, uint256, SaltedOutpointHasher, 10000> outpointCache GUARDED_BY(cs_db);
    void WriteInstantSendLockMined(CDBBatch& batch, const uint256& hash, int nHeight) EXCLUSIVE_LOCKS_REQUIRED(cs_db);

    void RemoveInstantSendLockMined(CDBBatch& batch, const uint256& hash, int nHeight) EXCLUSIVE_LOCKS_REQUIRED(cs_db);

    /**
     * This method removes an InstantSend Lock from the database and is called
     * when a tx with an IS Lock is confirmed and ChainLocked
     * @param batch Object used to batch many calls together
     * @param hash The hash of the InstantSend Lock
     * @param islock The InstantSend Lock object itself
     * @param keep_cache Should we still keep corresponding entries in the cache or not
     */
    void RemoveInstantSendLock(CDBBatch& batch, const uint256& hash, CInstantSendLockPtr islock, bool keep_cache = true) EXCLUSIVE_LOCKS_REQUIRED(cs_db);

    /**
     * Marks an InstantSend Lock as archived.
     * @param batch Object used to batch many calls together
     * @param hash The hash of the InstantSend Lock
     * @param nHeight The height that the transaction was included at
     */
    void WriteInstantSendLockArchived(CDBBatch& batch, const uint256& hash, int nHeight) EXCLUSIVE_LOCKS_REQUIRED(cs_db);

    /**
     * Gets a vector of IS Lock hashes of the IS Locks which rely on or are children of the parent IS Lock
     * @param parent The hash of the parent IS Lock
     * @param Returns a vector of IS Lock hashes
     */
    std::vector<uint256> GetInstantSendLocksByParent(const uint256& parent) const EXCLUSIVE_LOCKS_REQUIRED(cs_db);

    /**
     * See GetInstantSendLockByHash
     */
    CInstantSendLockPtr GetInstantSendLockByHashInternal(const uint256& hash, bool use_cache = true) const EXCLUSIVE_LOCKS_REQUIRED(cs_db);

    /**
     * See GetInstantSendLockHashByTxid
     */
    uint256 GetInstantSendLockHashByTxidInternal(const uint256& txid) const EXCLUSIVE_LOCKS_REQUIRED(cs_db);

public:
    explicit CInstantSendDb(bool unitTests, bool fWipe) :
        db(std::make_unique<CDBWrapper>(unitTests ? "" : (GetDataDir() / "llmq/isdb"), 32 << 20, unitTests, fWipe))
    {}

    void Upgrade() LOCKS_EXCLUDED(cs_db);

    void WriteNewInstantSendLock(const uint256& hash, const CInstantSendLock& islock) LOCKS_EXCLUDED(cs_db);

    void WriteInstantSendLockMined(const uint256& hash, int nHeight) LOCKS_EXCLUDED(cs_db);
    std::unordered_map<uint256, CInstantSendLockPtr> RemoveConfirmedInstantSendLocks(int nUntilHeight) LOCKS_EXCLUDED(cs_db);
    void RemoveArchivedInstantSendLocks(int nUntilHeight) LOCKS_EXCLUDED(cs_db);
    void WriteBlockInstantSendLocks(const std::shared_ptr<const CBlock>& pblock, const CBlockIndex* pindexConnected) LOCKS_EXCLUDED(cs_db);
    void RemoveBlockInstantSendLocks(const std::shared_ptr<const CBlock>& pblock, const CBlockIndex* pindexDisconnected) LOCKS_EXCLUDED(cs_db);
    bool KnownInstantSendLock(const uint256& islockHash) const LOCKS_EXCLUDED(cs_db);
    size_t GetInstantSendLockCount() const LOCKS_EXCLUDED(cs_db);

    CInstantSendLockPtr GetInstantSendLockByHash(const uint256& hash, bool use_cache = true) const LOCKS_EXCLUDED(cs_db)
    {
        LOCK(cs_db);
        return GetInstantSendLockByHashInternal(hash, use_cache);
    };

    uint256 GetInstantSendLockHashByTxid(const uint256& txid) const LOCKS_EXCLUDED(cs_db)
    {
        LOCK(cs_db);
        return GetInstantSendLockHashByTxidInternal(txid);
    };

    CInstantSendLockPtr GetInstantSendLockByTxid(const uint256& txid) const LOCKS_EXCLUDED(cs_db);

    CInstantSendLockPtr GetInstantSendLockByInput(const COutPoint& outpoint) const LOCKS_EXCLUDED(cs_db);

    std::vector<uint256> RemoveChainedInstantSendLocks(const uint256& islockHash, const uint256& txid, int nHeight) LOCKS_EXCLUDED(cs_db);

    void RemoveAndArchiveInstantSendLock(const CInstantSendLockPtr& islock, int nHeight) LOCKS_EXCLUDED(cs_db);
};

class CInstantSendManager : public CRecoveredSigsListener
{
private:
    CInstantSendDb db;
    CConnman& connman;
    CTxMemPool& mempool;

    std::atomic<bool> fUpgradedDB{false};

    std::thread workThread;
    CThreadInterrupt workInterrupt;

    mutable Mutex cs_inputRequests;

    /**
     * Request ids of inputs that we signed. Used to determine if a recovered signature belongs to an
     * in-progress input lock.
     */
    std::unordered_set<uint256, StaticSaltedHasher> inputRequestIds GUARDED_BY(cs_inputRequests);

    mutable Mutex cs_creating;

    /**
     * These are the islocks that are currently in the middle of being created. Entries are created when we observed
     * recovered signatures for all inputs of a TX. At the same time, we initiate signing of our sigshare for the islock.
     * When the recovered sig for the islock later arrives, we can finish the islock and propagate it.
     */
    std::unordered_map<uint256, CInstantSendLock, StaticSaltedHasher> creatingInstantSendLocks GUARDED_BY(cs_creating);
    // maps from txid to the in-progress islock
    std::unordered_map<uint256, CInstantSendLock*, StaticSaltedHasher> txToCreatingInstantSendLocks GUARDED_BY(cs_creating);

    mutable Mutex cs_pendingLocks;

    // Incoming and not verified yet
    std::unordered_map<uint256, std::pair<NodeId, CInstantSendLockPtr>, StaticSaltedHasher> pendingInstantSendLocks GUARDED_BY(cs_pendingLocks);
    // Tried to verify but there is no tx yet.
    std::unordered_map<uint256, std::pair<NodeId, CInstantSendLockPtr>, StaticSaltedHasher> pendingNoTxInstantSendLocks GUARDED_BY(cs_pendingLocks);

    // TXs which are neither IS locked nor ChainLocked. We use this to determine for which TXs we need to retry IS locking
    // of child TXs
    struct NonLockedTxInfo {
        const CBlockIndex* pindexMined;
        CTransactionRef tx;
        std::unordered_set<uint256, StaticSaltedHasher> children;
    };

    mutable Mutex cs_nonLocked;
    std::unordered_map<uint256, NonLockedTxInfo, StaticSaltedHasher> nonLockedTxs GUARDED_BY(cs_nonLocked);
    std::unordered_map<COutPoint, uint256, SaltedOutpointHasher> nonLockedTxsByOutpoints GUARDED_BY(cs_nonLocked);

    mutable Mutex cs_pendingRetry;
    std::unordered_set<uint256, StaticSaltedHasher> pendingRetryTxs GUARDED_BY(cs_pendingRetry);

public:
    explicit CInstantSendManager(CTxMemPool& _mempool, CConnman& _connman, bool unitTests, bool fWipe) : db(unitTests, fWipe), mempool(_mempool), connman(_connman) { workInterrupt.reset(); }
    ~CInstantSendManager() = default;

    void Start();
    void Stop();
    void InterruptWorkerThread() { workInterrupt(); };

private:
    void ProcessTx(const CTransaction& tx, bool fRetroactive, const Consensus::Params& params);
    bool CheckCanLock(const CTransaction& tx, bool printDebug, const Consensus::Params& params) const;
    bool CheckCanLock(const COutPoint& outpoint, bool printDebug, const uint256& txHash, const Consensus::Params& params) const;
    bool IsConflicted(const CTransaction& tx) const { return GetConflictingLock(tx) != nullptr; };

    void HandleNewInputLockRecoveredSig(const CRecoveredSig& recoveredSig, const uint256& txid);
    void HandleNewInstantSendLockRecoveredSig(const CRecoveredSig& recoveredSig) LOCKS_EXCLUDED(cs_creating, cs_pendingLocks);

    bool TrySignInputLocks(const CTransaction& tx, bool allowResigning, Consensus::LLMQType llmqType) LOCKS_EXCLUDED(cs_inputRequests);
    void TrySignInstantSendLock(const CTransaction& tx) LOCKS_EXCLUDED(cs_creating);

    void ProcessMessageInstantSendLock(const CNode* pfrom, const CInstantSendLockPtr& islock);
    static bool PreVerifyInstantSendLock(const CInstantSendLock& islock);
    bool ProcessPendingInstantSendLocks();

    std::unordered_set<uint256> ProcessPendingInstantSendLocks(int signOffset, const std::unordered_map<uint256, std::pair<NodeId, CInstantSendLockPtr>, StaticSaltedHasher>& pend, bool ban) LOCKS_EXCLUDED(cs_pendingLocks);
    void ProcessInstantSendLock(NodeId from, const uint256& hash, const CInstantSendLockPtr& islock) LOCKS_EXCLUDED(cs_creating, cs_pendingLocks);

    void AddNonLockedTx(const CTransactionRef& tx, const CBlockIndex* pindexMined) LOCKS_EXCLUDED(cs_pendingLocks, cs_nonLocked);
    void RemoveNonLockedTx(const uint256& txid, bool retryChildren) LOCKS_EXCLUDED(cs_nonLocked, cs_pendingRetry);
    void RemoveConflictedTx(const CTransaction& tx) LOCKS_EXCLUDED(cs_inputRequests);
    void TruncateRecoveredSigsForInputs(const CInstantSendLock& islock) LOCKS_EXCLUDED(cs_inputRequests);

    void RemoveMempoolConflictsForLock(const uint256& hash, const CInstantSendLock& islock);
    void ResolveBlockConflicts(const uint256& islockHash, const CInstantSendLock& islock) LOCKS_EXCLUDED(cs_pendingLocks, cs_nonLocked);
    static void AskNodesForLockedTx(const uint256& txid, const CConnman& connman);
    void ProcessPendingRetryLockTxs() LOCKS_EXCLUDED(cs_creating, cs_nonLocked, cs_pendingRetry);

    void WorkThreadMain();

    void HandleFullyConfirmedBlock(const CBlockIndex* pindex) LOCKS_EXCLUDED(cs_nonLocked);

public:
    bool IsLocked(const uint256& txHash) const;
    bool IsWaitingForTx(const uint256& txHash) const LOCKS_EXCLUDED(cs_pendingLocks);
    CInstantSendLockPtr GetConflictingLock(const CTransaction& tx) const;

    virtual void HandleNewRecoveredSig(const CRecoveredSig& recoveredSig) LOCKS_EXCLUDED(cs_inputRequests, cs_creating);

    void ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv);

    void TransactionAddedToMempool(const CTransactionRef& tx) LOCKS_EXCLUDED(cs_pendingLocks);
    void TransactionRemovedFromMempool(const CTransactionRef& tx);
    void BlockConnected(const std::shared_ptr<const CBlock>& pblock, const CBlockIndex* pindex, const std::vector<CTransactionRef>& vtxConflicted);
    void BlockDisconnected(const std::shared_ptr<const CBlock>& pblock, const CBlockIndex* pindexDisconnected);

    bool AlreadyHave(const CInv& inv) const LOCKS_EXCLUDED(cs_pendingLocks);
    bool GetInstantSendLockByHash(const uint256& hash, CInstantSendLock& ret) const LOCKS_EXCLUDED(cs_pendingLocks);
    CInstantSendLockPtr GetInstantSendLockByTxid(const uint256& txid) const;
    bool GetInstantSendLockHashByTxid(const uint256& txid, uint256& ret) const;

    void NotifyChainLock(const CBlockIndex* pindexChainLock);
    void UpdatedBlockTip(const CBlockIndex* pindexNew);

    void RemoveConflictingLock(const uint256& islockHash, const CInstantSendLock& islock);

    size_t GetInstantSendLockCount() const;
};

extern CInstantSendManager* quorumInstantSendManager;

bool IsInstantSendEnabled();
/**
 * If true, MN should sign all transactions, if false, MN should not sign
 * transactions in mempool, but should sign txes included in a block. This
 * allows ChainLocks to continue even while this spork is disabled.
 */
bool IsInstantSendMempoolSigningEnabled();
bool RejectConflictingBlocks();

} // namespace llmq

#endif // BITCOIN_LLMQ_QUORUMS_INSTANTSEND_H
