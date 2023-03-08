// Copyright (c) 2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_ASSETS_FEES_H
#define RAPTOREUM_ASSETS_FEES_H

#include <amount.h>
#include <coins.h>
#include <key_io.h>

#define RTM_R 0x72  //R
#define RTM_T 0x74  //T
#define RTM_M 0x6d  //M

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


#endif //RAPTOREUM_ASSETS_FEES_H