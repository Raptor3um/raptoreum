// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SPENTINDEX_H
#define BITCOIN_SPENTINDEX_H

#include <amount.h>
#include <indices/index.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

struct CSpentIndexKey : IndexKey {
    CSpentIndexKey(uint256 t, unsigned int i) :
            IndexKey(t, i) {
    }

    CSpentIndexKey() :
            IndexKey() {
    }
};

struct CSpentIndexValue {
    uint256 txid;
    unsigned int inputIndex;
    int blockHeight;
    CAmount satoshis;
    int addressType;
    uint160 addressHash;

    SERIALIZE_METHODS(CSpentIndexValue, obj
    )
    {
        READWRITE(obj.txid, obj.inputIndex, obj.blockHeight, obj.satoshis, obj.addressType, obj.addressHash);
    }

    CSpentIndexValue(uint256 t, unsigned int i, int h, CAmount s, int type, uint160 a) :
            txid(t),
            inputIndex(i),
            blockHeight(h),
            satoshis(s),
            addressType(type),
            addressHash(a) {
    }

    CSpentIndexValue() {
        SetNull();
    }

    void SetNull() {
        txid.SetNull();
        inputIndex = 0;
        blockHeight = 0;
        satoshis = 0;
        addressType = 0;
        addressHash.SetNull();
    }

    bool IsNull() const {
        return txid.IsNull();
    }
};

typedef std::map <CSpentIndexKey, CSpentIndexValue, CIndexKeyCompare> mapSpentIndex;
struct CSpentIndexTxInfo {
    mapSpentIndex mSpentInfo;
};

struct CTimestampIndexIteratorKey {
    unsigned int timestamp;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 4;
    }

    template<typename Stream>
    void Serialize(Stream &s) const {
        ser_writedata32be(s, timestamp);
    }

    template<typename Stream>
    void Unserialize(Stream &s) {
        timestamp = ser_readdata32be(s);
    }

    CTimestampIndexIteratorKey(unsigned int time) {
        timestamp = time;
    }

    CTimestampIndexIteratorKey() {
        SetNull();
    }

    void SetNull() {
        timestamp = 0;
    }
};

struct CTimestampIndexKey {
    unsigned int timestamp;
    uint256 blockHash;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 36;
    }

    template<typename Stream>
    void Serialize(Stream &s) const {
        ser_writedata32be(s, timestamp);
        blockHash.Serialize(s);
    }

    template<typename Stream>
    void Unserialize(Stream &s) {
        timestamp = ser_readdata32be(s);
        blockHash.Unserialize(s);
    }

    CTimestampIndexKey(unsigned int time, uint256 hash) {
        timestamp = time;
        blockHash = hash;
    }

    CTimestampIndexKey() {
        SetNull();
    }

    void SetNull() {
        timestamp = 0;
        blockHash.SetNull();
    }
};

struct CAddressUnspentKey {
    unsigned int type;
    uint160 hashBytes;
    std::string asset;
    uint256 txhash;
    size_t index;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 57 + asset.size();
    }

    template<typename Stream>
    void Serialize(Stream &s) const {
        ser_writedata8(s, type);
        hashBytes.Serialize(s);
        ::Serialize(s, asset);
        txhash.Serialize(s);
        ser_writedata32(s, index);
    }

    template<typename Stream>
    void Unserialize(Stream &s) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s);
        ::Unserialize(s, asset);
        txhash.Unserialize(s);
        index = ser_readdata32(s);
    }

    CAddressUnspentKey(unsigned int addressType, uint160 addressHash, uint256 txid, size_t indexValue) {
        type = addressType;
        hashBytes = addressHash;
        asset = "RTM";
        txhash = txid;
        index = indexValue;
    }

    CAddressUnspentKey(unsigned int addressType, uint160 addressHash, std::string assetId, uint256 txid, size_t indexValue) {
        type = addressType;
        hashBytes = addressHash;
        asset = assetId;
        txhash = txid;
        index = indexValue;
    }

    CAddressUnspentKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
        asset = "";
        txhash.SetNull();
        index = 0;
    }
};

struct CAddressUnspentValue {
    CAmount satoshis;
    CScript script;
    std::string asset;
    bool isUnique;
    uint64_t uniqueId;
    int blockHeight;
    int fSpendableHeight;
    int64_t fSpendableTime;

    SERIALIZE_METHODS(CAddressUnspentValue, obj
    )
    {
        READWRITE(obj.satoshis, obj.script, obj.asset, obj.isUnique, obj.uniqueId, obj.blockHeight, obj.fSpendableHeight, obj.fSpendableTime);
    }

