// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
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
namespace interfaces {
    class Node;
}

/** Data model for a walletmodel future transaction. */
class WalletModelFuturesTransaction {
public:
    explicit WalletModelFuturesTransaction(const QList <SendFuturesRecipient> &recipients);

    QList <SendFuturesRecipient> getRecipients() const;

    //CFutureTx *getTransaction();
    CWalletTx *getTransaction() const;

    unsigned int getTransactionSize() const;

    CTransactionRef &getWtx();

    void setTransactionFee(const CAmount &newFee);

    CAmount getTransactionFee() const;

    CAmount getTotalTransactionAmount() const;

    void reassignAmounts(); // needed for the subtract-fee-from-amount feature

    void assignFuturePayload();

private:
    QList <SendFuturesRecipient> recipients;
    CTransactionRef wtx;
    CAmount fee;
};

#endif // BITCOIN_QT_WALLETMODELFUTURESTRANSACTION_H
