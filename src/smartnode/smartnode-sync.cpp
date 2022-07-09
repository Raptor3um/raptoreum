// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <governance/governance.h>
#include <init.h>
#include <validation.h>
#include <smartnode/smartnode-sync.h>
#include <netfulfilledman.h>
#include <netmessagemaker.h>
#include <ui_interface.h>
#include <evo/deterministicmns.h>

class CSmartnodeSync;
CSmartnodeSync smartnodeSync;

CSmartnodeSync::CSmartnodeSync()
{
    nTimeAssetSyncStarted = GetTime();
    nTimeLastBumped = GetTime();
}

void CSmartnodeSync::Reset(bool fForce, bool fNotifyReset)
{
    // Avoid resetting the sync process if we just "recently" received a new block
    if (fForce) {
        if (GetTime() - nTimeLastUpdateBlockTip > SMARTNODE_SYNC_RESET_SECONDS) {
            return;
        }
    }
    nCurrentAsset = SMARTNODE_SYNC_BLOCKCHAIN;
    nTriedPeerCount = 0;
    nTimeAssetSyncStarted = GetTime();
    nTimeLastBumped = GetTime();
    nTimeLastUpdateBlockTip = 0;
    fReachedBestHeader = false;
    if (fNotifyReset) {
        uiInterface.NotifyAdditionalDataSyncProgressChanged(-1);
    }
}

void CSmartnodeSync::BumpAssetLastTime(const std::string& strFuncName)
{
    if (IsSynced()) return;
    nTimeLastBumped = GetTime();
    LogPrint(BCLog::MNSYNC, "CSmartnodeSync::BumpAssetLastTime -- %s\n", strFuncName);
}

std::string CSmartnodeSync::GetAssetName() const
{
    switch(nCurrentAsset)
    {
        case(SMARTNODE_SYNC_BLOCKCHAIN):   return "SMARTNODE_SYNC_BLOCKCHAIN";
        case(SMARTNODE_SYNC_GOVERNANCE):   return "SMARTNODE_SYNC_GOVERNANCE";
        case SMARTNODE_SYNC_FINISHED:      return "SMARTNODE_SYNC_FINISHED";
        default:                           return "UNKNOWN";
    }
}

void CSmartnodeSync::SwitchToNextAsset(CConnman& connman)
{
    switch(nCurrentAsset)
    {
        case(SMARTNODE_SYNC_BLOCKCHAIN):
            LogPrintf("CSmartnodeSync::SwitchToNextAsset -- Completed %s in %llds\n", GetAssetName(), GetTime() - nTimeAssetSyncStarted);
            nCurrentAsset = SMARTNODE_SYNC_GOVERNANCE;
            LogPrintf("CSmartnodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(SMARTNODE_SYNC_GOVERNANCE):
            LogPrintf("CSmartnodeSync::SwitchToNextAsset -- Completed %s in %llds\n", GetAssetName(), GetTime() - nTimeAssetSyncStarted);
            nCurrentAsset = SMARTNODE_SYNC_FINISHED;
            uiInterface.NotifyAdditionalDataSyncProgressChanged(1);

            connman.ForEachNode(CConnman::AllNodes, [](CNode* pnode) {
                netfulfilledman.AddFulfilledRequest(pnode->addr, "full-sync");
            });
            LogPrintf("CSmartnodeSync::SwitchToNextAsset -- Sync has finished\n");

            break;
    }
    nTriedPeerCount = 0;
    nTimeAssetSyncStarted = GetTime();
    BumpAssetLastTime("CSmartnodeSync::SwitchToNextAsset");
}

std::string CSmartnodeSync::GetSyncStatus() const
{
    switch (nCurrentAsset) {
        case SMARTNODE_SYNC_BLOCKCHAIN:    return _("Synchronizing blockchain...");
        case SMARTNODE_SYNC_GOVERNANCE:    return _("Synchronizing governance objects...");
        case SMARTNODE_SYNC_FINISHED:      return _("Synchronization finished");
        default:                           return "";
    }
}

void CSmartnodeSync::ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv) const
{
    if (strCommand == NetMsgType::SYNCSTATUSCOUNT) { //Sync status count

        //do not care about stats if sync process finished
        if (IsSynced()) return;

        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        LogPrintf("SYNCSTATUSCOUNT -- got inventory count: nItemID=%d  nCount=%d  peer=%d\n", nItemID, nCount, pfrom->GetId());
    }
}

