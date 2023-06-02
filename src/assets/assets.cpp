// Copyright (c) 2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <assets/assets.h>
#include <assets/assetsdb.h>
#include <assets/assetstype.h>
#include <chainparams.h>
#include <evo/providertx.h>
#include <evo/specialtx.h>
#include <regex>
#include <spork.h>
#include <validation.h>
#include <wallet/wallet.h>

static const std::regex name_characters("^[a-zA-Z0-9 ]{3,}$");
static const std::regex rtm_names("^RTM$|^RAPTOREUM$|^wRTM$|^WRTM$|^RTMcoin$|^RTMCOIN$");

bool IsAssetNameValid(std::string name)
{
    if (name.length() < 3 || name.length() > 128) return false;
    return std::regex_match(name, name_characters) && !std::regex_match(name, rtm_names);
}

CAmount getAssetsFeesCoin()
{
    return getAssetsFees() * COIN;
}

uint16_t getAssetsFees()
{
    if (!sporkManager.IsSporkActive(SPORK_22_SPECIAL_TX_FEE)) {
        return 0;
    }
    int64_t specialTxValue = sporkManager.GetSporkValue(SPORK_22_SPECIAL_TX_FEE);
    return specialTxValue >> 8 & 0xff;
}

bool GetAssetId(const CScript& script, std::string& assetId)
{
    CAssetTransfer assetTransfer;
    if (GetTransferAsset(script, assetTransfer)) {
        assetId = assetTransfer.AssetId;
        return true;
    }
    return false;
}

