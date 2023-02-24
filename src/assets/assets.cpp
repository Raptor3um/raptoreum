// Copyright (c) 2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <assets/assets.h>
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

bool GetTransferAsset(const CScript& script, CAssetTransfer& assetTransfer){
    int nIndex;
    if (!script.IsAssetScript(nIndex)){
        return false;
    }

    std::vector<unsigned char> vchAssetId;
    vchAssetId.insert(vchAssetId.end(), script.begin() + nIndex, script.end());
    CDataStream DSAsset(vchAssetId, SER_NETWORK, PROTOCOL_VERSION);
    try {
        DSAsset >> assetTransfer;
    } catch(std::exception& e) {
        error("Failed to get the transfer asset: %s", e.what());
        return false;
    }

    return true;
}

bool GetAssetId(const CScript& script, std::string& assetId){
    CAssetTransfer assetTransfer;
    if(GetTransferAsset(script,assetTransfer)){
        assetId = assetTransfer.AssetId;
        return true;
    }
    return false;
}

void CAssetTransfer::BuildAssetTransaction(CScript& script) const{
    CDataStream AssetTransfer(SER_NETWORK, PROTOCOL_VERSION);
    AssetTransfer << *this;
    std::vector<unsigned char> vchMessage;
    vchMessage.push_back(RTM_R); // r
    vchMessage.push_back(RTM_T); // t
    vchMessage.push_back(RTM_M); // m
    vchMessage.insert(vchMessage.end(), AssetTransfer.begin(), AssetTransfer.end());
    script << OP_ASSET_ID << ToByteVector(vchMessage) << OP_DROP;
}

CAssetTransfer::CAssetTransfer(const std::string& AssetId, const CAmount& nAmount)
{
    SetNull();
    this->AssetId = AssetId;
    this->nAmount = nAmount;
}


//temporary memory cache
static std::map<std::string, CAssetMetaData> mapAsset;

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
    auto it = mapAsset.find(asetId);
    if( it != mapAsset.end() ) {
        asset = it->second;
        return true;
    }
    return false;
}