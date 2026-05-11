// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/state_db.h>

#include <uint256.h>
#include <util/system.h>

namespace evm {

// Single-byte prefixes for the LevelDB key spaces. Single bytes are
// chosen so the encoded form is a simple `char + serialized-key` pair
// that LevelDB scans efficiently. Same scheme as CAssetsDB.
static constexpr char ACCOUNT_PREFIX = 'A';
static constexpr char CODE_PREFIX = 'C';
static constexpr char STORAGE_PREFIX = 'S';
// Phase 2.6 — per-block undo journal:
static constexpr char BLOCKUNDO_PREFIX = 'U';

CEvmStateDB::CEvmStateDB(size_t nCacheSize, bool fMemory, bool fWipe)
    : CDBWrapper(GetDataDir() / "evmstate", nCacheSize, fMemory, fWipe)
{
}

// ----------------------------------------------------------------------
// Account
// ----------------------------------------------------------------------

bool CEvmStateDB::WriteAccount(const uint160& address, const CEvmAccount& account)
{
    return Write(std::make_pair(ACCOUNT_PREFIX, address), account);
}

bool CEvmStateDB::ReadAccount(const uint160& address, CEvmAccount& account) const
{
    return Read(std::make_pair(ACCOUNT_PREFIX, address), account);
}

bool CEvmStateDB::EraseAccount(const uint160& address)
{
    return Erase(std::make_pair(ACCOUNT_PREFIX, address));
}

bool CEvmStateDB::HasAccount(const uint160& address) const
{
    return Exists(std::make_pair(ACCOUNT_PREFIX, address));
}

// ----------------------------------------------------------------------
// Code
// ----------------------------------------------------------------------

bool CEvmStateDB::WriteCode(const uint256& codeHash, const std::vector<uint8_t>& code)
{
    return Write(std::make_pair(CODE_PREFIX, codeHash), code);
}

bool CEvmStateDB::ReadCode(const uint256& codeHash, std::vector<uint8_t>& code) const
{
    return Read(std::make_pair(CODE_PREFIX, codeHash), code);
}

bool CEvmStateDB::HasCode(const uint256& codeHash) const
{
    return Exists(std::make_pair(CODE_PREFIX, codeHash));
}

// ----------------------------------------------------------------------
// Storage
// ----------------------------------------------------------------------

bool CEvmStateDB::WriteStorage(const uint160& address, const uint256& slot, const uint256& value)
{
    return Write(std::make_pair(STORAGE_PREFIX, std::make_pair(address, slot)), value);
}

bool CEvmStateDB::ReadStorage(const uint160& address, const uint256& slot, uint256& value) const
{
    return Read(std::make_pair(STORAGE_PREFIX, std::make_pair(address, slot)), value);
}

bool CEvmStateDB::EraseStorage(const uint160& address, const uint256& slot)
{
    return Erase(std::make_pair(STORAGE_PREFIX, std::make_pair(address, slot)));
}

// ----------------------------------------------------------------------
// Phase 2.6 — code erase + per-block undo journal
// ----------------------------------------------------------------------

bool CEvmStateDB::EraseCode(const uint256& codeHash)
{
    return Erase(std::make_pair(CODE_PREFIX, codeHash));
}

bool CEvmStateDB::WriteBlockUndoBytes(const uint256& blockHash,
                                     const std::vector<uint8_t>& bytes)
{
    return Write(std::make_pair(BLOCKUNDO_PREFIX, blockHash), bytes);
}

bool CEvmStateDB::ReadBlockUndoBytes(const uint256& blockHash,
                                    std::vector<uint8_t>& outBytes) const
{
    return Read(std::make_pair(BLOCKUNDO_PREFIX, blockHash), outBytes);
}

bool CEvmStateDB::EraseBlockUndo(const uint256& blockHash)
{
    return Erase(std::make_pair(BLOCKUNDO_PREFIX, blockHash));
}

} // namespace evm
