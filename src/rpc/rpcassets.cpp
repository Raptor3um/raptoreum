// Copyright (c) 2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assets/assets.h"
#include <assets/assetstype.h>
#include <rpc/server.h>
#include "chain.h"
#include "validation.h"
#include <txmempool.h>
#include <chainparams.h>


#ifdef ENABLE_WALLET
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>
#include <wallet/rpcwallet.h>
#endif//ENABLE_WALLET

#include <evo/specialtx.h>
#include <evo/providertx.h>

#include <iostream>


static CKeyID ParsePubKeyIDFromAddress(const std::string &strAddress, const std::string &paramName) {
    CTxDestination dest = DecodeDestination(strAddress);
    const CKeyID *keyID = boost::get<CKeyID>(&dest);
    if (!keyID) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           strprintf("%s must be a valid P2PKH address, not %s", paramName, strAddress));
    }
    return *keyID;
}

static std::string GetDistributionType(int t) {
    switch (t) {
        case 0:
            return "manual";
        case 1:
            return "coinbase";
        case 2:
            return "address";
        case 3:
            return "schedule";
    }
    return "invalid";
}

UniValue createasset(const JSONRPCRequest &request) {

    if (request.fHelp || !Params().IsAssetsActive(::ChainActive().Tip()) || request.params.size() < 1 ||
        request.params.size() > 1)
        throw std::runtime_error(
                "createasset asset_metadata\n"
                "Create a new asset\n"
                "\nArguments:\n"
                "1. \"asset\"               (string, required) A json object with asset metadata\n"
                "{\n"
                "   \"name:\"               (string) Asset name\n"
                "   \"updatable:\"          (bool, optional, default=true) if true this asset can be modify using reissue process.\n"
                "   \"is_unique:\"          (bool, optional, default=false) if true this is asset is unique it has an identity per token (NFT flag)\n"
                "   \"decimalpoint:\"       (numeric) [0 to 8] has to be 0 if is_unique is true.\n"
                "   \"referenceHash:\"      (string) hash of the underlying physical or digital assets, IPFS hash can be used here.\n"
                "   \"maxMintCount\"        (numeric, required) number of times this asset can be mint\n"
                "   \"type:\"               (numeric) distribution type manual=0, coinbase=1, address=2, schedule=3\n"
                "   \"targetAddress:\"      (string) address to be issued to when asset issue transaction is created.\n"
                "   \"issueFrequency:\"     (numeric) mint specific amount of token every x blocks\n"
                "   \"amount:\"             (numeric, (max 500 for unique) amount to distribute each time if type is not manual.\n"
                "   \"ownerAddress:\"       (string) address that this asset is owned by. Only key holder of this address will be able to mint new tokens\n"
                "}\n"
                "\nResult:\n"
                "\"txid\"                   (string) The transaction id for the new asset\n"

                "\nExamples:\n"
                + HelpExampleCli("createasset",
                                 "'{\"name\":\"test asset\", \"updatable\":true, \"isunique\":false, \"maxMintCount\":10, \n"
                                 "\"decimalpoint\":2, \"referenceHash\":\"\", \"type\":0, \"targetAddress\":\"yQPzaDmnF3FtRsoWijUN7aZDcEdyNAcmVk\", \n"
                                 "\"issueFrequency\":0, \"amount\":10000,\"ownerAddress\":\"yRyiTCKfqMG2dQ9oUvs932TjN1R1MNUTWM\"}'")
        );

    std::shared_ptr <CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet *const pwallet = wallet.get();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK2(cs_main, mempool.cs);
    LOCK(pwallet->cs_wallet);

    if (pwallet->GetBroadcastTransactions() && !pwallet->chain().p2pEnabled()) {
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
    }

    UniValue asset = request.params[0].get_obj();

    const UniValue &name = find_value(asset, "name");

    std::string assetname = name.getValStr();

    //check if asset name is valid
    if (!IsAssetNameValid(assetname)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: Invalid asset name");
    }
    // check if asset already exist
    std::string assetId;
    if (passetsCache->GetAssetId(assetname, assetId)) {
        CAssetMetaData tmpAsset;
        if (passetsCache->GetAssetMetaData(assetId, tmpAsset)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: Asset already exist");
        }
    }

    //check on mempool if asset already exist
    if (mempool.CheckForNewAssetConflict(assetname)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: Asset already exist on mempool");
    }

    CNewAssetTx assetTx;
    assetTx.name = assetname;
    const UniValue &updatable = find_value(asset, "updatable");
    if (!updatable.isNull()) {
        assetTx.updatable = updatable.get_bool();
    } else {
        assetTx.updatable = true;
    }

    const UniValue &referenceHash = find_value(asset, "referenceHash");
    if (!referenceHash.isNull()) {
        std::string ref = referenceHash.get_str();
        if (ref.length() != 46)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: Invalid referenceHash (must be 46 characters)");
        if (ref.substr(0, 2) != "Qm")
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: invalid referenceHash (must start with Qm)");
        assetTx.referenceHash = ref;
    }

    const UniValue &targetAddress = find_value(asset, "targetAddress");
    if (!targetAddress.isNull()) {
        assetTx.targetAddress = ParsePubKeyIDFromAddress(targetAddress.get_str(), "targetAddress");;
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: missing targetAddress");
    }

    const UniValue &ownerAddress = find_value(asset, "ownerAddress");
    if (!ownerAddress.isNull()) {
        assetTx.ownerAddress = ParsePubKeyIDFromAddress(ownerAddress.get_str(), "ownerAddress");
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: missing ownerAddress");
    }

    const UniValue &is_unique = find_value(asset, "isunique");
    if (!is_unique.isNull() && is_unique.get_bool()) {
        assetTx.isUnique = true;
        assetTx.updatable = false;
        assetTx.decimalPoint = 0; //alway 0
        assetTx.type = 0;
    } else {
        assetTx.isUnique = false;

        const UniValue &type = find_value(asset, "type");
        if (!type.isNull()) {
            assetTx.type = type.get_int();
        } else {
            assetTx.type = 0;
        }

        const UniValue &decimalpoint = find_value(asset, "decimalpoint");
        if (decimalpoint.isNull()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: decimalpoint not found");
        }
        int dp = decimalpoint.get_int();
        if (dp >= 0 && dp <= 8) {
            assetTx.decimalPoint = decimalpoint.get_int();
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: decimalpoint out off range. Valid value 0 to 8.");
        }
    }

    const UniValue &amount = find_value(asset, "amount");
    if (!amount.isNull()) {
        CAmount a = amount.get_int64();
        if (a <= 0 || a > MAX_MONEY || (assetTx.isUnique && a > 500 * COIN))
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");
        assetTx.amount = a * COIN;
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: missing amount");
    }

    const UniValue &maxMintCount = find_value(asset, "maxMintCount");
    if (!maxMintCount.isNull()) {
        uint16_t a = maxMintCount.get_int64();
        if (a <= 0 || a > MAX_UNIQUE_ID)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid maxMintCount");
        assetTx.maxMintCount = a;
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: missing maxMintCount");
    }

    CTransactionRef newTx;
    CAmount nFee;
    int nChangePos = -1;
    std::string strFailReason;
    std::vector <CRecipient> vecSend;
    CCoinControl coinControl;
    assetTx.fee = getAssetsFees();
    int Payloadsize;

    if (!pwallet->CreateTransaction(vecSend, newTx, nFee, nChangePos, strFailReason, coinControl, true, Payloadsize,
                                    nullptr, &assetTx)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, strFailReason);
    }

    pwallet->CommitTransaction(newTx, {}, {});

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", newTx->GetHash().GetHex());
    result.pushKV("Name", assetTx.name);
    result.pushKV("Isunique", assetTx.isUnique);
    result.pushKV("Updatable", assetTx.updatable);
    result.pushKV("Decimalpoint", (int) assetTx.decimalPoint);
    result.pushKV("ReferenceHash", assetTx.referenceHash);
    result.pushKV("MaxMintCount", assetTx.maxMintCount);
    result.pushKV("ownerAddress", EncodeDestination(assetTx.ownerAddress));
    result.pushKV("fee", assetTx.fee);
    UniValue dist(UniValue::VOBJ);
    dist.pushKV("Type", GetDistributionType(assetTx.type));
    dist.pushKV("TargetAddress", EncodeDestination(assetTx.targetAddress));
    //dist.pushKV("collateralAddress", EncodeDestination(assetTx.collateralAddress));
    dist.pushKV("IssueFrequency", assetTx.issueFrequency);
    dist.pushKV("Amount", assetTx.amount / COIN);
    result.pushKV("Distribution", dist);

    return result;
}

