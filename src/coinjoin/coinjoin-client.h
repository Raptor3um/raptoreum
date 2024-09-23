// Copyright (c) 2014-2019 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_COINJOIN_COINJOIN_CLIENT_H
#define BITCOIN_COINJOIN_COINJOIN_CLIENT_H

#include <coinjoin/coinjoin-util.h>
#include <coinjoin/coinjoin.h>

#include <utility>
#include <atomic>

class CDeterministicMN;

using CDeterministicMNCPtr = std::shared_ptr<const CDeterministicMN>;

class CCoinJoinClientManager;

class CCoinJoinClientQueueManager;

class CConnman;

class CNode;

class UniValue;


// The main object for accessing mixing
extern std::map<const std::string, std::shared_ptr < CCoinJoinClientManager>>
coinJoinClientManagers;

// The object to track mixing queues
extern CCoinJoinClientQueueManager coinJoinClientQueueManager;

class CPendingDsaRequest {
private:
    static const int TIMEOUT = 15;

    CService addr;
    CCoinJoinAccept dsa;
    int64_t nTimeCreated{0};

public:
    CPendingDsaRequest() = default;

    CPendingDsaRequest(CService addr_, CCoinJoinAccept dsa_) :
            addr(std::move(addr_)),
            dsa(std::move(dsa_)),
            nTimeCreated(GetTime()) {
    }

    CService GetAddr() const { return addr; }

    CCoinJoinAccept GetDSA() const { return dsa; }

    bool IsExpired() const { return GetTime() - nTimeCreated > TIMEOUT; }

    friend bool operator==(const CPendingDsaRequest &a, const CPendingDsaRequest &b) {
        return a.addr == b.addr && a.dsa == b.dsa;
    }

    friend bool operator!=(const CPendingDsaRequest &a, const CPendingDsaRequest &b) {
        return !(a == b);
    }

    explicit operator bool() const {
        return *this != CPendingDsaRequest();
    }
};

class CCoinJoinClientSession : public CCoinJoinBaseSession {
private:
    std::vector <COutPoint> vecOutPointLocked;

    std::string strLastMessage;
    std::string strAutoDenomResult;

    CDeterministicMNCPtr mixingSmartnode;
    CMutableTransaction txMyCollateral; // client side collateral
    CPendingDsaRequest pendingDsaRequest;

    CKeyHolderStorage keyHolderStorage; // storage for keys used in PrepareDenominate

    CWallet &mixingWallet;

    /// Create denominations
    bool CreateDenominated(CAmount nBalanceToDenominate);

    bool
    CreateDenominated(CAmount nBalanceToDenominate, const CompactTallyItem &tallyItem, bool fCreateMixingCollaterals);

    /// Split up large inputs or make fee sized inputs
    bool MakeCollateralAmounts();

    bool MakeCollateralAmounts(const CompactTallyItem &tallyItem, bool fTryDenominated);

    bool CreateCollateralTransaction(CMutableTransaction &txCollateral, std::string &strReason);

    bool JoinExistingQueue(CAmount nBalanceNeedsAnonymized, CConnman &connman);

    bool StartNewQueue(CAmount nBalanceNeedsAnonymized, CConnman &connman);

    /// step 0: select denominated inputs and txouts
    bool SelectDenominate(std::string &strErrorRet, std::vector <CTxDSIn> &vecTxDSInRet);

    /// step 1: prepare denominated inputs and outputs
    bool
    PrepareDenominate(int nMinRounds, int nMaxRounds, std::string &strErrorRet, const std::vector <CTxDSIn> &vecTxDSIn,
                      std::vector <std::pair<CTxDSIn, CTxOut>> &vecPSInOutPairsRet, bool fDryRun = false);

    /// step 2: send denominated inputs and outputs prepared in step 1
    bool SendDenominate(const std::vector <std::pair<CTxDSIn, CTxOut>> &vecPSInOutPairsIn, CConnman &connman)

    LOCKS_EXCLUDED(cs_coinjoin);

    /// Process smartnode updates about the progress of mixing
    void ProcessPoolStateUpdate(CCoinJoinStatusUpdate psssup);

    // Set the 'state' value, with some logging and capturing when the state changed
    void SetState(PoolState nStateNew);

    void CompletedTransaction(PoolMessage nMessageID);

    /// As a client, check and sign the final transaction
    bool SignFinalTransaction(const CTransaction &finalTransactionNew, CNode *pnode, CConnman &connman)

    LOCKS_EXCLUDED(cs_coinjoin);

    void RelayIn(const CCoinJoinEntry &entry, CConnman &connman) const;

    void SetNull()

    EXCLUSIVE_LOCKS_REQUIRED(cs_coinjoin);

public:
    explicit CCoinJoinClientSession(CWallet &pwallet) : mixingWallet(pwallet) {}

