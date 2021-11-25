// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2019 The Dash Core developers
// Copyright (c) 2020-2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionrecord.h"

#include "base58.h"
#include "consensus/consensus.h"
#include "validation.h"
#include "timedata.h"
#include "wallet/wallet.h"

#include "privatesend/privatesend.h"
#include "evo/providertx.h"
#include "evo/specialtx.h"

#include <stdint.h>


/* Return positive answer if transaction should be shown in list.
 */
bool TransactionRecord::showTransaction(const CWalletTx &wtx)
{
    // There are currently no cases where we hide transactions, but
    // we may want to use this in the future for things like RBF.
    return true;
}

/* Return block height for transaction */
int TransactionRecord::getTransactionBlockHeight(const CWalletTx &wtx)
{
    // Find the block the tx is in
    CBlockIndex* pindex = nullptr;
    BlockMap::iterator mi = mapBlockIndex.find(wtx.hashBlock);
    if (mi != mapBlockIndex.end())
        pindex = (*mi).second;

    int txBlock = pindex ? pindex->nHeight : chainActive.Height();

    return txBlock;
}

/* Return Maturity Block of Future TX */
int TransactionRecord::getFutureTxMaturityBlock(const CWalletTx &wtx, CFutureTx &ftx)
{
    int maturityBlock = (getTransactionBlockHeight(wtx) + ftx.maturity); //tx block height + maturity
    return maturityBlock;
}

/* Return Maturity Time of Future TX */
int TransactionRecord::getFutureTxMaturityTime(const CWalletTx &wtx, CFutureTx &ftx)
{
    int64_t maturityTime = (wtx.nTimeReceived + ftx.lockTime); //tx time + locked seconds
    return maturityTime;
}

/* Return positive answer if future transaction has matured.
 */
bool TransactionRecord::isFutureTxMatured(const CWalletTx &wtx, CFutureTx &ftx)
{
    if (chainActive.Height() >= getFutureTxMaturityBlock(wtx, ftx) || GetAdjustedTime() >= getFutureTxMaturityTime(wtx, ftx)) 
    {
        return true;
    }
    else
    {
        return false;
    }
}

void TransactionRecord::getFutureTxStatus(const CWalletTx &wtx, CFutureTx &ftx)
{

    if (wtx.IsInMainChain() && GetTxPayload(wtx.tx->vExtraPayload, ftx))
    {

        int maturityBlock = getFutureTxMaturityBlock(wtx, ftx);
        int64_t maturityTime = getFutureTxMaturityTime(wtx, ftx);

        //transaction depth in chain against maturity OR relative seconds of transaction against lockTime
        if (isFutureTxMatured(wtx, ftx)) {
            status.status = TransactionStatus::Confirmed;
        } else {
            status.countsForBalance = false;
           //display transaction is mature in x blocks or transaction is mature in days hh:mm:ss
            if(maturityBlock >= chainActive.Height())
            {
                status.status = TransactionStatus::OpenUntilBlock;
                status.open_for = maturityBlock; 
            }
            if(maturityTime >= GetAdjustedTime())
            {
                status.status = TransactionStatus::OpenUntilDate;
                status.open_for = maturityTime;
            }

        }

    }
    else
    {
        //not in main chain - new transaction
        status.status = TransactionStatus::NotAccepted;
    }
    
}

/*
 * Decompose CWallet transaction to model transaction records.
 */
