// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_ASSETDB_H
#define RAPTOREUM_ASSETDB_H

#include "fs.h"
#include "serialize.h"

#include <string>
#include <map>
#include <dbwrapper.h>
#include <amount.h>
#include <pubkey.h>

class CAssetMetaData;
class uint256;
class COutPoint;
class CDatabasedAssetData;

struct CBlockAssetUndo
{
    bool onlysuply;
    CAmount circulatingSupply;
    uint16_t mintCount;
    bool updatable;
    std::string referenceHash;
    //  distribution
    uint8_t type;
    CKeyID targetAddress;
    uint8_t issueFrequency;
    CAmount Amount;
    CKeyID ownerAddress;
    CKeyID collateralAddress;


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(onlysuply);
        READWRITE(circulatingSupply);
        READWRITE(mintCount);
        if(!onlysuply){
            READWRITE(updatable);
            READWRITE(referenceHash);
            READWRITE(type);
            READWRITE(targetAddress);
            READWRITE(issueFrequency);
            READWRITE(Amount);
            READWRITE(ownerAddress);
            READWRITE(collateralAddress);
        }
    }
};

/** Access to the block database (blocks/index/) */
class CAssetsDB : public CDBWrapper
{
public:
    explicit CAssetsDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CAssetsDB(const CAssetsDB&) = delete;
    CAssetsDB& operator=(const CAssetsDB&) = delete;

    // Write to database
    bool WriteAssetData(const CAssetMetaData& asset, const int nHeight, const uint256& blockHash);
    bool WriteAssetId(const std::string assetName, const std::string Txid);
    bool WriteBlockUndoAssetData(const uint256& blockhash, const std::vector<std::pair<std::string, CBlockAssetUndo> >& assetUndoData);

    // Read from database
    bool ReadAssetData(const std::string& txid, CAssetMetaData& asset, int& nHeight, uint256& blockHash);
    bool ReadAssetId(const std::string& assetName, std::string& Txid);
    bool ReadBlockUndoAssetData(const uint256& blockhash, std::vector<std::pair<std::string, CBlockAssetUndo> >& assetUndoData);

    // Erase from database
    bool EraseAssetData(const std::string& assetName);
    bool EraseAssetId(const std::string& assetName);
    
    // Helper functions
    bool LoadAssets();
};


#endif //RAPTOREUM_ASSETDB_H