UniValue mintAsset(const JSONRPCRequest &request) {
    if (request.fHelp || !Params().IsAssetsActive(::ChainActive().Tip()) || request.params.size() < 1 ||
        request.params.size() > 1)
        throw std::runtime_error(
                "mintAsset txid\n"
                "Mint assset\n"
                "\nArguments:\n"
                "1. \"txid\"               (string, required) asset txid reference\n"
                "\nResult:\n"
                "\"txid\"                  (string) The transaction id for the new issued asset\n"

                "\nExamples:\n"
                + HelpExampleCli("mintAsset", "773cf7e057127048711d16839e4612ffb0f1599aef663d96e60f5190eb7de9a9")
                + HelpExampleCli("mintAsset",
                                 "773cf7e057127048711d16839e4612ffb0f1599aef663d96e60f5190eb7de9a9" "yZBvV16YFvPx11qP2XhCRDi7y2e1oSMpKH" "1000")

        );

    std::shared_ptr <CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet *const pwallet = wallet.get();

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK2(cs_main, mempool.cs);
    LOCK(pwallet->cs_wallet);

    if (pwallet->GetBroadcastTransactions() && !pwallet->chain().p2pEnabled()) {
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
    }

    std::string assetId = request.params[0].get_str();

    // get asset metadadta
    CAssetMetaData tmpAsset;
    if (!passetsCache->GetAssetMetaData(assetId, tmpAsset)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: Asset asset metadata not found");
    }

    if (tmpAsset.mintCount >= tmpAsset.maxMintCount) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: max mint count reached");
    }

    //check on mempool if have a mint tx for this asset
    if (mempool.CheckForMintAssetConflict(tmpAsset.assetId)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: Already exist on mempool");
    }

    CMintAssetTx mintAsset;
    mintAsset.assetId = tmpAsset.assetId;
    mintAsset.fee = getAssetsFees();

    CTxDestination ownerAddress = CTxDestination(tmpAsset.ownerAddress);
    if (!IsValidDestination(ownerAddress)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");
    }
    CCoinControl coinControl;

    coinControl.destChange = ownerAddress;
    coinControl.fRequireAllInputs = false;

    std::vector <COutput> vecOutputs;
    pwallet->AvailableCoins(vecOutputs);

    for (const auto &out: vecOutputs) {
        CTxDestination txDest;
        if (ExtractDestination(out.tx->tx->vout[out.i].scriptPubKey, txDest) && txDest == ownerAddress) {
            coinControl.Select(COutPoint(out.tx->tx->GetHash(), out.i));
        }
    }

    if (!coinControl.HasSelected()) {
        throw JSONRPCError(RPC_INTERNAL_ERROR,
                           strprintf("No funds at specified address %s", EncodeDestination(ownerAddress)));
    }


    CTransactionRef wtx;
    CAmount nFee;
    int nChangePos = -1;
    std::string strFailReason;
    std::vector <CRecipient> vecSend;

    if (tmpAsset.isUnique) {
        uint32_t endid = (tmpAsset.circulatingSupply + tmpAsset.amount) / COIN;
        //build unique outputs using current supply as start unique id
        for (int id = tmpAsset.circulatingSupply / COIN; id < endid; ++id) {
            // Get the script for the target address
            CScript scriptPubKey = GetScriptForDestination(
                    DecodeDestination(EncodeDestination(tmpAsset.targetAddress)));
            // Update the scriptPubKey with the transfer asset information
            CAssetTransfer assetTransfer(tmpAsset.assetId, 1 * COIN, id);
            assetTransfer.BuildAssetTransaction(scriptPubKey);

            CRecipient recipient = {scriptPubKey, 0, false};
            vecSend.push_back(recipient);
        }
    } else {
        // Get the script for the target address
        CScript scriptPubKey = GetScriptForDestination(DecodeDestination(EncodeDestination(tmpAsset.targetAddress)));
        // Update the scriptPubKey with the transfer asset information
        CAssetTransfer assetTransfer(tmpAsset.assetId, tmpAsset.amount);
        assetTransfer.BuildAssetTransaction(scriptPubKey);

        CRecipient recipient = {scriptPubKey, 0, false};
        vecSend.push_back(recipient);
    }

    int Payloadsize;
    if (!pwallet->CreateTransaction(vecSend, wtx, nFee, nChangePos, strFailReason, coinControl, true, Payloadsize,
                                    nullptr, nullptr, &mintAsset)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, strFailReason);
    }

    pwallet->CommitTransaction(wtx, {}, {});

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", wtx->GetHash().GetHex());

    return result;
}

