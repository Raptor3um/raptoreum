// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assets/assets.h"
#include <assets/assetstype.h>
#include <rpc/server.h>
#include "chain.h"
#include "validation.h"
#include <txmempool.h>

#ifdef ENABLE_WALLET
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>
#include <wallet/rpcwallet.h>
#endif//ENABLE_WALLET

#include <evo/specialtx.h>
#include <evo/providertx.h>

#include <iostream>


static CKeyID ParsePubKeyIDFromAddress(const std::string& strAddress, const std::string& paramName)
{
    CTxDestination dest = DecodeDestination(strAddress);
    const CKeyID *keyID = boost::get<CKeyID>(&dest);
    if (!keyID) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("%s must be a valid P2PKH address, not %s", paramName, strAddress));
    }
    return *keyID;
}

UniValue createasset(const JSONRPCRequest& request)
{

    if (request.fHelp || !Params().IsAssetsActive(chainActive.Tip()) || request.params.size() < 1 || request.params.size() > 2)
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
            "   \"type:\"               (numeric) ditribution type manual=0, coinbase=1, address=2, schedule=3\n"
            "   \"targetAddress:\"      (string) address to be issued to when asset issue transaction is created.\n"
            "   \"issueFrequency:\"     (numeric) mint specific amount of token every x blocks\n"
            "   \"amount:\"             (numeric) amount to distribute each time if type is not manual.\n"
            "   \"ownerAddress:\"       (string) address that this asset is owned by. Only key holder of this address will be able to mint new tokens\n"
            "}\n"
            "\nResult:\n"
            "\"txid\"                   (string) The transaction id for the new asset\n"

            "\nExamples:\n"
            + HelpExampleCli("createasset", "'{\"name\":\"test asset\", \"updatable\":true, \"isunique\":false,\n"
                "\"decimalpoint\":2, \"referenceHash\":\"\", \"type\":0, \"targetAddress\":\"yQPzaDmnF3FtRsoWijUN7aZDcEdyNAcmVk\",\n"
                "\"issueFrequency\":0, \"amount\":10000,\"ownerAddress\":\"yRyiTCKfqMG2dQ9oUvs932TjN1R1MNUTWM\"}'")
        );

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }
    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK2(cs_main, mempool.cs);
    LOCK(pwallet->cs_wallet);

    if (pwallet->GetBroadcastTransactions() && !g_connman) {
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
    }
    std::cout << "get asset data " << endl;
    UniValue asset = request.params[0].get_obj();
    std::cout << "Found txid " << endl;
    const UniValue& name = find_value(asset, "name");
    std::cout << name.getValStr() << endl;
    std::string assetname = name.getValStr();
    
    //check if asset name is valid
    if(!IsAssetNameValid(assetname)){
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: Invalid asset name");
    }
    // check if asset already exist
    CAssetMetaData tmpasset;
    if(GetAssetMetaData(assetname, tmpasset)){
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: Asset already exist");
    }
    CNewAssetTx assettx;
    assettx.Name = assetname;
    const UniValue& updatable = find_value(asset, "updatable");
    if(!updatable.isNull()){
        assettx.updatable = updatable.get_bool();    
    }else{
        assettx.updatable = true;
    }

    const UniValue& is_unique = find_value(asset, "isunique");
    if(!is_unique.isNull()){
        assettx.isUnique = is_unique.get_bool();    
    }else{
        assettx.isUnique = false;
    }

    if (assettx.isUnique){
        assettx.decimalPoint = 0; //alway 0
    } else {
        const UniValue& decimalpoint = find_value(asset, "decimalpoint");
        if(decimalpoint.isNull()){
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: decimalpoint not found");    
        }
        int dp = decimalpoint.get_int();
        if(dp >= 0 && dp <= 8){
            assettx.decimalPoint = decimalpoint.get_int();    
        }else{
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: decimalpoint out off range. Valid value 0 to 8.");
        }
    }

    const UniValue& referenceHash = find_value(asset, "referenceHash");
    if(!referenceHash.isNull()){
        assettx.referenceHash = referenceHash.get_str();    
    }

    const UniValue& type = find_value(asset, "type");
    if(!type.isNull()){
        assettx.type = type.get_int();    
    }else{
        assettx.type = 0;   
    }

    const UniValue& targetAddress = find_value(asset, "targetAddress");
    if(!targetAddress.isNull()){
        std::cout << targetAddress.get_str() << std::endl;
        assettx.targetAddress = ParsePubKeyIDFromAddress(targetAddress.get_str(), "targetAddress");;    
    }else{
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: missing targetAddress");   
    }

    const UniValue& amount = find_value(asset, "amount");
    if(!amount.isNull()){
        CAmount a = amount.get_int64();
        if ( a <= 0)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");
        assettx.Amount = a * COIN;    
    }else{
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: missing amount");   
    }

    const UniValue& ownerAddress = find_value(asset, "ownerAddress");
    if(!ownerAddress.isNull()){
        assettx.ownerAddress = ParsePubKeyIDFromAddress(ownerAddress.get_str(), "ownerAddress");    
    }else{
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: missing ownerAddress");   
    }

    std::cout << "Create asset tx" << endl;

    if (request.params[1].isNull()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Raptoreum address: ") + request.params[1].get_str());
    }

    CTxDestination fundDest = DecodeDestination(request.params[1].get_str());
    if (!IsValidDestination(fundDest))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Raptoreum address: ") + request.params[1].get_str());
    
    CTransactionRef newTx;
    CReserveKey reservekey(pwallet);
    CAmount nFee;
    int nChangePos = -1;
    std::string strFailReason;
    std::vector<CRecipient> vecSend;
    CCoinControl coinControl;
    assettx.fee = getAssetsFees();
    int Payloadsize;

    if (!pwallet->CreateTransaction(vecSend, newTx, reservekey, nFee, nChangePos, strFailReason, coinControl, true, Payloadsize,nullptr, &assettx)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, strFailReason);
    }
    
    CValidationState state;
    if (!pwallet->CommitTransaction(newTx, {}, {}, {}, reservekey, g_connman.get(), state)) {
        strFailReason = strprintf("Error: The transaction was rejected! Reason given: %s", FormatStateMessage(state));
        throw JSONRPCError(RPC_WALLET_ERROR, strFailReason);
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", newTx->GetHash().GetHex());
    result.pushKV("Name", assettx.Name);
    result.pushKV("Isunique", assettx.isUnique); 
    result.pushKV("Updatable", assettx.updatable);  
    result.pushKV("Decimalpoint", (int) assettx.decimalPoint);
    result.pushKV("ReferenceHash", assettx.referenceHash);
    result.pushKV("fee", assettx.fee);
    result.pushKV("Type", assettx.type);
    result.pushKV("TargetAddress", EncodeDestination(assettx.targetAddress));
    result.pushKV("ownerAddress", EncodeDestination(assettx.ownerAddress));
    //result.pushKV("collateralAddress", EncodeDestination(assettx.collateralAddress));
    result.pushKV("IssueFrequency", assettx.issueFrequency);
    result.pushKV("Amount", assettx.Amount / COIN);
    
    return result;
}

