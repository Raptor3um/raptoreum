// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/undo.h>

#include <evm/state_cache.h>
#include <evm/state_db.h>

namespace evm {

CEvmStateUndo BuildUndoFromCache(const CEvmStateCache& cache,
                                 const CEvmStateDB& db)
{
    CEvmStateUndo out;

    // -- Accounts: dirty entries (writes / re-writes) ----------------
    // For every account in the dirty layer, we record what was at the
    // DB before this block ran. If the account didn't exist, the
    // record carries existedBefore=false.
    for (const auto& kv : cache.DirtyAccounts()) {
        AccountChangeUndo rec;
        rec.address = kv.first;
        CEvmAccount prev;
        if (db.ReadAccount(rec.address, prev)) {
            rec.existedBefore = true;
            rec.before = prev;
        } else {
            rec.existedBefore = false;
        }
        out.accountChanges.push_back(std::move(rec));
    }

    // -- Accounts: explicit deletions --------------------------------
    // Deletions are also dirty for our purposes — we need to restore
    // the previous account. (If the prior cache layer was empty too,
    // existedBefore=false and the undo is a no-op for this entry.)
    for (const auto& kv : cache.DeletedAccounts()) {
        AccountChangeUndo rec;
        rec.address = kv.first;
        CEvmAccount prev;
        if (db.ReadAccount(rec.address, prev)) {
            rec.existedBefore = true;
            rec.before = prev;
        } else {
            rec.existedBefore = false;
        }
        out.accountChanges.push_back(std::move(rec));
    }

    // -- Storage slots ----------------------------------------------
    for (const auto& kv : cache.DirtyStorage()) {
        StorageChangeUndo rec;
        rec.address = kv.first.first;
        rec.slot = kv.first.second;
        uint256 prev;
        if (db.ReadStorage(rec.address, rec.slot, prev)) {
            rec.existedBefore = true;
            rec.before = prev;
        } else {
            rec.existedBefore = false;
        }
        out.storageChanges.push_back(std::move(rec));
    }

    // -- Newly-installed code blobs ---------------------------------
    // Code is content-addressed by Keccak-256, so the same hash always
    // refers to the same bytes. We only need to revert when a hash is
    // entering the DB for the first time; pre-existing entries stay.
    for (const auto& kv : cache.DirtyCode()) {
        if (!db.HasCode(kv.first)) {
            out.addedCodeHashes.push_back(kv.first);
        }
    }

    return out;
}

bool ApplyUndoToDB(const CEvmStateUndo& undo, CEvmStateDB& db)
{
    // Reverse-order isn't strictly required (the inverses commute
    // on a per-key basis), but applying account writes first then
    // storage gives a clean fail-stop posture if the DB errors
    // partway: a partial undo of accounts followed by all storage
    // is still a recoverable state if the operator re-runs.
    for (const auto& rec : undo.accountChanges) {
        if (rec.existedBefore) {
            if (!db.WriteAccount(rec.address, rec.before)) return false;
        } else {
            if (!db.EraseAccount(rec.address)) return false;
        }
    }
    for (const auto& rec : undo.storageChanges) {
        if (rec.existedBefore) {
            if (!db.WriteStorage(rec.address, rec.slot, rec.before)) return false;
        } else {
            if (!db.EraseStorage(rec.address, rec.slot)) return false;
        }
    }
    for (const auto& codeHash : undo.addedCodeHashes) {
        if (!db.EraseCode(codeHash)) return false;
    }
    return true;
}

} // namespace evm