QList<TransactionRecord> TransactionRecord::decomposeTransaction(const CWallet *wallet, const CWalletTx &wtx)
{
    QList<TransactionRecord> parts;
    int64_t nTime = wtx.GetTxTime();
    CAmount nCredit = wtx.GetCredit(ISMINE_ALL);
    CAmount nDebit = wtx.GetDebit(ISMINE_ALL);
    CAmount nNet = nCredit - nDebit;
    uint256 hash = wtx.GetHash();
    std::map<std::string, std::string> mapValue = wtx.mapValue;

    if (nNet > 0 || wtx.IsCoinBase())
    {
        //
        // Credit
        //
        for(unsigned int i = 0; i < wtx.tx->vout.size(); i++)
        {
            const CTxOut& txout = wtx.tx->vout[i];
            isminetype mine = wallet->IsMine(txout);
            if(mine)
            {
                TransactionRecord sub(hash, nTime);
                CTxDestination address;
                sub.idx = i; // vout index
                sub.credit = txout.nValue;
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                if (ExtractDestination(txout.scriptPubKey, address) && IsMine(*wallet, address))
                {
                    // Received by Raptoreum Address
                    sub.type = TransactionRecord::RecvWithAddress;
                    sub.strAddress = CBitcoinAddress(address).ToString();
                }
                else
                {
                    // Received by IP connection (deprecated features), or a multisignature or other non-simple transaction
                    sub.type = TransactionRecord::RecvFromOther;
                    sub.strAddress = mapValue["from"];
                }
                if (wtx.IsCoinBase())
                {
                    // Generated
                    sub.type = TransactionRecord::Generated;
                }

                if(wtx.tx->nType == TRANSACTION_FUTURE)
                {
                    // Future TX Received
                    
                    if (ExtractDestination(wtx.tx->vout[1].scriptPubKey, address))
                    {
                        // Received by Raptoreum Address
                        sub.type = TransactionRecord::FutureReceive;
                        sub.strAddress = CBitcoinAddress(address).ToString();
                    }
                } 

                sub.address.SetString(sub.strAddress);
                sub.txDest = sub.address.Get();
                parts.append(sub);
            }
        }
    }

    else
    {
        bool fAllFromMeDenom = true;
        int nFromMe = 0;
        bool involvesWatchAddress = false;
        isminetype fAllFromMe = ISMINE_SPENDABLE;
        for (const CTxIn& txin : wtx.tx->vin)
        {
            if(wallet->IsMine(txin)) {
                fAllFromMeDenom = fAllFromMeDenom && wallet->IsDenominated(txin.prevout);
                nFromMe++;
            }
            isminetype mine = wallet->IsMine(txin);
            if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if(fAllFromMe > mine) fAllFromMe = mine;
        }

        isminetype fAllToMe = ISMINE_SPENDABLE;
        bool fAllToMeDenom = true;
        int nToMe = 0;
        for (const CTxOut& txout : wtx.tx->vout) {
            if(wallet->IsMine(txout)) {
                fAllToMeDenom = fAllToMeDenom && CPrivateSend::IsDenominatedAmount(txout.nValue);
                nToMe++;
            }
            isminetype mine = wallet->IsMine(txout);
            if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if(fAllToMe > mine) fAllToMe = mine;
        }

        if(fAllFromMeDenom && fAllToMeDenom && nFromMe * nToMe) {
            parts.append(TransactionRecord(hash, nTime, TransactionRecord::PrivateSendDenominate, "", -nDebit, nCredit));
            parts.last().involvesWatchAddress = false;   // maybe pass to TransactionRecord as constructor argument
        }
        else if (fAllFromMe && fAllToMe)
        {
            // Payment to self
            // TODO: this section still not accurate but covers most cases,
            // might need some additional work however

            TransactionRecord sub(hash, nTime);
            // Payment to self by default
            sub.type = TransactionRecord::SendToSelf;
            sub.strAddress = "";
            CTxDestination address;

            if(mapValue["DS"] == "1")
            {
                sub.type = TransactionRecord::PrivateSend;
                
                if (ExtractDestination(wtx.tx->vout[0].scriptPubKey, address))
                {
                    // Sent to Raptoreum Address
                    sub.strAddress = CBitcoinAddress(address).ToString();
                }
                else
                {
                    // Sent to IP, or other non-address transaction like OP_EVAL
                    sub.strAddress = mapValue["to"];
                }
            } 
            else
            {
                sub.idx = parts.size();
                if(wtx.tx->vin.size() == 1 && wtx.tx->vout.size() == 1
                    && CPrivateSend::IsCollateralAmount(nDebit)
                    && CPrivateSend::IsCollateralAmount(nCredit)
                    && CPrivateSend::IsCollateralAmount(-nNet))
                {
                    sub.type = TransactionRecord::PrivateSendCollateralPayment;
                } 
                else 
                {
                    for (const auto& txout : wtx.tx->vout) {
                        if (txout.nValue == CPrivateSend::GetMaxCollateralAmount()) {
                            sub.type = TransactionRecord::PrivateSendMakeCollaterals;
                            continue; // Keep looking, could be a part of PrivateSendCreateDenominations
                        } else if (CPrivateSend::IsDenominatedAmount(txout.nValue)) {
                            sub.type = TransactionRecord::PrivateSendCreateDenominations;
                            break; // Done, it's definitely a tx creating mixing denoms, no need to look any further
                        }
                    }
                }
            }

            if(wtx.tx->nType == TRANSACTION_FUTURE)
            {
                sub.type = TransactionRecord::FutureSend;
                if (ExtractDestination(wtx.tx->vout[0].scriptPubKey, address))
                {
                    // Sent to Raptoreum Address
                    sub.strAddress = CBitcoinAddress(address).ToString();
                }
            }

            CAmount nChange = wtx.GetChange();

            sub.debit = -(nDebit - nChange);
            sub.credit = nCredit - nChange;
            sub.address.SetString(sub.strAddress);
            sub.txDest = sub.address.Get();
            parts.append(sub);
            parts.last().involvesWatchAddress = involvesWatchAddress;   // maybe pass to TransactionRecord as constructor argument
        }
        else if (fAllFromMe)
        {
            //
            // Debit
            //
            CAmount nTxFee = nDebit - wtx.tx->GetValueOut();

            bool fDone = false;
            if(wtx.tx->vin.size() == 1 && wtx.tx->vout.size() == 1
                && CPrivateSend::IsCollateralAmount(nDebit)
                && nCredit == 0 // OP_RETURN
                && CPrivateSend::IsCollateralAmount(-nNet))
            {
                TransactionRecord sub(hash, nTime);
                sub.idx = 0;
                sub.type = TransactionRecord::PrivateSendCollateralPayment;
                sub.debit = -nDebit;
                parts.append(sub);
                fDone = true;
            }

            for (unsigned int nOut = 0; nOut < wtx.tx->vout.size() && !fDone; nOut++)
            {
                const CTxOut& txout = wtx.tx->vout[nOut];
                TransactionRecord sub(hash, nTime);
                sub.idx = nOut;
                sub.involvesWatchAddress = involvesWatchAddress;

                if(wallet->IsMine(txout))
                {
                    // Ignore parts sent to self, as this is usually the change
                    // from a transaction sent back to our own address.
                    continue;
                }

                CTxDestination address;
                if (ExtractDestination(txout.scriptPubKey, address))
                {
                    // Sent to Raptoreum Address
                    sub.type = TransactionRecord::SendToAddress;
                    sub.strAddress = CBitcoinAddress(address).ToString();
                }
                else
                {
                    // Sent to IP, or other non-address transaction like OP_EVAL
                    sub.type = TransactionRecord::SendToOther;
                    sub.strAddress = mapValue["to"];
                }

                if(mapValue["DS"] == "1")
                {
                    sub.type = TransactionRecord::PrivateSend;
                }

                if(wtx.tx->nType == TRANSACTION_FUTURE)
                {
                    sub.type = TransactionRecord::FutureSend;
                }

                CAmount nValue = txout.nValue;
                /* Add fee to first output */
                if (nTxFee > 0)
                {
                    nValue += nTxFee;
                    nTxFee = 0;
                }
                sub.debit = -nValue;

                sub.address.SetString(sub.strAddress);
                sub.txDest = sub.address.Get();

                parts.append(sub);
            }
        }
        else
        {
            //
            // Mixed debit transaction, can't break down payees
            //
            parts.append(TransactionRecord(hash, nTime, TransactionRecord::Other, "", nNet, 0));
            parts.last().involvesWatchAddress = involvesWatchAddress;
        }
    }

    return parts;
}

