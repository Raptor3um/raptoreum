// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_UNDO_H
#define RAPTOREUM_EVM_UNDO_H

#include <evm/account.h>
#include <serialize.h>
#include <uint256.h>

#include <cstdint>
#include <vector>

class uint160;

namespace evm {

class CEvmStateCache;
class CEvmStateDB;

/**
 * Reorg journal for the EVM side (Phase 2.6 — A10).
 *
 * ConnectBlock builds a CEvmStateUndo from the dirty layer of its
 * per-block CEvmStateCache by reading the pre-block values directly
 * from the underlying CEvmStateDB. The undo is persisted to the EVM
 * state DB keyed by block hash. DisconnectBlock reads the journal
 * and applies the inverse, restoring the pre-block state.
 *
 * What's recorded:
 *   - For every modified or newly-created account: the previous
 *     account record (or "did not exist").
 *   - For every modified or newly-created storage slot: the previous
 *     value (or "did not exist", i.e. read-as-zero per EVM convention).
 *   - For every newly-installed code blob: its keccak256 hash, so the
 *     entry can be erased on undo. Codes that were already in the DB
 *     don't need recording — they're content-addressed and unchanged.
 *
 * What's NOT recorded:
 *   - Logs / receipts: derived from the block itself; on reorg the
 *     consumer (RPC, log filter) re-derives them from the new chain.
 *   - The UTXO credits from SPEND txs: those live on the UTXO side
 *     and have their own undo path (CTxUndo).
 */

struct AccountChangeUndo
{
    uint160 address;
    bool existedBefore{false};
    CEvmAccount before;

    SERIALIZE_METHODS(AccountChangeUndo, obj)
    {
        READWRITE(obj.address);
        READWRITE(obj.existedBefore);
        if (obj.existedBefore) {
            READWRITE(obj.before);
        }
    }
};

struct StorageChangeUndo
{
    uint160 address;
    uint256 slot;
    bool existedBefore{false};
    uint256 before;

    SERIALIZE_METHODS(StorageChangeUndo, obj)
    {
        READWRITE(obj.address);
        READWRITE(obj.slot);
        READWRITE(obj.existedBefore);
        if (obj.existedBefore) {
            READWRITE(obj.before);
        }
    }
};

struct CEvmStateUndo
{
    std::vector<AccountChangeUndo> accountChanges;
    std::vector<StorageChangeUndo> storageChanges;
    /** Hashes of code blobs newly added by this block. On undo each
     *  is erased from the DB. Content-addressing means a hash that
     *  was already present remains valid for any prior account
     *  pointing at it. */
    std::vector<uint256> addedCodeHashes;

    SERIALIZE_METHODS(CEvmStateUndo, obj)
    {
        READWRITE(obj.accountChanges);
        READWRITE(obj.storageChanges);
        READWRITE(obj.addedCodeHashes);
    }

    bool IsEmpty() const
    {
        return accountChanges.empty() &&
               storageChanges.empty() &&
               addedCodeHashes.empty();
    }
};

/**
 * Build a CEvmStateUndo from a cache's dirty layer by reading the
 * pre-block values directly from the underlying DB.
 *
 * MUST be called BEFORE the caller invokes cache.Flush() — once the
 * dirty entries have been written to the DB, the pre-block state is
 * gone.
 *
 * Caches that touch an entry only to write the same value still emit
 * an undo record (the cache cannot distinguish "no-op write" from a
 * real change without an extra read). This bloats the journal
 * marginally but keeps the implementation straightforward.
 */
CEvmStateUndo BuildUndoFromCache(const CEvmStateCache& cache,
                                 const CEvmStateDB& db);

/**
 * Apply an undo journal to a state DB, restoring the pre-block state.
 *
 *   - For each account change: write `before` if existedBefore,
 *     else erase the account.
 *   - For each storage change: write `before` if existedBefore,
 *     else erase the slot.
 *   - For each added code hash: erase the code blob.
 *
 * Returns false on the first DB-write failure; the partial undo is
 * left in place (the caller treats this as a fatal node-state
 * inconsistency).
 */
bool ApplyUndoToDB(const CEvmStateUndo& undo, CEvmStateDB& db);

} // namespace evm

#endif // RAPTOREUM_EVM_UNDO_H
