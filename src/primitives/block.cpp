// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/block.h"

#include "hash.h"
#include "streams.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "crypto/common.h"
#include "unordered_lru_cache.h"

//commented out for now as window build error here
//#include "util.h"
//unordered_lru_cache<uint256, uint256, std::hash<uint256>, 1> powHashCache;
//void initializePowCacheIfNeeded() {
//	if(powHashCache.getMaxSize() == 1) {
//		int powCacheSize = gArgs.GetArg("-powhashcache", DEFAULT_POW_CACHE_SIZE);
//		powCacheSize = powCacheSize == 0 ? DEFAULT_POW_CACHE_SIZE : powCacheSize;
//		powHashCache.setMaxSize(powCacheSize);
//
//	}
//}
unordered_lru_cache<uint256, uint256, std::hash<uint256>, 200000> powHashCache;

uint256 CBlockHeader::GetHash() const
{
	return SerializeHash(*this);
}

uint256 CBlockHeader::GetPOWHash() const
{

//	initializePowCacheIfNeeded();
	uint256 headerHash = GetHash();
	uint256 powHash;
	if(powHashCache.get(headerHash, powHash)) {
		//do nothing
	} else {
		powHash = HashGR(BEGIN(nVersion), END(nNonce), hashPrevBlock);
		powHashCache.insert(headerHash, powHash);
	}
	return powHash;
}


std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce,
        vtx.size());
    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}
