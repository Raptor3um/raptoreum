// Copyright (c) 2014-2019 The Dash Core developers
// Copyright (c) 2020 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "smartnode/activesmartnode.h"
#include "consensus/validation.h"
#include "governance/governance-classes.h"
#include "init.h"
#include "smartnode/smartnode-payments.h"
#include "smartnode/smartnode-sync.h"
#include "messagesigner.h"
#include "netfulfilledman.h"
#include "netmessagemaker.h"
#include "spork.h"
#include "util.h"
#include "validation.h"

#include "evo/deterministicmns.h"

#include <string>

CSmartnodePayments mnpayments;

bool IsOldBudgetBlockValueValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string& strErrorRet) {
    const Consensus::Params& consensusParams = Params().GetConsensus();
    bool isBlockRewardValueMet = (block.vtx[0]->GetValueOut() <= blockReward);

    if (nBlockHeight < consensusParams.nBudgetPaymentsStartBlock) {
        strErrorRet = strprintf("Incorrect block %d, old budgets are not activated yet", nBlockHeight);
        return false;
    }

    if (nBlockHeight >= consensusParams.nSuperblockStartBlock) {
        strErrorRet = strprintf("Incorrect block %d, old budgets are no longer active", nBlockHeight);
        return false;
    }

    // we are still using budgets, but we have no data about them anymore,
    // all we know is predefined budget cycle and window

    int nOffset = nBlockHeight % consensusParams.nBudgetPaymentsCycleBlocks;
    if(nBlockHeight >= consensusParams.nBudgetPaymentsStartBlock &&
       nOffset < consensusParams.nBudgetPaymentsWindowBlocks) {
        // NOTE: old budget system is disabled since 12.1
        if(smartnodeSync.IsSynced()) {
            // no old budget blocks should be accepted here on mainnet,
            // testnet/devnet/regtest should produce regular blocks only
            LogPrint(BCLog::GOBJECT, "%s -- WARNING: Client synced but old budget system is disabled, checking block value against block reward\n", __func__);
            if(!isBlockRewardValueMet) {
                strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, old budgets are disabled",
                                        nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
            }
            return isBlockRewardValueMet;
        }
        // when not synced, rely on online nodes (all networks)
        LogPrint(BCLog::GOBJECT, "%s -- WARNING: Skipping old budget block value checks, accepting block\n", __func__);
        return true;
    }
    if(!isBlockRewardValueMet) {
        strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, block is not in old budget cycle window",
                                nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
    }
    return isBlockRewardValueMet;
}

/**
* IsBlockValueValid
*
*   Determine if coinbase outgoing created money is the correct value
*
*   Why is this needed?
*   - In Raptoreum some blocks are superblocks, which output much higher amounts of coins
*   - Otherblocks are 10% lower in outgoing value, so in total, no extra coins are created
*   - When non-superblocks are detected, the normal schedule should be maintained
*/