void CSmartnodeSync::ProcessTick(CConnman& connman)
{
    static int nTick = 0;
    nTick++;

    const static int64_t nSyncStart = GetTimeMillis();
    const static std::string strAllow = strprintf("allow-sync-%lld", nSyncStart);

    // reset the sync process if the last call to this function was more than 60 minutes ago (client was in sleep mode)
    static int64_t nTimeLastProcess = GetTime();
    if(GetTime() - nTimeLastProcess > 60*60 && !fSmartnodeMode) {
        LogPrintf("CSmartnodeSync::ProcessTick -- WARNING: no actions for too long, restarting sync...\n");
        Reset(true);
        nTimeLastProcess = GetTime();
        return;
    }

    if(GetTime() - nTimeLastProcess < SMARTNODE_SYNC_TICK_SECONDS) {
        // too early, nothing to do here
        return;
    }

    nTimeLastProcess = GetTime();
    std::vector<CNode*> vNodesCopy = connman.CopyNodeVector(CConnman::FullyConnectedOnly);

    // gradually request the rest of the votes after sync finished
    if(IsSynced()) {
        governance.RequestGovernanceObjectVotes(vNodesCopy, connman);
        connman.ReleaseNodeVector(vNodesCopy);
        return;
    }

    // Calculate "progress" for LOG reporting / GUI notification
    double nSyncProgress = double(nTriedPeerCount + (nCurrentAsset - 1) * 8) / (8*4);
    LogPrintf("CSmartnodeSync::ProcessTick -- nTick %d nCurrentAsset %d nTriedPeerCount %d nSyncProgress %f\n", nTick, nCurrentAsset, nTriedPeerCount, nSyncProgress);
    uiInterface.NotifyAdditionalDataSyncProgressChanged(nSyncProgress);

    for (auto& pnode : vNodesCopy)
    {
        CNetMsgMaker msgMaker(pnode->GetSendVersion());

        // Don't try to sync any data from outbound non-relay "smartnode" connections.
        // Inbound connection this early is most likely a "smartnode" connection
        // initiated from another node, so skip it too.
        if (!pnode->CanRelay() || (fSmartnodeMode && pnode->fInbound)) continue;

        // QUICK MODE (REGTEST ONLY!)
        if(Params().NetworkIDString() == CBaseChainParams::REGTEST)
        {
            if (nCurrentAsset == SMARTNODE_SYNC_BLOCKCHAIN) {
                connman.PushMessage(pnode, msgMaker.Make(NetMsgType::GETSPORKS)); //get current network sporks
                SwitchToNextAsset(connman);
            } else if (nCurrentAsset == SMARTNODE_SYNC_GOVERNANCE) {
                SendGovernanceSyncRequest(pnode, connman);
                SwitchToNextAsset(connman);
            }
            connman.ReleaseNodeVector(vNodesCopy);
            return;
        }

        // NORMAL NETWORK MODE - TESTNET/MAINNET
        {
            if ((pnode->fWhitelisted || pnode->m_manual_connection) && !netfulfilledman.HasFulfilledRequest(pnode->addr, strAllow)) {
                netfulfilledman.RemoveAllFulfilledRequests(pnode->addr);
                netfulfilledman.AddFulfilledRequest(pnode->addr, strAllow);
                LogPrintf("CSmartnodeSync::ProcessTick -- skipping mnsync restrictions for peer=%d\n", pnode->GetId());
            }

            if(netfulfilledman.HasFulfilledRequest(pnode->addr, "full-sync")) {
                // We already fully synced from this node recently,
                // disconnect to free this connection slot for another peer.
                pnode->fDisconnect = true;
                LogPrintf("CSmartnodeSync::ProcessTick -- disconnecting from recently synced peer=%d\n", pnode->GetId());
                continue;
            }

            // SPORK : ALWAYS ASK FOR SPORKS AS WE SYNC

            if(!netfulfilledman.HasFulfilledRequest(pnode->addr, "spork-sync")) {
                // always get sporks first, only request once from each peer
                netfulfilledman.AddFulfilledRequest(pnode->addr, "spork-sync");
                // get current network sporks
                connman.PushMessage(pnode, msgMaker.Make(NetMsgType::GETSPORKS));
                LogPrintf("CSmartnodeSync::ProcessTick -- nTick %d nCurrentAsset %d -- requesting sporks from peer=%d\n", nTick, nCurrentAsset, pnode->GetId());
            }

            if (nCurrentAsset == SMARTNODE_SYNC_BLOCKCHAIN) {
                int64_t nTimeSyncTimeout = vNodesCopy.size() > 3 ? SMARTNODE_SYNC_TICK_SECONDS : SMARTNODE_SYNC_TIMEOUT_SECONDS;
                if (fReachedBestHeader && (GetTime() - nTimeLastBumped > nTimeSyncTimeout)) {
                    // At this point we know that:
                    // a) there are peers (because we are looping on at least one of them);
                    // b) we waited for at least SMARTNODE_SYNC_TICK_SECONDS/SMARTNODE_SYNC_TIMEOUT_SECONDS
                    //    (depending on the number of connected peers) since we reached the headers tip the last
                    //    time (i.e. since fReachedBestHeader has been set to true);
                    // c) there were no blocks (UpdatedBlockTip, NotifyHeaderTip) or headers (AcceptedBlockHeader)
                    //    for at least SMARTNODE_SYNC_TICK_SECONDS/SMARTNODE_SYNC_TIMEOUT_SECONDS (depending on
                    //    the number of connected peers).
                    // We must be at the tip already, let's move to the next asset.
                    SwitchToNextAsset(connman);
                    uiInterface.NotifyAdditionalDataSyncProgressChanged(nSyncProgress);

                    if (gArgs.GetBoolArg("-syncmempool", DEFAULT_SYNC_MEMPOOL)) {
                        // Now that the blockchain is synced request the mempool from the connected outbound nodes if possible
                        for (auto pNodeTmp : vNodesCopy) {
                            bool fRequestedEarlier = netfulfilledman.HasFulfilledRequest(pNodeTmp->addr, "mempool-sync");
                            if (pNodeTmp->nVersion >= 70216 && !pNodeTmp->fInbound && !fRequestedEarlier) {
                                netfulfilledman.AddFulfilledRequest(pNodeTmp->addr, "mempool-sync");
                                connman.PushMessage(pNodeTmp, msgMaker.Make(NetMsgType::MEMPOOL));
                                LogPrintf("CSmartnodeSync::ProcessTick -- nTick %d nCurrentAsset %d -- syncing mempool from peer=%d\n", nTick, nCurrentAsset, pNodeTmp->GetId());
                            }
                        }
                    }
                }
            }

            // GOVOBJ : SYNC GOVERNANCE ITEMS FROM OUR PEERS

            if(nCurrentAsset == SMARTNODE_SYNC_GOVERNANCE) {
                if (fDisableGovernance) {
                    SwitchToNextAsset(connman);
                    connman.ReleaseNodeVector(vNodesCopy);
                    return;
                }
                LogPrint(BCLog::GOBJECT, "CSmartnodeSync::ProcessTick -- nTick %d nCurrentAsset %d nTimeLastBumped %lld GetTime() %lld diff %lld\n", nTick, nCurrentAsset, nTimeLastBumped, GetTime(), GetTime() - nTimeLastBumped);

                // check for timeout first
                if(GetTime() - nTimeLastBumped > SMARTNODE_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CSmartnodeSync::ProcessTick -- nTick %d nCurrentAsset %d -- timeout\n", nTick, nCurrentAsset);
                    if(nTriedPeerCount == 0) {
                        LogPrintf("CSmartnodeSync::ProcessTick -- WARNING: failed to sync %s\n", GetAssetName());
                        // it's kind of ok to skip this for now, hopefully we'll catch up later?
                    }
                    SwitchToNextAsset(connman);
                    connman.ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // only request obj sync once from each peer
                if(netfulfilledman.HasFulfilledRequest(pnode->addr, "governance-sync")) {
                    // will request votes on per-obj basis from each node in a separate loop
                    // below to avoid deadlocks here
                    continue;
                }
                netfulfilledman.AddFulfilledRequest(pnode->addr, "governance-sync");

                if (pnode->nVersion < MIN_GOVERNANCE_PEER_PROTO_VERSION) continue;
                nTriedPeerCount++;

                SendGovernanceSyncRequest(pnode, connman);

                break; // this will cause each poeer to get one request each six seconds for the various assets we need
            }
        }
    }

    if (nCurrentAsset != SMARTNODE_SYNC_GOVERNANCE) {
        connman.ReleaseNodeVector(vNodesCopy);
        return;
    }

    for (auto& pnode : vNodesCopy) {
        if(!netfulfilledman.HasFulfilledRequest(pnode->addr, "governance-sync")) {
            continue;
        }
        int nObjsLeftToAsk = governance.RequestGovernanceObjectVotes(pnode, connman);
        if(nObjsLeftToAsk == 0) {
            static int64_t nTimeNoObjectsLeft = 0;
            static int nLastTick = 0;
            static int nLastVotes = 0;
            if(nTimeNoObjectsLeft == 0) {
                nTimeNoObjectsLeft = GetTime();
            }
            if(nLastTick == nTick) continue;
            if(GetTime() - nTimeNoObjectsLeft > SMARTNODE_SYNC_TIMEOUT_SECONDS && governance.GetVoteCount() - nLastVotes < std::max(int(0.0001 * nLastVotes), SMARTNODE_SYNC_TICK_SECONDS)) {
                LogPrintf("CSmartnodeSync::ProcessTick -- nTick %d nCurrentAsset %d -- asked for all objects, nothing to do\n", nTick, SMARTNODE_SYNC_GOVERNANCE);
                nTimeNoObjectsLeft = 0;
                SwitchToNextAsset(connman);
                connman.ReleaseNodeVector(vNodesCopy);
                return;
            }
            nLastTick = nTick;
            nLastVotes = governance.GetVoteCount();
        }
    }
    // looped through all nodes, release them
    connman.ReleaseNodeVector(vNodesCopy);
}

void CSmartnodeSync::SendGovernanceSyncRequest(CNode* pnode, CConnman& connman)
{
    CNetMsgMaker msgMaker(pnode->GetSendVersion());

    CBloomFilter filter;
    filter.clear();

    connman.PushMessage(pnode, msgMaker.Make(NetMsgType::MNGOVERNANCESYNC, uint256(), filter));
}

void CSmartnodeSync::AcceptedBlockHeader(const CBlockIndex *pindexNew)
{
    LogPrint(BCLog::MNSYNC, "CSmartnodeSync::AcceptedBlockHeader -- pindexNew->nHeight: %d\n", pindexNew->nHeight);

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block header arrives while we are still syncing blockchain
        BumpAssetLastTime("CSmartnodeSync::AcceptedBlockHeader");
    }
}

