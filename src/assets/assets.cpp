// Copyright (c) 2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <assets/assets.h>
#include <assets/assetstype.h>
#include <assets/assetsdb.h>
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

CAssetMetaData::CAssetMetaData(const std::string txid, const CNewAssetTx assetTx){
    this->assetId = txid;
    this->circulatingSupply = 0;
    this->Name = assetTx.Name;
    this->updatable = assetTx.updatable;
    this->isunique = assetTx.isUnique;
    this->Decimalpoint = assetTx.decimalPoint;
    this->referenceHash = assetTx.referenceHash;
    this->fee = assetTx.fee;
    this->type = assetTx.type;
    this->targetAddress = assetTx.targetAddress;
    this->issueFrequency = assetTx.issueFrequency;
    this->Amount = assetTx.Amount;
    this->ownerAddress = assetTx.ownerAddress;
    this->collateralAddress = assetTx.collateralAddress;
}

CDatabasedAssetData::CDatabasedAssetData(const CAssetMetaData& asset, const int& nHeight, const uint256& blockHash)
{
    this->SetNull();
    this->asset = asset;
    this->blockHeight = nHeight;
    this->blockHash = blockHash;
}

CDatabasedAssetData::CDatabasedAssetData()
{
    this->SetNull();
}

bool CAssetsCache::InsertAsset(CNewAssetTx newasset, std::string assetid, int nheigth){

    if (CheckIfAssetExists(assetid))
        return error("%s: Tried adding new asset, but it already existed in the map of assets: %s", __func__, assetid);
    CAssetMetaData test(assetid, newasset);
    CDatabasedAssetData newAsset(test, nheigth, uint256());

    if(NewAssetsToRemove.count(newAsset))
        NewAssetsToRemove.erase(newAsset);

    NewAssetsToAdd.insert(newAsset);

    mapAsset.insert(std::make_pair(assetid, newAsset));
    mapAssetid.insert(std::make_pair(newAsset.asset.Name, assetid));
    
    return true;    
}

bool CAssetsCache::UpdateAsset(CUpdateAssetTx upasset){
    CAssetMetaData assetdata;
    if(!GetAssetMetaData(upasset.AssetId, assetdata)){
        return false;
    }

    if (NewAssetsToAdd.count(mapAsset[upasset.AssetId]))
            NewAssetsToAdd.erase(mapAsset[upasset.AssetId]);

    NewAssetsToRemove.insert(mapAsset[upasset.AssetId]);

    assetdata.updatable = upasset.updatable;
    assetdata.referenceHash = upasset.referenceHash;
    assetdata.type = upasset.type;
    assetdata.targetAddress = upasset.targetAddress;
    assetdata.issueFrequency = upasset.issueFrequency;
    assetdata.Amount = upasset.Amount;
    assetdata.ownerAddress = upasset.ownerAddress;
    assetdata.collateralAddress = upasset.collateralAddress;
    //update cache
    mapAsset[upasset.AssetId].asset = assetdata;
    //update db
    NewAssetsToAdd.insert(mapAsset[upasset.AssetId]);
    return true;
}

bool CAssetsCache::UpdateAsset(std::string assetid, CAmount amount){
    if(mapAsset.count(assetid) > 0 ){
        if (NewAssetsToAdd.count(mapAsset[assetid]))
            NewAssetsToAdd.erase(mapAsset[assetid]);
    
        NewAssetsToRemove.insert(mapAsset[assetid]);
        mapAsset[assetid].asset.circulatingSupply += amount;
        NewAssetsToAdd.insert(mapAsset[assetid]);
        return true;
    }
    return false;
}

bool CAssetsCache::DumpCacheToDatabase()
{
    try {
        //remove assets from db
        for (auto newAsset : NewAssetsToRemove){
            if (!passetsdb->EraseAssetData(newAsset.asset.assetId)){
                return error("%s : %s", __func__, "_Failed Erasing Asset Data from database");
            }
            if (!passetsdb->EraseAssetId(newAsset.asset.Name)){
                return error("%s : %s", __func__, "_Failed Erasing Asset Data from database");
            }
        }
        //add assets to db
        for (auto newAsset : NewAssetsToAdd){
            if (!passetsdb->WriteAssetData(newAsset.asset, newAsset.blockHeight, newAsset.blockHash)) {
               return error("%s : %s", __func__, "_Failed Writing New Asset Data to database");
            }
            if (!passetsdb->WriteAssetId(newAsset.asset.Name, newAsset.asset.assetId)) {
               return error("%s : %s", __func__, "_Failed Writing New Asset Data to database");
            }
        }
        ClearDirtyCache();
        return true;
    } catch (const std::runtime_error& e) {
        return error("%s : %s ", __func__, std::string("System error while flushing assets: ") + e.what());
    }
}

void AddAssets(const CTransaction& tx, int nHeight, bool check){
    if (Params().IsAssetsActive(chainActive.Tip())) {
        if (tx.nType == TRANSACTION_NEW_ASSET){
            CNewAssetTx assettx;
            if (GetTxPayload(tx, assettx)) {
                passetsCache->InsertAsset(assettx, tx.GetHash().ToString(), nHeight);
            }
        } else if (tx.nType == TRANSACTION_UPDATE_ASSET){
            CUpdateAssetTx assettx;
            if (GetTxPayload(tx, assettx)) {
                passetsCache->UpdateAsset(assettx);
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
                passetsCache->UpdateAsset(assettx.AssetId, amount); //update circulating suply
            }
        }
    }
}

bool CAssetsCache::CheckIfAssetExists(std::string name){
    if(mapAsset.count(name) > 0 ){
        return true;
    }
    return false;
}

bool CAssetsCache::GetAssetId(std::string name, std::string& assetId)
{
    //try to get assetid by asset name
    auto it = mapAssetid.find(name);
    if( it != mapAssetid.end() ) {
        assetId = it->second;
        return true;
    }
    //try to get asset id from the db
    if (passetsdb->ReadAssetId(name, assetId)){
        mapAssetid.insert(std::make_pair(name, assetId));
        return true;
    }
    return false;
}

bool CAssetsCache::GetAssetMetaData(std::string asetId, CAssetMetaData& asset)
{
    auto it = mapAsset.find(asetId);
    if (it != mapAsset.end() ) {
        asset = it->second.asset;
        return true;
    }

    int nHeight; 
    uint256 blockHash;
    if (passetsdb->ReadAssetData(asetId, asset, nHeight, blockHash)){
        CDatabasedAssetData newAsset(asset, nHeight, blockHash);
        mapAsset.insert(std::make_pair(asetId, newAsset));
        return true;
    }
    return false;
}

bool GetAssetData(const CScript& script, CAssetOutputEntry& data)
{
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