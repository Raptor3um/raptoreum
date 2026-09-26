// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <decoupledblock.h>

#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <core_memusage.h>
#include <version.h>

#include <stdexcept>

void CDecoupledReadBudget::ChargeTransaction(const CTransactionRef& tx)
{
    if (!tx) return;
    ChargeBytes(memusage::DynamicUsage(tx));
    ChargeBytes(RecursiveDynamicUsage(*tx));
    ChargeBytes(memusage::DynamicUsage(tx->vExtraPayload));
}

CDecoupledBlock::CDecoupledBlock(const CBlock& block, const std::set<uint256>& referenceIDs) :
    header(block.GetBlockHeader())
{
    if (block.vtx.empty() || block.vtx.size() > MAX_TRANSACTIONS) {
        throw std::invalid_argument("decoupled block transaction count out of range");
    }
    for (size_t i = 0; i < block.vtx.size(); ++i) {
        const auto& tx = block.vtx[i];
        if (!tx) throw std::invalid_argument("null decoupled block transaction");
        if (i != 0 && referenceIDs.count(tx->GetHash())) {
            vtxids.push_back({uint16_t(i), tx->GetHash()});
        } else {
            vtx.push_back(tx);
        }
    }
    if (!IsValidLayout()) throw std::invalid_argument("invalid decoupled block layout");
}

bool CDecoupledBlock::IsValidLayout() const
{
    if (version != VERSION || header.IsNull() || vtx.empty() || vtx.size() > MAX_TRANSACTIONS ||
        vtxids.size() > MAX_TRANSACTIONS - vtx.size()) return false;
    if (!vtx[0] || !vtx[0]->IsCoinBase()) return false;

    std::set<uint256> txids;
    for (size_t i = 0; i < vtx.size(); ++i) {
        const auto& tx = vtx[i];
        if (!tx || (i != 0 && tx->IsCoinBase()) || !txids.insert(tx->GetHash()).second) {
            return false;
        }
    }
    size_t previous = 0;
    const size_t count = vtx.size() + vtxids.size();
    for (const auto& ref : vtxids) {
        if (ref.index <= previous || ref.index >= count || !txids.insert(ref.txid).second) return false;
        previous = ref.index;
    }
    return true;
}

ReadStatus PartiallyDownloadedDecoupledBlock::InitData(
    const CDecoupledBlock& block, const std::function<CTransactionRef(const uint256&)>& lookup,
    CDecoupledReadBudget* budget)
{
    header.SetNull();
    txn_available.clear();
    missing.clear();
    if (budget) {
        budget->ChargeArray(block.vtx.size() + block.vtxids.size(),
                            memusage::MallocUsage(sizeof(memusage::stl_tree_node<uint256>)));
    }
    if (!block.IsValidLayout()) return READ_STATUS_INVALID;

    if (budget) budget->ChargeArray(block.vtx.size() + block.vtxids.size(), sizeof(CTransactionRef));
    std::vector<CTransactionRef> available(block.vtx.size() + block.vtxids.size());
    std::vector<CDecoupledTxRef> unresolved;
    if (budget) {
        budget->ChargeArray(block.vtxids.size(), sizeof(CDecoupledTxRef));
        unresolved.reserve(block.vtxids.size());
    }
    size_t body = 0, reference = 0;
    for (size_t i = 0; i < available.size(); ++i) {
        if (reference < block.vtxids.size() && block.vtxids[reference].index == i) {
            const auto& ref = block.vtxids[reference++];
            auto tx = lookup ? lookup(ref.txid) : CTransactionRef();
            if (tx && tx->GetHash() != ref.txid) return READ_STATUS_FAILED;
            if (budget) budget->ChargeTransaction(tx);
            if (!tx) unresolved.push_back(ref);
            available[i] = std::move(tx);
        } else {
            available[i] = block.vtx[body++];
        }
    }
    header = block.header;
    txn_available = std::move(available);
    missing = std::move(unresolved);
    return READ_STATUS_OK;
}

std::vector<uint16_t> PartiallyDownloadedDecoupledBlock::GetMissingIndexes(CDecoupledReadBudget* budget) const
{
    std::vector<uint16_t> indexes;
    if (budget) budget->ChargeArray(missing.size(), sizeof(uint16_t));
    indexes.reserve(missing.size());
    for (const auto& ref : missing) indexes.push_back(ref.index);
    return indexes;
}

ReadStatus PartiallyDownloadedDecoupledBlock::FillBlock(
    CBlock& block, const std::vector<CTransactionRef>& vtx_missing, CDecoupledReadBudget* budget)
{
    if (header.IsNull() || vtx_missing.size() != missing.size()) return READ_STATUS_FAILED;
    for (size_t i = 0; i < missing.size(); ++i) {
        if (!vtx_missing[i] || vtx_missing[i]->GetHash() != missing[i].txid) return READ_STATUS_FAILED;
    }

    if (budget) {
        budget->ChargeArray(vtx_missing.capacity(), sizeof(CTransactionRef));
        for (const auto& tx : vtx_missing) budget->ChargeTransaction(tx);
        budget->ChargeArray(txn_available.size(), sizeof(CTransactionRef));
    }
    CBlock reconstructed(header);
    reconstructed.vtx = txn_available;
    for (size_t i = 0; i < missing.size(); ++i) reconstructed.vtx[missing[i].index] = vtx_missing[i];
    if (GetSerializeSize(reconstructed, SER_NETWORK, PROTOCOL_VERSION) > MaxBlockSize()) return READ_STATUS_FAILED;
    if (budget) {
        const auto count = reconstructed.vtx.size();
        budget->ChargeArray(count, sizeof(uint256));
        // An odd initial leaf count makes ComputeMerkleRoot grow its vector.
        if (count > 1 && (count & 1)) budget->ChargeArray(2 * count, sizeof(uint256));
    }
    bool mutated = false;
    if (BlockMerkleRoot(reconstructed, &mutated) != header.hashMerkleRoot || mutated) return READ_STATUS_FAILED;

    // Validation uses this block's actual parent later; never inherit fChecked from a caller/cache.
    reconstructed.fChecked = false;
    block = std::move(reconstructed);
    header.SetNull();
    txn_available.clear();
    missing.clear();
    return READ_STATUS_OK;
}
