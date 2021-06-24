// Copyright (c) 2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "fee.h"
#include "spork.h"

CAmount getFutureFees() {
	int64_t specialTxValue = sporkManager.GetSporkValue(SPORK_22_SPEICAL_TX_FEE);
	int futureTxFee = specialTxValue & 0xff;
	return futureTxFee * COIN;
}
