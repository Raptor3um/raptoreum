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
        assetId = assetTransfer.assetId;
        return true;
    }
    return false;
}

CAssetMetaData::CAssetMetaData(const std::string txid, const CNewAssetTx assetTx)
{
    assetId = txid;
    circulatingSupply = 0;
    mintCount = 0;
    name = assetTx.name;
    updatable = assetTx.updatable;
    isUnique = assetTx.isUnique;
    decimalPoint = assetTx.decimalPoint;
    referenceHash = assetTx.referenceHash;
    maxMintCount = assetTx.maxMintCount;
    fee = assetTx.fee;
    type = assetTx.type;
    targetAddress = assetTx.targetAddress;
    issueFrequency = assetTx.issueFrequency;
    amount = assetTx.amount;
    ownerAddress = assetTx.ownerAddress;
    collateralAddress = assetTx.collateralAddress;
}

CDatabaseAssetData::CDatabaseAssetData(const CAssetMetaData& asset, const int& nHeight, const uint256& blockHash)
{
    SetNull();
    this->asset = asset;
    this->blockHeight = nHeight;
    this->blockHash = blockHash;
}

CDatabaseAssetData::CDatabaseAssetData()
{
    SetNull();
}

bool CAssetsCache::InsertAsset(CNewAssetTx newAsset, std::string assetId, int nHeight)
{
    if (CheckIfAssetExists(assetId))
        return error("%s: Tried adding new asset, but it already existed in the map of assets: %s", __func__, assetId);
    CAssetMetaData test(assetId, newAsset);
    CDatabaseAssetData newAssetData(test, nHeight, uint256());

    if (NewAssetsToRemove.count(newAssetData))
        NewAssetsToRemove.erase(newAssetData);

    NewAssetsToAdd.insert(newAssetData);

    mapAsset.insert(std::make_pair(assetId, newAssetData));
    mapAssetId.insert(std::make_pair(newAssetData.asset.name, assetId));

    return true;
}

bool CAssetsCache::UpdateAsset(CUpdateAssetTx upAsset)
{
    CAssetMetaData assetData;
    if (!GetAssetMetaData(upAsset.assetId, assetData)) {
        return false;
    }

    if (NewAssetsToAdd.count(mapAsset[upAsset.assetId]))
        NewAssetsToAdd.erase(mapAsset[upAsset.assetId]);

    NewAssetsToRemove.insert(mapAsset[upAsset.assetId]);

    assetData.updatable = upAsset.updatable;
    assetData.referenceHash = upAsset.referenceHash;
    assetData.type = upAsset.type;
    assetData.targetAddress = upAsset.targetAddress;
    assetData.issueFrequency = upAsset.issueFrequency;
    assetData.amount = upAsset.amount;
    assetData.ownerAddress = upAsset.ownerAddress;
    assetData.collateralAddress = upAsset.collateralAddress;
    //update cache
    mapAsset[upAsset.assetId].asset = assetData;
    //update db
    NewAssetsToAdd.insert(mapAsset[upAsset.assetId]);
    return true;
}

bool CAssetsCache::UpdateAsset(std::string assetId, CAmount amount)
{
    if (mapAsset.count(assetId) > 0) {
        if (NewAssetsToAdd.count(mapAsset[assetId]))
            NewAssetsToAdd.erase(mapAsset[assetId]);

        NewAssetsToRemove.insert(mapAsset[assetId]);
        mapAsset[assetId].asset.circulatingSupply += amount;
        mapAsset[assetId].asset.mintCount += 1;
        NewAssetsToAdd.insert(mapAsset[assetId]);
        return true;
    }
    return false;
}

bool CAssetsCache::RemoveAsset(std::string assetId)
{
    if (mapAsset.count(assetId) > 0) {
        if (NewAssetsToAdd.count(mapAsset[assetId]))
            NewAssetsToAdd.erase(mapAsset[assetId]);

        NewAssetsToRemove.insert(mapAsset[assetId]);
        return true;
    }
    return false;
}

bool CAssetsCache::UndoUpdateAsset(const CUpdateAssetTx upAsset, const std::vector<std::pair<std::string, CBlockAssetUndo>>& vUndoData)
{
    if (mapAsset.count(upAsset.assetId) > 0) {
        CAssetMetaData assetData;
        if (!GetAssetMetaData(upAsset.assetId, assetData)) {
            return false;
        }

        if (NewAssetsToAdd.count(mapAsset[upAsset.assetId]))
            NewAssetsToAdd.erase(mapAsset[upAsset.assetId]);

        NewAssetsToRemove.insert(mapAsset[upAsset.assetId]);

        for (auto item : vUndoData) {
            if (item.first == upAsset.assetId) {
                assetData.updatable = item.second.updatable;
                assetData.referenceHash = item.second.referenceHash;
                assetData.type = item.second.type;
                assetData.targetAddress = item.second.targetAddress;
                assetData.issueFrequency = item.second.issueFrequency;
                assetData.amount = item.second.amount;
                assetData.ownerAddress = item.second.ownerAddress;
                assetData.collateralAddress = item.second.collateralAddress;
            }
        }

        //update cache
        mapAsset[upAsset.assetId].asset = assetData;
        //update db
        NewAssetsToAdd.insert(mapAsset[upAsset.assetId]);
        return true;
    }
    return false;
}

