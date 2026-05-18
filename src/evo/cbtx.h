// Copyright (c) 2017-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_EVO_CBTX_H
#define BITCOIN_EVO_CBTX_H

#include <primitives/transaction.h>
#include <univalue.h>

class CBlock;

class CBlockIndex;

class CCoinsViewCache;

class CValidationState;

// coinbase transaction
class CCbTx {
public:
    static const uint16_t CURRENT_VERSION = 2;

    // D2 hard-fork: coinbase-committed EVM consensus roots live in
    // CCbTx version 3, NOT in the 80-byte CBlockHeader (which would
    // change PoW / the block hash and break every pool, miner and
    // SPV client). This reuses the proven version-gated additive
    // serialization that DIP0008 already used to add
    // merkleRootQuorums at v2 — old-format blocks stay byte-identical.
    //
    // CURRENT_VERSION stays 2 until the activation increment wires the
    // fixed-height hard-fork gate; until then CheckCbTx rejects v3, so
    // v3 is unconstructible in production and these fields are inert.
    static const uint16_t EVM_COMMIT_VERSION = 3;

    uint16_t nVersion{CURRENT_VERSION};
    int32_t nHeight{0};
    uint256 merkleRootMNList;
    uint256 merkleRootQuorums;
    // --- v3 (D2) EVM commitments. Only (de)serialized at nVersion>=3.
    uint256 evmStateRoot;       // MPT root of the full EVM world state
    uint256 evmReceiptsRoot;    // trie root of this block's receipts
    uint64_t evmBaseFee{0};     // EIP-1559 base fee (weis) for the block
    uint64_t evmGasUsed{0};     // total EVM gas used by this block.
                                // Committed so the NEXT block derives
                                // its EIP-1559 base fee from the
                                // parent without re-executing it
                                // (exactly why Ethereum's header
                                // carries gasUsed + baseFeePerGas).

    SERIALIZE_METHODS(CCbTx, obj
    )
    {
        READWRITE(obj.nVersion, obj.nHeight, obj.merkleRootMNList);
        if (obj.nVersion >= 2) {
            READWRITE(obj.merkleRootQuorums);
        }
        if (obj.nVersion >= EVM_COMMIT_VERSION) {
            READWRITE(obj.evmStateRoot, obj.evmReceiptsRoot,
                      obj.evmBaseFee, obj.evmGasUsed);
        }
    }

    std::string ToString() const;

    void ToJson(UniValue &obj) const {
        obj.clear();
        obj.setObject();
        obj.pushKV("version", (int) nVersion);
        obj.pushKV("height", nHeight);
        obj.pushKV("merkleRootMNList", merkleRootMNList.ToString());
        if (nVersion >= 2) {
            obj.pushKV("merkleRootQuorums", merkleRootQuorums.ToString());
        }
        if (nVersion >= EVM_COMMIT_VERSION) {
            obj.pushKV("evmStateRoot", evmStateRoot.ToString());
            obj.pushKV("evmReceiptsRoot", evmReceiptsRoot.ToString());
            obj.pushKV("evmBaseFee", (uint64_t) evmBaseFee);
            obj.pushKV("evmGasUsed", (uint64_t) evmGasUsed);
        }
    }
};

bool CheckCbTx(const CTransaction &tx, const CBlockIndex *pindexPrev, CValidationState &state);

bool CheckCbTxMerkleRoots(const CBlock &block, const CBlockIndex *pindex, CValidationState &state,
                          const CCoinsViewCache &view);

bool CalcCbTxMerkleRootMNList(const CBlock &block, const CBlockIndex *pindexPrev, uint256 &merkleRootRet,
                              CValidationState &state, const CCoinsViewCache &view);

bool CalcCbTxMerkleRootQuorums(const CBlock &block, const CBlockIndex *pindexPrev, uint256 &merkleRootRet,
                               CValidationState &state);

#endif // BITCOIN_EVO_CBTX_H
