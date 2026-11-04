// Copyright (c) 2014-2019 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_GOVERNANCE_GOVERNANCE_VOTE_H
#define BITCOIN_GOVERNANCE_GOVERNANCE_VOTE_H

#include <key.h>
#include <primitives/transaction.h>
#include <bls/bls.h>
#include <pubkey.h>

class CGovernanceVote;

class CConnman;

// INTENTION OF SMARTNODES REGARDING ITEM
enum vote_outcome_enum_t {
    VOTE_OUTCOME_NONE = 0,
    VOTE_OUTCOME_YES = 1,
    VOTE_OUTCOME_NO = 2,
    VOTE_OUTCOME_ABSTAIN = 3
};


// SIGNAL VARIOUS THINGS TO HAPPEN:
enum vote_signal_enum_t {
    VOTE_SIGNAL_NONE = 0,
    VOTE_SIGNAL_FUNDING = 1, //   -- fund this object for it's stated amount
    VOTE_SIGNAL_VALID = 2, //   -- this object checks out in sentinel engine
    VOTE_SIGNAL_DELETE = 3, //   -- this object should be deleted from memory entirely
    VOTE_SIGNAL_ENDORSED = 4, //   -- officially endorsed by the network somehow (delegation)
};

static const int MAX_SUPPORTED_VOTE_SIGNAL = VOTE_SIGNAL_ENDORSED;

/**
* Governance Voting
*
*   Static class for accessing governance data
*/

class CGovernanceVoting {
public:
    static vote_outcome_enum_t ConvertVoteOutcome(const std::string &strVoteOutcome);

    static vote_signal_enum_t ConvertVoteSignal(const std::string &strVoteSignal);

    static std::string ConvertOutcomeToString(vote_outcome_enum_t nOutcome);

    static std::string ConvertSignalToString(vote_signal_enum_t nSignal);
};

//
// CGovernanceVote - Allow a smartnode to vote and broadcast throughout the network
//

class CGovernanceVote {
    friend bool operator==(const CGovernanceVote &vote1, const CGovernanceVote &vote2);

    friend bool operator<(const CGovernanceVote &vote1, const CGovernanceVote &vote2);

public:
    // Wire-valid signature encodings: compact ECDSA voting key, or BLS operator key. Kept in
    // sync via static_assert against CPubKey::COMPACT_SIGNATURE_SIZE / CBLSSignature::SerSize
    // in governance-vote.cpp.
    static constexpr size_t COMPACT_SIG_SIZE = 65;
    static constexpr size_t BLS_SIG_SIZE = 96;

private:
    bool fValid;     //if the vote is currently valid / counted
    bool fSynced;    //if we've sent this to our peers
    int nVoteSignal; // see VOTE_ACTIONS above
    COutPoint smartnodeOutpoint;
    uint256 nParentHash;
    int nVoteOutcome; // see VOTE_OUTCOMES above
    int64_t nTime;
    std::vector<unsigned char> vchSig;

    /** Memory only. */
    const uint256 hash;

    void UpdateHash() const;

public:
    CGovernanceVote();

    CGovernanceVote(const COutPoint &outpointSmartnodeIn, const uint256 &nParentHashIn,
                    vote_signal_enum_t eVoteSignalIn, vote_outcome_enum_t eVoteOutcomeIn);

    bool IsValid() const { return fValid; }

    bool IsSynced() const { return fSynced; }

    int64_t GetTimestamp() const { return nTime; }

    vote_signal_enum_t GetSignal() const { return vote_signal_enum_t(nVoteSignal); }

    vote_outcome_enum_t GetOutcome() const { return vote_outcome_enum_t(nVoteOutcome); }

    const uint256 &GetParentHash() const { return nParentHash; }

    void SetTime(int64_t nTimeIn) {
        nTime = nTimeIn;
        UpdateHash();
    }

    void SetSignature(const std::vector<unsigned char> &vchSigIn) { vchSig = vchSigIn; }

    bool Sign(const CKey &key, const CKeyID &keyID);

    bool CheckSignature(const CKeyID &keyID) const;

    bool Sign(const CBLSSecretKey &key);

    bool CheckSignature(const CBLSPublicKey &pubKey) const;

    bool IsValid(bool useVotingKey) const;

    void Relay(CConnman &connman) const;

    const COutPoint &GetSmartnodeOutpoint() const { return smartnodeOutpoint; }

    /**
    *   GetHash()
    *
    *   GET UNIQUE HASH WITH DETERMINISTIC VALUE OF THIS SPECIFIC VOTE
    */

    uint256 GetHash() const;

    uint256 GetSignatureHash() const;

    std::string ToString() const;

    SERIALIZE_METHODS(CGovernanceVote, obj
    )
    {
        READWRITE(obj.smartnodeOutpoint, obj.nParentHash, obj.nVoteOutcome, obj.nVoteSignal, obj.nTime);
        if (!(s.GetType() & SER_GETHASH)) {
            // Network reads: cap the signature vector before allocation and require one of the
            // two legitimate encodings. Other paths (disk, write) keep the unbounded default --
            // they're never fed attacker-controlled bytes the way a P2P read is.
            if (ser_action.ForRead() && (s.GetType() & SER_NETWORK)) {
                READWRITE(LIMITED_VECTOR(obj.vchSig, BLS_SIG_SIZE));
                if (obj.vchSig.size() != COMPACT_SIG_SIZE && obj.vchSig.size() != BLS_SIG_SIZE) {
                    throw std::ios_base::failure("bad governance vote signature size");
                }
            } else {
                READWRITE(obj.vchSig);
            }
        }
        SER_READ(obj, obj.UpdateHash());
    }
};

#endif // BITCOIN_GOVERNANCE_GOVERNANCE_VOTE_H
