// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_STATE_CACHE_H
#define RAPTOREUM_EVM_STATE_CACHE_H

#include <evm/account.h>
#include <uint256.h>

#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

class uint160;

namespace evm {

class CEvmStateDB;

/**
 * In-memory write-through cache layered over CEvmStateDB.
 *
 * The cache holds **dirty entries** (accounts, storage slots, code
 * blobs) that have been written but not yet persisted, plus a
 * **read-through path** that promotes DB reads into memory so repeated
 * accesses within the same block / EVM execution are O(1).
 *
 * Lifecycle on a block:
 *   1. Construct a cache on top of the persistent DB at block start.
 *   2. EVM transactions in the block call Get/Set on this cache.
 *   3. At block commit, Flush() writes all dirty entries to the DB
 *      atomically. At block discard (failed validation, reorg),
 *      Discard() abandons them.
 *
 * Snapshot support (for EVM CALL/CREATE nested-revert semantics) is
 * out of scope for Phase 2.1 — it lands together with the evmc::Host
 * implementation in Phase 2.2.
 *
 * Thread safety: this class is NOT thread-safe. Each EVM execution
 * gets its own cache instance during the worker-pool phase (per D1),
 * and the merge to a shared parent happens under cs_main.
 */
class CEvmStateCache
{
public:
    explicit CEvmStateCache(CEvmStateDB& dbIn) : db(dbIn) {}

    // ----------------------------------------------------------------
    // Account access
    // ----------------------------------------------------------------

    /** Read the account record. Returns false if the address is unknown
     *  to both the dirty layer and the DB. `out` is touched only on
     *  success. */
    bool GetAccount(const uint160& address, CEvmAccount& out);

    /** Returns true if the address has any state at all (dirty or in DB),
     *  including deletions in flight. */
    bool HasAccount(const uint160& address);

    /** Mark an account as modified. The provided record will be written
     *  to the DB at Flush() time. */
    void SetAccount(const uint160& address, const CEvmAccount& account);

    /** Mark an account for deletion at Flush() time. */
    void DeleteAccount(const uint160& address);

    // ----------------------------------------------------------------
    // Code access (by Keccak-256 of the bytecode)
    // ----------------------------------------------------------------

    bool GetCode(const uint256& codeHash, std::vector<uint8_t>& out);
    void SetCode(const uint256& codeHash, std::vector<uint8_t> code);

    // ----------------------------------------------------------------
    // Storage slot access
    // ----------------------------------------------------------------

    /** Read a 32-byte storage value at the given slot for the given
     *  contract. Returns false on miss (no entry in dirty layer nor DB),
     *  leaving `out` untouched. The EVM convention "missing == zero" is
     *  the caller's responsibility, not this method's. */
    bool GetStorage(const uint160& address, const uint256& slot, uint256& out);

    /** Write a storage slot. Per SSTORE semantics, writing a zero value
     *  is allowed and stays in dirty as a zero (Flush() will EraseStorage
     *  if value.IsNull() to keep the DB compact). */
    void SetStorage(const uint160& address, const uint256& slot, const uint256& value);

    // ----------------------------------------------------------------
    // Commit / discard
    // ----------------------------------------------------------------

    /** Persist all dirty entries to the DB atomically and clear the
     *  dirty layer. Returns false if the underlying DB write fails;
     *  on failure the dirty layer is left intact for the caller to
     *  decide retry or discard. */
    bool Flush();

    /** Drop all dirty entries without writing them. Used when the
     *  surrounding block fails validation. */
    void Discard();

    /** Diagnostics: number of currently-dirty entries by kind. */
    size_t DirtyAccountCount() const { return mAccountsDirty.size(); }
    size_t DirtyStorageCount() const { return mStorageDirty.size(); }
    size_t DirtyCodeCount() const    { return mCodeDirty.size(); }

private:
    CEvmStateDB& db;

    // Dirty layer: entries written to the cache but not yet flushed.
    // Read path checks here before falling back to the DB.
    std::map<uint160, CEvmAccount> mAccountsDirty;
    // Addresses marked for deletion at Flush() time. Present means
    // "deleted in this block"; takes precedence over mAccountsDirty.
    std::map<uint160, bool> mAccountsDeleted;

    // (address, slot) -> value
    std::map<std::pair<uint160, uint256>, uint256> mStorageDirty;

    // codeHash -> bytecode
    std::map<uint256, std::vector<uint8_t>> mCodeDirty;
};

} // namespace evm

#endif // RAPTOREUM_EVM_STATE_CACHE_H