UniValue mintasset(const JSONRPCRequest& request)
{
    if (request.fHelp || !Params().IsAssetsActive(chainActive.Tip()) || request.params.size() < 1 || request.params.size() > 3)
        throw std::runtime_error(
            "mintasset txid address amount\n"
            "Mint assset\n"
            "\nArguments:\n"
            "1. \"txid\"               (string, required) asset txid reference\n"
            "2. \"address\"            (string, only if type is manual) address to where asset will be issued\n"
            "3. \"amount\"             (string, only if type is manual) Amount of issue asset\n"
            "\nResult:\n"
            "\"txid\"                  (string) The transaction id for the new issued asset\n"

            "\nExamples:\n"
            + HelpExampleCli("mintasset", "773cf7e057127048711d16839e4612ffb0f1599aef663d96e60f5190eb7de9a9")
            + HelpExampleCli("mintasset", "773cf7e057127048711d16839e4612ffb0f1599aef663d96e60f5190eb7de9a9" "yZBvV16YFvPx11qP2XhCRDi7y2e1oSMpKH" "1000")
        
        );

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }
    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    LOCK2(cs_main, mempool.cs);
    LOCK(pwallet->cs_wallet);

    if (pwallet->GetBroadcastTransactions() && !g_connman) {
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");
    }
  
    std::string assetid = request.params[0].get_str();
    
    // get asset metadadta
    CAssetMetaData tmpasset;
    if(!GetAssetMetaData(assetid, tmpasset)){
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: Asset asset metadata not found");
    }
    CMintAssetTx mintasset;
    mintasset.AssetId = tmpasset.assetId;
    mintasset.fee = getAssetsFees();
  
    CTxDestination ownerAddress = CTxDestination(tmpasset.ownerAddress);
    if (!IsValidDestination(ownerAddress)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");
    }
    CCoinControl coinControl;

    coinControl.destChange = ownerAddress;
    coinControl.fRequireAllInputs = false;

    std::vector<COutput> vecOutputs;
    pwallet->AvailableCoins(vecOutputs);

    for (const auto& out : vecOutputs) {
        CTxDestination txDest;
        if (ExtractDestination(out.tx->tx->vout[out.i].scriptPubKey, txDest) && txDest == ownerAddress) {
            coinControl.Select(COutPoint(out.tx->tx->GetHash(), out.i));
        }
    }

    if (!coinControl.HasSelected()) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("No funds at specified address %s", EncodeDestination(ownerAddress)));
    }

    
    CTransactionRef wtx;
    CReserveKey reservekey(pwallet);
    CAmount nFee;
    int nChangePos = -1;
    std::string strFailReason;
    std::vector<CRecipient> vecSend;
   
    // Get the script for the target address
    CScript scriptPubKey = GetScriptForDestination(DecodeDestination(EncodeDestination(tmpasset.targetAddress)));

    // Update the scriptPubKey with the transfer asset information
    CAssetTransfer assetTransfer(tmpasset.assetId, tmpasset.Amount);
    assetTransfer.BuildAssetTransaction(scriptPubKey);

    CRecipient recipient = {scriptPubKey, 0, false};
    vecSend.push_back(recipient);   

    int Payloadsize;        
    if (!pwallet->CreateTransaction(vecSend, wtx, reservekey, nFee, nChangePos, strFailReason, coinControl, true, Payloadsize, nullptr, nullptr, &mintasset)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, strFailReason);
    }

    CValidationState state;
    if (!pwallet->CommitTransaction(wtx, {}, {}, {}, reservekey, g_connman.get(), state)) {
        strFailReason = strprintf("Error: The transaction was rejected! Reason given: %s", FormatStateMessage(state));
        throw JSONRPCError(RPC_WALLET_ERROR, strFailReason);
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", wtx->GetHash().GetHex());

    return result;
}

