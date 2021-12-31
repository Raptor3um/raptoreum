// Copyright (c) 2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/validation.h>
#include <core_io.h>
#include <init.h>
#include <key_io.h>
#include <messagesigner.h>
#include <rpc/server.h>
#include <utilmoneystr.h>
#include <validation.h>

#ifdef ENABLE_WALLET
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>
#include <wallet/rpcwallet.h>
#endif//ENABLE_WALLET

#include <netbase.h>
#include <evo/specialtx.h>
#include <evo/providertx.h>

#include <iostream>
#include <unistd.h>

using namespace std;

#ifdef ENABLE_WALLET
extern UniValue signrawtransaction(const JSONRPCRequest& request);
extern UniValue sendrawtransaction(const JSONRPCRequest& request);

template<typename SpecialTxPayload>
static void FundSpecialTx(CWallet* pwallet, CMutableTransaction& tx, const SpecialTxPayload& payload, const CTxDestination& fundDest, CAmount specialFees = 0)
{
    assert(pwallet != nullptr);
    LOCK2(cs_main, pwallet->cs_wallet);

    CTxDestination nodest = CNoDestination();
    if (fundDest == nodest) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "No source of funds specified");
    }

    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << payload;
    tx.vExtraPayload.assign(ds.begin(), ds.end());

    static CTxOut dummyTxOut(0, CScript() << OP_RETURN);
    std::vector<CRecipient> vecSend;
    bool dummyTxOutAdded = false;

    if (tx.vout.empty()) {
        // add dummy txout as CreateTransaction requires at least one recipient
        tx.vout.emplace_back(dummyTxOut);
        dummyTxOutAdded = true;
    }

    for (const auto& txOut : tx.vout) {
        CRecipient recipient = {txOut.scriptPubKey, txOut.nValue, false};
        vecSend.push_back(recipient);
    }

    CCoinControl coinControl;
    coinControl.destChange = fundDest;
    coinControl.fRequireAllInputs = false;

    std::vector<COutput> vecOutputs;
    pwallet->AvailableCoins(vecOutputs);

    for (const auto& out : vecOutputs) {
        CTxDestination txDest;
        if (ExtractDestination(out.tx->tx->vout[out.i].scriptPubKey, txDest) && txDest == fundDest) {
            coinControl.Select(COutPoint(out.tx->tx->GetHash(), out.i));
        }
    }

    if (!coinControl.HasSelected()) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "No funds at specified address");
    }

    CTransactionRef wtx;
    CReserveKey reservekey(pwallet);
    CAmount nFee;
    int nChangePos = -1;
    std::string strFailReason;

    if (!pwallet->CreateTransaction(vecSend, wtx, reservekey, nFee, nChangePos, strFailReason, coinControl, false, tx.vExtraPayload.size(), specialFees)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, strFailReason);
    }

    tx.vin = wtx->vin;
    tx.vout = wtx->vout;

    if (dummyTxOutAdded && tx.vout.size() > 1) {
        // CreateTransaction added a change output, so we don't need the dummy txout anymore.
        // Removing it results in slight overpayment of fees, but we ignore this for now (as it's a very low amount).
        auto it = std::find(tx.vout.begin(), tx.vout.end(), dummyTxOut);
        assert(it != tx.vout.end());
        tx.vout.erase(it);
    }
}

template<typename SpecialTxPayload>
static void UpdateSpecialTxInputsHash(const CMutableTransaction& tx, SpecialTxPayload& payload)
{
    payload.inputsHash = CalcTxInputsHash(tx);
}

template<typename SpecialTxPayload>
static void SignSpecialTxPayloadByHash(const CMutableTransaction& tx, SpecialTxPayload& payload, const CKey& key)
{
    UpdateSpecialTxInputsHash(tx, payload);
    payload.vchSig.clear();

    uint256 hash = ::SerializeHash(payload);
    if (!CHashSigner::SignHash(hash, key, payload.vchSig)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "failed to sign special tx");
    }
}

template<typename SpecialTxPayload>
static void SignSpecialTxPayloadByString(const CMutableTransaction& tx, SpecialTxPayload& payload, const CKey& key)
{
    UpdateSpecialTxInputsHash(tx, payload);
    payload.vchSig.clear();

    std::string m = payload.MakeSignString();
    if (!CMessageSigner::SignMessage(m, payload.vchSig, key)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "failed to sign special tx");
    }
}

template<typename SpecialTxPayload>
static void SignSpecialTxPayloadByHash(const CMutableTransaction& tx, SpecialTxPayload& payload, const CBLSSecretKey& key)
{
    UpdateSpecialTxInputsHash(tx, payload);

    uint256 hash = ::SerializeHash(payload);
    payload.sig = key.Sign(hash);
}

static std::string SignAndSendSpecialTx(const CMutableTransaction& tx)
{
    LOCK(cs_main);

    CValidationState state;
//    CCoinsViewCache view;
    if (!CheckSpecialTx(tx, chainActive.Tip(), state, *pcoinsTip.get())) {
        throw std::runtime_error(FormatStateMessage(state));
    }

    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << tx;

    JSONRPCRequest signRequest;
    signRequest.params.setArray();
    signRequest.params.push_back(HexStr(ds.begin(), ds.end()));
    UniValue signResult = signrawtransaction(signRequest);

    JSONRPCRequest sendRequest;
    sendRequest.params.setArray();
    sendRequest.params.push_back(signResult["hex"].get_str());
    return sendrawtransaction(sendRequest).get_str();
}
#endif
