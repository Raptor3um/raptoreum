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
// Phase 3.6 — per-tx receipts + Ethereum hash cross-index:
static constexpr char RECEIPT_PREFIX = 'R';
static constexpr char ETH_HASH_INDEX_PREFIX = 'X';     // eth -> rtm
static constexpr char RTM_HASH_INDEX_PREFIX = 'Y';     // rtm -> eth

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

void CEvmStateDB::ForEachAccount(
    const std::function<void(const uint160&, const CEvmAccount&)>& fn)
{
    std::unique_ptr<CDBIterator> it(NewIterator());
    for (it->Seek(std::make_pair(ACCOUNT_PREFIX, uint160()));
         it->Valid(); it->Next())
    {
        std::pair<char, uint160> key;
        if (!it->GetKey(key) || key.first != ACCOUNT_PREFIX) break;
        CEvmAccount acc;
        if (it->GetValue(acc)) fn(key.second, acc);
    }
}

void CEvmStateDB::ForEachStorage(
    const uint160& address,
    const std::function<void(const uint256&, const uint256&)>& fn)
{
    std::unique_ptr<CDBIterator> it(NewIterator());
    const auto seekKey =
        std::make_pair(STORAGE_PREFIX, std::make_pair(address, uint256()));
    for (it->Seek(seekKey); it->Valid(); it->Next()) {
        std::pair<char, std::pair<uint160, uint256>> key;
        if (!it->GetKey(key) || key.first != STORAGE_PREFIX) break;
        if (key.second.first != address) break; // left the address range
        uint256 value;
        if (it->GetValue(value)) fn(key.second.second, value);
    }
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

// ----------------------------------------------------------------------
// Phase 3.6 — receipt + eth-hash cross-index
// ----------------------------------------------------------------------

bool CEvmStateDB::WriteReceiptBytes(const uint256& rtmTxHash,
                                   const std::vector<uint8_t>& bytes)
{
    return Write(std::make_pair(RECEIPT_PREFIX, rtmTxHash), bytes);
}

bool CEvmStateDB::ReadReceiptBytes(const uint256& rtmTxHash,
                                  std::vector<uint8_t>& outBytes) const
{
    return Read(std::make_pair(RECEIPT_PREFIX, rtmTxHash), outBytes);
}

bool CEvmStateDB::EraseReceipt(const uint256& rtmTxHash)
{
    return Erase(std::make_pair(RECEIPT_PREFIX, rtmTxHash));
}

bool CEvmStateDB::WriteEthToRtmHash(const uint256& ethTxHash,
                                   const uint256& rtmTxHash)
{
    return Write(std::make_pair(ETH_HASH_INDEX_PREFIX, ethTxHash), rtmTxHash);
}

bool CEvmStateDB::ReadEthToRtmHash(const uint256& ethTxHash,
                                  uint256& rtmTxHash) const
{
    return Read(std::make_pair(ETH_HASH_INDEX_PREFIX, ethTxHash), rtmTxHash);
}

bool CEvmStateDB::EraseEthToRtmHash(const uint256& ethTxHash)
{
    return Erase(std::make_pair(ETH_HASH_INDEX_PREFIX, ethTxHash));
}

bool CEvmStateDB::WriteRtmToEthHash(const uint256& rtmTxHash,
                                   const uint256& ethTxHash)
{
    return Write(std::make_pair(RTM_HASH_INDEX_PREFIX, rtmTxHash), ethTxHash);
}

bool CEvmStateDB::ReadRtmToEthHash(const uint256& rtmTxHash,
                                  uint256& ethTxHash) const
{
    return Read(std::make_pair(RTM_HASH_INDEX_PREFIX, rtmTxHash), ethTxHash);
}

bool CEvmStateDB::EraseRtmToEthHash(const uint256& rtmTxHash)
{
    return Erase(std::make_pair(RTM_HASH_INDEX_PREFIX, rtmTxHash));
}

} // namespace evm
