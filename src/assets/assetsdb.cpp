// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2021-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validation.h"
#include <assets/assets.h>
#include <assets/assetsdb.h>
#include <consensus/params.h>
#include <script/ismine.h>
#include <tinyformat.h>

#include <boost/thread.hpp>

static const char ASSET_FLAG = 'A';
static const char ASSET_NAME_TXID_FLAG = 'B';
static const char BLOCK_ASSET_UNDO_DATA = 'U';
static const char ASSET_ADDRESS_AMOUNT = 'C';
static const char ADDRESS_ASSET_AMOUNT = 'D';

static size_t MAX_DATABASE_RESULTS = 50000;

CAssetsDB::CAssetsDB(size_t nCacheSize, bool fMemory, bool fWipe) :
        CDBWrapper(GetDataDir() / "assets", nCacheSize, fMemory, fWipe) {
}

bool CAssetsDB::WriteAssetData(const CAssetMetaData &asset, const int nHeight, const uint256 &blockHash) {
    CDatabaseAssetData data(asset, nHeight, blockHash);
    return Write(std::make_pair(ASSET_FLAG, asset.assetId), data);
}

bool CAssetsDB::WriteAssetId(const std::string assetName, const std::string Txid) {
    return Write(std::make_pair(ASSET_NAME_TXID_FLAG, assetName), Txid);
}

bool CAssetsDB::WriteAssetAddressAmount(const std::string &assetId, const std::string &address, const CAmount128 &amount) {
    return Write(std::make_pair(ASSET_ADDRESS_AMOUNT, std::make_pair(assetId, address)), amount.str());
}
    
bool CAssetsDB::WriteAddressAssetAmount(const std::string &address, const std::string &assetId, const CAmount128 &amount) {
    return Write(std::make_pair(ADDRESS_ASSET_AMOUNT, std::make_pair(address, assetId)), amount);
}

bool CAssetsDB::ReadAssetData(const std::string &txid, CAssetMetaData &asset, int &nHeight, uint256 &blockHash) {
    CDatabaseAssetData data;
    bool ret = Read(std::make_pair(ASSET_FLAG, txid), data);

    if (ret) {
        asset = data.asset;
        nHeight = data.blockHeight;
        blockHash = data.blockHash;
    }

    return ret;
}

bool CAssetsDB::ReadAssetId(const std::string &assetName, std::string &Txid) {
    return Read(std::make_pair(ASSET_NAME_TXID_FLAG, assetName), Txid);
}

bool CAssetsDB::ReadAssetAddressAmount(const std::string &assetId, const std::string &address, CAmount128 &amount) {
    return Read(std::make_pair(ASSET_ADDRESS_AMOUNT, std::make_pair(assetId, address)), amount);
}
    
bool CAssetsDB::ReadAssetAddressAssetAmount(const std::string &address, const std::string &assetId, CAmount128 &amount){
    return Read(std::make_pair(ADDRESS_ASSET_AMOUNT, std::make_pair(address, assetId)), amount);
}

bool CAssetsDB::EraseAssetData(const std::string &assetName) {
    return Erase(std::make_pair(ASSET_FLAG, assetName));
}

bool CAssetsDB::EraseAssetId(const std::string &assetName) {
    return Erase(std::make_pair(ASSET_NAME_TXID_FLAG, assetName));
}

bool CAssetsDB::EraseAssetAddressAmount(const std::string &assetId, const std::string &address) {
    return Erase(std::make_pair(ASSET_ADDRESS_AMOUNT, std::make_pair(assetId, address)));
}

bool CAssetsDB::EraseAddressAssetAmount(const std::string &address, const std::string &assetId) {
    return Erase(std::make_pair(ADDRESS_ASSET_AMOUNT, std::make_pair(address, assetId)));
}

bool CAssetsDB::WriteBlockUndoAssetData(const uint256 &blockHash,
                                        const std::vector <std::pair<std::string, CBlockAssetUndo>> &assetUndoData) {
    return Write(std::make_pair(BLOCK_ASSET_UNDO_DATA, blockHash), assetUndoData);
}

bool CAssetsDB::ReadBlockUndoAssetData(const uint256 &blockHash,
                                       std::vector <std::pair<std::string, CBlockAssetUndo>> &assetUndoData) {
    // If it exists, return the read value.
    if (Exists(std::make_pair(BLOCK_ASSET_UNDO_DATA, blockHash)))
        return Read(std::make_pair(BLOCK_ASSET_UNDO_DATA, blockHash), assetUndoData);

    // If it doesn't exist, we just return true because we don't want to fail just because it didn't exist in the db
    return true;
}