bool IsBlockValueValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string& strErrorRet)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    bool isBlockRewardValueMet = (block.vtx[0]->GetValueOut() <= blockReward);

    strErrorRet = "";

    if (nBlockHeight < consensusParams.nBudgetPaymentsStartBlock) {
        // old budget system is not activated yet, just make sure we do not exceed the regular block reward
        if(!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, old budgets are not activated yet",
                                    nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
        }
        return isBlockRewardValueMet;
    } else if (nBlockHeight < consensusParams.nSuperblockStartBlock) {
        // superblocks are not enabled yet, check if we can pass old budget rules
        return IsOldBudgetBlockValueValid(block, nBlockHeight, blockReward, strErrorRet);
    }

    LogPrint(BCLog::MNPAYMENTS, "block.vtx[0]->GetValueOut() %lld <= blockReward %lld\n", block.vtx[0]->GetValueOut(), blockReward);

    CAmount nSuperblockMaxValue =  blockReward + CSuperblock::GetPaymentsLimit(nBlockHeight);
    bool isSuperblockMaxValueMet = (block.vtx[0]->GetValueOut() <= nSuperblockMaxValue);

    LogPrint(BCLog::GOBJECT, "block.vtx[0]->GetValueOut() %lld <= nSuperblockMaxValue %lld\n", block.vtx[0]->GetValueOut(), nSuperblockMaxValue);

    if (!CSuperblock::IsValidBlockHeight(nBlockHeight)) {
        // can't possibly be a superblock, so lets just check for block reward limits
        if (!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, only regular blocks are allowed at this height",
                                    nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
        }
        return isBlockRewardValueMet;
    }

    // bail out in case superblock limits were exceeded
    if (!isSuperblockMaxValueMet) {
        strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded superblock max value",
                                nBlockHeight, block.vtx[0]->GetValueOut(), nSuperblockMaxValue);
        return false;
    }

    if(!smartnodeSync.IsSynced() || fLiteMode) {
        LogPrint(BCLog::MNPAYMENTS, "%s -- WARNING: Not enough data, checked superblock max bounds only\n", __func__);
        // not enough data for full checks but at least we know that the superblock limits were honored.
        // We rely on the network to have followed the correct chain in this case
        return true;
    }

    // we are synced and possibly on a superblock now

    if (!sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED)) {
        // should NOT allow superblocks at all, when superblocks are disabled
        // revert to block reward limits in this case
        LogPrint(BCLog::GOBJECT, "%s -- Superblocks are disabled, no superblocks allowed\n", __func__);
        if(!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, superblocks are disabled",
                                    nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
        }
        return isBlockRewardValueMet;
    }

    if (!CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
        // we are on a valid superblock height but a superblock was not triggered
        // revert to block reward limits in this case
        if(!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, no triggered superblock detected",
                                    nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
        }
        return isBlockRewardValueMet;
    }

    // this actually also checks for correct payees and not only amount
    if (!CSuperblockManager::IsValid(*block.vtx[0], nBlockHeight, blockReward)) {
        // triggered but invalid? that's weird
        LogPrintf("%s -- ERROR: Invalid superblock detected at height %d: %s", __func__, nBlockHeight, block.vtx[0]->ToString());
        // should NOT allow invalid superblocks, when superblocks are enabled
        strErrorRet = strprintf("invalid superblock detected at height %d", nBlockHeight);
        return false;
    }

    // we got a valid superblock
    return true;
}

bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward, CAmount specialTxFees)
{
    if(fLiteMode) {
        //there is no budget data to use to check anything, let's just accept the longest chain
        LogPrint(BCLog::MNPAYMENTS, "%s -- WARNING: Not enough data, skipping block payee checks\n", __func__);
        return true;
    }

    // we are still using budgets, but we have no data about them anymore,
    // we can only check smartnode payments

    const Consensus::Params& consensusParams = Params().GetConsensus();

    if(nBlockHeight < consensusParams.smartnodePaymentFixedBlock && nBlockHeight < consensusParams.nSuperblockStartBlock) {
        // NOTE: old budget system is disabled since 12.1 and we should never enter this branch
        // anymore when sync is finished (on mainnet). We have no old budget data but these blocks
        // have tons of confirmations and can be safely accepted without payee verification
        LogPrintf("%s -- WARNING: Client synced but old budget system is disabled, accepting any payee\n", __func__);
        return true;
    }

    // superblocks started
    // SEE IF THIS IS A VALID SUPERBLOCK

    if(sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED)) {
        if(CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
            if(CSuperblockManager::IsValid(txNew, nBlockHeight, blockReward)) {
                LogPrint(BCLog::GOBJECT, "%s -- Valid superblock at height %d: %s", __func__, nBlockHeight, txNew.ToString());
                // continue validation, should also pay MN
            } else {
                LogPrintf("%s -- ERROR: Invalid superblock detected at height %d: %s", __func__, nBlockHeight, txNew.ToString());
                // should NOT allow such superblocks, when superblocks are enabled
                return false;
            }
        } else {
            LogPrint(BCLog::GOBJECT, "%s -- No triggered superblock detected at height %d\n", __func__, nBlockHeight);
        }
    } else {
        // should NOT allow superblocks at all, when superblocks are disabled
        LogPrint(BCLog::GOBJECT, "%s -- Superblocks are disabled, no superblocks allowed\n", __func__);
    }

    // Check for correct smartnode payment
    if(mnpayments.IsTransactionValid(txNew, nBlockHeight, blockReward, specialTxFees)) {
        LogPrint(BCLog::MNPAYMENTS, "%s -- Valid smartnode payment at height %d: %s", __func__, nBlockHeight, txNew.ToString());
        return true;
    }

    LogPrintf("%s -- ERROR: Invalid smartnode payment detected at height %d: %s", __func__, nBlockHeight, txNew.ToString());
    return false;
}

