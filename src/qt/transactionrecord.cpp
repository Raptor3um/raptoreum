// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/transactionrecord.h>

#include <chain.h>
#include <consensus/consensus.h>
#include <interfaces/wallet.h>
#include <interfaces/node.h>
#include <timedata.h>
#include <validation.h>
#include <wallet/wallet.h>
#include <evo/providertx.h>
#include <evo/specialtx.h>

#include <utilmoneystr.h>

#include <stdint.h>

#include <QDateTime>

/* Return positive answer if transaction should be shown in list.
 */
bool TransactionRecord::showTransaction()
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
bool TransactionRecord::isFutureTxMatured(const CWalletTx &wtx, CFutureTx &ftx) {
    if (chainActive.Height() >= getFutureTxMaturityBlock(wtx, ftx) || GetAdjustedTime() >= getFutureTxMaturityTime(wtx, ftx)) {
        return true;
    } else {
        return false;
    }
}

void TransactionRecord::getFutureTxStatus(const interfaces::WalletTx& wtx, const interfaces::WalletTxStatus& wtxStatus, CFutureTx &ftx) {
    if (GetTxPayload(wtx.tx->vExtraPayload, ftx)) {
        int maturityBlock = wtxStatus.block_height + ftx.maturity;
        int64_t maturityTime = wtxStatus.time_received + ftx.lockTime;
        int currentHeight = chainActive.Height();
        //transaction depth in chain against maturity OR relative seconds of transaction against lockTime
        if (currentHeight >= maturityBlock || GetAdjustedTime() >= maturityTime) {
            status.status = TransactionStatus::Confirmed;
        } else {
            status.countsForBalance = false;
           //display transaction is mature in x blocks or transaction is mature in days hh:mm:ss
            if (maturityBlock >= chainActive.Height()) {
                status.status = TransactionStatus::OpenUntilBlock;
                status.open_for = maturityBlock;
            }
            if (maturityTime >= GetAdjustedTime()) {
                status.status = TransactionStatus::OpenUntilDate;
                status.open_for = maturityTime;
            }

        }

    } else {
        //not in main chain - new transaction
        status.status = TransactionStatus::NotAccepted;
    }

}

/*
 * Decompose CWallet transaction to model transaction records.
 */
