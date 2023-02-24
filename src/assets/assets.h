// Copyright (c) 2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_ASSETS_H
#define RAPTOREUM_ASSETS_H

#include <amount.h>
#include <coins.h>
#include <key_io.h>

#define RTM_R 0x72  //R
#define RTM_T 0x74  //T
#define RTM_M 0x6d  //M

CAmount getAssetsFeesCoin();
uint16_t getAssetsFees();
bool IsAssetNameValid(std::string name);
bool GetAssetId(const CScript& script, std::string& assetId);

class CAssetTransfer
{
public:
    std::string AssetId;
    CAmount nAmount;

    CAssetTransfer()
    {
        SetNull();
    }

    void SetNull()
    {
        nAmount = 0;
        AssetId = "";
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(AssetId);
        READWRITE(nAmount);
    }

    CAssetTransfer(const std::string& AssetId, const CAmount& nAmount);
    void BuildAssetTransaction(CScript& script) const;
};

bool GetTransferAsset(const CScript& script, CAssetTransfer& assetTransfer);


//temporary memory cache class
class CNewAssetTx;
class CUpdateAssetTx;

class CAssetMetaData {
public:
    std::string assetId; //Transaction hash of asset creation
    uint32_t blockHeight; //block height of asset creation transaction
    CAmount circulatingSuply; //update every mint transaction.
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

public:
    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
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
};

//temporary memory cache
void InsertAsset(CNewAssetTx newasset, std::string assetid,int nheigth);
bool UpdateAsset(CUpdateAssetTx upasset);
bool UpdateAsset(std::string assetid, CAmount amount);
void AddAssets(const CTransaction& tx, int nHeight, bool check = false);

bool CheckIfAssetExists(std::string name);
bool GetAssetMetaData(std::string asetId, CAssetMetaData& asset);


#endif //RAPTOREUM_ASSETS_H
