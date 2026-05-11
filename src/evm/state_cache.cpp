// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/state_cache.h>

#include <evm/state_db.h>
#include <uint256.h>

namespace evm {

// ----------------------------------------------------------------------
// Account
// ----------------------------------------------------------------------

bool CEvmStateCache::GetAccount(const uint160& address, CEvmAccount& out)
{
    // Deletion in dirty layer wins — the account does not exist for
    // anyone who reads through this cache.
    if (mAccountsDeleted.count(address)) {
        return false;
    }
    auto it = mAccountsDirty.find(address);
    if (it != mAccountsDirty.end()) {
        out = it->second;
        return true;
    }
    return db.ReadAccount(address, out);
}

bool CEvmStateCache::HasAccount(const uint160& address)
{
    if (mAccountsDeleted.count(address)) {
        return false;
    }
    if (mAccountsDirty.count(address)) {
        return true;
    }
    return db.HasAccount(address);
}

void CEvmStateCache::SetAccount(const uint160& address, const CEvmAccount& account)
{
    // A SetAccount supersedes any pending deletion.
    mAccountsDeleted.erase(address);
    mAccountsDirty[address] = account;
}

void CEvmStateCache::DeleteAccount(const uint160& address)
{
    mAccountsDirty.erase(address);
    mAccountsDeleted[address] = true;
}

// ----------------------------------------------------------------------
// Code
// ----------------------------------------------------------------------

bool CEvmStateCache::GetCode(const uint256& codeHash, std::vector<uint8_t>& out)
{
    auto it = mCodeDirty.find(codeHash);
    if (it != mCodeDirty.end()) {
        out = it->second;
        return true;
    }
    return db.ReadCode(codeHash, out);
}

void CEvmStateCache::SetCode(const uint256& codeHash, std::vector<uint8_t> code)
{
    mCodeDirty[codeHash] = std::move(code);
}

// ----------------------------------------------------------------------
// Storage
// ----------------------------------------------------------------------

bool CEvmStateCache::GetStorage(const uint160& address, const uint256& slot, uint256& out)
{
    auto key = std::make_pair(address, slot);
    auto it = mStorageDirty.find(key);
    if (it != mStorageDirty.end()) {
        out = it->second;
        return true;
    }
    return db.ReadStorage(address, slot, out);
}

void CEvmStateCache::SetStorage(const uint160& address, const uint256& slot, const uint256& value)
{
    mStorageDirty[std::make_pair(address, slot)] = value;
}

// ----------------------------------------------------------------------
// Commit / discard
// ----------------------------------------------------------------------

bool CEvmStateCache::Flush()
{
    // Apply deletions first so a later Write(same address) wins.
    for (const auto& kv : mAccountsDeleted) {
        if (!db.EraseAccount(kv.first)) {
            return false;
        }
    }
    for (const auto& kv : mAccountsDirty) {
        if (!db.WriteAccount(kv.first, kv.second)) {
            return false;
        }
    }
    // Code is content-addressed by Keccak-256, so we always write
    // (collisions are impossible by design; rewriting the same value
    // is a no-op for LevelDB except for the lookup cost).
    for (const auto& kv : mCodeDirty) {
        if (!db.WriteCode(kv.first, kv.second)) {
            return false;
        }
    }
    // SSTORE convention: a zero value clears the slot from storage
    // (refunds gas in real EVM execution). We honor that on flush so
    // the on-disk form stays compact.
    for (const auto& kv : mStorageDirty) {
        const auto& addr = kv.first.first;
        const auto& slot = kv.first.second;
        const auto& value = kv.second;
        bool ok;
        if (value.IsNull()) {
            ok = db.EraseStorage(addr, slot);
        } else {
            ok = db.WriteStorage(addr, slot, value);
        }
        if (!ok) {
            return false;
        }
    }

    // Successful commit: clear dirty layer.
    mAccountsDirty.clear();
    mAccountsDeleted.clear();
    mCodeDirty.clear();
    mStorageDirty.clear();
    return true;
}

void CEvmStateCache::Discard()
{
    mAccountsDirty.clear();
    mAccountsDeleted.clear();
    mCodeDirty.clear();
    mStorageDirty.clear();
    mSavepoints.clear();
}

// ----------------------------------------------------------------------
// Snapshot / revert (Phase 2.3d, for nested CALL/CREATE)
// ----------------------------------------------------------------------

int CEvmStateCache::Snapshot()
{
    Savepoint sp;
    sp.id = mNextSavepointId++;
    sp.accountsDirty = mAccountsDirty;
    sp.accountsDeleted = mAccountsDeleted;
    sp.storageDirty = mStorageDirty;
    sp.codeDirty = mCodeDirty;
    mSavepoints.push_back(std::move(sp));
    return mSavepoints.back().id;
}

void CEvmStateCache::Revert(int id)
{
    // Find the savepoint with the given id; drop everything above it
    // (those frames are gone — outer revert subsumes inner state).
    for (size_t i = mSavepoints.size(); i-- > 0; ) {
        if (mSavepoints[i].id == id) {
            // Restore from this savepoint.
            mAccountsDirty = std::move(mSavepoints[i].accountsDirty);
            mAccountsDeleted = std::move(mSavepoints[i].accountsDeleted);
            mStorageDirty = std::move(mSavepoints[i].storageDirty);
            mCodeDirty = std::move(mSavepoints[i].codeDirty);
            // Drop this savepoint and any nested ones above it.
            mSavepoints.resize(i);
            return;
        }
    }
    // Unknown id: silently no-op rather than throwing — the host wraps
    // every call() in Snapshot/Revert and we don't want a programming
    // bug here to corrupt consensus.
}

void CEvmStateCache::Commit(int id)
{
    // Drop the savepoint with the matching id. Inner savepoints
    // (deeper in the stack) survive — they belong to frames that
    // already committed within the now-also-committed outer frame.
    for (size_t i = mSavepoints.size(); i-- > 0; ) {
        if (mSavepoints[i].id == id) {
            mSavepoints.erase(mSavepoints.begin() + i);
            return;
        }
    }
}

} // namespace evm
