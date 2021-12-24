// Copyright (c) 2021 The Raptoreum Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_FUTUREINDEX_H
#define BITCOIN_FUTUREINDEX_H

#include <uint256.h>
#include <amount.h>
#include <script/script.h>
#include <serialize.h>

struct IndexKey {
	uint256 txid;
	unsigned int outputIndex;

	ADD_SERIALIZE_METHODS;

	template <typename Stream, typename Operation>
	inline void SerializationOp(Stream& s, Operation ser_action) {
		READWRITE(txid);
		READWRITE(outputIndex);
	}

	IndexKey(uint256 hash, unsigned int index) {
		txid = hash;
		outputIndex = index;
	}

	IndexKey() {
		SetNull();
	}

	void SetNull() {
		txid.SetNull();
		outputIndex = 0;
	}
};

struct CFutureIndexKey : IndexKey {
	CFutureIndexKey(uint256 hash, unsigned int index) : IndexKey(hash, index) {
	}

	CFutureIndexKey() : IndexKey() {
	}
};

struct CFutureIndexValue {
    CAmount satoshis;
    int addressType;
    uint160 addressHash;
    int confirmedHeight;
    uint32_t lockedToHeight;
    int64_t lockedToTime;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(satoshis);
        READWRITE(addressType);
        READWRITE(addressHash);
        READWRITE(confirmedHeight);
        READWRITE(lockedToHeight);
        READWRITE(lockedToTime);
    }

    CFutureIndexValue(CAmount amount, int type, uint160 addrHash, int confirmedBlock, uint32_t toHeight, int64_t toTime) {
        satoshis = amount;
        addressType = type;
        confirmedHeight = confirmedBlock;
        lockedToHeight = toHeight;
        lockedToTime = toTime;
        addressHash = addrHash;
    }

    CFutureIndexValue() {
        SetNull();
    }

    void SetNull() {
    	confirmedHeight = 0;
        lockedToHeight = 0;
        lockedToTime = 0;
        satoshis = 0;
        addressType = 0;
        addressHash.SetNull();
    }

    bool IsNull() const {
        return addressHash.IsNull();
    }
};

struct IndexKeyCompare
{
    bool operator()(const IndexKey& a, const IndexKey& b) const {
        if (a.txid == b.txid) {
            return a.outputIndex < b.outputIndex;
        } else {
            return a.txid < b.txid;
        }
    }
};

struct CFutureIndexTxInfo
{
    std::map<CFutureIndexKey, CFutureIndexValue, IndexKeyCompare> mSpentInfo;
};

#endif
