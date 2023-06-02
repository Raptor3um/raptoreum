// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validation.h"
#include <assets/assets.h>
#include <assets/assetsdb.h>
#include <consensus/params.h>
#include <script/ismine.h>
#include <tinyformat.h>
#include <util.h>

#include <boost/thread.hpp>

static const char ASSET_FLAG = 'A';
static const char ASSET_NAME_TXID_FLAG = 'B';
static const char BLOCK_ASSET_UNDO_DATA = 'U';

static size_t MAX_DATABASE_RESULTS = 50000;

CAssetsDB::CAssetsDB(size_t nCacheSize, bool fMemory, bool fWipe) :
    CDBWrapper(GetDataDir() / "assets", nCacheSize, fMemory, fWipe)
{
}

bool CAssetsDB::WriteAssetData(const CAssetMetaData& asset, const int nHeight, const uint256& blockHash)
{
    CDatabaseAssetData data(asset, nHeight, blockHash);
    return Write(std::make_pair(ASSET_FLAG, asset.assetId), data);
}

bool CAssetsDB::WriteAssetId(const std::string assetName, const std::string Txid)
{
    return Write(std::make_pair(ASSET_NAME_TXID_FLAG, assetName), Txid);
}

bool CAssetsDB::ReadAssetData(const std::string& txid, CAssetMetaData& asset, int& nHeight, uint256& blockHash)
{
    CDatabaseAssetData data;
    bool ret = Read(std::make_pair(ASSET_FLAG, txid), data);

    if (ret) {
        asset = data.asset;
        nHeight = data.blockHeight;
        blockHash = data.blockHash;
    }

    return ret;
}

bool CAssetsDB::ReadAssetId(const std::string& assetName, std::string& Txid)
{
    return Read(std::make_pair(ASSET_NAME_TXID_FLAG, assetName), Txid);
}

bool CAssetsDB::EraseAssetData(const std::string& assetName)
{
    return Erase(std::make_pair(ASSET_FLAG, assetName));
}

bool CAssetsDB::EraseAssetId(const std::string& assetName)
{
    return Erase(std::make_pair(ASSET_NAME_TXID_FLAG, assetName));
}

bool CAssetsDB::WriteBlockUndoAssetData(const uint256& blockHash, const std::vector<std::pair<std::string, CBlockAssetUndo>>& assetUndoData)
{
    return Write(std::make_pair(BLOCK_ASSET_UNDO_DATA, blockHash), assetUndoData);
}

bool CAssetsDB::ReadBlockUndoAssetData(const uint256& blockHash, std::vector<std::pair<std::string, CBlockAssetUndo>>& assetUndoData)
{
    // If it exists, return the read value.
    if (Exists(std::make_pair(BLOCK_ASSET_UNDO_DATA, blockHash)))
        return Read(std::make_pair(BLOCK_ASSET_UNDO_DATA, blockHash), assetUndoData);

    // If it doesn't exist, we just return true because we don't want to fail just because it didn't exist in the db
    return true;
}

bool CAssetsDB::LoadAssets()
{
    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(std::make_pair(ASSET_FLAG, std::string()));

    // Load assets
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, std::string> key;
        if (pcursor->GetKey(key) && key.first == ASSET_FLAG) {
            CDatabaseAssetData data;
            if (pcursor->GetValue(data)) {
                passetsCache->mapAsset.insert(std::make_pair(data.asset.assetId, data));
                pcursor->Next();

                // Loaded enough from database to have in memory.
                // No need to load everything if it is just going to be removed from the cache
                if (passetsCache->mapAsset.size() == MAX_CACHE_ASSETS_SIZE)
                    break;
            } else {
                return error("%s: failed to read asset", __func__);
            }
        } else {
            break;
        }
    }

    std::unique_ptr<CDBIterator> pcursor2(NewIterator());
    pcursor2->Seek(std::make_pair(ASSET_NAME_TXID_FLAG, std::string()));

    // Load mapAssetId
    while (pcursor2->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, std::string> key; // <Asset Name> -> assetId
        if (pcursor2->GetKey(key) && key.first == ASSET_NAME_TXID_FLAG) {
            std::string value;
            if (pcursor2->GetValue(value)) {
                passetsCache->mapAssetId.insert(
                    std::make_pair(key.second, value));
                if (passetsCache->mapAssetId.size() > MAX_CACHE_ASSETS_SIZE)
                    break;
                pcursor2->Next();
            } else {
                return error("%s: failed to read my assetId from database", __func__);
            }
        } else {
            break;
        }
    }

    return true;
}