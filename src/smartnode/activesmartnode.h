// Copyright (c) 2014-2019 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVESMARTNODE_H
#define ACTIVESMARTNODE_H

#include <chainparams.h>
#include <key.h>
#include <net.h>
#include <primitives/transaction.h>
#include <validationinterface.h>

#include <evo/deterministicmns.h>
#include <evo/providertx.h>

struct CActiveSmartnodeInfo;
class CActiveSmartnodeManager;

extern CActiveSmartnodeInfo activeSmartnodeInfo;
extern CActiveSmartnodeManager* activeSmartnodeManager;

struct CActiveSmartnodeInfo {
    // Keys for the active Smartnode
    std::unique_ptr<CBLSPublicKey> blsPubKeyOperator;
    std::unique_ptr<CBLSSecretKey> blsKeyOperator;

    // Initialized while registering Smartnode
    uint256 proTxHash;
    COutPoint outpoint;
    CService service;
};


class CActiveSmartnodeManager : public CValidationInterface
{
public:
    enum smartnode_state_t {
        SMARTNODE_WAITING_FOR_PROTX,
        SMARTNODE_POSE_BANNED,
        SMARTNODE_REMOVED,
        SMARTNODE_OPERATOR_KEY_CHANGED,
        SMARTNODE_PROTX_IP_CHANGED,
        SMARTNODE_READY,
        SMARTNODE_ERROR,
    };

private:
    smartnode_state_t state{SMARTNODE_WAITING_FOR_PROTX};
    std::string strError;

public:
    virtual void UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload);

    void Init(const CBlockIndex* pindex);

    std::string GetStateString() const;
    std::string GetStatus() const;

    static bool IsValidNetAddr(CService addrIn);

private:
    bool GetLocalAddress(CService& addrRet);
};

#endif
