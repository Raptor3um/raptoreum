// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Adapted from evmone's test/state/mpt.{cpp,hpp} — Apache-2.0,
// Copyright 2022 The evmone Authors. Same algorithm and node layout;
// types and helpers swapped to our codebase conventions
// (std::vector<uint8_t> for bytes, evm::Keccak256, evm::RlpEncode*).

#include <evm/mpt.h>

#include <evm/account.h>
#include <evm/hashing.h>
#include <evm/rlp.h>
#include <evm/state_cache.h>
#include <evm/state_db.h>

#include <uint256.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <map>
#include <set>

namespace evm {

namespace {

using bytes = std::vector<uint8_t>;

enum class Kind : uint8_t
{
    Leaf,
    Ext,
    Branch,
};

// A path of 4-bit nibbles, up to 64 long (2 nibbles per byte, max key size = 32).
class Path
{
public:
    static constexpr size_t kCapacity = 64;

    Path() = default;
    Path(const uint8_t* first, const uint8_t* last) : mSize(static_cast<size_t>(last - first))
    {
        assert(mSize <= kCapacity);
        std::copy(first, last, mNibbles);
    }
    explicit Path(const bytes& key) : mSize(2 * key.size())
    {
        assert(mSize <= kCapacity && "MPT key must be <= 32 bytes");
        size_t i = 0;
        for (uint8_t b : key) {
            mNibbles[i++] = static_cast<uint8_t>(b >> 4);
            mNibbles[i++] = static_cast<uint8_t>(b & 0x0f);
        }
    }

    bool empty() const { return mSize == 0; }
    size_t size() const { return mSize; }
    const uint8_t* begin() const { return mNibbles; }
    const uint8_t* end() const { return mNibbles + mSize; }