void FillBlockPayments(CMutableTransaction& txNew, int nBlockHeight, CAmount mintReward, std::vector<CTxOut>& voutSmartnodePaymentsRet, std::vector<CTxOut>& voutSuperblockPaymentsRet, CAmount nFees, CAmount specialTxFees)
{
    // only create superblocks if spork is enabled AND if superblock is actually triggered
    // (height should be validated inside)
    if(sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED) &&
        CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
            LogPrint(BCLog::GOBJECT, "%s -- triggered superblock creation at height %d\n", __func__, nBlockHeight);
            CSuperblockManager::GetSuperblockPayments(nBlockHeight, voutSuperblockPaymentsRet);
    }
    CAmount normalBlockReward = mintReward + nFees;
    if (!mnpayments.GetSmartnodeTxOuts(nBlockHeight, normalBlockReward, voutSmartnodePaymentsRet, specialTxFees)) {
        LogPrint(BCLog::MNPAYMENTS, "%s -- no smartnode to pay (MN list probably empty)\n", __func__);
    }

    txNew.vout.insert(txNew.vout.end(), voutSmartnodePaymentsRet.begin(), voutSmartnodePaymentsRet.end());
    txNew.vout.insert(txNew.vout.end(), voutSuperblockPaymentsRet.begin(), voutSuperblockPaymentsRet.end());

    std::string voutSmartnodeStr;
    for (const auto& txout : voutSmartnodePaymentsRet) {
        // subtract MN payment from miner reward
        txNew.vout[0].nValue -= txout.nValue;
        if (!voutSmartnodeStr.empty())
            voutSmartnodeStr += ",";
        voutSmartnodeStr += txout.ToString();
    }

    LogPrint(BCLog::MNPAYMENTS, "%s -- nBlockHeight %d normalBlockReward %lld specialTxFees %lld voutSmartnodePaymentsRet \"%s\" txNew %s", __func__,
                            nBlockHeight, normalBlockReward, specialTxFees, voutSmartnodeStr, txNew.ToString());
}

std::string GetRequiredPaymentsString(int nBlockHeight, const CDeterministicMNCPtr &payee)
{
    std::string strPayee = "Unknown";
    if (payee) {
        CTxDestination dest;
        if (!ExtractDestination(payee->pdmnState->scriptPayout, dest))
            assert(false);
        strPayee = CBitcoinAddress(dest).ToString();
    }
    if (CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
        strPayee += ", " + CSuperblockManager::GetRequiredPaymentsString(nBlockHeight);
    }
    return strPayee;
}

std::map<int, std::string> GetRequiredPaymentsStrings(int nStartHeight, int nEndHeight)
{
    std::map<int, std::string> mapPayments;

    if (nStartHeight < 1) {
        nStartHeight = 1;
    }

    LOCK(cs_main);
    int nChainTipHeight = chainActive.Height();

    bool doProjection = false;
    for(int h = nStartHeight; h < nEndHeight; h++) {
        if (h <= nChainTipHeight) {
            auto payee = deterministicMNManager->GetListForBlock(chainActive[h - 1]).GetMNPayee();
            mapPayments.emplace(h, GetRequiredPaymentsString(h, payee));
        } else {
            doProjection = true;
            break;
        }
    }
    if (doProjection) {
        auto projection = deterministicMNManager->GetListAtChainTip().GetProjectedMNPayees(nEndHeight - nChainTipHeight);
        for (size_t i = 0; i < projection.size(); i++) {
            auto payee = projection[i];
            int h = nChainTipHeight + 1 + i;
            mapPayments.emplace(h, GetRequiredPaymentsString(h, payee));
        }
    }

    return mapPayments;
}