UniValue sendasset(const JSONRPCRequest& request)
{
    if (request.fHelp || !Params().IsAssetsActive(chainActive.Tip()) || request.params.size() < 3 || request.params.size() > 7)
        throw std::runtime_error(
                "sendasset \"asst_id\" qty \"to_address\" \"change_address\" \"asset_change_address\"\n"
                "\nTransfers a quantity of an owned asset to a given address"

                "\nArguments:\n"
                "1. \"asset_id\"                (string, required) name of asset\n"
                "2. \"qty\"                     (numeric, required) number of assets you want to send to the address\n"
                "3. \"to_address\"              (string, required) address to send the asset to\n"
                "4. \"change_address\"          (string, optional, default = \"\") the transactions RVN change will be sent to this address\n"
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

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }
 
    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    CAmount curBalance = pwallet->GetBalance();
    if (curBalance == 0) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Error: This wallet doesn't contain any RTM, transfering an asset requires a network fee");
    }

    // get asset metadadta
    CAssetMetaData tmpasset;
    if(!GetAssetMetaData(request.params[0].get_str(), tmpasset)){ //check if the asset exist
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: Asset not found");
    }
    std::string assetId = tmpasset.assetId;

    CAmount nAmount = AmountFromValue(request.params[1]);
    if (nAmount <= 0){
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: invalid amount");
    }

    std::map<std::string, std::vector<COutput> > mapAssetCoins;
    pwallet->AvailableAssets(mapAssetCoins);

    if (!mapAssetCoins.count(assetId))
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Wallet doesn't have asset: %s", assetId));

    std::string to_address = request.params[2].get_str();
    CTxDestination to_dest = DecodeDestination(to_address);
    if (!IsValidDestination(to_dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Raptoreum address: ") + to_address);
    }

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
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("RTM change address must be a valid address. Invalid address: ") + change_address);

    CTxDestination asset_change_dest = DecodeDestination(asset_change_address);
    if (!asset_change_address.empty() && !IsValidDestination(asset_change_dest))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Asset change address must be a valid address. Invalid address: ") + asset_change_address);

    CCoinControl coinControl;
    coinControl.destChange = change_dest;
    coinControl.assetDestChange = asset_change_dest;

    CTransactionRef wtx;
    CReserveKey reservekey(pwallet);
    CAmount nFee;
    int nChangePos = -1;
    std::string strFailReason;
    std::vector<CRecipient> vecSend;
   
    // Get the script for the destination address
    CScript scriptPubKey = GetScriptForDestination(to_dest);

    // Update the scriptPubKey with the transfer asset information
    CAssetTransfer assetTransfer(assetId, nAmount);
    assetTransfer.BuildAssetTransaction(scriptPubKey);

    CRecipient recipient = {scriptPubKey, 0, false};
    vecSend.push_back(recipient);   
       
    if (!pwallet->CreateTransaction(vecSend, wtx, reservekey, nFee, nChangePos, strFailReason, coinControl, true)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, strFailReason);
    }

    CValidationState state;
    if (!pwallet->CommitTransaction(wtx, {}, {}, {}, reservekey, g_connman.get(), state)) {
        strFailReason = strprintf("Error: The transaction was rejected! Reason given: %s", FormatStateMessage(state));
        throw JSONRPCError(RPC_WALLET_ERROR, strFailReason);
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", wtx->GetHash().GetHex());
    return result;
}


static const CRPCCommand commands[] =
{ //  category              name                      actor (function)
  //  --------------------- ------------------------  -----------------------
    { "assets",                "createasset",         &createasset,                   {"asset"}  },
    { "assets",                "mintasset",           &mintasset,                     {"asset"}  },
    { "assets",                "sendasset",           &sendasset,                     {"asset", "amount", "address", "change_address", "asset_change_address"}  },
};

void RegisterAssetsRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}