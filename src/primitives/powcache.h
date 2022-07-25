// Copyright (c) 2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POWCACHE_H
#define BITCOIN_POWCACHE_H

#include <uint256.h>
#include <sync.h>
#include <serialize.h>
#include <unordered_lru_cache.h>
#include <util/system.h>

// Default size of ProofOfWork cache in megabytes.
static const uint64_t DEFAULT_POW_CACHE_SIZE = 50;
// Default for -powcachevalidate.
static const bool DEFAULT_VALIDATE_POW_CACHE = false;

class CPowCache : public unordered_lru_cache<uint256, uint256, std::hash<uint256>>
{
private:
    static CPowCache* instance;
    static const int CURRENT_VERSION = 1;

    int nVersion;
    bool bValidate;
    RecursiveMutex cs;

public:
    static CPowCache& Instance();

    CPowCache(uint64_t maxSize = DEFAULT_POW_CACHE_SIZE, bool validate = DEFAULT_VALIDATE_POW_CACHE);
    virtual ~CPowCache();

    void Clear();
    void CheckAndRemove();
    bool IsValidate() const { return bValidate; }

    std::string ToString() const;

    ADD_POWCACHE_METHOD;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        LOCK(cs);
        READWRITE(nVersion);

        uint64_t cacheSize = (uint64_t)cacheMap.size();
        READWRITE(COMPACTSIZE(cacheSize));

        if (ser_action.ForRead())
        {
            uint256 headerHash;
            uint256 powHash;
            for (int i = 0; i < cacheSize; ++i)
            {
                READWRITE(headerHash);
                READWRITE(powHash);
                insert(headerHash, powHash);
            }
            nVersion = CURRENT_VERSION;
        }
        else
        {
            for (auto it = cacheMap.begin(); it != cacheMap.end(); ++it)
            {
                uint256 headerHash = it->first;
                uint256 powHash    = it->second.first;
                READWRITE(headerHash);
                READWRITE(powHash);
            };
        }
    }

/*
    template<typename Stream>
    void Serialize(Stream& s) const
    {
        uint64_t cacheSize = (uint64_t)cacheMap.size();

        s << nVersion;
        s << COMPACTSIZE(cacheSize);
        for (auto it = cacheMap.begin(); it != cacheMap.end(); ++it)
        {
            uint256 headerHash = it->first;
            uint256 powHash = it->second.first;
            s << headerHash;
            s << powHash;
        }
    }

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        nVersion = CURRENT_VERSION;
        nLoadedSize = cacheMap.size();
        uint64_t cacheSize = (uint64_t)cacheMap.size();
        uint256 headerHash, powHash;
        s >> nVersion;
        s >> COMPACTSIZE(cacheSize);
        for (int i = 0; i < cacheSize; ++i)
        {
            s >> headerHash;
            s >> powHash;
            insert(headerHash, powHash);
        }
    }
*/
};

#endif // RTM_POWCACHE_H
