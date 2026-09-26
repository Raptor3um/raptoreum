// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_DECOUPLEDBLOCK_H
#define BITCOIN_DECOUPLEDBLOCK_H

#include <blockencodings.h>
#include <memusage.h>

#include <functional>
#include <limits>
#include <set>

/** Conservative allocation reservations for one experimental decode/reconstruction.
 * Charges are not refunded: old/new buffers and temporary structures can overlap.
 * Charge externally owned input buffers before allocating or retaining them. Use
 * fresh decode/reconstruction objects and the same budget throughout; a budget is
 * not an allocator and does not account for unrelated RPC/network state.
 */
class CDecoupledReadBudget {
private:
    size_t limit;
    size_t used{0};

public:
    explicit CDecoupledReadBudget(size_t limitIn) : limit(limitIn) {}

    void SetLimit(size_t limitIn)
    {
        if (limitIn < used) throw std::ios_base::failure("decoupled allocation budget exceeded");
        limit = limitIn;
    }

    void ChargeBytes(size_t bytes)
    {
        if (bytes > limit - used) throw std::ios_base::failure("decoupled allocation budget exceeded");
        used += bytes;
    }

    void ChargeArray(size_t count, size_t elementSize)
    {
        if (elementSize != 0 && count > (std::numeric_limits<size_t>::max() - 31) / elementSize) {
            throw std::ios_base::failure("decoupled allocation budget exceeded");
        }
        ChargeBytes(memusage::MallocUsage(count * elementSize));
    }

    void ChargeShared(size_t objectSize)
    {
        ChargeArray(1, objectSize);
        ChargeArray(1, sizeof(memusage::stl_shared_counter));
    }

    void ChargeTransaction(const CTransactionRef& tx);
    size_t GetUsed() const { return used; }
};

/** Non-owning wrapper. The source, its backing bytes and the budget must outlive it.
 * Reuses the normal transaction serializers. Byte vectors reserve a second copy
 * because CTransaction(CMutableTransaction&&) copies the extra payload today.
 */
template<typename Stream>
class CDecoupledBudgetedReader {
private:
    Stream& source;
    CDecoupledReadBudget& budget;

public:
    CDecoupledBudgetedReader(Stream& sourceIn, CDecoupledReadBudget& budgetIn) :
        source(sourceIn), budget(budgetIn) {}

    void read(char* data, size_t size) { source.read(data, size); }
    int GetType() const { return source.GetType(); }
    int GetVersion() const { return source.GetVersion(); }
    void ChargeReadAllocation(size_t count, size_t elementSize) { budget.ChargeArray(count, elementSize); }
    void ChargeReadSharedAllocation(size_t objectSize) { budget.ChargeShared(objectSize); }

    template<typename T>
    CDecoupledBudgetedReader& operator>>(T&& object)
    {
        ::Unserialize(*this, std::forward<T>(object));
        return *this;
    }
};

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
        ReadAllocation(s, bodies, sizeof(CTransactionRef), 0);
        vtx.resize(bodies);
        for (auto& tx : vtx) s >> tx;
        const uint64_t references = ReadCompactSize(s);
        if (references > MAX_TRANSACTIONS - bodies) {
            throw std::ios_base::failure("decoupled block transaction count out of range");
        }
        ReadAllocation(s, references, sizeof(CDecoupledTxRef), 0);
        vtxids.resize(references);
        for (auto& ref : vtxids) s >> ref;
        // Each set node is a separate allocation; include its allocator overhead.
        ReadAllocation(s, bodies + references, memusage::MallocUsage(sizeof(memusage::stl_tree_node<uint256>)), 0);
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
    // Pass the decode budget to every step. Resolved/missing bodies are charged
    // before being retained; callers must also budget their response buffers.
    // INVALID describes the encoding, never permanent consensus invalidity of its header.
    // A failed lookup/fill returns FAILED and must trigger retry or full-block fallback.
    ReadStatus InitData(const CDecoupledBlock& block,
                        const std::function<CTransactionRef(const uint256&)>& lookup,
                        CDecoupledReadBudget* budget = nullptr);
    std::vector<uint16_t> GetMissingIndexes(CDecoupledReadBudget* budget = nullptr) const;
    ReadStatus FillBlock(CBlock& block, const std::vector<CTransactionRef>& vtx_missing,
                         CDecoupledReadBudget* budget = nullptr);
};

#endif // BITCOIN_DECOUPLEDBLOCK_H
