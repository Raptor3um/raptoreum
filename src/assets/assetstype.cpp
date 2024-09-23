// Copyright (c) 2021-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <assets/assetstype.h>
#include <streams.h>


bool GetTransferAsset(const CScript &script, CAssetTransfer &assetTransfer) {
    int nIndex;
    if (!script.IsAssetScript(nIndex)) {
        return false;
    }

    std::vector<unsigned char> vchAssetId;
    vchAssetId.insert(vchAssetId.end(), script.begin() + nIndex, script.end());
    CDataStream DSAsset(vchAssetId, SER_NETWORK, PROTOCOL_VERSION);
    try {
        DSAsset >> assetTransfer;
    } catch (std::exception &e) {
        //error("Failed to get the transfer asset: %s", e.what());
        return false;
    }

    return true;
}

void CAssetTransfer::BuildAssetTransaction(CScript &script) const {
    CDataStream AssetTransfer(SER_NETWORK, PROTOCOL_VERSION);
    AssetTransfer << *this;
    std::vector<unsigned char> vchMessage;
    vchMessage.push_back(RTM_R); // r
    vchMessage.push_back(RTM_T); // t
    vchMessage.push_back(RTM_M); // m
    vchMessage.insert(vchMessage.end(), AssetTransfer.begin(), AssetTransfer.end());
    script << OP_ASSET_ID << ToByteVector(vchMessage) << OP_DROP;
}

CAssetTransfer::CAssetTransfer(const std::string &assetId, const CAmount &nAmount, const uint64_t &uniqueId) {
    SetNull();
    this->assetId = assetId;
    this->isUnique = true;
    this->uniqueId = uniqueId;
    this->nAmount = nAmount;
}

CAssetTransfer::CAssetTransfer(const std::string &assetId, const CAmount &nAmount) {
    SetNull();
    this->assetId = assetId;
    this->isUnique = false;
    this->uniqueId = MAX_UNIQUE_ID;
    this->nAmount = nAmount;
}