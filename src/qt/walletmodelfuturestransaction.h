// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_WALLETMODELFUTURESTRANSACTION_H
#define BITCOIN_QT_WALLETMODELFUTURESTRANSACTION_H

#include "walletmodel.h"

#include <QObject>

class SendFuturesRecipient;

class CReserveKey;
class CWallet;
class CWalletTx;
//class CFutureTx;

/** Data model for a walletmodel future transaction. */
class WalletModelFuturesTransaction
{
public:
    explicit WalletModelFuturesTransaction(const QList<SendFuturesRecipient> &recipients);
    ~WalletModelFuturesTransaction();

    QList<SendFuturesRecipient> getRecipients();

    //CFutureTx *getTransaction();
    CWalletTx *getTransaction();
    unsigned int getTransactionSize();

    void setTransactionFee(const CAmount& newFee);
    CAmount getTransactionFee();

    CAmount getTotalTransactionAmount();

    void newPossibleKeyChange(CWallet *wallet);
    CReserveKey *getPossibleKeyChange();

    void reassignAmounts(); // needed for the subtract-fee-from-amount feature

private:
    QList<SendFuturesRecipient> recipients;
    //CFutureTx *walletTransaction;
    CWalletTx *walletTransaction;
    CReserveKey *keyChange;
    CAmount fee;
};

#endif // BITCOIN_QT_WALLETMODELFUTURESTRANSACTION_H
