// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifdef HAVE_CONFIG_H
#include <config/raptoreum-config.h>
#endif

#include <qt/walletmodeltransaction.h>

#include <interfaces/node.h>
#include <key_io.h>

WalletModelTransaction::WalletModelTransaction(const QList <SendCoinsRecipient> &_recipients) :
        recipients(_recipients),
        fee(0) {
}

QList <SendCoinsRecipient> WalletModelTransaction::getRecipients() const {
    return recipients;
}

CTransactionRef &WalletModelTransaction::getWtx() {
    return wtx;
}

unsigned int WalletModelTransaction::getTransactionSize() {
    return wtx != nullptr ? ::GetSerializeSize(*wtx, SER_NETWORK, PROTOCOL_VERSION) : 0;
}

CAmount WalletModelTransaction::getTransactionFee() const {
    return fee;
}

void WalletModelTransaction::setTransactionFee(const CAmount &newFee) {
    fee = newFee;
}

void WalletModelTransaction::reassignAmounts() {
    // For each recipient look for a matching CTxOut in walletTransaction and reassign amounts
    for (QList<SendCoinsRecipient>::iterator it = recipients.begin(); it != recipients.end(); ++it) {
        SendCoinsRecipient &rcp = (*it);
        {
            for (const auto &txout: wtx.get()->vout) {
                CScript scriptPubKey = GetScriptForDestination(DecodeDestination(rcp.address.toStdString()));
                if (txout.scriptPubKey == scriptPubKey) {
                    rcp.amount = txout.nValue;
                    break;
                }
            }
        }
    }
}

CAmount WalletModelTransaction::getTotalTransactionAmount() const {
    CAmount totalTransactionAmount = 0;
    for (const SendCoinsRecipient &rcp: recipients) {
        totalTransactionAmount += rcp.amount;
    }
    return totalTransactionAmount;
}
