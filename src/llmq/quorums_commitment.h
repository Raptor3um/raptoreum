// Copyright (c) 2018-2019 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_QUORUMS_COMMITMENT_H
#define RAPTOREUM_QUORUMS_COMMITMENT_H

#include "consensus/params.h"

#include "evo/deterministicmns.h"

#include "bls/bls.h"

#include "univalue.h"

namespace llmq
{

// This message is an aggregation of all received premature commitments and only valid if
// enough (>=threshold) premature commitments were aggregated
// This is mined on-chain as part of TRANSACTION_QUORUM_COMMITMENT
class CFinalCommitment
{
public:
    static const uint16_t CURRENT_VERSION = 1;

public:
    uint16_t nVersion{CURRENT_VERSION};
    Consensus::LLMQType llmqType{Consensus::LLMQ_NONE};
    uint256 quorumHash;
    std::vector<bool> signers;
    std::vector<bool> validMembers;

    CBLSPublicKey quorumPublicKey;
    uint256 quorumVvecHash;

    CBLSSignature quorumSig; // recovered threshold sig of blockHash+validMembers+pubKeyHash+vvecHash
    CBLSSignature membersSig; // aggregated member sig of blockHash+validMembers+pubKeyHash+vvecHash

public:
    CFinalCommitment() {}
    CFinalCommitment(const Consensus::LLMQParams& params, const uint256& _quorumHash);

    int CountSigners() const
    {
        return (int)std::count(signers.begin(), signers.end(), true);
    }
    int CountValidMembers() const
    {
        return (int)std::count(validMembers.begin(), validMembers.end(), true);
    }

    bool Verify(const std::vector<CDeterministicMNCPtr>& members, bool checkSigs) const;
    bool VerifyNull() const;
    bool VerifySizes(const Consensus::LLMQParams& params) const;

public:
    ADD_SERIALIZE_METHODS

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nVersion);
        READWRITE(llmqType);
        READWRITE(quorumHash);
        READWRITE(DYNBITSET(signers));
        READWRITE(DYNBITSET(validMembers));
        READWRITE(quorumPublicKey);
        READWRITE(quorumVvecHash);
        READWRITE(quorumSig);
        READWRITE(membersSig);
    }

public:
    bool IsNull() const
    {
        if (std::count(signers.begin(), signers.end(), true) ||
            std::count(validMembers.begin(), validMembers.end(), true)) {
            return false;
        }
        if (quorumPublicKey.IsValid() ||
            !quorumVvecHash.IsNull() ||
            membersSig.IsValid() ||
            quorumSig.IsValid()) {
            return false;
        }
        return true;
    }

    void ToJson(UniValue& obj) const
    {
        obj.setObject();
        obj.push_back(Pair("version", (int)nVersion));
        obj.push_back(Pair("llmqType", (int)llmqType));
        obj.push_back(Pair("quorumHash", quorumHash.ToString()));
        obj.push_back(Pair("signersCount", CountSigners()));
        obj.push_back(Pair("validMembersCount", CountValidMembers()));
        obj.push_back(Pair("quorumPublicKey", quorumPublicKey.ToString()));
    }
};

class CFinalCommitmentTxPayload
{
public:
    static const uint16_t CURRENT_VERSION = 1;

public:
    uint16_t nVersion{CURRENT_VERSION};
    uint32_t nHeight{(uint32_t)-1};
    CFinalCommitment commitment;

public:
    ADD_SERIALIZE_METHODS

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nVersion);
        READWRITE(nHeight);
        READWRITE(commitment);
    }

    void ToJson(UniValue& obj) const
    {
        obj.setObject();
        obj.push_back(Pair("version", (int)nVersion));
        obj.push_back(Pair("height", (int)nHeight));

        UniValue qcObj;
        commitment.ToJson(qcObj);
        obj.push_back(Pair("commitment", qcObj));
    }
};

bool CheckLLMQCommitment(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state);

} // namespace llmq

#endif //RAPTOREUM_QUORUMS_COMMITMENT_H