    // Encode the path bytes per yellow-paper App. D / Eq. (197).
    // Leaf paths are prefixed with 0x20 (terminator) or 0x30 (odd
    // length terminator). Ext paths are 0x00 / 0x10 (odd length).
    bytes Encode(Kind kind) const
    {
        assert(kind == Kind::Leaf || kind == Kind::Ext);
        const uint8_t kindPrefix = (kind == Kind::Leaf) ? 0x20 : 0x00;
        const bool hasOdd = (mSize % 2) != 0;
        const uint8_t nibblePrefix = hasOdd ? static_cast<uint8_t>(0x10 | mNibbles[0]) : 0x00;

        bytes encoded{static_cast<uint8_t>(kindPrefix | nibblePrefix)};
        for (size_t i = hasOdd ? 1 : 0; i < mSize; i += 2) {
            encoded.push_back(static_cast<uint8_t>((mNibbles[i] << 4) | mNibbles[i + 1]));
        }
        return encoded;
    }

private:
    size_t mSize = 0;
    uint8_t mNibbles[kCapacity]{};
};

// Trim leading zero bytes off a 32-byte big-endian value. Used for
// RLP-encoding "stripped" integers (balance, slot value, nonce).
bytes TrimLeadingZeros(const bytes& v)
{
    size_t start = 0;
    while (start < v.size() && v[start] == 0) ++start;
    return bytes(v.begin() + start, v.end());
}

// Convenience: concat two byte vectors.
bytes Concat(const bytes& a, const bytes& b)
{
    bytes out;
    out.reserve(a.size() + b.size());
    out.insert(out.end(), a.begin(), a.end());
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// MPTNode — owns one trie node and recursively encodes itself.
// ---------------------------------------------------------------------------

class MPTNode
{
public:
    static constexpr size_t kNumChildren = 16;

    static std::unique_ptr<MPTNode> Leaf(const Path& path, bytes value)
    {
        return std::unique_ptr<MPTNode>(new MPTNode(Kind::Leaf, path, std::move(value)));
    }

    void Insert(const Path& path, bytes value); // forward decl

    bytes Encode() const;

    MPTNode() = default;
    MPTNode(Kind kind, const Path& path, bytes value = {}) noexcept
        : mKind(kind), mPath(path), mValue(std::move(value)) {}

    Kind mKind = Kind::Leaf;
    Path mPath;
    bytes mValue;
    std::unique_ptr<MPTNode> mChildren[kNumChildren];

private:
    static MPTNode Ext(const Path& path, std::unique_ptr<MPTNode> child)
    {
        assert(child->mKind == Kind::Branch);
        MPTNode node{Kind::Ext, path};
        node.mChildren[0] = std::move(child);
        return node;
    }

    static std::unique_ptr<MPTNode> OptionalExt(const Path& path, std::unique_ptr<MPTNode> child)
    {
        if (!path.empty())
            return std::make_unique<MPTNode>(Ext(path, std::move(child)));
        return child;
    }

    static MPTNode ExtBranch(const Path& path, size_t idx1, std::unique_ptr<MPTNode> child1,
                             size_t idx2, std::unique_ptr<MPTNode> child2)
    {
        assert(idx1 != idx2);
        assert(idx1 < kNumChildren && idx2 < kNumChildren);
        MPTNode br{Kind::Branch, Path{}};
        br.mChildren[idx1] = std::move(child1);
        br.mChildren[idx2] = std::move(child2);
        if (!path.empty())
            return Ext(path, std::make_unique<MPTNode>(std::move(br)));
        return br;
    }
};

void MPTNode::Insert(const Path& path, bytes value)
{
    // Find first mismatching nibble between this node's path and the
    // path being inserted.
    auto mismatch = std::mismatch(mPath.begin(), mPath.end(), path.begin(), path.end());
    const uint8_t* thisIdx = mismatch.first;
    const uint8_t* insertIdx = mismatch.second;

    assert(insertIdx != path.end() && "MPT key must not be a prefix of another key");

    const Path common{mPath.begin(), thisIdx};
    const Path insertTail{insertIdx + 1, path.end()};

    switch (mKind) {
        case Kind::Branch: {
            assert(mPath.empty());
            auto& child = mChildren[*insertIdx];
            if (child) {
                child->Insert(insertTail, std::move(value));
            } else {
                child = Leaf(insertTail, std::move(value));
            }
            break;
        }
        case Kind::Ext: {
            assert(!mPath.empty());
            if (thisIdx == mPath.end()) {
                // Full prefix match — descend into existing branch.
                Path remaining{insertIdx, path.end()};
                mChildren[0]->Insert(remaining, std::move(value));
                return;
            }
            // Split: push existing branch down and build a new branch.
            auto thisBranch = OptionalExt(Path{thisIdx + 1, mPath.end()},
                                          std::move(mChildren[0]));
            auto newLeaf = Leaf(insertTail, std::move(value));
            *this = ExtBranch(common, *thisIdx, std::move(thisBranch),
                              *insertIdx, std::move(newLeaf));
            break;
        }
        case Kind::Leaf: {
            assert(!mPath.empty());
            assert(thisIdx != mPath.end() && "MPT keys must be unique");
            auto thisLeaf = Leaf(Path{thisIdx + 1, mPath.end()}, std::move(mValue));
            auto newLeaf = Leaf(insertTail, std::move(value));
            *this = ExtBranch(common, *thisIdx, std::move(thisLeaf),
                              *insertIdx, std::move(newLeaf));
            break;
        }
    }
}

namespace {

// Wrap a payload as an RLP list (short or long form).
bytes RlpWrapList(const bytes& payload)
{
    return RlpEncodeList(payload);
}

// Encode a child for embedding into a parent: short children inline,
// long ones replaced by an RLP-encoded keccak256 hash.
bytes EncodeChild(const MPTNode& child)
{
    bytes e = child.Encode();
    if (e.size() < 32) return e;
    // Hash and RLP-wrap as bytes.
    uint256 h = Keccak256(e);
    bytes hb(h.begin(), h.end()); // 32 raw hash bytes (byte[0]=MSB)
    return RlpEncodeBytes(hb);
}

} // namespace

bytes MPTNode::Encode() const
{
    bytes encoded;
    switch (mKind) {
        case Kind::Leaf: {
            encoded = Concat(RlpEncodeBytes(mPath.Encode(mKind)),
                             RlpEncodeBytes(mValue));
            break;
        }
        case Kind::Branch: {
            assert(mPath.empty());
            for (const auto& child : mChildren) {
                if (child) {
                    bytes ce = EncodeChild(*child);
                    encoded.insert(encoded.end(), ce.begin(), ce.end());
                } else {
                    encoded.push_back(0x80); // RLP empty string
                }
            }
            encoded.push_back(0x80); // value slot (always empty for our use)
            break;
        }
        case Kind::Ext: {
            encoded = Concat(RlpEncodeBytes(mPath.Encode(mKind)),
                             EncodeChild(*mChildren[0]));
            break;
        }
    }
    return RlpWrapList(encoded);
}

// ---------------------------------------------------------------------------
// MPT public API
// ---------------------------------------------------------------------------

MPT::MPT() noexcept = default;
MPT::~MPT() noexcept = default;

void MPT::Insert(const std::vector<uint8_t>& key, std::vector<uint8_t> value)
{
    assert(key.size() <= 32);
    const Path path{key};
    if (mRoot == nullptr) {
        mRoot = MPTNode::Leaf(path, std::move(value));
    } else {
        mRoot->Insert(path, std::move(value));
    }
}

uint256 MPT::Hash() const
{
    if (mRoot == nullptr) {
        // Canonical empty-trie root: keccak256(rlp("")) = 0x56e8…b421.
        return CEvmAccount::EmptyStorageRoot();
    }
    return Keccak256(mRoot->Encode());
}

// ---------------------------------------------------------------------------
// State-root construction
// ---------------------------------------------------------------------------

namespace {

// Strip leading zero bytes; if all zero, returns empty bytes (the
// canonical encoding of zero per RLP / Ethereum convention).
bytes TrimU256(const uint256& v)
{
    size_t start = 0;
    while (start < 32 && *(v.begin() + start) == 0) ++start;
    return bytes(v.begin() + start, v.end());
}

// 32-byte big-endian copy of a uint256 — same byte order our EVM
// types store internally (byte[0] = MSB).
bytes ToBytes32(const uint256& v)
{
    return bytes(v.begin(), v.end());
}

// 20-byte big-endian copy of a uint160 address.
bytes ToBytes20(const uint160& a)
{
    return bytes(a.begin(), a.end());
}

// Compute storage MPT root for one account's storage map.
uint256 StorageRoot(const std::map<uint256, uint256>& storage)
{
    MPT trie;
    for (const auto& [slot, value] : storage) {
        // EVM convention: zero values are absent. Encoding zero slots
        // would change the root.
        if (value == uint256()) continue;
        uint256 keyHash = Keccak256(ToBytes32(slot));
        bytes keyBytes(keyHash.begin(), keyHash.end());
        // RLP-encode the trimmed value bytes (RLP of stripped big-endian).
        bytes value_rlp = RlpEncodeBytes(TrimU256(value));
        trie.Insert(keyBytes, std::move(value_rlp));
    }
    return trie.Hash();
}

} // namespace

uint256 ComputeStateRoot(const std::vector<StateRootAccount>& accounts)
{
    MPT trie;
    for (const auto& acc : accounts) {
        const bytes addrBytes = ToBytes20(acc.address);
        uint256 keyHash = Keccak256(addrBytes);
        bytes keyBytes(keyHash.begin(), keyHash.end());

        // RLP-encode the account: [nonce, balance, storageRoot, codeHash].
        // Each integer is encoded as stripped big-endian bytes (yellow
        // paper canonical form).
        const uint256 sRoot = StorageRoot(acc.storage);
        const uint256 cHash = acc.code.empty()
                                  ? CEvmAccount::EmptyCodeHash()
                                  : Keccak256(acc.code);

        bytes nonceTrim;
        for (int i = 7; i >= 0; --i) {
            uint8_t b = static_cast<uint8_t>((acc.nonce >> (8 * i)) & 0xFF);
            if (!nonceTrim.empty() || b != 0) nonceTrim.push_back(b);
        }
        bytes balanceTrim = TrimU256(acc.balance);

        bytes payload;
        bytes nonceRlp   = RlpEncodeBytes(nonceTrim);
        bytes balanceRlp = RlpEncodeBytes(balanceTrim);
        bytes srootRlp   = RlpEncodeBytes(ToBytes32(sRoot));
        bytes chashRlp   = RlpEncodeBytes(ToBytes32(cHash));

        payload.insert(payload.end(), nonceRlp.begin(),   nonceRlp.end());
        payload.insert(payload.end(), balanceRlp.begin(), balanceRlp.end());
        payload.insert(payload.end(), srootRlp.begin(),   srootRlp.end());
        payload.insert(payload.end(), chashRlp.begin(),   chashRlp.end());

        bytes value_rlp = RlpEncodeList(payload);
        trie.Insert(keyBytes, std::move(value_rlp));
    }
    return trie.Hash();
}

std::vector<StateRootAccount> CollectAccountsForStateRoot(CEvmStateCache& cache)
{
    // The canonical state root must cover the COMPLETE world state,
    // not just what the current transaction dirtied. Since the
    // pre-state is flushed into the DB before execution (so the
    // EIP-2200 "original" lookup works), accounts the tx never
    // touched live only in the DB. Merge: start from every DB
    // account, then overlay the dirty layer (dirty wins), then drop
    // anything in the deleted set.
    const auto& deleted = cache.DeletedAccounts();
    const auto& dirtyAccts = cache.DirtyAccounts();

    std::map<uint160, CEvmAccount> merged;
    cache.Db().ForEachAccount(
        [&merged](const uint160& a, const CEvmAccount& acc) {
            merged[a] = acc;
        });
    for (const auto& [addr, acc] : dirtyAccts) {
        merged[addr] = acc;
    }

    std::vector<StateRootAccount> out;
    out.reserve(merged.size());
    for (const auto& [addr, acc] : merged) {
        auto delIt = deleted.find(addr);
        if (delIt != deleted.end() && delIt->second) continue;

        // EIP-158/161: an account that is empty (nonce 0, balance 0,
        // no code) is not part of the canonical state trie.
        if (acc.nonce == 0 &&
            acc.balance == uint256() &&
            acc.codeHash == CEvmAccount::EmptyCodeHash())
        {
            continue;
        }

        StateRootAccount entry;
        entry.address = addr;
        entry.nonce = acc.nonce;
        entry.balance = acc.balance;
        if (acc.codeHash != CEvmAccount::EmptyCodeHash()) {
            cache.GetCode(acc.codeHash, entry.code);
        }

        // Storage = DB-persisted slots overlaid with the dirty layer
        // (dirty wins; an explicit zero in dirty clears the slot).
        std::map<uint256, uint256> slots;
        cache.Db().ForEachStorage(
            addr, [&slots](const uint256& slot, const uint256& val) {
                if (val != uint256()) slots[slot] = val;
            });
        for (const auto& [key, value] : cache.DirtyStorage()) {
            if (key.first != addr) continue;
            if (value == uint256()) slots.erase(key.second);
            else slots[key.second] = value;
        }
        entry.storage = std::move(slots);

        out.push_back(std::move(entry));
    }
    return out;
}

} // namespace evm
