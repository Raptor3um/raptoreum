// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_WALLETMODELFUTURESTRANSACTION_H
#define BITCOIN_QT_WALLETMODELFUTURESTRANSACTION_H

#include <qt/walletmodel.h>

#include <memory>

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

    QList<SendFuturesRecipient> getRecipients() const;

    //CFutureTx *getTransaction();
    CWalletTx *getTransaction() const;
    unsigned int getTransactionSize() const;

    void setTransactionFee(const CAmount& newFee);
    CAmount getTransactionFee() const;

    CAmount getTotalTransactionAmount() const;

    void newPossibleKeyChange(CWallet *wallet);
    CReserveKey *getPossibleKeyChange();

    void reassignAmounts(); // needed for the subtract-fee-from-amount feature

    void assignFuturePayload();

private:
    QList<SendFuturesRecipient> recipients;
    //CFutureTx *walletTransaction;
    CWalletTx *walletTransaction;
    std::unique_ptr<CReserveKey> keyChange;
    CAmount fee;
};

#endif // BITCOIN_QT_WALLETMODELFUTURESTRANSACTION_H
