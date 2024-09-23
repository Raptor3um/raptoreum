// Copyright (c) 2017-2023 The Bitcoin Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <version.h>
#include <chainparams.h>

#include <consensus/consensus.h>
#include <consensus/tx_check.h>

#include <primitives/transaction.h>
#include <consensus/validation.h>
#include <assets/assetstype.h>

CChain &ChainActive();

bool CheckTransaction(const CTransaction &tx, CValidationState &state, int nHeight, CAmount blockReward) {
    bool allowEmptyTxInOut = false;
    if (tx.nType == TRANSACTION_QUORUM_COMMITMENT) {
        allowEmptyTxInOut = true;
    }

    // Basic checks that don't depend on any context
    if (!allowEmptyTxInOut && tx.vin.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vin-empty");
    if (!allowEmptyTxInOut && tx.vout.empty())
        return state.DoS(10, false, REJECT_INVALID, "bad-txns-vout-empty");
    // Size limits
    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) > MAX_LEGACY_BLOCK_SIZE)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-oversize");
    if (tx.vExtraPayload.size() > MAX_TX_EXTRA_PAYLOAD)
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-payload-oversize");

    // Check for negative or overflow output values
    CAmount nValueOut = 0;
    std::map <std::string, CAmount> nAssetVout;
    for (const auto &txout: tx.vout) {
        if (txout.nValue < 0)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-negative");

        if (txout.nValue > MAX_MONEY)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-toolarge");
        
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge");

        if (txout.scriptPubKey.IsAssetScript()) {
            CAssetTransfer assetTransfer;
            if (!GetTransferAsset(txout.scriptPubKey, assetTransfer))
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-assets-output");

            if (nAssetVout.count(assetTransfer.assetId))
                nAssetVout[assetTransfer.assetId] += assetTransfer.nAmount;
            else
                nAssetVout.insert(std::make_pair(assetTransfer.assetId, assetTransfer.nAmount));

            if (assetTransfer.nAmount < 0)
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-negative");

            if (assetTransfer.nAmount > MAX_MONEY)
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-vout-toolarge");

            if (!MoneyRange(nAssetVout.at(assetTransfer.assetId))) {
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-outputvalues-outofrange");
            }
        }
    }

    // Check for duplicate inputs
    std::set <COutPoint> vInOutPoints;
    for (const auto &txin: tx.vin) {
        if (!vInOutPoints.insert(txin.prevout).second)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-duplicate");
    }

    if (tx.IsCoinBase()) {
        size_t minCbSize = 2;
        if (tx.nType == TRANSACTION_COINBASE) {
            // With the introduction of CbTx, coinbase scripts are not required anymore to hold a valid block height
            minCbSize = 1;
        }
        if (tx.vin[0].scriptSig.size() < minCbSize || tx.vin[0].scriptSig.size() > 100)
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-length");
        FounderPayment founderPayment = Params().GetConsensus().nFounderPayment;
        CAmount founderReward = founderPayment.getFounderPaymentAmount(nHeight, blockReward);
        int founderStartHeight = founderPayment.getStartBlock();
        if (nHeight > founderStartHeight && founderReward &&
            !founderPayment.IsBlockPayeeValid(tx, nHeight, blockReward)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-founder-payment-not-found");
        }
        //don't allow assets on coinbase. coinbase assets are not implemented
        for (auto vout : tx.vout) {
            if (vout.scriptPubKey.IsAssetScript()) {
                return state.DoS(100, false, REJECT_INVALID, "bad-cb-contains-asset");
            }
        }
    } else {
        for (const auto &txin: tx.vin)
            if (txin.prevout.IsNull())
                return state.DoS(10, false, REJECT_INVALID, "bad-txns-prevout-null");
    }

    return true;
}