UniValue sendasset(const JSONRPCRequest &request) {
    if (request.fHelp || !Params().IsAssetsActive(::ChainActive().Tip()) || request.params.size() < 3 ||
        request.params.size() > 7)
        throw std::runtime_error(
                "sendasset \"asset_id\" qty \"to_address\" \"change_address\" \"asset_change_address\"\n"
                "\nTransfers a quantity of an owned asset to a given address"

                "\nArguments:\n"
                "1. \"asset_id\"                (string, required) asset hash id or asset name\n"
                "2. \"qty/uniqueid\"            (numeric, required) number of assets you want to send to the address / unique asset identifier\n"
                "3. \"to_address\"              (string, required) address to send the asset to\n"
                "4. \"change_address\"          (string, optional, default = \"\") the transactions RTM change will be sent to this address\n"
                "5. \"asset_change_address\"    (string, optional, default = \"\") the transactions Asset change will be sent to this address\n"

                "\nResult:\n"
                "txid"
                "[ \n"
                "txid\n"
                "]\n"

                "\nExamples:\n"
                + HelpExampleCli("transfer", "\"ASSET_NAME\" 20 \"address\"")
                + HelpExampleCli("transfer", "\"ASSET_NAME\" 20 \"address\"")
        );

    std::shared_ptr <CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet *const pwallet = wallet.get();

    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    CAmount curBalance = pwallet->GetBalance().m_mine_trusted;
    if (curBalance == 0) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                           "Error: This wallet doesn't contain any RTM, transfering an asset requires a network fee");
    }

    // get asset metadadta
    CAssetMetaData tmpAsset;
    if (!passetsCache->GetAssetMetaData(request.params[0].get_str(), tmpAsset)) { //check if the asset exist
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: Asset not found");
    }
    std::string assetId = tmpAsset.assetId;

    std::string to_address = request.params[2].get_str();
    CTxDestination to_dest = DecodeDestination(to_address);
    if (!IsValidDestination(to_dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Raptoreum address: ") + to_address);
    }

    CAmount nAmount = 0;
    uint32_t unique_identifier = MAX_UNIQUE_ID;

    if (tmpAsset.isUnique) {
        nAmount = 1 * COIN;
        UniValue value = request.params[1];
        if (!value.isNum() && !value.isStr())
            throw std::runtime_error("Amount is not a number or string");
        CAmount amount;
        if (!ParseFixedPoint(value.getValStr(), 0, &amount))
            throw std::runtime_error("Error: invalid unique identifier");
        uint32_t unique_identifier = amount & MAX_UNIQUE_ID;
        if (unique_identifier < 0 || unique_identifier >= tmpAsset.circulatingSupply / COIN)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: invalid unique identifier");
    } else {
        nAmount = AmountFromValue(request.params[1]);
        if (nAmount <= 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: invalid amount");
        }
        if (!validateAmount(nAmount, tmpAsset.decimalPoint)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: invalid amount");
        }
    }

    std::map <std::string, std::vector<COutput>> mapAssetCoins;
    pwallet->AvailableAssets(mapAssetCoins);

    if (!mapAssetCoins.count(assetId))
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Wallet doesn't have asset: %s", assetId));


    std::string change_address = "";
    if (request.params.size() > 3) {
        change_address = request.params[3].get_str();
    }

    std::string asset_change_address = "";
    if (request.params.size() > 4) {
        asset_change_address = request.params[4].get_str();
    }

    CTxDestination change_dest = DecodeDestination(change_address);
    if (!change_address.empty() && !IsValidDestination(change_dest))
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           std::string("RTM change address must be a valid address. Invalid address: ") +
                           change_address);

    CTxDestination asset_change_dest = DecodeDestination(asset_change_address);
    if (!asset_change_address.empty() && !IsValidDestination(asset_change_dest))
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           std::string("Asset change address must be a valid address. Invalid address: ") +
                           asset_change_address);

    CCoinControl coinControl;
    coinControl.destChange = change_dest;
    coinControl.assetDestChange = asset_change_dest;

    CTransactionRef wtx;
    CAmount nFee;
    int nChangePos = -1;
    std::string strFailReason;
    std::vector <CRecipient> vecSend;

    // Get the script for the destination address
    CScript scriptPubKey = GetScriptForDestination(to_dest);

    // Update the scriptPubKey with the transfer asset information
    if (tmpAsset.isUnique) {
        CAssetTransfer assetTransfer(assetId, nAmount, unique_identifier);
        assetTransfer.BuildAssetTransaction(scriptPubKey);
    } else {
        CAssetTransfer assetTransfer(assetId, nAmount);
        assetTransfer.BuildAssetTransaction(scriptPubKey);
    }

    CRecipient recipient = {scriptPubKey, 0, false};
    vecSend.push_back(recipient);

    if (!pwallet->CreateTransaction(vecSend, wtx, nFee, nChangePos, strFailReason, coinControl, true)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, strFailReason);
    }

    pwallet->CommitTransaction(wtx, {}, {});

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", wtx->GetHash().GetHex());
    return result;
}


