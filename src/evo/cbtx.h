// Copyright (c) 2017-2019 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_CBTX_H
#define RAPTOREUM_CBTX_H

#include "consensus/validation.h"
#include "primitives/transaction.h"
#include "univalue.h"

class CBlock;
class CBlockIndex;

// coinbase transaction
class CCbTx
{
public:
    static const uint16_t CURRENT_VERSION = 2;

public:
    uint16_t nVersion{CURRENT_VERSION};
    int32_t nHeight{0};
    uint256 merkleRootMNList;
    uint256 merkleRootQuorums;

public:
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nVersion);
        READWRITE(nHeight);
        READWRITE(merkleRootMNList);

        if (nVersion >= 2) {
            READWRITE(merkleRootQuorums);
        }
    }

    std::string ToString() const;

    void ToJson(UniValue& obj) const
    {
        obj.clear();
        obj.setObject();
        obj.push_back(Pair("version", (int)nVersion));
        obj.push_back(Pair("height", (int)nHeight));
        obj.push_back(Pair("merkleRootMNList", merkleRootMNList.ToString()));
        if (nVersion >= 2) {
            obj.push_back(Pair("merkleRootQuorums", merkleRootQuorums.ToString()));
        }
    }
};

bool CheckCbTx(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state);

bool CheckCbTxMerkleRoots(const CBlock& block, const CBlockIndex* pindex, CValidationState& state);
bool CalcCbTxMerkleRootMNList(const CBlock& block, const CBlockIndex* pindexPrev, uint256& merkleRootRet, CValidationState& state);
bool CalcCbTxMerkleRootQuorums(const CBlock& block, const CBlockIndex* pindexPrev, uint256& merkleRootRet, CValidationState& state);

#endif //RAPTOREUM_CBTX_H