void CSmartnodeSync::NotifyHeaderTip(const CBlockIndex *pindexNew, bool fInitialDownload, CConnman& connman)
{
    if (pindexNew == nullptr) {
        return;
    }
    LogPrint(BCLog::MNSYNC, "CSmartnodeSync::NotifyHeaderTip -- pindexNew->nHeight: %d fInitialDownload=%d\n", pindexNew->nHeight, fInitialDownload);
    if (IsSynced())
        return;

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block arrives while we are still syncing blockchain
        BumpAssetLastTime("CSmartnodeSync::NotifyHeaderTip");
    }
}

void CSmartnodeSync::UpdatedBlockTip(const CBlockIndex *pindexNew, bool fInitialDownload, CConnman& connman)
{
    LogPrint(BCLog::MNSYNC, "CSmartnodeSync::UpdatedBlockTip -- pindexNew->nHeight: %d fInitialDownload=%d\n", pindexNew->nHeight, fInitialDownload);
    nTimeLastUpdateBlockTip = GetAdjustedTime();

    CBlockIndex* pindexTip;
    {
        LOCK(cs_main);
        pindexTip = pindexBestHeader;
    }

    if (IsSynced() || !pindexTip)
        return;

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block arrives while we are still syncing blockchain
        BumpAssetLastTime("CSmartnodeSync::UpdatedBlockTip");
    }

    if (fInitialDownload) {
        // switched too early
        if (IsBlockchainSynced()) {
            Reset(true);
        }

        // no need to check any further while still in IBD mode
        return;
    }

    // Note: since we sync headers first, it should be ok to use this
    bool fReachedBestHeaderNew = pindexNew->GetBlockHash() == pindexTip->GetBlockHash();

    if (fReachedBestHeader && !fReachedBestHeaderNew) {
        // Switching from true to false means that we previously stuck syncing headers for some reason,
        // probably initial timeout was not enough,
        // because there is no way we can update tip not having best header
        Reset(true);
    }

    fReachedBestHeader = fReachedBestHeaderNew;
    LogPrint(BCLog::MNSYNC, "CSmartnodeSync::UpdatedBlockTip -- pindexNew->nHeight: %d pindexTip->nHeight: %d fInitialDownload=%d fReachedBestHeader=%d\n",
                pindexNew->nHeight, pindexTip->nHeight, fInitialDownload, fReachedBestHeader);
}

void CSmartnodeSync::DoMaintenance(CConnman &connman)
{
    if (ShutdownRequested()) return;

    ProcessTick(connman);
}
