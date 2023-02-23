// Copyright (c) 2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <assets/assets.h>
#include <assets/assetstype.h>
#include <wallet/wallet.h>
#include <spork.h>
#include <evo/specialtx.h>
#include <evo/providertx.h>
#include <chainparams.h>
#include <validation.h>
#include <regex>

static const std::regex name_characters("^[a-zA-Z0-9 ]{3,}$");
static const std::regex rtm_names("^RTM$|^RAPTOREUM$|^wRTM$|^WRTM$|^RTMcoin$|^RTMCOIN$");

bool IsAssetNameValid(std::string name){ 
    if (name.length() < 3 || name.length() > 128) return false;
    return std::regex_match(name, name_characters) && !std::regex_match(name, rtm_names);

}

CAmount getAssetsFeesCoin() {
	return getAssetsFees() * COIN;
}

uint16_t getAssetsFees() {
    if(!sporkManager.IsSporkActive(SPORK_22_SPECIAL_TX_FEE)) {
        return 0;
    }
    int64_t specialTxValue = sporkManager.GetSporkValue(SPORK_22_SPECIAL_TX_FEE);
    return specialTxValue >> 8 & 0xff;
}

bool GetAssetId(const CScript& script, std::string& assetId){
    CAssetTransfer assetTransfer;
    if(GetTransferAsset(script,assetTransfer)){
        assetId = assetTransfer.AssetId;
        return true;
    }
    return false;
}

//temporary memory cache
static std::map<std::string, CAssetMetaData> mapAsset;
static std::map<std::string, std::string> mapAssetid;

void InsertAsset(CNewAssetTx newasset, std::string assetid,int nheigth){
    CAssetMetaData assetdata;
    assetdata.assetId = assetid;
    assetdata.blockHeight = nheigth;
    assetdata.circulatingSuply = 0;
    assetdata.Name = newasset.Name;
    assetdata.updatable = newasset.updatable;
    assetdata.isunique = newasset.isUnique;
    assetdata.Decimalpoint = newasset.decimalPoint;
    assetdata.referenceHash = newasset.referenceHash;
    assetdata.fee = newasset.fee;
    assetdata.type = newasset.type;
    assetdata.targetAddress = newasset.targetAddress;
    assetdata.issueFrequency = newasset.issueFrequency;
    assetdata.Amount = newasset.Amount;
    assetdata.ownerAddress = newasset.ownerAddress;
    assetdata.collateralAddress = newasset.collateralAddress;

    mapAsset.insert(std::make_pair(assetid, assetdata));
    mapAssetid.insert(std::make_pair(assetdata.Name, assetid));    
}

bool UpdateAsset(CUpdateAssetTx upasset){
    CAssetMetaData assetdata;
    if(!GetAssetMetaData(upasset.AssetId, assetdata)){
        return false;
    }

    assetdata.updatable = upasset.updatable;
    assetdata.referenceHash = upasset.referenceHash;
    assetdata.type = upasset.type;
    assetdata.targetAddress = upasset.targetAddress;
    assetdata.issueFrequency = upasset.issueFrequency;
    assetdata.Amount = upasset.Amount;
    assetdata.ownerAddress = upasset.ownerAddress;
    assetdata.collateralAddress = upasset.collateralAddress;

    mapAsset[upasset.AssetId] = assetdata;
    return true;
}

bool UpdateAsset(std::string assetid, CAmount amount){
    if(mapAsset.count(assetid) > 0 ){
        mapAsset[assetid].circulatingSuply += amount;
        return true;
    }
    return false;
}

void AddAssets(const CTransaction& tx, int nHeight, bool check){
    if (Params().IsAssetsActive(chainActive.Tip())) {
        if (tx.nType == TRANSACTION_NEW_ASSET){
            CNewAssetTx assettx;
            if (GetTxPayload(tx, assettx)) {
                InsertAsset(assettx, tx.GetHash().ToString(), nHeight);
            }
        } else if (tx.nType == TRANSACTION_UPDATE_ASSET){
            CUpdateAssetTx assettx;
            if (GetTxPayload(tx, assettx)) {
                UpdateAsset(assettx);
            }
        } else if (tx.nType == TRANSACTION_MINT_ASSET){
            CMintAssetTx assettx;
            if (GetTxPayload(tx, assettx)) {
                CAmount amount = 0;
                for (auto out : tx.vout){
                    if (out.scriptPubKey.IsAssetScript()){
                        CAssetTransfer assetTransfer;
                        if(GetTransferAsset(out.scriptPubKey, assetTransfer))
                            amount += assetTransfer.nAmount;                
                    }
                }
                UpdateAsset(assettx.AssetId, amount); //update circulating suply
            }
        }
    }
}

bool CheckIfAssetExists(std::string name){
    if(mapAsset.count(name) > 0 ){
        return true;
    }
    return false;
}

bool GetAssetMetaData(std::string asetId, CAssetMetaData& asset){
    std::string id = asetId;
    auto itid = mapAssetid.find(asetId);//try to get assetid by asset name
    if( itid != mapAssetid.end() ) {
        id = itid->second;
    }
    auto it = mapAsset.find(id);
    if( it != mapAsset.end() ) {
        asset = it->second;
        return true;
    }
    return false;
}

bool GetAssetData(const CScript& script, CAssetOutputEntry& data)
{
    // Placeholder strings that will get set if you successfully get the transfer or asset from the script
    std::string address = "";
    std::string assetName = "";

    if (!script.IsAssetScript()) {
        return false;
    }
    CAssetTransfer transfer;
    if (GetTransferAsset(script, transfer)) {
        data.nAmount = transfer.nAmount;
        ExtractDestination(script, data.destination);
        data.assetId = transfer.AssetId;
        return true;
    }
    return false;
}