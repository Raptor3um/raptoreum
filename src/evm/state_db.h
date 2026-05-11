// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_STATE_DB_H
#define RAPTOREUM_EVM_STATE_DB_H

#include <dbwrapper.h>
#include <evm/account.h>
#include <fs.h>
#include <uint256.h>

#include <cstdint>
#include <cstddef>
#include <vector>

class uint160;

namespace evm {

/**
 * Persistent storage for EVM account state (Phase 2.1).
 *
 * Thin LevelDB wrapper following the same pattern as CAssetsDB.
 * Three key spaces live in this DB; each is identified by a single
 * leading byte to keep keys cheap to range-scan:
 *
 *   'A' + address(20)                  -> CEvmAccount
 *   'C' + codeHash(32)                 -> std::vector<uint8_t>  (bytecode)
 *   'S' + address(20) + slot(32)       -> uint256                (storage value)
 *
 * Logs ('L' + blockHash + txIdx + logIdx) and receipts ('R' + txid)
 * will be added when execution lands (Phase 2.2+).
 *
 * The cache (CEvmStateCache) is the only thing that should mutate this
 * DB directly during block validation. Reads are safe from any thread
 * provided the LevelDB instance is alive.
 *
 * On-disk path: $datadir/evmstate/   (matches the design plan).
 */
class CEvmStateDB : public CDBWrapper
{
public:
    /** Construct (or open) the EVM state DB rooted at `$datadir/evmstate/`. */
    explicit CEvmStateDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    // ----------------------------------------------------------------
    // Account record
    // ----------------------------------------------------------------

    bool WriteAccount(const uint160& address, const CEvmAccount& account);
    bool ReadAccount(const uint160& address, CEvmAccount& account) const;
    bool EraseAccount(const uint160& address);
    bool HasAccount(const uint160& address) const;

    // ----------------------------------------------------------------
    // Code (contract bytecode keyed by its Keccak-256)
    // ----------------------------------------------------------------

    bool WriteCode(const uint256& codeHash, const std::vector<uint8_t>& code);
    bool ReadCode(const uint256& codeHash, std::vector<uint8_t>& code) const;
    bool HasCode(const uint256& codeHash) const;

    // ----------------------------------------------------------------
    // Storage slot (32 bytes -> 32 bytes per address)
    // ----------------------------------------------------------------

    /** Write a storage slot. A value of all-zeros is allowed and is
     *  treated as a real "0" by the caller; callers that want delete-on-
     *  zero-value semantics (the EVM SSTORE convention) should call
     *  EraseStorage() explicitly. */
    bool WriteStorage(const uint160& address, const uint256& slot, const uint256& value);

    /** Read a storage slot. Returns false if the slot is unset, leaving
     *  `value` untouched. Callers that want "zero on miss" semantics
     *  should default-initialize `value` before calling. */
    bool ReadStorage(const uint160& address, const uint256& slot, uint256& value) const;

    bool EraseStorage(const uint160& address, const uint256& slot);

    // ----------------------------------------------------------------
    // Code erase (Phase 2.6 — needed for reorg journaling to retract
    // a code blob that this block was the first to install). For an
    // address still pointing at the codeHash, callers must first
    // delete or rewrite the account record; this method does NOT
    // walk the account index.
    // ----------------------------------------------------------------
    bool EraseCode(const uint256& codeHash);

    // ----------------------------------------------------------------
    // Phase 2.6 — Block undo journal keyed by block hash.
    //
    //   'U' + blockHash(32)  ->  serialized CEvmStateUndo
    //
    // ConnectBlock writes the journal during the flush phase;
    // DisconnectBlock reads it, applies the inverse, then erases the
    // entry. Mirrors how CAssetsDB stores per-block asset undo.
    // ----------------------------------------------------------------

    /** Write a serialized undo blob keyed by block hash. */
    bool WriteBlockUndoBytes(const uint256& blockHash,
                             const std::vector<uint8_t>& bytes);
    /** Read a serialized undo blob; returns false if no entry. */
    bool ReadBlockUndoBytes(const uint256& blockHash,
                           std::vector<uint8_t>& outBytes) const;
    /** Erase the per-block undo entry. */
    bool EraseBlockUndo(const uint256& blockHash);
};

} // namespace evm

#endif // RAPTOREUM_EVM_STATE_DB_H