void TransactionRecord::updateStatus(const CWalletTx &wtx, int chainLockHeight)
{
    AssertLockHeld(cs_main);
    AssertLockHeld(wtx.GetWallet()->cs_wallet);
    // Determine transaction status

    // Find the block the tx is in
    CBlockIndex* pindex = nullptr;
    BlockMap::iterator mi = mapBlockIndex.find(wtx.hashBlock);
    if (mi != mapBlockIndex.end())
        pindex = (*mi).second;

    // Sort order, unrecorded transactions sort to the top
    status.sortKey = strprintf("%010d-%01d-%010u-%03d",
        (pindex ? pindex->nHeight : std::numeric_limits<int>::max()),
        (wtx.IsCoinBase() ? 1 : 0),
        wtx.nTimeReceived,
        idx);
    status.countsForBalance = wtx.IsTrusted() && !(wtx.GetBlocksToMaturity() > 0);
    status.depth = wtx.GetDepthInMainChain();
    status.cur_num_blocks = chainActive.Height();
    status.cachedChainLockHeight = chainLockHeight;

    bool oldLockedByChainLocks = status.lockedByChainLocks;
    if (!status.lockedByChainLocks) {
        status.lockedByChainLocks = wtx.IsChainLocked();
    }

    auto addrBookIt = wtx.GetWallet()->mapAddressBook.find(this->txDest);
    if (addrBookIt == wtx.GetWallet()->mapAddressBook.end()) {
        status.label = "";
    } else {
        status.label = QString::fromStdString(addrBookIt->second.name);
    }

    if (!CheckFinalTx(wtx))
    {
        if (wtx.tx->nLockTime < LOCKTIME_THRESHOLD)
        {
            status.status = TransactionStatus::OpenUntilBlock;
            status.open_for = wtx.tx->nLockTime - chainActive.Height();
        }
        else
        {
            status.status = TransactionStatus::OpenUntilDate;
            status.open_for = wtx.tx->nLockTime;
        }
    }
    // For generated transactions, determine maturity
    else if(type == TransactionRecord::Generated)
    {
        if (wtx.GetBlocksToMaturity() > 0)
        {
            status.status = TransactionStatus::Immature;

            if (wtx.IsInMainChain())
            {
                status.matures_in = wtx.GetBlocksToMaturity();

                // Check if the block was requested by anyone
                if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
                    status.status = TransactionStatus::MaturesWarning;
            }
            else
            {
                status.status = TransactionStatus::NotAccepted;
            }
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
    //For Future transactions, determine maturity
    else if(type == TransactionRecord::FutureReceive)
    {
        CFutureTx ftx;
        getFutureTxStatus(wtx, ftx);
        
    }
    else
    {
        // The IsLockedByInstantSend call is quite expensive, so we only do it when a state change is actually possible.
        if (status.lockedByChainLocks) {
            if (oldLockedByChainLocks != status.lockedByChainLocks) {
                status.lockedByInstantSend = wtx.IsLockedByInstantSend();
            } else {
                status.lockedByInstantSend = false;
            }
        } else if (!status.lockedByInstantSend) {
            status.lockedByInstantSend = wtx.IsLockedByInstantSend();
        }

        if (status.depth < 0)
        {
            status.status = TransactionStatus::Conflicted;
        }
        else if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
        {
            status.status = TransactionStatus::Offline;
        }
        else if (status.depth == 0)
        {
            status.status = TransactionStatus::Unconfirmed;
            if (wtx.isAbandoned())
                status.status = TransactionStatus::Abandoned;
        }
        else if (status.depth < RecommendedNumConfirmations && !status.lockedByChainLocks)
        {
            status.status = TransactionStatus::Confirming;
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
    status.needsUpdate = false;
}

bool TransactionRecord::statusUpdateNeeded(int chainLockHeight)
{
    AssertLockHeld(cs_main);
    return status.cur_num_blocks != chainActive.Height() || status.needsUpdate
        || (!status.lockedByChainLocks && status.cachedChainLockHeight != chainLockHeight);
}

QString TransactionRecord::getTxID() const
{
    return QString::fromStdString(hash.ToString());
}

int TransactionRecord::getOutputIndex() const
{
    return idx;
}