/**
*   GetSmartnodeTxOuts
*
*   Get smartnode payment tx outputs
*/

bool CSmartnodePayments::GetSmartnodeTxOuts(int nBlockHeight, CAmount blockReward, std::vector<CTxOut>& voutSmartnodePaymentsRet, CAmount specialTxFee) const
{
    // make sure it's not filled yet
    voutSmartnodePaymentsRet.clear();

    if(!GetBlockTxOuts(nBlockHeight, blockReward, voutSmartnodePaymentsRet, specialTxFee)) {
        LogPrintf("CSmartnodePayments::%s -- no payee (deterministic smartnode list empty)\n", __func__);
        return false;
    }

    for (const auto& txout : voutSmartnodePaymentsRet) {
        CTxDestination address1;
        ExtractDestination(txout.scriptPubKey, address1);
        CBitcoinAddress address2(address1);

        LogPrintf("CSmartnodePayments::%s -- Smartnode payment %lld to %s\n", __func__, txout.nValue, address2.ToString());
    }

    return true;
}

bool CSmartnodePayments::GetBlockTxOuts(int nBlockHeight, CAmount blockReward, std::vector<CTxOut>& voutSmartnodePaymentsRet, CAmount specialTxFee) const
{
    voutSmartnodePaymentsRet.clear();

    CAmount smartnodeReward = GetSmartnodePayment(nBlockHeight, blockReward, specialTxFee);
    if(smartnodeReward == 0) {
    	return false;
    }

    const CBlockIndex* pindex;
    {
        LOCK(cs_main);
        pindex = chainActive[nBlockHeight - 1];
    }
    uint256 proTxHash;
    auto dmnPayee = deterministicMNManager->GetListForBlock(pindex).GetMNPayee();
    if (!dmnPayee) {
        return false;
    }

    CAmount operatorReward = 0;
    if (dmnPayee->nOperatorReward != 0 && dmnPayee->pdmnState->scriptOperatorPayout != CScript()) {
        // This calculation might eventually turn out to result in 0 even if an operator reward percentage is given.
        // This will however only happen in a few years when the block rewards drops very low.
        operatorReward = (smartnodeReward * dmnPayee->nOperatorReward) / 10000;
        smartnodeReward -= operatorReward;
    }

    if (smartnodeReward > 0) {
        voutSmartnodePaymentsRet.emplace_back(smartnodeReward, dmnPayee->pdmnState->scriptPayout);
    }
    if (operatorReward > 0) {
        voutSmartnodePaymentsRet.emplace_back(operatorReward, dmnPayee->pdmnState->scriptOperatorPayout);
    }

    return true;
}

// Is this smartnode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 blocks of votes
bool CSmartnodePayments::IsScheduled(const CDeterministicMNCPtr& dmnIn, int nNotBlockHeight) const
{
    auto projectedPayees = deterministicMNManager->GetListAtChainTip().GetProjectedMNPayees(8);
    for (const auto &dmn : projectedPayees) {
        if (dmn->proTxHash == dmnIn->proTxHash) {
            return true;
        }
    }
    return false;
}

bool CSmartnodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward, CAmount specialTxFee) const
{
    if (!deterministicMNManager->IsDIP3Enforced(nBlockHeight)) {
        // can't verify historical blocks here
        return true;
    }

    std::vector<CTxOut> voutSmartnodePayments;
    if (!GetBlockTxOuts(nBlockHeight, blockReward, voutSmartnodePayments, specialTxFee)) {
        LogPrintf("CSmartnodePayments::%s -- ERROR failed to get payees for block at height %s\n", __func__, nBlockHeight);
        return true;
    }

    for (const auto& txout : voutSmartnodePayments) {
        bool found = false;
        for (const auto& txout2 : txNew.vout) {
            if (txout == txout2) {
                found = true;
                break;
            }
        }
        if (!found) {
            CTxDestination dest;
            if (!ExtractDestination(txout.scriptPubKey, dest))
                assert(false);
            LogPrintf("CSmartnodePayments::%s -- ERROR failed to find expected payee %s in block at height %s\n", __func__, CBitcoinAddress(dest).ToString(), nBlockHeight);
            return false;
        }
    }
    return true;
}
