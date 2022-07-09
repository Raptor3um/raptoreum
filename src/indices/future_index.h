// Copyright (c) 2021 The Raptoreum Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_FUTUREINDEX_H
#define BITCOIN_FUTUREINDEX_H

#include <amount.h>
#include <indices/index.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

struct CFutureIndexKey : IndexKey {
    CFutureIndexKey(uint256 hash, unsigned int index) :
        IndexKey(hash, index)
    {
    }

    CFutureIndexKey() :
        IndexKey()
    {
    }
};

struct CFutureIndexValue {
    CAmount satoshis;
    int addressType;
    uint160 addressHash;
    int confirmedHeight;
    int32_t lockedToHeight;
    int64_t lockedToTime;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(satoshis);
        READWRITE(addressType);
        READWRITE(addressHash);
        READWRITE(confirmedHeight);
        READWRITE(lockedToHeight);
        READWRITE(lockedToTime);
    }

    CFutureIndexValue(CAmount amount, int type, uint160 addrHash, int confirmedBlock, uint32_t toHeight, int64_t toTime) :
        satoshis(amount),
        addressType(type),
        addressHash(addrHash),
        confirmedHeight(confirmedBlock),
        lockedToHeight(toHeight),
        lockedToTime(toTime)
    {
    }

    CFutureIndexValue()
    {
        SetNull();
    }

    void SetNull()
    {
        satoshis = 0;
        addressType = 0;
        addressHash.SetNull();
        confirmedHeight = 0;
        lockedToHeight = 0;
        lockedToTime = 0;
    }

    bool IsNull() const
    {
        return addressHash.IsNull();
    }
};

typedef std::map<CFutureIndexKey, CFutureIndexValue, CIndexKeyCompare> mapFutureIndex;
struct CFutureIndexTxInfo {
    mapFutureIndex mFutureInfo;
};

#endif
