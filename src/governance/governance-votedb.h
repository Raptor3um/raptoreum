// Copyright (c) 2014-2019 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_GOVERNANCE_GOVERNANCE_VOTEDB_H
#define BITCOIN_GOVERNANCE_GOVERNANCE_VOTEDB_H

#include <list>
#include <map>

#include <governance/governance-vote.h>
#include <serialize.h>
#include <streams.h>
#include <uint256.h>

/**
 * Represents the collection of votes associated with a given CGovernanceObject
 * Recently received votes are held in memory until a maximum size is reached after
 * which older votes a flushed to a disk file.
 *
 * Note: This is a stub implementation that doesn't limit the number of votes held
 * in memory and doesn't flush to disk.
 */
class CGovernanceObjectVoteFile
{
public: // Types
    using vote_l_t = std::list<CGovernanceVote>;

    using vote_m_t = std::map<uint256, vote_l_t::iterator>;

private:
    int nMemoryVotes;

    vote_l_t listVotes;

    vote_m_t mapVoteIndex;

public:
    CGovernanceObjectVoteFile();

    CGovernanceObjectVoteFile(const CGovernanceObjectVoteFile& other);

    /**
     * Add a vote to the file
     */
    void AddVote(const CGovernanceVote& vote);

    /**
     * Return true if the vote with this hash is currently cached in memory
     */
    bool HasVote(const uint256& nHash) const;

    /**
     * Retrieve a vote cached in memory
     */
    bool SerializeVoteToStream(const uint256& nHash, CDataStream& ss) const;

    int GetVoteCount() const
    {
        return nMemoryVotes;
    }

    std::vector<CGovernanceVote> GetVotes() const;

    void RemoveVotesFromSmartnode(const COutPoint& outpointSmartnode);
    std::set<uint256> RemoveInvalidVotes(const COutPoint& outpointSmartnode, bool fProposal);

    SERIALIZE_METHODS(CGovernanceObjectVoteFile, obj)
    {
        READWRITE(obj.nMemoryVotes, obj.listVotes);
        SER_READ(obj, obj.RebuildIndex());
    }

private:
    // Drop older votes for the same gobject from the same smartnode
    void RemoveOldVotes(const CGovernanceVote& vote);

    void RebuildIndex();
};

#endif // BITCOIN_GOVERNANCE_GOVERNANCE_VOTEDB_H
