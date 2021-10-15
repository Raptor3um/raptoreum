// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletmodelfuturestransaction.h"

#include "wallet/wallet.h"

WalletModelFuturesTransaction::WalletModelFuturesTransaction(const QList<SendFuturesRecipient> &_recipients) :
    recipients(_recipients),
    walletTransaction(0),
    keyChange(0),
    fee(0)
{
    //walletTransaction = new CFutureTx();
    walletTransaction = new CWalletTx();
}

WalletModelFuturesTransaction::~WalletModelFuturesTransaction()
{
    delete keyChange;
    delete walletTransaction;
}

QList<SendFuturesRecipient> WalletModelFuturesTransaction::getRecipients()
{
    return recipients;
}

CWalletTx *WalletModelFuturesTransaction::getTransaction()
{
    return walletTransaction;
}

unsigned int WalletModelFuturesTransaction::getTransactionSize()
{
    return (!walletTransaction ? 0 : (::GetSerializeSize(walletTransaction->tx, SER_NETWORK, PROTOCOL_VERSION)));
}

CAmount WalletModelFuturesTransaction::getTransactionFee()
{
    return fee;
}

void WalletModelFuturesTransaction::setTransactionFee(const CAmount& newFee)
{
    fee = newFee;
}

void WalletModelFuturesTransaction::reassignAmounts()
{
    // For each recipient look for a matching CTxOut in walletTransaction and reassign amounts
    for (QList<SendFuturesRecipient>::iterator it = recipients.begin(); it != recipients.end(); ++it)
    {
        SendFuturesRecipient& rcp = (*it);

        if (rcp.paymentRequest.IsInitialized())
        {
            CAmount subtotal = 0;
            const payments::PaymentDetails& details = rcp.paymentRequest.getDetails();
            for (int j = 0; j < details.outputs_size(); j++)
            {
                const payments::Output& out = details.outputs(j);
                if (out.amount() <= 0) continue;
                const unsigned char* scriptStr = (const unsigned char*)out.script().data();
                CScript scriptPubKey(scriptStr, scriptStr+out.script().size());
                for (const auto& txout : walletTransaction->tx->vout) {
                    if (txout.scriptPubKey == scriptPubKey) {
                        subtotal += txout.nValue;
                        break;
                    }
                }
            }
            rcp.amount = subtotal;
        }
        else // normal recipient (no payment request)
        {
            for (const auto& txout : walletTransaction->tx->vout) {
                CScript scriptPubKey = GetScriptForDestination(CBitcoinAddress(rcp.address.toStdString()).Get());
                if (txout.scriptPubKey == scriptPubKey) {
                    rcp.amount = txout.nValue;
                    break;
                }
            }
        }
    }
}

CAmount WalletModelFuturesTransaction::getTotalTransactionAmount()
{
    CAmount totalTransactionAmount = 0;
    for (const SendFuturesRecipient &rcp : recipients)
    {
        totalTransactionAmount += rcp.amount;
    }
    return totalTransactionAmount;
}

void WalletModelFuturesTransaction::newPossibleKeyChange(CWallet *wallet)
{
    keyChange = new CReserveKey(wallet);
}

CReserveKey *WalletModelFuturesTransaction::getPossibleKeyChange()
{
    return keyChange;
}
