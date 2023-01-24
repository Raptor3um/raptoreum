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
void BuildAssetTransaction(CScript& script, std::string assetId);

#endif //RAPTOREUM_ASSETS_H
