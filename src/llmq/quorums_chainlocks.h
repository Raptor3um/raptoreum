// Copyright (c) 2019-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LLMQ_QUORUMS_CHAINLOCKS_H
#define BITCOIN_LLMQ_QUORUMS_CHAINLOCKS_H

#include <bls/bls.h>
#include <llmq/quorums_signing.h>
#include <net.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <saltedhasher.h>
#include <streams.h>
#include <sync.h>

#include <atomic>
#include <unordered_set>

class CConnman;

class CBlockIndex;

class CScheduler;

class CTxMemPool;

namespace llmq {

    extern const std::string CLSIG_REQUESTID_PREFIX;

    class CChainLockSig {
    private:
        int32_t nHeight{-1};
        uint256 blockHash;
        CBLSSignature sig;

    public:
        CChainLockSig(int32_t nHeight, const uint256 &blockHash, const CBLSSignature &sig)
                : nHeight(nHeight), blockHash(blockHash), sig(sig) {}

        CChainLockSig() = default;

        [[nodiscard]] int32_t getHeight() const;

        [[nodiscard]] const uint256 &getBlockHash() const;

        [[nodiscard]] const CBLSSignature &getSig() const;

        [[nodiscard]] bool IsNull() const;

        [[nodiscard]] std::string ToString() const;

        SERIALIZE_METHODS(CChainLockSig, obj
        )
        {
            READWRITE(obj.nHeight, obj.blockHash, obj.sig);
        }
    };

    class CChainLocksHandler : public CRecoveredSigsListener {
        static const int64_t CLEANUP_INTERVAL = 1000 * 30;
        static const int64_t CLEANUP_SEEN_TIMEOUT = 24 * 60 * 60 * 1000;

        // how long to wait for islocks until we consider a block with non-islocked TXs to be safe to sign
        static const int64_t WAIT_FOR_ISLOCK_TIMEOUT = 10 * 60;

    private:
        CConnman &connman;
        CTxMemPool &mempool;
        std::unique_ptr <CScheduler> scheduler;
        std::unique_ptr <std::thread> scheduler_thread;
        mutable RecursiveMutex cs;
        bool tryLockChainTipScheduled
        GUARDED_BY(cs) {false};
        bool isEnabled
        GUARDED_BY(cs) {false};
        bool isEnforced
        GUARDED_BY(cs) {false};

        uint256 bestChainLockHash
        GUARDED_BY(cs);
        CChainLockSig bestChainLock
        GUARDED_BY(cs);

        CChainLockSig bestChainLockWithKnownBlock
        GUARDED_BY(cs);
        const CBlockIndex *bestChainLockBlockIndex
        GUARDED_BY(cs) {nullptr};
        const CBlockIndex *lastNotifyChainLockBlockIndex
        GUARDED_BY(cs) {nullptr};

        int32_t lastSignedHeight
        GUARDED_BY(cs) {-1};
        uint256 lastSignedRequestId
        GUARDED_BY(cs);
        uint256 lastSignedMsgHash
        GUARDED_BY(cs);

        // We keep track of txids from recently received blocks so that we can check if all TXs got islocked
        using BlockTxs = std::unordered_map <uint256, std::shared_ptr<
                std::unordered_set < uint256, StaticSaltedHasher>>>;
        BlockTxs blockTxs
        GUARDED_BY(cs);
        std::unordered_map <uint256, int64_t> txFirstSeenTime
        GUARDED_BY(cs);

        std::map <uint256, int64_t> seenChainLocks
        GUARDED_BY(cs);

        int64_t lastCleanupTime
        GUARDED_BY(cs) {0};

    public:
        explicit CChainLocksHandler(CTxMemPool &_mempool, CConnman &_connman);

        ~CChainLocksHandler();

        void Start();

        void Stop();

        bool AlreadyHave(const CInv &inv) const;

        bool GetChainLockByHash(const uint256 &hash, CChainLockSig &ret) const;

        CChainLockSig GetBestChainLock() const;

        void ProcessMessage(CNode *pfrom, const std::string &strCommand, CDataStream &vRecv);

        void ProcessNewChainLock(NodeId from, const CChainLockSig &clsig, const uint256 &hash);

        void AcceptedBlockHeader(const CBlockIndex *pindexNew);

        void UpdatedBlockTip();

        void TransactionAddedToMempool(const CTransactionRef &tx, int64_t nAcceptTime);

        void BlockConnected(const std::shared_ptr<const CBlock> &pblock, const CBlockIndex *pindex,
                            const std::vector <CTransactionRef> &vtxConflicted);

        void BlockDisconnected(const std::shared_ptr<const CBlock> &pblock, const CBlockIndex *pindexDisconnected);

        void CheckActiveState();

        void TrySignChainTip();

        void EnforceBestChainLock();

        void HandleNewRecoveredSig(const CRecoveredSig &recoveredSig) override;

        bool HasChainLock(int nHeight, const uint256 &blockHash) const;

        bool HasConflictingChainLock(int nHeight, const uint256 &blockHash) const;

        bool IsTxSafeForMining(const uint256 &txid) const;

    private:
        // these require locks to be held already
        bool InternalHasChainLock(int nHeight, const uint256 &blockHash) const

        EXCLUSIVE_LOCKS_REQUIRED(cs);

        bool InternalHasConflictingChainLock(int nHeight, const uint256 &blockHash) const

        EXCLUSIVE_LOCKS_REQUIRED(cs);

        BlockTxs::mapped_type GetBlockTxs(const uint256 &blockHash);

        void Cleanup();
    };

    extern CChainLocksHandler *chainLocksHandler;

    bool AreChainLocksEnabled();

/*
template<typename Callable> void TraceCL(const std::string name, Callable func)
{
  std::string namestr = "rtm-" + name; util::ThreadRename(namestr.c_str());
  try { LogPrintf("%s thread start\n", name); func(); LogPrintf("%s thread stop\n", name); }
  catch (...) { PrintExceptionContinue(std::current_exception(), name.c_str()); throw; }
}
*/

} // namespace llmq

#endif // BITCOIN_LLMQ_QUORUMS_CHAINLOCKS_H
