// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/raptoreum-config.h>
#endif

#include <qt/walletmodelfuturestransaction.h>
#include <rpc/specialtx_utilities.h>
#include <timedata.h>

#include <wallet/wallet.h>
#include <key_io.h>

WalletModelFuturesTransaction::WalletModelFuturesTransaction(const QList<SendFuturesRecipient> &_recipients) :
    recipients(_recipients),
    fee(0)
{
    //walletTransaction = new CFutureTx();
   // walletTransaction = new CWalletTx();
}

CTransactionRef& WalletModelFuturesTransaction::getWtx()
{
    return wtx;
}


QList<SendFuturesRecipient> WalletModelFuturesTransaction::getRecipients() const
{
    return recipients;
}

unsigned int WalletModelFuturesTransaction::getTransactionSize() const
{
    return wtx != nullptr ? ::GetSerializeSize(*wtx, SER_NETWORK, PROTOCOL_VERSION) : 0;
}

CAmount WalletModelFuturesTransaction::getTransactionFee() const
{
    return fee;
}

void WalletModelFuturesTransaction::setTransactionFee(const CAmount& newFee)
{
    fee = newFee;
}

void WalletModelFuturesTransaction::assignFuturePayload() {
	for (QList<SendFuturesRecipient>::iterator it = recipients.begin(); it != recipients.end(); ++it)
	{
		SendFuturesRecipient& rcp = (*it);
	}
}

void WalletModelFuturesTransaction::reassignAmounts()
{
    // For each recipient look for a matching CTxOut in walletTransaction and reassign amounts

    for (QList<SendFuturesRecipient>::iterator it = recipients.begin(); it != recipients.end(); ++it)
    {
        SendFuturesRecipient& rcp = (*it);
        {
            CFutureTx ftx;
            for (const auto& txout : wtx.get()->vout) {
                CScript scriptPubKey = GetScriptForDestination(DecodeDestination(rcp.address.toStdString()));
                if (txout.scriptPubKey == scriptPubKey) {
                    rcp.amount = txout.nValue;
                    break;
                }
            }
        }
    }
}

CAmount WalletModelFuturesTransaction::getTotalTransactionAmount() const
{
    CAmount totalTransactionAmount = 0;
    for (const SendFuturesRecipient &rcp : recipients)
    {
        totalTransactionAmount += rcp.amount;
    }
    return totalTransactionAmount;
}
