// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/transactionrecord.h>

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

void TransactionRecord::getFutureTxStatus(const interfaces::WalletTx& wtx, const interfaces::WalletTxStatus& wtxStatus, CFutureTx &ftx)
{

    if (GetTxPayload(wtx.tx->vExtraPayload, ftx))
    {

        int maturityBlock = wtxStatus.block_height + ftx.maturity;
        int64_t maturityTime = wtxStatus.time_received + ftx.lockTime;
        int currentHeight = chainActive.Height();
        //transaction depth in chain against maturity OR relative seconds of transaction against lockTime
        if (currentHeight >= maturityBlock || GetAdjustedTime() >= maturityTime) {
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
QList<TransactionRecord> TransactionRecord::decomposeTransaction(interfaces::Wallet& wallet, const interfaces::WalletTx& wtx)
{
    QList<TransactionRecord> parts;
    int64_t nTime = wtx.time;
    CAmount nCredit = wtx.credit;
    CAmount nDebit = wtx.debit;
    CAmount nNet = nCredit - nDebit;
    uint256 hash = wtx.tx->GetHash();
    std::map<std::string, std::string> mapValue = wtx.value_map;
    auto node = interfaces::MakeNode();
    auto& coinJoinOptions = node->coinJoinOptions();

    if (nNet > 0 || wtx.is_coinbase)
    {
        //
        // Credit
        //
        for(unsigned int i = 0; i < wtx.tx->vout.size(); i++)
        {
            const CTxOut& txout = wtx.tx->vout[i];
            isminetype mine = wtx.txout_is_mine[i];
            if(mine)
            {
                TransactionRecord sub(hash, nTime);
                sub.idx = i; // vout index
                sub.credit = txout.nValue;
                sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
                if (wtx.txout_address_is_mine[i])
                {
                    // Received by Raptoreum Address
                    sub.type = TransactionRecord::RecvWithAddress;
                    sub.strAddress = EncodeDestination(wtx.txout_address[i]);
                    sub.txDest = wtx.txout_address[i];
                    sub.updateLabel(wallet);
                }
                else
                {
                    // Received by IP connection (deprecated features), or a multisignature or other non-simple transaction
                    sub.type = TransactionRecord::RecvFromOther;
                    sub.strAddress = mapValue["from"];
                    sub.txDest = DecodeDestination(sub.strAddress);
                }
                if (wtx.is_coinbase)
                {
                    // Generated
                    sub.type = TransactionRecord::Generated;
                }

                if(wtx.tx->nType == TRANSACTION_FUTURE)
                {
                    // Future TX Received
                    CTxDestination address;
                    if (ExtractDestination(wtx.tx->vout[1].scriptPubKey, address))
                    {
                        // Received by Raptoreum Address
                        sub.type = TransactionRecord::FutureReceive;
                        sub.strAddress = EncodeDestination(address);
                        sub.txDest = address;
                    }
                }
                
                parts.append(sub);
            }
        }
    }
    else
    {
        bool involvesWatchAddress = false;
        isminetype fAllFromMe = ISMINE_SPENDABLE;
        for (isminetype mine : wtx.txin_is_mine)
        {
            if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if(fAllFromMe > mine) fAllFromMe = mine;
        }

        isminetype fAllToMe = ISMINE_SPENDABLE;
        for (isminetype mine : wtx.txout_is_mine)
        {
            if(mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
            if(fAllToMe > mine) fAllToMe = mine;
        }

        if(wtx.is_denominate) {
            parts.append(TransactionRecord(hash, nTime, TransactionRecord::CoinJoinMixing, "", -nDebit, nCredit));
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

            if(mapValue["DS"] == "1")
            {
                sub.type = TransactionRecord::CoinJoinSend;
                CTxDestination address;
                if (ExtractDestination(wtx.tx->vout[0].scriptPubKey, address))
                {
                    // Sent to Dash Address
                    sub.strAddress = EncodeDestination(address);
                    sub.txDest = address;
                    sub.updateLabel(wallet);
                }
                else
                {
                    // Sent to IP, or other non-address transaction like OP_EVAL
                    sub.strAddress = mapValue["to"];
                    sub.txDest = DecodeDestination(sub.strAddress);
                }
            }
            else
            {
                sub.idx = parts.size();
                if(wtx.tx->vin.size() == 1 && wtx.tx->vout.size() == 1
                    && coinJoinOptions.isCollateralAmount(nDebit)
                    && coinJoinOptions.isCollateralAmount(nCredit)
                    && coinJoinOptions.isCollateralAmount(-nNet))
                {
                    sub.type = TransactionRecord::CoinJoinCollateralPayment;
                } else {
                    bool fMakeCollateral{false};
                    if (wtx.tx->vout.size() == 2) {
                        CAmount nAmount0 = wtx.tx->vout[0].nValue;
                        CAmount nAmount1 = wtx.tx->vout[1].nValue;
                        // <case1>, see CCoinJoinClientSession::MakeCollateralAmounts
                        fMakeCollateral = (nAmount0 == coinJoinOptions.getMaxCollateralAmount() && !coinJoinOptions.isDenominated(nAmount1) && nAmount1 >= coinJoinOptions.getMinCollateralAmount()) ||
                                          (nAmount1 == coinJoinOptions.getMaxCollateralAmount() && !coinJoinOptions.isDenominated(nAmount0) && nAmount0 >= coinJoinOptions.getMinCollateralAmount()) ||
                        // <case2>, see CCoinJoinClientSession::MakeCollateralAmounts
                                          (nAmount0 == nAmount1 && coinJoinOptions.isCollateralAmount(nAmount0));
                    } else if (wtx.tx->vout.size() == 1) {
                        // <case3>, see CCoinJoinClientSession::MakeCollateralAmounts
                        fMakeCollateral = coinJoinOptions.isCollateralAmount(wtx.tx->vout[0].nValue);
                    }
                    if (fMakeCollateral) {
                        sub.type = TransactionRecord::CoinJoinMakeCollaterals;
                    } else {
                        for (const auto& txout : wtx.tx->vout) {
                            if (coinJoinOptions.isDenominated(txout.nValue)) {
                                sub.type = TransactionRecord::CoinJoinCreateDenominations;
                                break; // Done, it's definitely a tx creating mixing denoms, no need to look any further
                            }
                        }
                    }
                }
            }

            CAmount nChange = wtx.change;
            if(wtx.tx->nType == TRANSACTION_FUTURE)
            {
                sub.type = TransactionRecord::FutureSend;
                CTxDestination address;
                if (ExtractDestination(wtx.tx->vout[0].scriptPubKey, address))
                {
                    // Sent to Raptoreum Address
                    sub.strAddress = EncodeDestination(address);
                    sub.txDest = address;
                }
            }

            sub.debit = -(nDebit - nChange);
            sub.credit = nCredit - nChange;
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
                && coinJoinOptions.isCollateralAmount(nDebit)
                && nCredit == 0 // OP_RETURN
                && coinJoinOptions.isCollateralAmount(-nNet))
            {
                TransactionRecord sub(hash, nTime);
                sub.idx = 0;
                sub.type = TransactionRecord::CoinJoinCollateralPayment;
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

                if(wtx.txout_is_mine[nOut])
                {
                    // Ignore parts sent to self, as this is usually the change
                    // from a transaction sent back to our own address.
                    continue;
                }

                if (!boost::get<CNoDestination>(&wtx.txout_address[nOut]))
                {
                    // Sent to Raptoreum Address
                    sub.type = TransactionRecord::SendToAddress;
                    sub.strAddress = EncodeDestination(wtx.txout_address[nOut]);
                    sub.txDest = wtx.txout_address[nOut];
                    sub.updateLabel(wallet);
                }
                parts.append(sub);
            }
        }
       //LogPrintf("TransactionRecord::%s TxId: %s, vOutIdx: %d, Unhandled\n", __func__, hash.ToString(), vOutIdx);
    }
    return parts;
}

void TransactionRecord::updateStatus(const interfaces::WalletTx& wtx, const interfaces::WalletTxStatus& wtxStatus, int numBlocks, int64_t adjustedTime, int chainLockHeight)
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

    if (!wtxStatus.is_final)
    {
        if (wtxStatus.lock_time < LOCKTIME_THRESHOLD)
        {
            status.status = TransactionStatus::OpenUntilBlock;
            status.open_for = wtxStatus.lock_time - numBlocks;
        }
        else
        {
            status.status = TransactionStatus::OpenUntilDate;
            status.open_for = wtxStatus.lock_time;
        }
    }
    // For generated transactions, determine maturity
    else if(type == TransactionRecord::Generated)
    {
        if (wtxStatus.blocks_to_maturity > 0)
        {
            status.status = TransactionStatus::Immature;

            if (wtxStatus.is_in_main_chain)
            {
                status.matures_in = wtxStatus.blocks_to_maturity;
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
        getFutureTxStatus(wtx, wtxStatus, ftx);

    }
    else
    {
        if (status.depth < 0)
        {
            status.status = TransactionStatus::Conflicted;
        }
        else if (status.depth == 0)
        {
            status.status = TransactionStatus::Unconfirmed;
            if (wtxStatus.is_abandoned)
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