    void ProcessMessage(CNode *pfrom, const std::string &strCommand, CDataStream &vRecv, CConnman &connman,
                        bool enable_bip61);

    void UnlockCoins();

    void ResetPool()

    LOCKS_EXCLUDED(cs_coinjoin);

    std::string GetStatus(bool fWaitForBlock) const;

    bool GetMixingSmartnodeInfo(CDeterministicMNCPtr &ret) const;

    /// Passively run mixing in the background according to the configuration in settings
    bool DoAutomaticDenominating(CConnman &connman, bool fDryRun = false)

    LOCKS_EXCLUDED(cs_coinjoin);

    /// As a client, submit part of a future mixing transaction to a Smartnode to start the process
    bool SubmitDenominate(CConnman &connman);

    bool ProcessPendingDsaRequest(CConnman &connman);

    bool CheckTimeout();

    void GetJsonInfo(UniValue &obj) const;
};

/** Used to keep track of mixing queues
 */
class CCoinJoinClientQueueManager : public CCoinJoinBaseManager {
public:
    void ProcessMessage(CNode *pfrom, const std::string &strCommand, CDataStream &vRecv, CConnman &connman,
                        bool enable_bip61)

    LOCKS_EXCLUDED(cs_vecqueue);

    void DoMaintenance();
};

/** Used to keep track of current status of mixing pool
 */
class CCoinJoinClientManager {
private:
    // Keep track of the used Smartnodes
    std::vector <COutPoint> vecSmartnodesUsed;

    mutable Mutex cs_deqsessions;
    // TODO: or map<denom, CCoinJoinClientSession> ??
    std::deque <CCoinJoinClientSession> deqSessions
    GUARDED_BY(cs_deqsessions);

    std::atomic<bool> fMixing{false};

    int nCachedLastSuccessBlock{0};
    int nMinBlocksToWait{1}; // how many blocks to wait after one successful mixing tx in non-multisession mode
    std::string strAutoDenomResult;

    CWallet &mixingWallet;

    // Keep track of current block height
    int nCachedBlockHeight{0};

    bool WaitForAnotherBlock() const;

    // Make sure we have enough keys since last backup
    bool CheckAutomaticBackup();

public:
    int nCachedNumBlocks{std::numeric_limits<int>::max()};    // used for the overview screen
    bool fCreateAutoBackups{true}; // builtin support for automatic backups

    CCoinJoinClientManager() = delete;

    CCoinJoinClientManager(CCoinJoinClientManager const &) = delete;

    CCoinJoinClientManager &operator=(CCoinJoinClientManager const &) = delete;

    explicit CCoinJoinClientManager(CWallet &wallet) : mixingWallet(wallet) {}

    void ProcessMessage(CNode *pfrom, const std::string &strCommand, CDataStream &vRecv, CConnman &connman,
                        bool enable_bip61)

    LOCKS_EXCLUDED(cs_deqsessions);

    bool StartMixing();

    void StopMixing();

    bool IsMixing() const;

    void ResetPool()

    LOCKS_EXCLUDED(cs_deqsessions);

    std::string GetStatuses()

    LOCKS_EXCLUDED(cs_deqsessions);

    std::string GetSessionDenoms()

    LOCKS_EXCLUDED(cs_deqsessions);

    bool GetMixingSmartnodesInfo(std::vector <CDeterministicMNCPtr> &vecDmnsRet) const

    LOCKS_EXCLUDED(cs_deqsessions);

    /// Passively run mixing in the background according to the configuration in settings
    bool DoAutomaticDenominating(CConnman &connman, bool fDryRun = false)

    LOCKS_EXCLUDED(cs_deqsessions);

    bool TrySubmitDenominate(const CService &mnAddr, CConnman &connman)

    LOCKS_EXCLUDED(cs_deqsessions);

    bool MarkAlreadyJoinedQueueAsTried(CCoinJoinQueue &dsq) const

    LOCKS_EXCLUDED(cs_deqsessions);

    void CheckTimeout()

    LOCKS_EXCLUDED(cs_deqsessions);

    void ProcessPendingDsaRequest(CConnman &connman)

    LOCKS_EXCLUDED(cs_deqsessions);

    void AddUsedSmartnode(const COutPoint &outpointMn);

    CDeterministicMNCPtr GetRandomNotUsedSmartnode();

    void UpdatedSuccessBlock();

    void UpdatedBlockTip(const CBlockIndex *pindex);

    void DoMaintenance(CConnman &connman);

    void GetJsonInfo(UniValue &obj) const

    LOCKS_EXCLUDED(cs_deqsessions);
};


void DoCoinJoinMaintenance(CConnman &connman);

#endif // BITCOIN_COINJOIN_COINJOIN_CLIENT_H
