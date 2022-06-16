// Copyright (c) 2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RTM_POWCACHE_H
#define RTM_POWCACHE_H

#include <uint256.h>
#include <serialize.h>
#include <unordered_lru_cache.h>
#include <util/system.h>

#include <cachemap.h>

class CPowCache : public unordered_lru_cache<uint256, uint256, std::hash<uint256>>
{
private:
    static CPowCache* instance;

public:
    static const int CURRENT_VERSION = 1;

    bool bValidate;
    int nVersion{CURRENT_VERSION};

    static CPowCache& Instance();

    CPowCache(int maxSize = DEFAULT_POW_CACHE_SIZE, bool validate = false);
    virtual ~CPowCache();

    void Clear();
    void CheckAndRemove();
    bool IsValidate() const { return bValidate; }

    std::string ToString() const;

    uint64_t cacheSize = cacheMap.size();

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        s << nVersion << COMPACTSIZE(cacheSize);
        for (auto it = cacheMap.begin(); it != cacheMap.end(); ++it)
        {
            uint256 headerHash = it->first;
            uint256 powHash = it->second.first;
            s << headerHash << powHash;
        }
    }

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        uint256 headerHash, powHash;
        s >> nVersion >> COMPACTSIZE(cacheSize);
        for (int i = 0; i < cacheSize; ++i)
        {
            s >> headerHash >> powHash;
            insert(headerHash, powHash);
        }
    }
};

#endif // RTM_POWCACHE_H