CAssetMetaData::CAssetMetaData(const std::string txid, const CNewAssetTx assetTx)
{
    this->assetId = txid;
    this->circulatingSupply = 0;
    this->mintCount = 0;
    this->Name = assetTx.Name;
    this->updatable = assetTx.updatable;
    this->isunique = assetTx.isUnique;
    this->Decimalpoint = assetTx.decimalPoint;
    this->referenceHash = assetTx.referenceHash;
    this->maxMintCount = assetTx.maxMintCount;
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

bool CAssetsCache::InsertAsset(CNewAssetTx newasset, std::string assetid, int nheigth)
{
    if (CheckIfAssetExists(assetid))
        return error("%s: Tried adding new asset, but it already existed in the map of assets: %s", __func__, assetid);
    CAssetMetaData test(assetid, newasset);
    CDatabasedAssetData newAsset(test, nheigth, uint256());

    if (NewAssetsToRemove.count(newAsset))
        NewAssetsToRemove.erase(newAsset);

    NewAssetsToAdd.insert(newAsset);

    mapAsset.insert(std::make_pair(assetid, newAsset));
    mapAssetid.insert(std::make_pair(newAsset.asset.Name, assetid));

    return true;
}

bool CAssetsCache::UpdateAsset(CUpdateAssetTx upasset)
{
    CAssetMetaData assetdata;
    if (!GetAssetMetaData(upasset.AssetId, assetdata)) {
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

bool CAssetsCache::UpdateAsset(std::string assetid, CAmount amount)
{
    if (mapAsset.count(assetid) > 0) {
        if (NewAssetsToAdd.count(mapAsset[assetid]))
            NewAssetsToAdd.erase(mapAsset[assetid]);

        NewAssetsToRemove.insert(mapAsset[assetid]);
        mapAsset[assetid].asset.circulatingSupply += amount;
        mapAsset[assetid].asset.mintCount += 1;
        NewAssetsToAdd.insert(mapAsset[assetid]);
        return true;
    }
    return false;
}

bool CAssetsCache::RemoveAsset(std::string asetId)
{
    if (mapAsset.count(asetId) > 0) {
        if (NewAssetsToAdd.count(mapAsset[asetId]))
            NewAssetsToAdd.erase(mapAsset[asetId]);

        NewAssetsToRemove.insert(mapAsset[asetId]);
        return true;
    }
    return false;
}

bool CAssetsCache::UndoUpdateAsset(const CUpdateAssetTx upasset, const std::vector<std::pair<std::string, CBlockAssetUndo>>& vUndoData)
{
    if (mapAsset.count(upasset.AssetId) > 0) {
        CAssetMetaData assetdata;
        if (!GetAssetMetaData(upasset.AssetId, assetdata)) {
            return false;
        }

        if (NewAssetsToAdd.count(mapAsset[upasset.AssetId]))
            NewAssetsToAdd.erase(mapAsset[upasset.AssetId]);

        NewAssetsToRemove.insert(mapAsset[upasset.AssetId]);

        for (auto item : vUndoData) {
            if (item.first == upasset.AssetId) {
                assetdata.updatable = item.second.updatable;
                assetdata.referenceHash = item.second.referenceHash;
                assetdata.type = item.second.type;
                assetdata.targetAddress = item.second.targetAddress;
                assetdata.issueFrequency = item.second.issueFrequency;
                assetdata.Amount = item.second.Amount;
                assetdata.ownerAddress = item.second.ownerAddress;
                assetdata.collateralAddress = item.second.collateralAddress;
            }
        }

        //update cache
        mapAsset[upasset.AssetId].asset = assetdata;
        //update db
        NewAssetsToAdd.insert(mapAsset[upasset.AssetId]);
        return true;
    }
    return false;
}

bool CAssetsCache::UndoMintAsset(const CMintAssetTx assettx, const std::vector<std::pair<std::string, CBlockAssetUndo>>& vUndoData)
{
    if (mapAsset.count(assettx.AssetId) > 0) {
        CAssetMetaData assetdata;
        if (!GetAssetMetaData(assettx.AssetId, assetdata)) {
            return false;
        }

        if (NewAssetsToAdd.count(mapAsset[assettx.AssetId]))
            NewAssetsToAdd.erase(mapAsset[assettx.AssetId]);

        NewAssetsToRemove.insert(mapAsset[assettx.AssetId]);

        for (auto item : vUndoData) {
            if (item.first == assettx.AssetId) {
                assetdata.circulatingSupply = item.second.circulatingSupply;
                assetdata.mintCount = item.second.mintCount;
            }
        }

        //update cache
        mapAsset[assettx.AssetId].asset = assetdata;
        //update db
        NewAssetsToAdd.insert(mapAsset[assettx.AssetId]);
        return true;
    }
    return false;
}

bool CAssetsCache::CheckIfAssetExists(std::string assetId)
{
    //check if the asset is removed
    CAssetMetaData tempAsset;
    tempAsset.assetId = assetId;
    CDatabasedAssetData cachedAsset(tempAsset, 0, uint256());
    if (NewAssetsToRemove.count(cachedAsset)) {
        return false;
    }

    if (mapAsset.count(assetId) > 0) {
        return true;
    }

    //check if the asset exist on the db
    int nHeight;
    uint256 blockHash;
    CAssetMetaData asset;
    if (passetsdb->ReadAssetData(assetId, asset, nHeight, blockHash)) {
        CDatabasedAssetData newAsset(asset, nHeight, blockHash);
        mapAsset.insert(std::make_pair(assetId, newAsset));
        return true;
    }
    return false;
}

bool CAssetsCache::GetAssetId(std::string name, std::string& assetId)
{
    //try to get assetid by asset name
    auto it = mapAssetid.find(name);
    if (it != mapAssetid.end()) {
        assetId = it->second;
        return true;
    }
    //try to get asset id from the db
    if (passetsdb->ReadAssetId(name, assetId)) {
        mapAssetid.insert(std::make_pair(name, assetId));
        return true;
    }
    return false;
}

bool CAssetsCache::GetAssetMetaData(std::string assetId, CAssetMetaData& asset)
{
    auto it = mapAsset.find(assetId);
    if (it != mapAsset.end()) {
        asset = it->second.asset;
        return true;
    }

    auto it2 = passetsCache->mapAsset.find(assetId);
    if (it2 != passetsCache->mapAsset.end()) {
        mapAsset.insert(std::make_pair(assetId, it2->second));
        asset = it2->second.asset;
        return true;
    }

    int nHeight;
    uint256 blockHash;
    if (passetsdb->ReadAssetData(assetId, asset, nHeight, blockHash)) {
        CDatabasedAssetData newAsset(asset, nHeight, blockHash);
        mapAsset.insert(std::make_pair(assetId, newAsset));
        return true;
    }
    return false;
}

bool CAssetsCache::DumpCacheToDatabase()
{
    try {
        //remove assets from db
        for (auto newAsset : NewAssetsToRemove) {
            if (!passetsdb->EraseAssetData(newAsset.asset.assetId)) {
                return error("%s : %s", __func__, "_Failed Erasing Asset Data from database");
            }
            if (!passetsdb->EraseAssetId(newAsset.asset.Name)) {
                return error("%s : %s", __func__, "_Failed Erasing Asset Data from database");
            }
        }
        //add assets to db
        for (auto newAsset : NewAssetsToAdd) {
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

bool CAssetsCache::Flush()
{
    if (!passetsCache)
        return error("%s: Couldn't find passetsCache pointer while trying to flush assets cache", __func__);

    try {
        for (auto& item : NewAssetsToRemove) {
            if (passetsCache->NewAssetsToAdd.count(item))
                passetsCache->NewAssetsToAdd.erase(item);
            passetsCache->NewAssetsToRemove.insert(item);
        }

        for (auto& item : NewAssetsToAdd) {
            if (passetsCache->NewAssetsToRemove.count(item))
                passetsCache->NewAssetsToRemove.erase(item);
            passetsCache->NewAssetsToAdd.insert(item);
        }

        for (auto& item : mapAsset)
            passetsCache->mapAsset[item.first] = item.second;

        for (auto& item : mapAssetid)
            passetsCache->mapAssetid[item.first] = item.second;

        return true;

    } catch (const std::runtime_error& e) {
        return error("%s : %s ", __func__, std::string("System error while flushing assets: ") + e.what());
    }
}

void AddAssets(const CTransaction& tx, int nHeight, CAssetsCache* assetCache, std::pair<std::string, CBlockAssetUndo>* undoAssetData)
{
    if (Params().IsAssetsActive(chainActive.Tip()) && assetCache) {
        if (tx.nType == TRANSACTION_NEW_ASSET) {
            CNewAssetTx assettx;
            if (GetTxPayload(tx, assettx)) {
                assetCache->InsertAsset(assettx, tx.GetHash().ToString(), nHeight);
            }
        } else if (tx.nType == TRANSACTION_UPDATE_ASSET) {
            CUpdateAssetTx assettx;
            if (GetTxPayload(tx, assettx)) {
                CAssetMetaData asset;
                if (!assetCache->GetAssetMetaData(assettx.AssetId, asset))
                    return;
                assetCache->UpdateAsset(assettx);
                undoAssetData->first = assettx.AssetId; // Asset Name
                undoAssetData->second = CBlockAssetUndo{false, asset.circulatingSupply,
                    asset.mintCount,
                    asset.updatable,
                    asset.referenceHash,
                    asset.type,
                    asset.targetAddress,
                    asset.issueFrequency,
                    asset.Amount,
                    asset.ownerAddress,
                    asset.collateralAddress};
            }
        } else if (tx.nType == TRANSACTION_MINT_ASSET) {
            CMintAssetTx assettx;
            if (GetTxPayload(tx, assettx)) {
                CAmount amount = 0;
                for (auto out : tx.vout) {
                    if (out.scriptPubKey.IsAssetScript()) {
                        CAssetTransfer assetTransfer;
                        if (GetTransferAsset(out.scriptPubKey, assetTransfer))
                            amount += assetTransfer.nAmount;
                    }
                }
                CAssetMetaData asset;
                if (!assetCache->GetAssetMetaData(assettx.AssetId, asset))
                    return;
                assetCache->UpdateAsset(assettx.AssetId, amount); //update circulating suply
                undoAssetData->first = assettx.AssetId;           // Asset Name
                undoAssetData->second = CBlockAssetUndo{true, asset.circulatingSupply,
                    asset.mintCount,
                    asset.updatable,
                    asset.referenceHash,
                    asset.type,
                    asset.targetAddress,
                    asset.issueFrequency,
                    asset.Amount,
                    asset.ownerAddress,
                    asset.collateralAddress};
            }
        }
    }
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

bool validateAmount(const CAmount nAmount, const uint16_t decimalPoint)
{
    if (nAmount % int64_t(pow(10, (8 - decimalPoint))) != 0) {
        return false;
    }
    return true;
}

bool validateAmount(const std::string& assetId, const CAmount nAmount)
{
    CAssetMetaData asset;
    if (!passetsCache->GetAssetMetaData(assetId, asset))
        return false; //this should never happen

    return validateAmount(nAmount, asset.Decimalpoint);
}