QList<TransactionRecord> TransactionRecord::decomposeTransaction(interfaces::Wallet& wallet, const interfaces::WalletTx& wtx) {
    QList<TransactionRecord> parts;
    isminefilter creditMineTypes = 0;
    isminefilter debitMineTypes = 0;

    int64_t nTime = wtx.time; // TODO: Should this be GetConfirmationTime?
    CAmount nCredit = wtx.credit;
    CAmount nDebit = wtx.debit;
    CAmount nNet = nCredit - nDebit;
    CAmount nTxFee = nDebit - wtx.tx->GetValueOut();
    uint256 hash = wtx.tx->GetHash();
    bool isFuture = wtx.tx->nType == TRANSACTION_FUTURE;

    for (isminetype mine : wtx.txout_is_mine)
    {
        creditMineTypes |= mine;
    }

    for (isminetype mine : wtx.txin_is_mine)
    {
        debitMineTypes |= mine;
    }

    std::map<std::string, std::string> mapValue = wtx.value_map;

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

    // I/J: Watched, nothing in wallet: out of watched: FutureSend, SendToOther, into watched: FutureReceived, RecvFromOther
    // This handles transactions betwen watched addresses and unmonitored addresses
    // I: watched  -> other   Send from watched address to other
    // J: other    -> watched Send from other address to watched

    // LogPrintf("TransactionRecord::%s TxId: %s debitMineTypes: %02X, creditMineTypes: %02X, debit: %s, credit: %s, ValueOut: %s, TxFee: %s, nType: %d\n",
    //         __func__, hash.ToString(), debitMineTypes, creditMineTypes, FormatMoney(nDebit), FormatMoney(nCredit), FormatMoney(wtx.tx->GetValueOut()), FormatMoney(nTxFee), wtx.tx->nType);

    for (unsigned int vOutIdx = 0; vOutIdx < wtx.tx->vout.size(); ++vOutIdx)
    {
        const CTxOut& txout = wtx.tx->vout[vOutIdx];

        isminetype mine = wtx.txout_is_mine[vOutIdx];

        TransactionRecord sub(hash, nTime);
        CTxDestination address;
        bool validDestination = ExtractDestination(txout.scriptPubKey, address);
        sub.idx = vOutIdx;
        sub.credit = txout.nValue;
        sub.strAddress = validDestination ? EncodeDestination(address) : mapValue["from"];
        //TODO: sub.address.SetString(sub.strAddress);
        sub.txDest = address;
        sub.updateLabel(wallet);

        // Check if all inputs are from wallet and if output are to wallet
        bool fAllFromMeDenom = true;
        int nFromMe = 0;
        bool inputInvolvesWatchAddress = false;
        bool outputInvolvesWatchAddress = false;
        isminetype fAllFromMe = ISMINE_SPENDABLE;
        for (isminetype mine : wtx.txin_is_mine)
        {
            if (mine & ISMINE_WATCH_ONLY) inputInvolvesWatchAddress = true;
            if (fAllFromMe > mine) fAllFromMe = mine;
        }

        isminetype fAllToMe = ISMINE_SPENDABLE;
        for (isminetype mine : wtx.txout_is_mine)
        {
            if (mine & ISMINE_WATCH_ONLY) outputInvolvesWatchAddress = true;
            if (fAllToMe > mine) fAllToMe = mine;
        }

        // LogPrintf("TransactionRecord::%s TxId: %s vOutIdx: %d, CoinBase: %d, AllFromMe: %d, AllToMe: %d, inputWatch: %d, outputWatch: %d\n",
        //         __func__, hash.ToString(), vOutIdx, wtx.is_coinbase, fAllFromMe, fAllToMe, inputInvolvesWatchAddress, outputInvolvesWatchAddress);

        // A/B Generated:
        // A: coinbase -> self    Generated
        // B: coinbase -> watched Generated (watched)
        if (wtx.is_coinbase && mine)
        {
            sub.type = TransactionRecord::Generated;
            sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
            parts.append(sub);
            continue;
        }

        // C/D: SendToSelf, PrivateSend*
        // C: self    -> self     Send to self
        // D: watched -> watched  Send to self (watched)
        if (fAllFromMe && fAllToMe && (debitMineTypes == creditMineTypes))
        {
            CAmount nChange = wtx.change;
            sub.debit = -(nDebit - nChange);
            sub.credit = nCredit - nChange;
            sub.involvesWatchAddress = inputInvolvesWatchAddress;

            if (wtx.is_denominate)
            {
                sub.type = TransactionRecord::CoinJoinCreateDenominations;
                sub.debit = -nDebit;
                sub.credit = nCredit;
                parts.append(sub);
                break; // Only report first of the batch
            }

            if (mapValue["DS"] == "1")
            {
                sub.type = TransactionRecord::CoinJoinSend;
                CAmount nChange = wtx.change;
                parts.append(sub);
                break; // Only report first of the batch
            }


            sub.type = TransactionRecord::SendToSelf;

            sub.idx = parts.size();
            if (wtx.tx->vin.size() == 1 && wtx.tx->vout.size() == 1
                && CCoinJoin::IsCollateralAmount(nDebit)
                && CCoinJoin::IsCollateralAmount(nCredit)
                && CCoinJoin::IsCollateralAmount(-nNet))
            {
                sub.type = TransactionRecord::CoinJoinCollateralPayment;
            }
            else
            {
                for (const auto& txout : wtx.tx->vout)
                {
                    if (txout.nValue == CCoinJoin::GetMaxCollateralAmount()) {
                        sub.type = TransactionRecord::CoinJoinMakeCollaterals;
                        continue; // Keep looking, could be a part of PrivateSendCreateDenominations
                    } else if (CCoinJoin::IsDenominatedAmount(txout.nValue)) {
                        sub.type = TransactionRecord::CoinJoinCreateDenominations;
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

        // I/J: xxx SendToAddress, SendToOther, watched: ReceiveWithAddress, RecvFromOther + FutureSend, FutureReceive
        // This handles watched addresses going to unmonitored addresses
        // I: watched  -> other   Send from watched address to other
        // J: other    -> watched Send from other address to watched
        if (!(debitMineTypes & ISMINE_SPENDABLE) && !(mine & ISMINE_SPENDABLE))
        {
            sub.involvesWatchAddress = true;

            // Sent by watched address:
            if (inputInvolvesWatchAddress)
            {
                sub.type = isFuture ? TransactionRecord::FutureSend : TransactionRecord::SendToOther;
                sub.debit = -(txout.nValue + nTxFee);
                nTxFee = 0; // Add fee to first output
                sub.credit = 0;
                parts.append(sub);
            }

            // If received by Watch, add a receive transaction on the watched side:
            if (outputInvolvesWatchAddress)
            {
                sub.involvesWatchAddress = true;
                sub.type = isFuture ? TransactionRecord::FutureReceive : TransactionRecord::RecvFromOther;
                sub.debit = 0;
                sub.credit = txout.nValue;
                parts.append(sub);
            }
            continue;
        }

        LogPrintf("TransactionRecord::%s TxId: %s, vOutIdx: %d, Unhandled\n", __func__, hash.ToString(), vOutIdx);
    }
    return parts;
}

void TransactionRecord::updateStatus(const interfaces::WalletTx& wtx, const interfaces::WalletTxStatus& wtxStatus, int numBlocks, int64_t adjustedTime, int chainLockHeight, int64_t block_time)
{
    // Determine transaction status

    // Sort order, unrecorded transactions sort to the top
    status.sortKey = strprintf("%010d-%01d-%010u-%03d",
        wtxStatus.block_height,
        wtxStatus.is_coinbase ? 1 : 0,
        wtxStatus.time_received,
        idx);
    status.countsForBalance = wtxStatus.is_trusted && !(wtxStatus.blocks_to_maturity > 0);
    status.depth = wtxStatus.depth_in_main_chain;
    status.cur_num_blocks = numBlocks;
    status.cachedChainLockHeight = chainLockHeight;
    status.lockedByChainLocks = wtxStatus.is_chainlocked;
    status.lockedByInstantSend = wtxStatus.is_islocked;

    const bool up_to_date = ((int64_t)QDateTime::currentMSecsSinceEpoch() / 1000 - block_time < MAX_BLOCK_TIME_GAP);
    if (up_to_date && !wtxStatus.is_final) {
        if (wtxStatus.lock_time < LOCKTIME_THRESHOLD) {
            status.status = TransactionStatus::OpenUntilBlock;
            status.open_for = wtxStatus.lock_time - numBlocks;
        } else {
            status.status = TransactionStatus::OpenUntilDate;
            status.open_for = wtxStatus.lock_time;
        }
    }
    // For generated transactions, determine maturity
    else if(type == TransactionRecord::Generated) {
        if (wtxStatus.blocks_to_maturity > 0) {
            status.status = TransactionStatus::Immature;

            if (wtxStatus.is_in_main_chain) {
                status.matures_in = wtxStatus.blocks_to_maturity;
            } else {
                status.status = TransactionStatus::NotAccepted;
            }
        } else {
            status.status = TransactionStatus::Confirmed;
        }
    }
    // For Future transactions, determine maturity
    else if (type == TransactionRecord::FutureReceive) {
        CFutureTx ftx;
        getFutureTxStatus(wtx, wtxStatus, ftx);
    } else {
        if (status.depth < 0) {
            status.status = TransactionStatus::Conflicted;
        } else if (status.depth == 0) {
            status.status = TransactionStatus::Unconfirmed;
            if (wtxStatus.is_abandoned)
                status.status = TransactionStatus::Abandoned;
        } else if (status.depth < RecommendedNumConfirmations && !status.lockedByChainLocks) {
            status.status = TransactionStatus::Confirming;
        } else {
            status.status = TransactionStatus::Confirmed;
        }
    }
    status.needsUpdate = false;
}

bool TransactionRecord::statusUpdateNeeded(int numBlocks, int chainLockHeight) const
{
    return status.cur_num_blocks != numBlocks || status.needsUpdate
        || (!status.lockedByChainLocks && status.cachedChainLockHeight != chainLockHeight);
}

void TransactionRecord::updateLabel(interfaces::Wallet& wallet)
{
    if (IsValidDestination(txDest)) {
        std::string name;
        if (wallet.getAddress(txDest, &name)) {
            label = QString::fromStdString(name);
        } else {
            label = "";
        }
    }
}

QString TransactionRecord::getTxHash() const
{
    return QString::fromStdString(hash.ToString());
}

int TransactionRecord::getOutputIndex() const
{
    return idx;
}
