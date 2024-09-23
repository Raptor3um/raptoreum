/*
 * Copyright (c) 2018 The Pigeon Core developers
 * Distributed under the MIT software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 * 
 * FounderPayment.cpp
 *
 *  Created on: Jun 24, 2018
 *      Author: Tri Nguyen
 */

#include <founder_payment.h>
#include <rpc/server.h>

#include <util/system.h>
#include <chainparams.h>
#include <boost/foreach.hpp>
#include <key_io.h>

CAmount FounderPayment::getFounderPaymentAmount(int blockHeight, CAmount blockReward) {
    if (blockHeight <= startBlock) {
        return 0;
    }
    for (int i = 0; i < rewardStructures.size(); i++) {
        FounderRewardStructure rewardStructure = rewardStructures[i];
        if (rewardStructure.blockHeight == INT_MAX || blockHeight <= rewardStructure.blockHeight) {
            return blockReward * rewardStructure.rewardPercentage / 100;
        }
    }
    return 0;
}

void FounderPayment::FillFounderPayment(CMutableTransaction &txNew, int nBlockHeight, CAmount blockReward,
                                        CTxOut &txoutFounderRet) {
    // make sure it's not filled yet
    CAmount founderPayment = getFounderPaymentAmount(nBlockHeight, blockReward);
//	if(founderPayment == 0) {
//	    LogPrintf("FounderPayment::FillFounderPayment -- Founder payment has not started\n");
//	    return;
//
//	}
    txoutFounderRet = CTxOut();
    // fill payee with the foundFounderRewardStrcutureFounderRewardStrcutureer address
    CTxDestination founderAddr = DecodeDestination(founderAddress);
    if (!IsValidDestination(founderAddr))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           strprintf("Invalid Raptoreum Founder Address: %s", founderAddress.c_str()));
    CScript payee = GetScriptForDestination(founderAddr);
    // GET FOUNDER PAYMENT VARIABLES SETUP

    // split reward between miner ...
    txNew.vout[0].nValue -= founderPayment;
    txoutFounderRet = CTxOut(founderPayment, payee);
    txNew.vout.push_back(txoutFounderRet);
    LogPrintf("FounderPayment::FillFounderPayment -- Founder payment %lld to %s\n", founderPayment,
              founderAddress.c_str());
}

bool FounderPayment::IsBlockPayeeValid(const CTransaction &txNew, const int height, const CAmount blockReward) {
    // fill payee with the founder address
    CScript payee = GetScriptForDestination(DecodeDestination(founderAddress));
    const CAmount founderReward = getFounderPaymentAmount(height, blockReward);
    BOOST_FOREACH(
    const CTxOut &out, txNew.vout) {
        if (out.scriptPubKey == payee && out.nValue >= founderReward) {
            return true;
        }
    }

    return false;
}



