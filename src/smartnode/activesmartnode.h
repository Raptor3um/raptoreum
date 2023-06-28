// Copyright (c) 2014-2019 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SMARTNODE_ACTIVESMARTNODE_H
#define BITCOIN_SMARTNODE_ACTIVESMARTNODE_H

#include <netaddress.h>
#include <primitives/transaction.h>
#include <validationinterface.h>

class CBLSPublicKey;

class CBLSSecretKey;

struct CActiveSmartnodeInfo;

class CActiveSmartnodeManager;

extern CActiveSmartnodeInfo activeSmartnodeInfo;
extern RecursiveMutex activeSmartnodeInfoCs;
extern CActiveSmartnodeManager *activeSmartnodeManager;

struct CActiveSmartnodeInfo {
    // Keys for the active Smartnode
    std::unique_ptr <CBLSPublicKey> blsPubKeyOperator;
    std::unique_ptr <CBLSSecretKey> blsKeyOperator;

    // Initialized while registering Smartnode
    uint256 proTxHash;
    COutPoint outpoint;
    CService service;
};


class CActiveSmartnodeManager : public CValidationInterface {
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
    CConnman &connman;

public:
    explicit CActiveSmartnodeManager(CConnman &_connman) : connman(_connman) {};

    ~CActiveSmartnodeManager() = default;

    void UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload) override;

    void Init(const CBlockIndex *pindex);

    std::string GetStateString() const;

    std::string GetStatus() const;

    static bool IsValidNetAddr(CService addrIn);

private:
    bool GetLocalAddress(CService &addrRet);
};

#endif // BITCOIN_SMARTNODE_ACTIVESMARTNODE_H
