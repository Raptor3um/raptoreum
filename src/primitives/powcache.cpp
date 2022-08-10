// Copyright (c) 2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/powcache.h>
#include <primitives/block.h>
#include <flat-database.h>
#include <hash.h>
#include <sync.h>
#include <util.h>

CCriticalSection cs_pow;

CPowCache* CPowCache::instance = nullptr;

CPowCache& CPowCache::Instance()
{
    if (CPowCache::instance == nullptr)
    {
        int  powCacheSize     = gArgs.GetArg("-powcachesize", DEFAULT_POW_CACHE_SIZE);
        bool powCacheValidate = gArgs.GetArg("-powcachevalidate", 0) > 0 ? true : false;
        int  maxLoadSize     = gArgs.GetArg("-powmaxloadsize", DEFAULT_MAX_LOAD_SIZE);

        powCacheSize = powCacheSize == 0 ? DEFAULT_POW_CACHE_SIZE : powCacheSize;

        CPowCache::instance = new CPowCache(powCacheSize, powCacheValidate, maxLoadSize);
    }
    return *instance;
}

void CPowCache::DoMaintenance()
{
    LOCK(cs_pow);
    // If cache has grown enough, save it:
    if (cacheMap.size() - nLoadedSize > nMaxLoadSize)
    {
        CFlatDB<CPowCache> flatDb("powcache.dat", "powCache");
        flatDb.Dump(*this);
    }
}

CPowCache::CPowCache(int maxSize, bool validate, int maxLoadSize) : unordered_lru_cache<uint256, uint256, std::hash<uint256>>(maxSize),
   nVersion(CURRENT_VERSION),
   nLoadedSize(0),
   bValidate(validate),
   nMaxLoadSize(maxLoadSize)
{
    if (bValidate) LogPrintf("PowCache: Validation and auto correction enabled\n");
}

CPowCache::~CPowCache()
{
}

void CPowCache::Clear()
{
   cacheMap.clear();
}

void CPowCache::CheckAndRemove()
{
}

std::string CPowCache::ToString() const
{
    std::ostringstream info;
    info << "PowCache: elements: " << (int)cacheMap.size();
    return info.str();
}
