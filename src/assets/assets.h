// Copyright (c) 2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_ASSETS_H
#define RAPTOREUM_ASSETS_H

#include <amount.h>
#include <coins.h>
#include <key_io.h>
#include <pubkey.h>

class CNewAssetTx;
class CUpdateAssetTx;
struct CAssetOutputEntry;

#define MAX_CACHE_ASSETS_SIZE 2500

CAmount getAssetsFeesCoin();
uint16_t getAssetsFees();
bool IsAssetNameValid(std::string name);
bool GetAssetId(const CScript& script, std::string& assetId);

class CAssetMetaData {
public:
    std::string assetId; //Transaction hash of asset creation
    CAmount circulatingSupply; //update every mint transaction.
    std::string Name;
    bool updatable = false;//if true this asset meta can be modify using assetTx update process. 
    bool isunique = false;//true if this is asset is unique it has an identity per token (NFT flag)
    uint8_t Decimalpoint = 0;
    std::string referenceHash; //hash of the underlying physical or digital assets, IPFS hash can be used here.
    uint16_t fee; // fee was paid for this asset creation in addition to miner fee. it is a whole non-decimal point value.
    //  distribution
    uint8_t type;//manual, coinbase, address, schedule
    CKeyID targetAddress;
    uint8_t issueFrequency;
    CAmount Amount;
    CKeyID ownerAddress;
    CKeyID collateralAddress;  

    CAssetMetaData()
    {
        SetNull();
    }

    CAssetMetaData(const std::string txid, const CNewAssetTx assetTx);

    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(assetId);
        READWRITE(circulatingSupply);
        READWRITE(Name);
        READWRITE(updatable);
        READWRITE(isunique);
        READWRITE(Decimalpoint);
        READWRITE(referenceHash);
        READWRITE(fee);
        READWRITE(type);
        READWRITE(targetAddress);
        READWRITE(issueFrequency);
        READWRITE(Amount);
        READWRITE(ownerAddress);
        READWRITE(collateralAddress);
    }

    void SetNull(){
        assetId = "";
        circulatingSupply= CAmount(-1);
        Name = "";
        updatable = false; 
        isunique = false;
        Decimalpoint  = uint8_t(-1);
        referenceHash = "";
        fee = uint8_t(-1);
        type = uint8_t(-1);
        targetAddress = CKeyID();
        issueFrequency;
        Amount = 0;
        ownerAddress = CKeyID();
        collateralAddress = CKeyID();
    }
};

class CDatabasedAssetData
{
public:
    CAssetMetaData asset;
    int blockHeight;
    uint256 blockHash;

    CDatabasedAssetData(const CAssetMetaData& asset, const int& nHeight, const uint256& blockHash);
    CDatabasedAssetData();

    void SetNull()
    {
        asset.SetNull();
        blockHeight = -1;
        blockHash = uint256();
    }

    bool operator<(const CDatabasedAssetData& rhs) const
    {
        return blockHeight < blockHeight;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(asset);
        READWRITE(blockHeight);
        READWRITE(blockHash);
    }
};

class CAssets {
public:
    std::map<std::string, CDatabasedAssetData> mapAsset;
    std::map<std::string, std::string> mapAssetid;

    CAssets(const CAssets& assets) {
        this->mapAsset = assets.mapAsset;
        this->mapAssetid = assets.mapAssetid;
    }

    CAssets& operator=(const CAssets& other) {
        mapAsset = other.mapAsset;
        mapAssetid = other.mapAssetid;
        return *this;
    }

    CAssets() {
        SetNull();
    }

    void SetNull() {
        mapAsset.clear();
        mapAssetid.clear();
    }
};

class CAssetsCache : public CAssets
{
public:
    std::set<CDatabasedAssetData> NewAssetsToRemove;
    std::set<CDatabasedAssetData> NewAssetsToAdd;

    CAssetsCache() : CAssets()
    {
        SetNull();
        ClearDirtyCache();
    }

    CAssetsCache(const CAssetsCache& cache) : CAssets(cache)
    {
        this->NewAssetsToRemove = cache.NewAssetsToRemove;
        this->NewAssetsToAdd = cache.NewAssetsToAdd;
    }

    bool InsertAsset(CNewAssetTx newasset, std::string assetid, int nheigth);
    bool UpdateAsset(CUpdateAssetTx upasset);
    bool UpdateAsset(std::string assetid, CAmount amount);

    bool CheckIfAssetExists(std::string name);
    bool GetAssetMetaData(std::string asetId, CAssetMetaData& asset);
    bool GetAssetId(std::string name, std::string& assetId);

    bool Flush();
    bool DumpCacheToDatabase();
    void ClearDirtyCache() {
        NewAssetsToAdd.clear();
        NewAssetsToRemove.clear();
    }
};

void AddAssets(const CTransaction& tx, int nHeight, bool check = false);
bool GetAssetData(const CScript& script, CAssetOutputEntry& data);


#endif //RAPTOREUM_ASSETS_H
