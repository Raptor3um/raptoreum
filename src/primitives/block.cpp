// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>
#include <primitives/powcache.h>
#include <uint256.h>
#include <hash.h>
#include <utilstrencodings.h>
#include <util.h>

uint256 CBlockHeader::GetHash() const
{
	return SerializeHash(*this);
}

uint256 CBlockHeader::GetPOWHash() const
{
    CPowCache& cache(CPowCache::Instance());

	uint256 headerHash = GetHash();
	uint256 powHash;
	if (!cache.get(headerHash, powHash))
    {
        // Not found, hash and save
		powHash = HashGR(BEGIN(nVersion), END(nNonce), hashPrevBlock);
		cache.insert(headerHash, powHash);
	}
    else
    {
        if (cache.IsValidate())
        {
            // Validate PowCache and correct if needed
            uint256 powHash2 = HashGR(BEGIN(nVersion), END(nNonce), hashPrevBlock);
            if (powHash2 != powHash)
            {
                LogPrintf("PowCache failure: headerHash: %s, from cache: %s, computed: %s, correcting\n", headerHash.ToString(), powHash.ToString(), powHash2.ToString());
        		cache.insert(headerHash, powHash2);
            }
        }
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
