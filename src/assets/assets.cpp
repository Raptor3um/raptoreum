// Copyright (c) 2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <assets/assets.h>
#include <spork.h>
#include <evo/specialtx.h>
#include <evo/providertx.h>
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
    int nIndex;
    if (!script.IsAssetScript(nIndex)){
        return false;
    }

    std::vector<unsigned char> vchAssetId;
    vchAssetId.insert(vchAssetId.end(), script.begin() + nIndex, script.end());
    CDataStream DSAsset(vchAssetId, SER_NETWORK, PROTOCOL_VERSION);

    try {
        DSAsset >> assetId;
    } catch(std::exception& e) {
        error("Failed to get asset Id: %s", e.what());
        return false;
    }

    return true;
}

void BuildAssetTransaction(CScript& script, std::string assetId){
    CDataStream DSAsset(SER_NETWORK, PROTOCOL_VERSION);
    DSAsset << assetId;
    std::vector<unsigned char> vchMessage;
    vchMessage.push_back(RTM_R); // r
    vchMessage.push_back(RTM_T); // t
    vchMessage.push_back(RTM_M); // m
    vchMessage.insert(vchMessage.end(), DSAsset.begin(), DSAsset.end());
    script << OP_ASSET_ID << ToByteVector(vchMessage) << OP_DROP;
}