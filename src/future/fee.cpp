// Copyright (c) 2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <future/fee.h>
#include <spork.h>
#include <evo/specialtx.h>
#include <evo/providertx.h>

CAmount getFutureFeesCoin() {
	return getFutureFees() * COIN;
}

uint16_t getFutureFees() {
    if(!sporkManager.IsSporkActive(SPORK_22_SPECIAL_TX_FEE)) {
        return 0;
    }
    int64_t specialTxValue = sporkManager.GetSporkValue(SPORK_22_SPECIAL_TX_FEE);
    return specialTxValue & 0xff;
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

//void maybeSetPayload(Coin& coin, const COutPoint& outpoint, const int16_t& nType, const std::vector<uint8_t>& vExtraPayload) {
//	if(nType == TRANSACTION_FUTURE) {
//		CFutureTx futureTx;
//		if(GetTxPayload(vExtraPayload, futureTx) && outpoint.n == futureTx.lockOutputIndex) {
//			coin.nType = nType;
//			coin.vExtraPayload = vExtraPayload;
//		}
//	}
//}
