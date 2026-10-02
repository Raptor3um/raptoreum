// Copyright (c) 2017-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_EVO_CBTX_H
#define BITCOIN_EVO_CBTX_H

#include <primitives/transaction.h>
#include <primitives/txcertificate.h>
#include <univalue.h>

class CBlock;

class CBlockIndex;

class CCoinsViewCache;

class CValidationState;

// coinbase transaction
class CCbTx {
public:
    static const uint16_t CURRENT_VERSION = 2;
    static constexpr uint16_t TX_CERTIFICATE_VERSION = 0x8001;
    // 70-byte base payload, one count byte and at most 41 238-byte entries fit 10 KB.
    static constexpr size_t MAX_CERTIFICATES = 41;

    uint16_t nVersion{CURRENT_VERSION};
    int32_t nHeight{0};
    uint256 merkleRootMNList;
    uint256 merkleRootQuorums;
    std::vector<CTxCertificateEntry> txCertificates;

    SERIALIZE_METHODS(CCbTx, obj
    )
    {
        READWRITE(obj.nVersion, obj.nHeight, obj.merkleRootMNList);
        if (obj.nVersion >= 2) {
            READWRITE(obj.merkleRootQuorums);
        }
        if (obj.nVersion == TX_CERTIFICATE_VERSION) {
            SER_READ(obj, {
                const uint64_t count = ReadCompactSize(s);
                if (count > MAX_CERTIFICATES) throw std::ios_base::failure("too many transaction certificates");
                obj.txCertificates.resize(count);
                for (auto& entry : obj.txCertificates) ::Unserialize(s, entry);
            });
            SER_WRITE(obj, {
                if (obj.txCertificates.size() > MAX_CERTIFICATES)
                    throw std::ios_base::failure("too many transaction certificates");
                WriteCompactSize(s, obj.txCertificates.size());
                for (const auto& entry : obj.txCertificates) ::Serialize(s, entry);
            });
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
