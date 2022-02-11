// Copyright (c) 2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/powcache.h>
#include <hash.h>
#include <util.h>

CPowCache* CPowCache::instance = nullptr;

CPowCache& CPowCache::Instance()
{
    if (CPowCache::instance == nullptr)
    {
        int powCacheSize = gArgs.GetArg("-powhashcache", DEFAULT_POW_CACHE_SIZE);
        powCacheSize = powCacheSize == 0 ? DEFAULT_POW_CACHE_SIZE : powCacheSize;

        CPowCache::instance = new CPowCache(powCacheSize);
    }
    return *instance;
}

CPowCache::CPowCache(int maxSize) : unordered_lru_cache<uint256, uint256, std::hash<uint256>>(maxSize)
{
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
    info << "Powcache: elements: " << (int)cacheMap.size();
    return info.str();
}
