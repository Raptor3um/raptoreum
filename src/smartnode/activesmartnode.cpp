// Copyright (c) 2014-2020 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <smartnode/activesmartnode.h>
#include <evo/deterministicmns.h>
#include <smartnode/smartnode-sync.h>
#include <netbase.h>
#include <protocol.h>
#include <validation.h>
#include <warnings.h>

// Keep track of the active Smartnode
CCriticalSection activeSmartnodeInfoCs;
CActiveSmartnodeInfo activeSmartnodeInfo GUARDED_BY(activeSmartnodeInfoCs);
CActiveSmartnodeManager* activeSmartnodeManager;

std::string CActiveSmartnodeManager::GetStateString() const
{
    switch (state) {
    case SMARTNODE_WAITING_FOR_PROTX:
        return "WAITING_FOR_PROTX";
    case SMARTNODE_POSE_BANNED:
        return "POSE_BANNED";
    case SMARTNODE_REMOVED:
        return "REMOVED";
    case SMARTNODE_OPERATOR_KEY_CHANGED:
        return "OPERATOR_KEY_CHANGED";
    case SMARTNODE_PROTX_IP_CHANGED:
        return "PROTX_IP_CHANGED";
    case SMARTNODE_READY:
        return "READY";
    case SMARTNODE_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

std::string CActiveSmartnodeManager::GetStatus() const
{
    switch (state) {
    case SMARTNODE_WAITING_FOR_PROTX:
        return "Waiting for ProTx to appear on-chain";
    case SMARTNODE_POSE_BANNED:
        return "Smartnode was PoSe banned";
    case SMARTNODE_REMOVED:
        return "Smartnode removed from list";
    case SMARTNODE_OPERATOR_KEY_CHANGED:
        return "Operator key changed or revoked";
    case SMARTNODE_PROTX_IP_CHANGED:
        return "IP address specified in ProTx changed";
    case SMARTNODE_READY:
        return "Ready";
    case SMARTNODE_ERROR:
        return "Error. " + strError;
    default:
        return "Unknown";
    }
}

void CActiveSmartnodeManager::Init(const CBlockIndex* pindex)
{
    LOCK2(cs_main, activeSmartnodeInfoCs);

    if (!fSmartnodeMode) return;

    if (!deterministicMNManager->IsDIP3Enforced(pindex->nHeight)) return;

    // Check that our local network configuration is correct
    if (!fListen && Params().RequireRoutableExternalIP()) {
        // listen option is probably overwritten by something else, no good
        state = SMARTNODE_ERROR;
        strError = "Smartnode must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrintf("CActiveSmartnodeManager::Init -- ERROR: %s\n", strError);
        return;
    }

    if (!GetLocalAddress(activeSmartnodeInfo.service)) {
        state = SMARTNODE_ERROR;
        return;
    }

    CDeterministicMNList mnList = deterministicMNManager->GetListForBlock(pindex);

    CDeterministicMNCPtr dmn = mnList.GetMNByOperatorKey(*activeSmartnodeInfo.blsPubKeyOperator);
    if (!dmn) {
        // MN not appeared on the chain yet
        return;
    }

    if (!mnList.IsMNValid(dmn->proTxHash)) {
        if (mnList.IsMNPoSeBanned(dmn->proTxHash)) {
            state = SMARTNODE_POSE_BANNED;
        } else {
            state = SMARTNODE_REMOVED;
        }
        return;
    }

    LogPrintf("CActiveSmartnodeManager::Init -- proTxHash=%s, proTx=%s\n", dmn->proTxHash.ToString(), dmn->ToString());

    if (activeSmartnodeInfo.service != dmn->pdmnState->addr) {
        state = SMARTNODE_ERROR;
        strError = "Local address does not match the address from ProTx";
        LogPrintf("CActiveSmartnodeManager::Init -- ERROR: %s\n", strError);
        return;
    }

    // Check socket connectivity
    LogPrintf("CActiveSmartnodeManager::Init -- Checking inbound connection to '%s'\n", activeSmartnodeInfo.service.ToString());
    SOCKET hSocket = CreateSocket(activeSmartnodeInfo.service);
    if (hSocket == INVALID_SOCKET) {
        state = SMARTNODE_ERROR;
        strError = "Could not create socket to connect to " + activeSmartnodeInfo.service.ToString();
        LogPrintf("CActiveSmartnodeManager::Init -- ERROR: %s\n", strError);
        return;
    }
    bool fConnected = ConnectSocketDirectly(activeSmartnodeInfo.service, hSocket, nConnectTimeout, true) && IsSelectableSocket(hSocket);
    CloseSocket(hSocket);

    if (!fConnected && Params().RequireRoutableExternalIP()) {
        state = SMARTNODE_ERROR;
        strError = "Could not connect to " + activeSmartnodeInfo.service.ToString();
        LogPrintf("CActiveSmartnodeManager::Init -- ERROR: %s\n", strError);
        return;
    }

    activeSmartnodeInfo.proTxHash = dmn->proTxHash;
    activeSmartnodeInfo.outpoint = dmn->collateralOutpoint;
    state = SMARTNODE_READY;
}

void CActiveSmartnodeManager::UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload)
{
    LOCK2(cs_main, activeSmartnodeInfoCs);

    if (!fSmartnodeMode) return;

    if (!deterministicMNManager->IsDIP3Enforced(pindexNew->nHeight)) return;

    if (state == SMARTNODE_READY) {
        auto oldMNList = deterministicMNManager->GetListForBlock(pindexNew->pprev);
        auto newMNList = deterministicMNManager->GetListForBlock(pindexNew);
        if (!newMNList.IsMNValid(activeSmartnodeInfo.proTxHash)) {
            // MN disappeared from MN list
            state = SMARTNODE_REMOVED;
            activeSmartnodeInfo.proTxHash = uint256();
            activeSmartnodeInfo.outpoint.SetNull();
            // MN might have reappeared in same block with a new ProTx
            Init(pindexNew);
            return;
        }

        auto oldDmn = oldMNList.GetMN(activeSmartnodeInfo.proTxHash);
        auto newDmn = newMNList.GetMN(activeSmartnodeInfo.proTxHash);
        if (newDmn->pdmnState->pubKeyOperator != oldDmn->pdmnState->pubKeyOperator) {
            // MN operator key changed or revoked
            state = SMARTNODE_OPERATOR_KEY_CHANGED;
            activeSmartnodeInfo.proTxHash = uint256();
            activeSmartnodeInfo.outpoint.SetNull();
            // MN might have reappeared in same block with a new ProTx
            Init(pindexNew);
            return;
        }

        if (newDmn->pdmnState->addr != oldDmn->pdmnState->addr) {
            // MN IP changed
            state = SMARTNODE_PROTX_IP_CHANGED;
            activeSmartnodeInfo.proTxHash = uint256();
            activeSmartnodeInfo.outpoint.SetNull();
            Init(pindexNew);
            return;
        }
    } else {
        // MN might have (re)appeared with a new ProTx or we've found some peers
        // and figured out our local address
        Init(pindexNew);
    }
}

bool CActiveSmartnodeManager::GetLocalAddress(CService& addrRet)
{
    // First try to find whatever our own local address is known internally.
    // Addresses could be specified via externalip or bind option, discovered via UPnP
    // or added by TorController. Use some random dummy IPv4 peer to prefer the one
    // reachable via IPv4.
    CNetAddr addrDummyPeer;
    bool fFoundLocal{false};
    if (LookupHost("8.8.8.8", addrDummyPeer, false)) {
        fFoundLocal = GetLocal(addrRet, &addrDummyPeer) && IsValidNetAddr(addrRet);
    }
    if (!fFoundLocal && !Params().RequireRoutableExternalIP()) {
        if (Lookup("127.0.0.1", addrRet, GetListenPort(), false)) {
            fFoundLocal = true;
        }
    }
    if (!fFoundLocal) {
        bool empty = true;
        // If we have some peers, let's try to find our local address from one of them
        auto service = WITH_LOCK(activeSmartnodeInfoCs, return activeSmartnodeInfo.service);
        g_connman->ForEachNodeContinueIf(CConnman::AllNodes, [&](CNode* pnode) {
            empty = false;
            if (pnode->addr.IsIPv4())
                fFoundLocal = GetLocal(service, &pnode->addr) && IsValidNetAddr(service);
            return !fFoundLocal;
        });
        // nothing and no live connections, can't do anything for now
        if (empty) {
            strError = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
            LogPrintf("CActiveSmartnodeManager::GetLocalAddress -- ERROR: %s\n", strError);
            return false;
        }
    }
    return true;
}

bool CActiveSmartnodeManager::IsValidNetAddr(CService addrIn)
{
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return !Params().RequireRoutableExternalIP() ||
           (addrIn.IsIPv4() && IsReachable(addrIn) && addrIn.IsRoutable());
}
