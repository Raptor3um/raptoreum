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
 * Snapshot support for nested CALL/CREATE revert semantics ships in
 * Phase 2.3d via Snapshot()/Revert()/Commit(). Snapshots compose:
 * deeper savepoints sit inside outer ones, and reverting an outer one
 * also discards everything saved below it.
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

    /** Read the COMMITTED storage value — bypassing the dirty layer,
     *  going straight to the DB. This is the EIP-2200 "original"
     *  value (the slot's value at the start of the transaction)
     *  needed to return the correct 9-state evmc_storage_status from
     *  CEvmHost::set_storage so evmone charges SSTORE gas + refunds
     *  per EIP-2200/3529. Returns false on miss (treat as zero). */
    bool GetCommittedStorage(const uint160& address, const uint256& slot,
                             uint256& out);

    /** EIP-7610 collision predicate: true iff the account has ANY
     *  storage slot whose EFFECTIVE value (dirty layer overlaid on
     *  the committed DB) is non-zero. Must consider committed slots,
     *  not just the dirty layer — after the pre-state flush a
     *  counterfactual address can carry storage that lives only in
     *  the DB. A dirty zero correctly masks a committed non-zero. */
    bool HasNonEmptyStorage(const uint160& address);

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

    // ----------------------------------------------------------------
    // Snapshot / revert (Phase 2.3d, for nested CALL/CREATE)
    // ----------------------------------------------------------------
    //
    // Snapshots capture the dirty layer at a point in time. Reverting
    // restores it; committing simply drops the saved copy. Used by
    // CEvmHost::call() to roll back state changes when a nested frame
    // returns EVMC_REVERT or any failure status.
    //
    // Snapshots compose:
    //   - Nested CALLs each take their own savepoint. A revert of an
    //     outer savepoint discards all savepoints saved below it (the
    //     surrounding frame is going away anyway).
    //   - Commit only the savepoint at the matching id; inner frames
    //     that already committed have their changes preserved.

    /** Capture the current dirty state and return an opaque id. */
    int Snapshot();

    /** Restore the dirty state captured at id. Also drops any
     *  savepoints taken after id. */
    void Revert(int id);

    /** Discard the saved copy at id without touching current state.
     *  Inner savepoints between id and the top of the stack survive. */
    void Commit(int id);

    /** Diagnostics: number of currently-dirty entries by kind. */
    size_t DirtyAccountCount() const { return mAccountsDirty.size(); }
    size_t DirtyStorageCount() const { return mStorageDirty.size(); }
    size_t DirtyCodeCount() const    { return mCodeDirty.size(); }

    // ----------------------------------------------------------------
    // Phase 2.6 — Reorg journaling helpers
    // ----------------------------------------------------------------
    //
    // Const accessors over the dirty layer for callers that need to
    // walk it (currently: BuildUndoFromCache, defined in undo.cpp).
    // They expose internal storage by const reference; the iteration
    // contract assumes the cache is not mutated during the walk.

    const std::map<uint160, CEvmAccount>& DirtyAccounts() const { return mAccountsDirty; }
    const std::map<uint160, bool>& DeletedAccounts() const { return mAccountsDeleted; }
    const std::map<std::pair<uint160, uint256>, uint256>& DirtyStorage() const { return mStorageDirty; }
    const std::map<uint256, std::vector<uint8_t>>& DirtyCode() const { return mCodeDirty; }

    /** Access to the backing DB, for full-state enumeration (the
     *  canonical state-root computation needs accounts/storage that
     *  the current tx never touched and therefore live only in the
     *  flushed DB, not the dirty layer). Non-const because the
     *  underlying CDBWrapper iterator API is non-const even for a
     *  read-only walk. */
    CEvmStateDB& Db() { return db; }

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

    // Savepoint stack for nested CALL/CREATE revert. Each savepoint is
    // a copy of the four dirty maps at the time Snapshot() was called.
    // Copying eagerly trades memory for a simple revert path; for the
    // small dirty sets typical of a single tx this is fine. A journal-
    // based approach (record deltas) is a perf optimization for later.
    struct Savepoint
    {
        int id;
        std::map<uint160, CEvmAccount> accountsDirty;
        std::map<uint160, bool> accountsDeleted;
        std::map<std::pair<uint160, uint256>, uint256> storageDirty;
        std::map<uint256, std::vector<uint8_t>> codeDirty;
    };
    std::vector<Savepoint> mSavepoints;
    int mNextSavepointId{1};
};

} // namespace evm

#endif // RAPTOREUM_EVM_STATE_CACHE_H
