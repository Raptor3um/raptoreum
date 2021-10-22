// Copyright (c) 2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_FUTILS_H
#define RAPTOREUM_FUTILS_H

//#include "coins.h"
class Coin;
class COutPoint;

void maybeSetPayload(Coin& coin, const COutPoint& outpoint, const int16_t& nType, const std::vector<uint8_t>& vExtraPayload);
//bool checkFutureCoin(const Coin& coin, int nSpendHeight, uint32_t confirmedTime, int64_t adjustCurrentTime);

#endif //RAPTOREUM_FUTILS_H
