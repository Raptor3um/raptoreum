// Copyright (c) 2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string>

#include <primitives/powcache.h>
#include <primitives/block.h>
#include <flat-database.h>
#include <hash.h>
#include <sync.h>
#include <util/system.h>


CPowCache* CPowCache::instance = nullptr;

CPowCache& CPowCache::Instance()
{
    if (CPowCache::instance == nullptr)
    {
        int  powCacheSize     = gArgs.GetArg("-powcachesize", DEFAULT_POW_CACHE_SIZE);
        powCacheValidate = gArgs.GetBoolArg("-powcachevalidate", false); // > 0 ? true : false;
        powCacheSize = powCacheSize == 0 ? DEFAULT_POW_CACHE_SIZE : powCacheSize;

        CPowCache::instance = new CPowCache(powCacheSize, powCacheValidate);
    }
    return *instance;
}

CPowCache::CPowCache(int maxSize, bool validate)
    : unordered_lru_cache<uint256, uint256, std::hash<uint256>>(maxSize)
    , nVersion(CURRENT_VERSION)
    , bValidate(validate)
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
