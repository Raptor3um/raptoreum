// Copyright (c) 2014-2020 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_GOVERNANCE_GOVERNANCE_CLASSES_H
#define BITCOIN_GOVERNANCE_GOVERNANCE_CLASSES_H

#include <base58.h>
#include <governance/governance.h>
#include <key.h>
#include <script/standard.h>
#include <util/system.h>
#include <key_io.h>

class CSuperblock;

class CGovernanceTriggerManager;

class CSuperblockManager;

using CSuperblock_sptr = std::shared_ptr<CSuperblock>;

// DECLARE GLOBAL VARIABLES FOR GOVERNANCE CLASSES
extern CGovernanceTriggerManager triggerman;

/**
*   Trigger Manager
*
*   - Track governance objects which are triggers
*   - After triggers are activated and executed, they can be removed
*/

class CGovernanceTriggerManager {
    friend class CSuperblockManager;

    friend class CGovernanceManager;

private:
    std::map <uint256, CSuperblock_sptr> mapTrigger;

    std::vector <CSuperblock_sptr> GetActiveTriggers();

    bool AddNewTrigger(uint256 nHash);

    void CleanAndRemove();

public:
    CGovernanceTriggerManager() :
            mapTrigger() {}
};

/**
*   Superblock Manager
*
*   Class for querying superblock information
*/

class CSuperblockManager {
private:
    static bool GetBestSuperblock(CSuperblock_sptr &pSuperblockRet, int nBlockHeight);

public:
    static bool IsSuperblockTriggered(int nBlockHeight);

    static bool GetSuperblockPayments(int nBlockHeight, std::vector <CTxOut> &voutSuperblockRet);

    static void ExecuteBestSuperblock(int nBlockHeight);

    static bool IsValid(const CTransaction &txNew, int nBlockHeight, CAmount blockReward);
};

/**
*   Governance Object Payment
*
*/

class CGovernancePayment {
private:
    bool fValid;

public:
    CScript script;
    CAmount nAmount;

    CGovernancePayment() :
            fValid(false),
            script(),
            nAmount(0) {
    }

    CGovernancePayment(const CTxDestination &destIn, CAmount nAmountIn) :
            fValid(false),
            script(),
            nAmount(0) {
        try {
            script = GetScriptForDestination(destIn);
            nAmount = nAmountIn;
            fValid = true;
        } catch (std::exception &e) {
            LogPrintf("CGovernancePayment Payment not valid: destIn = %s, nAmountIn = %d, what = %s\n",
                      EncodeDestination(destIn), nAmountIn, e.what());
        } catch (...) {
            LogPrintf("CGovernancePayment Payment not valid: destIn = %s, nAmountIn = %d\n",
                      EncodeDestination(destIn), nAmountIn);
        }
    }

    bool IsValid() const { return fValid; }
};


/**
*   Trigger : Superblock
*
*   - Create payments on the network
*
*   object structure:
*   {
*       "governance_object_id" : last_id,
*       "type" : govtypes.trigger,
*       "subtype" : "superblock",
*       "superblock_name" : superblock_name,
*       "start_epoch" : start_epoch,
*       "payment_addresses" : "addr1|addr2|addr3",
*       "payment_amounts"   : "amount1|amount2|amount3"
*   }
*/

class CSuperblock : public CGovernanceObject {
private:
    uint256 nGovObjHash;

    int nBlockHeight;
    int nStatus;
    std::vector <CGovernancePayment> vecPayments;

    void ParsePaymentSchedule(const std::string &strPaymentAddresses, const std::string &strPaymentAmounts);

public:
    CSuperblock();

    explicit CSuperblock(uint256 &nHash);

    static bool IsValidBlockHeight(int nBlockHeight);

    static void GetNearestSuperblocksHeights(int nBlockHeight, int &nLastSuperblockRet, int &nNextSuperblockRet);

    static CAmount GetPaymentsLimit(int nBlockHeight);

    int GetStatus() const { return nStatus; }

    void SetStatus(int nStatusIn) { nStatus = nStatusIn; }

    // TELL THE ENGINE WE EXECUTED THIS EVENT
    void SetExecuted() { nStatus = SEEN_OBJECT_EXECUTED; }

    CGovernanceObject *GetGovernanceObject() {
        AssertLockHeld(governance.cs);
        CGovernanceObject *pObj = governance.FindGovernanceObject(nGovObjHash);
        return pObj;
    }

    int GetBlockHeight() const {
        return nBlockHeight;
    }

    int CountPayments() { return (int) vecPayments.size(); }

    bool GetPayment(int nPaymentIndex, CGovernancePayment &paymentRet);

    CAmount GetPaymentsTotalAmount();

    bool IsValid(const CTransaction &txNew, int nBlockHeight, CAmount blockReward);

    bool IsExpired() const;
};

#endif // BITCOIN_GOVERNANCE_GOVERNANCE_CLASSES_H
