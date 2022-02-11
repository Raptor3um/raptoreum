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
	uint256 headerHash = GetHash();
	uint256 powHash;
	if (!CPowCache::Instance().get(headerHash, powHash))
    {
        // Not found, hash and save
		powHash = HashGR(BEGIN(nVersion), END(nNonce), hashPrevBlock);
		CPowCache::Instance().insert(headerHash, powHash);
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