bool CAssetsCache::UndoMintAsset(const CMintAssetTx assetTx, const std::vector<std::pair<std::string, CBlockAssetUndo>>& vUndoData)
{
    if (mapAsset.count(assetTx.assetId) > 0) {
        CAssetMetaData assetData;
        if (!GetAssetMetaData(assetTx.assetId, assetData)) {
            return false;
        }

        if (NewAssetsToAdd.count(mapAsset[assetTx.assetId]))
            NewAssetsToAdd.erase(mapAsset[assetTx.assetId]);

        NewAssetsToRemove.insert(mapAsset[assetTx.assetId]);

        for (auto item : vUndoData) {
            if (item.first == assetTx.assetId) {
                assetData.circulatingSupply = item.second.circulatingSupply;
                assetData.mintCount = item.second.mintCount;
            }
        }

        //update cache
        mapAsset[assetTx.assetId].asset = assetData;
        //update db
        NewAssetsToAdd.insert(mapAsset[assetTx.assetId]);
        return true;
    }
    return false;
}

bool CAssetsCache::CheckIfAssetExists(std::string assetId)
{
    //check if the asset is removed
    CAssetMetaData tempAsset;
    tempAsset.assetId = assetId;
    CDatabaseAssetData cachedAsset(tempAsset, 0, uint256());
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
        CDatabaseAssetData newAsset(asset, nHeight, blockHash);
        mapAsset.insert(std::make_pair(assetId, newAsset));
        return true;
    }
    return false;
}

bool CAssetsCache::GetAssetId(std::string name, std::string& assetId)
{
    //try to get assetId by asset name
    auto it = mapAssetId.find(name);
    if (it != mapAssetId.end()) {
        assetId = it->second;
        return true;
    }
    //try to get asset id from the db
    if (passetsdb->ReadAssetId(name, assetId)) {
        mapAssetId.insert(std::make_pair(name, assetId));
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
        CDatabaseAssetData newAsset(asset, nHeight, blockHash);
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
            if (!passetsdb->EraseAssetId(newAsset.asset.name)) {
                return error("%s : %s", __func__, "_Failed Erasing Asset Data from database");
            }
        }
        //add assets to db
        for (auto newAsset : NewAssetsToAdd) {
            if (!passetsdb->WriteAssetData(newAsset.asset, newAsset.blockHeight, newAsset.blockHash)) {
                return error("%s : %s", __func__, "_Failed Writing New Asset Data to database");
            }
            if (!passetsdb->WriteAssetId(newAsset.asset.name, newAsset.asset.assetId)) {
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

        for (auto& item : mapAssetId)
            passetsCache->mapAssetId[item.first] = item.second;

        return true;

    } catch (const std::runtime_error& e) {
        return error("%s : %s ", __func__, std::string("System error while flushing assets: ") + e.what());
    }
}

void AddAssets(const CTransaction& tx, int nHeight, CAssetsCache* assetCache, std::pair<std::string, CBlockAssetUndo>* undoAssetData)
{
    if (Params().IsAssetsActive(::ChainActive().Tip()) && assetCache) {
        if (tx.nType == TRANSACTION_NEW_ASSET) {
            CNewAssetTx assetTx;
            if (GetTxPayload(tx, assetTx)) {
                assetCache->InsertAsset(assetTx, tx.GetHash().ToString(), nHeight);
            }
        } else if (tx.nType == TRANSACTION_UPDATE_ASSET) {
            CUpdateAssetTx assetTx;
            if (GetTxPayload(tx, assetTx)) {
                CAssetMetaData asset;
                if (!assetCache->GetAssetMetaData(assetTx.assetId, asset))
                    return;
                assetCache->UpdateAsset(assetTx);
                undoAssetData->first = assetTx.assetId; // Asset Name
                undoAssetData->second = CBlockAssetUndo{false, asset.circulatingSupply,
                    asset.mintCount,
                    asset.updatable,
                    asset.referenceHash,
                    asset.type,
                    asset.targetAddress,
                    asset.issueFrequency,
                    asset.amount,
                    asset.ownerAddress,
                    asset.collateralAddress};
            }
        } else if (tx.nType == TRANSACTION_MINT_ASSET) {
            CMintAssetTx assetTx;
            if (GetTxPayload(tx, assetTx)) {
                CAmount amount = 0;
                for (auto out : tx.vout) {
                    if (out.scriptPubKey.IsAssetScript()) {
                        CAssetTransfer assetTransfer;
                        if (GetTransferAsset(out.scriptPubKey, assetTransfer))
                            amount += assetTransfer.nAmount;
                    }
                }
                CAssetMetaData asset;
                if (!assetCache->GetAssetMetaData(assetTx.assetId, asset))
                    return;
                assetCache->UpdateAsset(assetTx.assetId, amount); // Update circulating supply
                undoAssetData->first = assetTx.assetId;           // Asset Name
                undoAssetData->second = CBlockAssetUndo{true, asset.circulatingSupply,
                    asset.mintCount,
                    asset.updatable,
                    asset.referenceHash,
                    asset.type,
                    asset.targetAddress,
                    asset.issueFrequency,
                    asset.amount,
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
        data.assetId = transfer.assetId;
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

    return validateAmount(nAmount, asset.decimalPoint);
}