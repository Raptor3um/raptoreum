// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_DECOUPLEDBLOCK_H
#define BITCOIN_DECOUPLEDBLOCK_H

#include <blockencodings.h>

#include <functional>
#include <limits>
#include <set>

struct CDecoupledTxRef {
    uint16_t index;
    uint256 txid;

    SERIALIZE_METHODS(CDecoupledTxRef, obj) { READWRITE(obj.index, obj.txid); }
};

/** Versioned transport only; CBlock remains the canonical storage/consensus object. */
class CDecoupledBlock {
private:
    bool IsValidLayout() const;
    friend class PartiallyDownloadedDecoupledBlock;

public:
    static constexpr uint16_t VERSION = 1;
    static constexpr size_t MAX_TRANSACTIONS = std::numeric_limits<uint16_t>::max();

    uint16_t version{VERSION};
    CBlockHeader header;
    std::vector<CTransactionRef> vtx;
    std::vector<CDecoupledTxRef> vtxids;

    CDecoupledBlock() = default;
    CDecoupledBlock(const CBlock& block, const std::set<uint256>& referenceIDs);

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        if (!IsValidLayout()) throw std::ios_base::failure("invalid decoupled block layout");
        s << version << header << vtx << vtxids;
    }

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        vtx.clear();
        vtxids.clear();
        s >> version;
        if (version != VERSION) throw std::ios_base::failure("unsupported decoupled block version");
        s >> header;
        const uint64_t bodies = ReadCompactSize(s);
        if (bodies == 0 || bodies > MAX_TRANSACTIONS) {
            throw std::ios_base::failure("decoupled block transaction count out of range");
        }
        vtx.resize(bodies);
        for (auto& tx : vtx) s >> tx;
        const uint64_t references = ReadCompactSize(s);
        if (references > MAX_TRANSACTIONS - bodies) {
            throw std::ios_base::failure("decoupled block transaction count out of range");
        }
        vtxids.resize(references);
        for (auto& ref : vtxids) s >> ref;
        if (!IsValidLayout()) throw std::ios_base::failure("invalid decoupled block layout");
    }
};

/** Resolves exact txids without applying mempool policy or contextual block checks. */
class PartiallyDownloadedDecoupledBlock {
private:
    CBlockHeader header;
    std::vector<CTransactionRef> txn_available;
    std::vector<CDecoupledTxRef> missing;

public:
    // INVALID describes the encoding, never permanent consensus invalidity of its header.
    // A failed lookup/fill returns FAILED and must trigger retry or full-block fallback.
    ReadStatus InitData(const CDecoupledBlock& block,
                        const std::function<CTransactionRef(const uint256&)>& lookup);
    std::vector<uint16_t> GetMissingIndexes() const;
    ReadStatus FillBlock(CBlock& block, const std::vector<CTransactionRef>& vtx_missing);
};

#endif // BITCOIN_DECOUPLEDBLOCK_H
