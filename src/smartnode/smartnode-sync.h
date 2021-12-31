// Copyright (c) 2014-2019 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef SMARTNODE_SYNC_H
#define SMARTNODE_SYNC_H

#include <chain.h>
#include <net.h>

class CSmartnodeSync;

static const int SMARTNODE_SYNC_BLOCKCHAIN      = 1;
static const int SMARTNODE_SYNC_GOVERNANCE      = 4;
static const int SMARTNODE_SYNC_GOVOBJ          = 10;
static const int SMARTNODE_SYNC_GOVOBJ_VOTE     = 11;
static const int SMARTNODE_SYNC_FINISHED        = 999;

static const int SMARTNODE_SYNC_TICK_SECONDS    = 6;
static const int SMARTNODE_SYNC_TIMEOUT_SECONDS = 30; // our blocks are 2.5 minutes so 30 seconds should be fine
static const int SMARTNODE_SYNC_RESET_SECONDS = 600; // Reset fReachedBestHeader in CSmartnodeSync::Reset if UpdateBlockTip hasn't been called for this seconds

extern CSmartnodeSync smartnodeSync;

//
// CSmartnodeSync : Sync smartnode assets in stages
//

class CSmartnodeSync
{
private:
    // Keep track of current asset
    int nCurrentAsset;
    // Count peers we've requested the asset from
    int nTriedPeerCount;

    // Time when current smartnode asset sync started
    int64_t nTimeAssetSyncStarted;
    // ... last bumped
    int64_t nTimeLastBumped;

    /// Set to true if best header is reached in CSmartnodeSync::UpdatedBlockTip
    bool fReachedBestHeader{false};
    /// Last time UpdateBlockTip has been called
    int64_t nTimeLastUpdateBlockTip{0};

public:
    CSmartnodeSync() { Reset(true, false); }


    static void SendGovernanceSyncRequest(CNode* pnode, CConnman& connman);

    bool IsBlockchainSynced() const { return nCurrentAsset > SMARTNODE_SYNC_BLOCKCHAIN; }
    bool IsSynced() const { return nCurrentAsset == SMARTNODE_SYNC_FINISHED; }

    int GetAssetID() const { return nCurrentAsset; }
    int GetAttempt() const { return nTriedPeerCount; }
    void BumpAssetLastTime(const std::string& strFuncName);
    int64_t GetAssetStartTime() const { return nTimeAssetSyncStarted; }
    std::string GetAssetName() const;
    std::string GetSyncStatus() const;

    void Reset(bool fForce = false, bool fNotifyReset = true);
    void SwitchToNextAsset(CConnman& connman);

    void ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv) const;
    void ProcessTick(CConnman& connman);

    void AcceptedBlockHeader(const CBlockIndex *pindexNew);
    void NotifyHeaderTip(const CBlockIndex *pindexNew, bool fInitialDownload, CConnman& connman);
    void UpdatedBlockTip(const CBlockIndex *pindexNew, bool fInitialDownload, CConnman& connman);

    void DoMaintenance(CConnman &connman);
};

#endif
