// Copyright (c) 2022-2023 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POWCACHE_H
#define BITCOIN_POWCACHE_H

#include <uint256.h>
#include <sync.h>
#include <serialize.h>
#include <unordered_lru_cache.h>
#include <util/system.h>

extern RecursiveMutex cs_pow;

class CPowCache : public unordered_lru_cache<uint256, uint256, std::hash < uint256>>

{
private:
static CPowCache *instance;
static const int CURRENT_VERSION = 1;

int nVersion;
int nLoadedSize;
int nMaxLoadSize;
bool bValidate;
RecursiveMutex cs;

public:

static CPowCache &Instance();

CPowCache(int maxSize = DEFAULT_POW_CACHE_SIZE, bool validate = DEFAULT_VALIDATE_POW_CACHE,
          int maxLoadSize = DEFAULT_MAX_LOAD_SIZE);

virtual ~

CPowCache();

void Clear();

void CheckAndRemove();

bool IsValidate() const { return bValidate; }

void DoMaintenance();

std::string ToString() const;

ADD_POWCACHE_METHOD

template<typename Stream, typename Operation>
inline void SerializationOp(Stream &s, Operation ser_action) {
    LOCK(cs);
    READWRITE(nVersion);

    uint64_t cacheSize = (uint64_t) cacheMap.size();
    READWRITE(COMPACTSIZE(cacheSize));

    if (ser_action.ForRead()) {
        uint256 headerHash;
        uint256 powHash;
        for (int i = 0; i < cacheSize; ++i) {
            READWRITE(headerHash);
            READWRITE(powHash);
            insert(headerHash, powHash);
        }
        nVersion = CURRENT_VERSION;
        nLoadedSize = cacheMap.size();
    } else {
        for (auto it = cacheMap.begin(); it != cacheMap.end(); ++it) {
            uint256 headerHash = it->first;
            uint256 powHash = it->second.first;
            READWRITE(headerHash);
            READWRITE(powHash);
        };
        nLoadedSize = cacheMap.size();
    }
}

};

#endif // BITCOIN_POWCACHE_H
