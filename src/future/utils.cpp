// Copyright (c) 2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <coins.h>
#include <future/utils.h>
#include <evo/specialtx.h>
#include <evo/providertx.h>

void maybeSetPayload(Coin& coin, const COutPoint& outpoint, const uint16_t& nType, const std::vector<uint8_t>& vExtraPayload) {
	if(nType == TRANSACTION_FUTURE) {
		CFutureTx futureTx;
		if(GetTxPayload(vExtraPayload, futureTx) && outpoint.n == futureTx.lockOutputIndex) {
			coin.nType = nType;
			coin.vExtraPayload = vExtraPayload;
		}
	}
}

//bool checkFutureCoin(const Coin& coin, int nSpendHeight, uint32_t confirmedTime, int64_t adjustCurrentTime) {
//	if(coin.nType == TRANSACTION_FUTURE) {
//		CFutureTx futureTx;
//		if(GetTxPayload(coin.vExtraPayload, futureTx)) {
//			bool isBlockMature = futureTx.maturity > 0 && nSpendHeight - coin.nHeight >= futureTx.maturity;
//			bool isTimeMature = futureTx.lockTime > 0 && adjustCurrentTime - confirmedTime  >= futureTx.lockTime;
//			return isBlockMature || isTimeMature;
//		}
//		return false;
//	}
//	return true;
//}
