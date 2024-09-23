// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_WALLETMODELTRANSACTION_H
#define BITCOIN_QT_WALLETMODELTRANSACTION_H

#include <qt/walletmodel.h>

#include <QObject>

class SendCoinsRecipient;

namespace interfaces {
    class Node;
}

/** Data model for a walletmodel transaction. */
class WalletModelTransaction {
public:
    explicit WalletModelTransaction(const QList <SendCoinsRecipient> &recipients);

    QList <SendCoinsRecipient> getRecipients() const;

    CTransactionRef &getWtx();

    unsigned int getTransactionSize();

    void setTransactionFee(const CAmount &newFee);

    CAmount getTransactionFee() const;

    CAmount getTotalTransactionAmount() const;

    void reassignAmounts(); // needed for the subtract-fee-from-amount feature

private:
    QList <SendCoinsRecipient> recipients;
    CTransactionRef wtx;
    CAmount fee;
};

#endif // BITCOIN_QT_WALLETMODELTRANSACTION_H