    CAddressUnspentValue(CAmount sats, CScript scriptPubKey, int height, int spendableHeight, int spendableTime) {
        satoshis = sats;
        script = scriptPubKey;
        asset = "RTM";
        isUnique = false;
        uniqueId = 0;
        blockHeight = height;
        fSpendableHeight = spendableHeight;
        fSpendableTime = spendableTime;
    }

    CAddressUnspentValue(CAmount sats, CScript scriptPubKey, std::string assetId, bool unique, uint64_t uniqueId, int height, int spendableHeight, int spendableTime) {
        satoshis = sats;
        script = scriptPubKey;
        asset = assetId;
        isUnique = unique;
        uniqueId = uniqueId;
        blockHeight = height;
        fSpendableHeight = spendableHeight;
        fSpendableTime = spendableTime;
    }
    CAddressUnspentValue() {
        SetNull();
    }

    void SetNull() {
        satoshis = -1;
        script.clear();
        asset = "";
        isUnique = false;
        uniqueId = 0;
        blockHeight = 0;
        fSpendableHeight = 0;
        fSpendableTime = 0;
    }

    bool IsNull() const {
        return (satoshis == -1);
    }
};

struct CAddressIndexKey {
    unsigned int type;
    uint160 hashBytes;
    std::string asset;
    int blockHeight;
    unsigned int txindex;
    uint256 txhash;
    size_t index;
    bool spending;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 66;
    }

    template<typename Stream>
    void Serialize(Stream &s) const {
        ser_writedata8(s, type);
        hashBytes.Serialize(s);
        ::Serialize(s, asset);
        // Heights are stored big-endian for key sorting in LevelDB
        ser_writedata32be(s, blockHeight);
        ser_writedata32be(s, txindex);
        txhash.Serialize(s);
        ser_writedata32(s, index);
        char f = spending;
        ser_writedata8(s, f);
    }

    template<typename Stream>
    void Unserialize(Stream &s) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s);
        ::Unserialize(s, asset);
        blockHeight = ser_readdata32be(s);
        txindex = ser_readdata32be(s);
        txhash.Unserialize(s);
        index = ser_readdata32(s);
        char f = ser_readdata8(s);
        spending = f;
    }

    CAddressIndexKey(unsigned int addressType, uint160 addressHash, int height, int blockindex, uint256 txid,
                     size_t indexValue, bool isSpending) {
        type = addressType;
        hashBytes = addressHash;
        asset = "RTM";
        blockHeight = height;
        txindex = blockindex;
        txhash = txid;
        index = indexValue;
        spending = isSpending;
    }

    CAddressIndexKey(unsigned int addressType, uint160 addressHash, std::string assetId, int height, int blockindex, uint256 txid,
                     size_t indexValue, bool isSpending) {
        type = addressType;
        hashBytes = addressHash;
        asset = assetId;
        blockHeight = height;
        txindex = blockindex;
        txhash = txid;
        index = indexValue;
        spending = isSpending;
    }

    CAddressIndexKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
        asset = "";
        blockHeight = 0;
        txindex = 0;
        txhash.SetNull();
        index = 0;
        spending = false;
    }
};

struct CAddressIndexIteratorKey {
    unsigned int type;
    uint160 hashBytes;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 21;
    }

    template<typename Stream>
    void Serialize(Stream &s) const {
        ser_writedata8(s, type);
        hashBytes.Serialize(s);
    }

    template<typename Stream>
    void Unserialize(Stream &s) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s);
    }

    CAddressIndexIteratorKey(unsigned int addressType, uint160 addressHash) {
        type = addressType;
        hashBytes = addressHash;
    }

    CAddressIndexIteratorKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
    }
};

struct CAddressIndexIteratorHeightKey {
    unsigned int type;
    uint160 hashBytes;
    int blockHeight;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 25;
    }

    template<typename Stream>
    void Serialize(Stream &s) const {
        ser_writedata8(s, type);
        hashBytes.Serialize(s);
        ser_writedata32be(s, blockHeight);
    }

    template<typename Stream>
    void Unserialize(Stream &s) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s);
        blockHeight = ser_readdata32be(s);
    }

    CAddressIndexIteratorHeightKey(unsigned int addressType, uint160 addressHash, int height) {
        type = addressType;
        hashBytes = addressHash;
        blockHeight = height;
    }

    CAddressIndexIteratorHeightKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
        blockHeight = 0;
    }
};

#endif // BITCOIN_SPENTINDEX_H
