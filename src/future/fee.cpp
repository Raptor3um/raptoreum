// Copyright (c) 2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <future/fee.h>
#include <spork.h>
#include <evo/specialtx.h>
#include <evo/providertx.h>

CAmount getFutureFees() {
	int64_t specialTxValue = sporkManager.GetSporkValue(SPORK_22_SPEICAL_TX_FEE);
	int futureTxFee = specialTxValue & 0xff;
	return futureTxFee * COIN;
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
