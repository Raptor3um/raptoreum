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

#include "utilmoneystr.h"

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
    isminefilter creditMineTypes = 0;
    isminefilter debitMineTypes = 0;

    int64_t nTime = wtx.GetTxTime();
    CAmount nCredit = wtx.GetCredit(ISMINE_ALL, &creditMineTypes);
    CAmount nDebit = wtx.GetDebit(ISMINE_ALL, &debitMineTypes);
    CAmount nNet = nCredit - nDebit;
    CAmount nTxFee = nDebit - wtx.tx->GetValueOut();
    uint256 hash = wtx.GetHash();
    bool isFuture = wtx.tx->nType == TRANSACTION_FUTURE;

    std::map<std::string, std::string> mapValue = wtx.mapValue;

    // Combinations of transactions:
    // NB: This treats all watched addresses like a single, other wallet

    // A/B: Generated:
    // A: coinbase -> self    Generated
    // B: coinbase -> watched Generated (watched)

    // C/D: SendToSelf, PrivateSend*
    // C: self    -> self     Send to self
    // D: watched -> watched  Send to self (watched)

    // E/F: SendToAddress, SendToOther, watched: ReceiveWithAddress, RecvFromOther + FutureSend, FutureReceive
    // These create one or two records (one normal, possibly one watched):
    // E: self     -> other   Send from wallet to external
    // F: self     -> watched Send from wallet to watched external (and watched receive)

    // G/H: SendToAddress, SendToOther, watched: ReceiveWithAddress, RecvFromOther + FutureSend, FutureReceive
    // These create one or two records (one normal, possibly one watched):
    // G: other    -> self    Send from watched to wallet no change
    // H: watched  -> self    Send from watched to wallet with change

    // LogPrintf("TransactionRecord::%s TxId: %s debitMineTypes: %02X, creditMineTypes: %02X, debit: %s, credit: %s, ValueOut: %s, TxFee: %s, nType: %d\n",
    //         __func__, hash.ToString(), debitMineTypes, creditMineTypes, FormatMoney(nDebit), FormatMoney(nCredit), FormatMoney(wtx.tx->GetValueOut()), FormatMoney(nTxFee), wtx.tx->nType);

    for (unsigned int vOutIdx = 0; vOutIdx < wtx.tx->vout.size(); ++vOutIdx)
    {
        const CTxOut& txout = wtx.tx->vout[vOutIdx];

        isminetype mine = wallet->IsMine(txout);

        TransactionRecord sub(hash, nTime);
        CTxDestination address;
        bool validDestination = ExtractDestination(txout.scriptPubKey, address);
        sub.idx = vOutIdx;
        sub.credit = txout.nValue;
        sub.strAddress = validDestination ? CBitcoinAddress(address).ToString() : mapValue["from"];
        sub.address.SetString(sub.strAddress);
        sub.txDest = sub.address.Get();

        // Check if all inputs are from wallet and if output are to wallet
        bool fAllFromMeDenom = true;
        int nFromMe = 0;
        bool inputInvolvesWatchAddress = false;
        bool outputInvolvesWatchAddress = false;
        isminetype fAllFromMe = ISMINE_SPENDABLE;
        for (const CTxIn& txin : wtx.tx->vin)
        {
            if(wallet->IsMine(txin)) {
                fAllFromMeDenom = fAllFromMeDenom && wallet->IsDenominated(txin.prevout);
                nFromMe++;
            }
            isminetype mine = wallet->IsMine(txin);
            if(mine & ISMINE_WATCH_ONLY) inputInvolvesWatchAddress = true;
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
            if(mine & ISMINE_WATCH_ONLY) outputInvolvesWatchAddress = true;
            if(fAllToMe > mine) fAllToMe = mine;
        }

        // LogPrintf("TransactionRecord::%s TxId: %s, vOutIdx: %d, CoinBase: %d, AllFromMe: %d, AllToMe: %d, inputWatch: %d, outputWatch: %d\n",
        //         __func__, hash.ToString(), vOutIdx, wtx.IsCoinBase(), fAllFromMe, fAllToMe, inputInvolvesWatchAddress, outputInvolvesWatchAddress);

        // A/B Generated:
        // A: coinbase -> self    Generated
        // B: coinbase -> watched Generated (watched)
        if (wtx.IsCoinBase() && mine)
        {
            sub.type = TransactionRecord::Generated;
            sub.involvesWatchAddress = creditMineTypes & ISMINE_WATCH_ONLY;
            parts.append(sub);
            continue;
        }

        // C/D: SendToSelf, PrivateSend*
        // C: self    -> self     Send to self
        // D: watched -> watched  Send to self (watched)
        if (fAllFromMe && fAllToMe && (debitMineTypes == creditMineTypes))
        {
            CAmount nChange = wtx.GetChange();
            sub.debit = -(nDebit - nChange);
            sub.credit = nCredit - nChange;
            sub.involvesWatchAddress = inputInvolvesWatchAddress;

            if (fAllFromMeDenom && fAllToMeDenom && nFromMe * nToMe)
            {
                sub.type = TransactionRecord::PrivateSendDenominate;
                sub.debit = -nDebit;
                sub.credit = nCredit;
                parts.append(sub);
                break; // Only report first of the batch
            }

            if (mapValue["DS"] == "1")
            {
                sub.type = TransactionRecord::PrivateSend;
                CAmount nChange = wtx.GetChange();
                parts.append(sub);
                break; // Only report first of the batch
            } 


            sub.type = TransactionRecord::SendToSelf;

            sub.idx = parts.size();
            if (wtx.tx->vin.size() == 1 && wtx.tx->vout.size() == 1
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
            parts.append(sub);
            break; // Only report vout[0], consider the other outputs change
        }

        // E/F: SendToAddress, SendToOther, watched: ReceiveWithAddress, RecvFromOther + FutureSend, FutureReceive
        // These create one or two records (one normal, possibly one watched):
        // E: self     -> other   Send from wallet to external
        // F: self     -> watched Send from wallet to watched external (and watched receive)
        if (debitMineTypes & ISMINE_SPENDABLE && !(mine & ISMINE_SPENDABLE))
        {
            // Generate one or two records - sent from wallet, received by watched:
            sub.type = isFuture ? TransactionRecord::FutureSend : (validDestination ? TransactionRecord::SendToAddress : TransactionRecord::SendToOther);

            // Sent from wallet:
            sub.involvesWatchAddress = false;
            sub.debit = -(txout.nValue + nTxFee);
            nTxFee = 0; // Add fee to first output
            sub.credit = 0;
            parts.append(sub);

            // If received by Watch, add a receive transaction on the watched side:
            if (mine & ISMINE_WATCH_ONLY)
            {
                sub.involvesWatchAddress = true;
                sub.type = isFuture ? TransactionRecord::FutureReceive : (validDestination ? TransactionRecord::RecvWithAddress : TransactionRecord::RecvFromOther);
                sub.debit = 0;
                sub.credit = txout.nValue;
                parts.append(sub);
            }
            continue;
        }

        // G/H: SendToAddress, SendToOther, watched: ReceiveWithAddress, RecvFromOther + FutureSend, FutureReceive
        // These create one or two records (one normal, possibly one watched):
        // G: other    -> self    Send from watched to wallet no change
        // H: watched  -> self    Send from watched to wallet with change
        if (!(debitMineTypes & ISMINE_SPENDABLE) && mine & ISMINE_SPENDABLE)
        {
            // Generate one or two records - receive with wallet, sent by watched:
            sub.involvesWatchAddress = false;
            sub.type = isFuture ? TransactionRecord::FutureReceive : (validDestination ? TransactionRecord::RecvWithAddress : TransactionRecord::RecvFromOther);

            // Received with wallet:
            sub.credit = txout.nValue;
            parts.append(sub);

            // If sent by Watch, add a sent transaction on the watched side:
            if (debitMineTypes & ISMINE_WATCH_ONLY)
            {
                sub.involvesWatchAddress = true;
                sub.type = isFuture ? TransactionRecord::FutureSend : (validDestination ? TransactionRecord::SendToAddress : TransactionRecord::SendToOther);
                sub.debit = -(txout.nValue + nTxFee);
                nTxFee = 0; // Add fee to first output
                sub.credit = 0;
                parts.append(sub);
            }
            continue;
        }
        LogPrintf("TransactionRecord::%s TxId: %s, vOutIdx: %d, Unhandled\n", __func__, hash.ToString(), vOutIdx);
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