UniValue assetdetails(const JSONRPCRequest &request) {
    if (request.fHelp || !Params().IsAssetsActive(::ChainActive().Tip()) || request.params.size() < 1 ||
        request.params.size() > 1)
        throw std::runtime_error(
                "assetdetails 'asset_id or asset_name'\n"
                "asset details\n"
                "\nArguments:\n"
                "1. \"asset_id\"                (string, required) asset name or txid reference\n"
                "\nResult:\n"
                "\"asset\"                      (string) The asset details\n"

                "\nExamples:\n"
                + HelpExampleCli("assetdetails", "773cf7e057127048711d16839e4612ffb0f1599aef663d96e60f5190eb7de9a9")
        );

    std::string name = request.params[0].get_str();
    std::string assetId = name;

    // try to get asset id in case asset name was used
    passetsCache->GetAssetId(name, assetId);

    // get asset metadadta
    CAssetMetaData tmpAsset;
    if (!passetsCache->GetAssetMetaData(assetId, tmpAsset)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: Asset asset metadata not found");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("Asset_id", tmpAsset.assetId);
    result.pushKV("Asset_name", tmpAsset.name);
    result.pushKV("Circulating_supply", tmpAsset.circulatingSupply / COIN);
    result.pushKV("MintCount", tmpAsset.mintCount);
    result.pushKV("maxMintCount", tmpAsset.maxMintCount);
    result.pushKV("owner", EncodeDestination(tmpAsset.ownerAddress));
    result.pushKV("Isunique", tmpAsset.isUnique);
    result.pushKV("Updatable", tmpAsset.updatable);
    result.pushKV("Decimalpoint", (int) tmpAsset.decimalPoint);
    result.pushKV("ReferenceHash", tmpAsset.referenceHash);
    UniValue dist(UniValue::VOBJ);
    dist.pushKV("Type", GetDistributionType(tmpAsset.type));
    dist.pushKV("TargetAddress", EncodeDestination(tmpAsset.targetAddress));
    //tmp.pushKV("collateralAddress", EncodeDestination(tmpAsset.collateralAddress));
    dist.pushKV("IssueFrequency", tmpAsset.issueFrequency);
    dist.pushKV("Amount", tmpAsset.amount / COIN);
    result.pushKV("Distribution", dist);

    return result;
}

UniValue listassetsbalance(const JSONRPCRequest &request) {
    if (request.fHelp || !Params().IsAssetsActive(::ChainActive().Tip()) || request.params.size() > 0)
        throw std::runtime_error(
                "listassetsbalance\n"
                "\nResult:\n"
                "\"asset\"                      (string) list assets balance\n"

                "\nExamples:\n"
                + HelpExampleCli("assetbalance", "")
        );

    std::shared_ptr <CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet *const pwallet = wallet.get();

    LOCK2(cs_main, pwallet->cs_wallet);

    std::map <std::string, std::vector<COutput>> mapAssetCoins;
    pwallet->AvailableAssets(mapAssetCoins);

    std::map <std::string, CAmount> mapAssetbalance;
    for (auto asset: mapAssetCoins) {
        std::cout << asset.first << std::endl;
        CAmount balance = 0;
        for (auto output: asset.second) {
            CInputCoin coin(output.tx->tx, output.i);

            if (!coin.txout.scriptPubKey.IsAssetScript()) {
                continue;
            }

            CAssetTransfer transferTemp;
            if (!GetTransferAsset(coin.txout.scriptPubKey, transferTemp))
                continue;
            balance += transferTemp.nAmount;
        }
        mapAssetbalance.insert(std::make_pair(asset.first, balance));
    }

    UniValue result(UniValue::VOBJ);
    for (auto asset: mapAssetbalance) {
        // get asset metadadta
        std::string assetName = "db error";
        CAssetMetaData tmpAsset;
        if (passetsCache->GetAssetMetaData(asset.first, tmpAsset)) { //check if the asset exist
            assetName = tmpAsset.name;
        }
        UniValue tmp(UniValue::VOBJ);
        tmp.pushKV("Asset_Id", asset.first);
        tmp.pushKV("Balance", asset.second / COIN);
        result.pushKV(assetName, tmp);
    }

    return result;
}

UniValue listunspentassets(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 5)
        throw std::runtime_error(
            "listunspentassets ( minconf maxconf  [\"addresses\",...] [include_unsafe] [query_options])\n"
            "\nReturns array of unspent transaction outputs\n"
            "with between minconf and maxconf (inclusive) confirmations.\n"
            "Optionally filter to only include txouts paid to specified addresses.\n"
            "\nArguments:\n"
            "1. minconf          (numeric, optional, default=1) The minimum confirmations to filter\n"
            "2. maxconf          (numeric, optional, default=9999999) The maximum confirmations to filter\n"
            "3. \"addresses\"      (string) A json array of raptoreum addresses to filter\n"
            "    [\n"
            "      \"address\"     (string) raptoreum address\n"
            "      ,...\n"
            "    ]\n"
            "4. include_unsafe (bool, optional, default=true) Include outputs that are not safe to spend\n"
            "                  See description of \"safe\" attribute below.\n"
            "5. query_options    (json, optional) JSON with query options\n"
            "    {\n"
            "      \"minimumAmount\"    (numeric or string, default=0) Minimum value of each UTXO in " + CURRENCY_UNIT + "\n"
            "      \"maximumAmount\"    (numeric or string, default=unlimited) Maximum value of each UTXO in " + CURRENCY_UNIT + "\n"
            "      \"maximumCount\"     (numeric or string, default=unlimited) Maximum number of UTXOs\n"
            "      \"minimumSumAmount\" (numeric or string, default=unlimited) Minimum sum value of all UTXOs in " + CURRENCY_UNIT + "\n"
            "      \"coinType\"         (numeric, default=0) Filter coinTypes as follows:\n"
            "                         0=ALL_COINS, 1=ONLY_FULLY_MIXED, 2=ONLY_READY_TO_MIX, 3=ONLY_NONDENOMINATED,\n"
            "                         4=ONLY_SMARTNODE_COLLATERAL, 5=ONLY_COINJOIN_COLLATERAL\n"
            "    }\n"
            "\nResult\n"
            "[                   (array of json object)\n"
            "  {\n"
            "    \"txid\" : \"txid\",          (string) the transaction id \n"
            "    \"vout\" : n,               (numeric) the vout value\n"
            "    \"address\" : \"address\",    (string) the raptoreum address\n"
            "    \"label\" : \"label\",        (string) The associated label, or \"\" for the default label\n"
            "    \"account\" : \"account\",    (string) DEPRECATED. This field will be removed in V0.18. To see this deprecated field, start raptoreumd with -deprecatedrpc=accounts. The associated account, or \"\" for the default account\n"
            "    \"scriptPubKey\" : \"key\",   (string) the script key\n"
            "    \"amount\" : x.xxx,         (numeric) the transaction output amount in " + CURRENCY_UNIT + "\n"
            "    \"confirmations\" : n,      (numeric) The number of confirmations\n"
            "    \"redeemScript\" : n        (string) The redeemScript if scriptPubKey is P2SH\n"
            "    \"spendable\" : xxx,        (bool) Whether we have the private keys to spend this output\n"
            "    \"solvable\" : xxx,         (bool) Whether we know how to spend this output, ignoring the lack of keys\n"
            "    \"safe\" : xxx              (bool) Whether this output is considered safe to spend. Unconfirmed transactions\n"
            "                              from outside keys and unconfirmed replacement transactions are considered unsafe\n"
            "                              and are not eligible for spending by fundrawtransaction and sendtoaddress.\n"
            "    \"coinjoin_rounds\" : n     (numeric) The number of CoinJoin rounds\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n"
            + HelpExampleCli("listunspentassets", "")
            + HelpExampleCli("listunspentassets", "6 9999999 \"[\\\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\\\",\\\"XuQQkwA4FYkq2XERzMY2CiAZhJTEDAbtcg\\\"]\"")
            + HelpExampleRpc("listunspentassets", "6, 9999999 \"[\\\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\\\",\\\"XuQQkwA4FYkq2XERzMY2CiAZhJTEDAbtcg\\\"]\"")
            + HelpExampleCli("listunspentassets", "6 9999999 '[]' true '{ \"minimumAmount\": 0.005 }'")
            + HelpExampleRpc("listunspentassets", "6, 9999999, [] , true, { \"minimumAmount\": 0.005 } ")
        );

    std::shared_ptr <CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet *const pwallet = wallet.get();

    int nMinDepth = 1;
    if (!request.params[0].isNull()) {
        RPCTypeCheckArgument(request.params[0], UniValue::VNUM);
        nMinDepth = request.params[0].get_int();
    }

    int nMaxDepth = 9999999;
    if (!request.params[1].isNull()) {
        RPCTypeCheckArgument(request.params[1], UniValue::VNUM);
        nMaxDepth = request.params[1].get_int();
    }

    std::set<CTxDestination> destinations;
    if (!request.params[2].isNull()) {
        RPCTypeCheckArgument(request.params[2], UniValue::VARR);
        UniValue inputs = request.params[2].get_array();
        for (unsigned int idx = 0; idx < inputs.size(); idx++) {
            const UniValue& input = inputs[idx];
            CTxDestination dest = DecodeDestination(input.get_str());
            if (!IsValidDestination(dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Raptoreum address: ") + input.get_str());
            }
            if (!destinations.insert(dest).second) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + input.get_str());
            }
        }
    }

    bool include_unsafe = true;
    if (!request.params[3].isNull()) {
        RPCTypeCheckArgument(request.params[3], UniValue::VBOOL);
        include_unsafe = request.params[3].get_bool();
    }

    CAmount nMinimumAmount = 0;
    CAmount nMaximumAmount = MAX_MONEY;
    CAmount nMinimumSumAmount = MAX_MONEY;
    uint64_t nMaximumCount = 0;
    CCoinControl coinControl;
    coinControl.nCoinType = CoinType::ALL_COINS;

    if (!request.params[4].isNull()) {
        const UniValue& options = request.params[4].get_obj();

        // Note: Keep this vector up to date with the options processed below
        const std::vector<std::string> vecOptions {
            "minimumAmount",
            "maximumAmount",
            "minimumSumAmount",
            "maximumCount"
        };

        for (const auto& key : options.getKeys()) {
            if (std::find(vecOptions.begin(), vecOptions.end(), key) == vecOptions.end()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid key used in query_options JSON object: ") + key);
            }
        }

        if (options.exists("minimumAmount"))
            nMinimumAmount = AmountFromValue(options["minimumAmount"]);

        if (options.exists("maximumAmount"))
            nMaximumAmount = AmountFromValue(options["maximumAmount"]);

        if (options.exists("minimumSumAmount"))
            nMinimumSumAmount = AmountFromValue(options["minimumSumAmount"]);

        if (options.exists("maximumCount"))
            nMaximumCount = options["maximumCount"].get_int64();

    }

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    UniValue results(UniValue::VARR);
    std::map<std::string, std::vector<COutput> > mapAssetCoins;
    LOCK2(cs_main, pwallet->cs_wallet);

    pwallet->AvailableAssets(mapAssetCoins, !include_unsafe, &coinControl, nMinimumAmount, nMaximumAmount, nMinimumSumAmount, nMaximumCount, nMinDepth, nMaxDepth);
    for (const auto asset : mapAssetCoins) {
        for (auto output : asset.second){
            CInputCoin coin(output.tx->tx, output.i);

            CTxDestination address;
            const CScript& scriptPubKey = coin.txout.scriptPubKey;
            bool fValidAddress = ExtractDestination(scriptPubKey, address);

            if (destinations.size() && (!fValidAddress || !destinations.count(address)))
                continue;

            if (!coin.txout.scriptPubKey.IsAssetScript()) {
                continue;
            }

            CAssetTransfer transferTemp;
            if (!GetTransferAsset(scriptPubKey, transferTemp))
                continue;
         
            UniValue entry(UniValue::VOBJ);
            entry.pushKV("txid", output.tx->GetHash().GetHex());
            entry.pushKV("vout", output.i);

            if (fValidAddress) {
                entry.pushKV("address", EncodeDestination(address));

                if (pwallet->mapAddressBook.count(address)) {
                    entry.pushKV("label", pwallet->mapAddressBook[address].name);
                    if (IsDeprecatedRPCEnabled("accounts")) {
                        entry.pushKV("account", pwallet->mapAddressBook[address].name);
                    }
                }
            }

            entry.pushKV("assetId", transferTemp.assetId);
            if (transferTemp.isUnique)
            entry.pushKV("uniqueId", (int64_t)transferTemp.uniqueId);
            entry.pushKV("amount", transferTemp.nAmount);
            entry.pushKV("scriptPubKey", HexStr(scriptPubKey));
            entry.pushKV("confirmations", output.nDepth);
            entry.pushKV("spendable", output.fSpendable);
            entry.pushKV("solvable", output.fSolvable);
            entry.pushKV("future", output.isFuture);
            entry.pushKV("futureSpendable", output.isFutureSpendable);
            entry.pushKV("safe", output.fSafe);
            results.push_back(entry);
        }
    }

    return results;
}

static const CRPCCommand commands[] =
        { //  category              name                      actor (function)
                //  --------------------- ------------------------  -----------------------
                {"assets", "createasset",       &createasset,       {"asset"}},
                {"assets", "mintAsset",         &mintAsset,         {"assetId"}},
                {"assets", "sendasset",         &sendasset,         {"assetId", "amount", "address", "change_address", "asset_change_address"}},
                {"assets", "assetdetails",      &assetdetails,      {"assetId"}},
                {"assets", "listassetsbalance", &listassetsbalance, {}},
                {"assets", "listunspentassets", &listunspentassets, {"minconf","maxconf","addresses","include_unsafe","query_options"}},
        };

void RegisterAssetsRPCCommands(CRPCTable &tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}