bool CAssetsDB::LoadAssets() {
    std::unique_ptr <CDBIterator> pcursor(NewIterator());

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

    std::unique_ptr <CDBIterator> pcursor2(NewIterator());
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

bool CAssetsDB::GetListAssets(std::vector<CDatabaseAssetData>& assets, const size_t count, const long start) {
    ::ChainstateActive().ForceFlushStateToDisk();

    std::unique_ptr<CDBIterator> pcursor(NewIterator());
    pcursor->Seek(std::make_pair(ASSET_FLAG, std::string()));

    size_t skip = 0;
    if (start >= 0) {
        skip = start;
    }

    size_t loaded = 0;
    size_t offset = 0;

    // Load assets
    while (pcursor->Valid() && loaded < count) {
        boost::this_thread::interruption_point();

        std::pair<char, std::string> key;
        if (pcursor->GetKey(key) && key.first == ASSET_FLAG) {
            if (offset < skip) {
                offset += 1;
            } else {
                CDatabaseAssetData data;
                if (pcursor->GetValue(data)) {
                    assets.push_back(data);
                    loaded += 1;
                } else {
                    return error("%s: failed to read asset", __func__);
                }
            }
            pcursor->Next();
        } else {
            break;
        }
    }

    return true;
}

bool CAssetsDB::GetListAssetsByAddress(std::vector<std::pair<std::string, CAmount128> >& vecAssetAmount, int& totalEntries, const bool& fGetTotal, const std::string& address, const size_t count, const long start) {
    ::ChainstateActive().ForceFlushStateToDisk();

    std::unique_ptr<CDBIterator> pcursor(NewIterator());
    pcursor->Seek(std::make_pair(ADDRESS_ASSET_AMOUNT, std::make_pair(address, std::string())));

    if (fGetTotal) {
        totalEntries = 0;
        while (pcursor->Valid()) {
            boost::this_thread::interruption_point();

            std::pair<char, std::pair<std::string, std::string> > key;
            if (pcursor->GetKey(key) && key.first == ADDRESS_ASSET_AMOUNT && key.second.first == address) {
                totalEntries++;
            }
            pcursor->Next();
        }
        return true;
    }

    size_t skip = 0;
    if (start >= 0) {
        skip = start;
    }
    else {
        // compute table size for backwards offset
        long table_size = 0;
        while (pcursor->Valid()) {
            boost::this_thread::interruption_point();

            std::pair<char, std::pair<std::string, std::string> > key;
            if (pcursor->GetKey(key) && key.first == ADDRESS_ASSET_AMOUNT && key.second.first == address) {
                table_size += 1;
            }
            pcursor->Next();
        }
        skip = table_size + start;
        pcursor->SeekToFirst();
    }


    size_t loaded = 0;
    size_t offset = 0;

    // Load assets
    while (pcursor->Valid() && loaded < count && loaded < MAX_DATABASE_RESULTS) {
        boost::this_thread::interruption_point();

        std::pair<char, std::pair<std::string, std::string> > key;
        if (pcursor->GetKey(key) && key.first == ADDRESS_ASSET_AMOUNT && key.second.first == address) {
                if (offset < skip) {
                    offset += 1;
                }
                else {
                    CAmount128 amount;
                    if (pcursor->GetValue(amount)) {
                        vecAssetAmount.emplace_back(std::make_pair(key.second.second, amount));
                        loaded += 1;
                    } else {
                        return error("%s: failed to Address Asset Quanity", __func__);
                    }
                }
            pcursor->Next();
        } else {
            break;
        }
    }

    return true;
}

// Can get to total count of addresses that belong to a certain assetId, or get you the list of all address that belong to a certain assetId
bool CAssetsDB::GetListAddressByAssets(std::vector<std::pair<std::string, CAmount128> >& vecAddressAmount, int& totalEntries, const bool& fGetTotal, const std::string& assetId, const size_t count, const long start) {
    ::ChainstateActive().ForceFlushStateToDisk();

    std::unique_ptr<CDBIterator> pcursor(NewIterator());
    pcursor->Seek(std::make_pair(ASSET_ADDRESS_AMOUNT, std::make_pair(assetId, std::string())));

    if (fGetTotal) {
        totalEntries = 0;
        while (pcursor->Valid()) {
            boost::this_thread::interruption_point();

            std::pair<char, std::pair<std::string, std::string> > key;
            if (pcursor->GetKey(key) && key.first == ASSET_ADDRESS_AMOUNT && key.second.first == assetId) {
                totalEntries += 1;
            }
            pcursor->Next();
        }
        return true;
    }

    size_t skip = 0;
    if (start >= 0) {
        skip = start;
    }
    else {
        // compute table size for backwards offset
        long table_size = 0;
        while (pcursor->Valid()) {
            boost::this_thread::interruption_point();

            std::pair<char, std::pair<std::string, std::string> > key;
            if (pcursor->GetKey(key) && key.first == ASSET_ADDRESS_AMOUNT && key.second.first == assetId) {
                table_size += 1;
            }
            pcursor->Next();
        }
        skip = table_size + start;
        pcursor->SeekToFirst();
    }

    size_t loaded = 0;
    size_t offset = 0;

    // Load assets
    while (pcursor->Valid() && loaded < count && loaded < MAX_DATABASE_RESULTS) {
        boost::this_thread::interruption_point();

        std::pair<char, std::pair<std::string, std::string> > key;
        if (pcursor->GetKey(key) && key.first == ASSET_ADDRESS_AMOUNT && key.second.first == assetId) {
            if (offset < skip) {
                offset += 1;
            }
            else {
                CAmount128 amount;
                if (pcursor->GetValue(amount)) {
                    vecAddressAmount.emplace_back(std::make_pair(key.second.second, amount));
                    loaded += 1;
                } else {
                    return error("%s: failed to Asset Address Quanity", __func__);
                }
            }
            pcursor->Next();
        } else {
            break;
        }
    }

    return true;
}