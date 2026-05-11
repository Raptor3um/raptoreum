// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <evm/account.h>
#include <evm/state_cache.h>
#include <evm/state_db.h>

#include <serialize.h>
#include <streams.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <vector>

/**
 * Phase 2.1 unit tests for the EVM state primitives:
 *
 *   - CEvmAccount      serialization, defaults, IsEmpty(), constants
 *   - CEvmStateDB      in-memory LevelDB wrapper (Read/Write/Erase/Has)
 *   - CEvmStateCache   dirty-layer semantics, Flush, Discard, deletion
 *
 * No consensus integration yet — these primitives are the data layer
 * for Phase 2 EVM execution. They're stand-alone testable in memory.
 */

namespace {

template <typename T>
T RoundTripBytes(const T& in)
{
    CDataStream ds(SER_DISK, CLIENT_VERSION);
    ds << in;
    T out;
    ds >> out;
    BOOST_REQUIRE(ds.empty());
    return out;
}

uint160 MakeAddr(uint8_t fill)
{
    uint160 a;
    std::vector<unsigned char> raw(20, fill);
    a = uint160(raw);
    return a;
}

uint256 MakeWord(uint8_t fill)
{
    std::vector<unsigned char> raw(32, fill);
    return uint256(raw);
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(evm_state_tests, BasicTestingSetup)

// ----------------------------------------------------------------------------
// CEvmAccount
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(account_default_is_empty)
{
    evm::CEvmAccount a;
    BOOST_CHECK_EQUAL(a.nonce, 0U);
    BOOST_CHECK(a.balance.IsNull());
    BOOST_CHECK(a.codeHash.IsNull());
    BOOST_CHECK(a.storageRoot.IsNull());

    // Default-constructed has null codeHash/storageRoot; IsEmpty() compares
    // them to the canonical empty constants, so a freshly-constructed
    // record is NOT considered "empty" yet until the caller initializes
    // those two fields. This catches accidental "all zeros" mistakes.
    BOOST_CHECK(!a.IsEmpty());

    a.codeHash = evm::CEvmAccount::EmptyCodeHash();
    a.storageRoot = evm::CEvmAccount::EmptyStorageRoot();
    BOOST_CHECK(a.IsEmpty());
}

BOOST_AUTO_TEST_CASE(account_canonical_constants)
{
    // Canonical Keccak-256("") — pinned by the Ethereum yellow paper.
    BOOST_CHECK_EQUAL(
        evm::CEvmAccount::EmptyCodeHash().ToString(),
        "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");

    // Empty Merkle-Patricia-Trie root.
    BOOST_CHECK_EQUAL(
        evm::CEvmAccount::EmptyStorageRoot().ToString(),
        "56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421");
}

BOOST_AUTO_TEST_CASE(account_round_trip)
{
    evm::CEvmAccount in(
        /*nonce=*/ 42,
        /*balance=*/ MakeWord(0xAA),
        /*codeHash=*/ MakeWord(0xBB),
        /*storageRoot=*/ MakeWord(0xCC));

    evm::CEvmAccount out = RoundTripBytes(in);

    BOOST_CHECK_EQUAL(out.nonce, in.nonce);
    BOOST_CHECK(out.balance == in.balance);
    BOOST_CHECK(out.codeHash == in.codeHash);
    BOOST_CHECK(out.storageRoot == in.storageRoot);
    BOOST_CHECK(in == out);
}

// ----------------------------------------------------------------------------
// CEvmStateDB (in-memory)
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(db_account_write_read_erase)
{
    evm::CEvmStateDB db(/*nCacheSize=*/ 1 << 20, /*fMemory=*/ true);

    uint160 addr = MakeAddr(0x11);

    BOOST_CHECK(!db.HasAccount(addr));

    evm::CEvmAccount written(7, MakeWord(0x01), MakeWord(0x02), MakeWord(0x03));
    BOOST_REQUIRE(db.WriteAccount(addr, written));
    BOOST_CHECK(db.HasAccount(addr));

    evm::CEvmAccount read;
    BOOST_REQUIRE(db.ReadAccount(addr, read));
    BOOST_CHECK(read == written);

    BOOST_REQUIRE(db.EraseAccount(addr));
    BOOST_CHECK(!db.HasAccount(addr));
    BOOST_CHECK(!db.ReadAccount(addr, read));
}

BOOST_AUTO_TEST_CASE(db_code_write_read)
{
    evm::CEvmStateDB db(/*nCacheSize=*/ 1 << 20, /*fMemory=*/ true);

    uint256 codeHash = MakeWord(0x77);
    std::vector<uint8_t> code = {0x60, 0x05, 0x60, 0x04, 0x01}; // PUSH1 5 PUSH1 4 ADD

    BOOST_CHECK(!db.HasCode(codeHash));
    BOOST_REQUIRE(db.WriteCode(codeHash, code));
    BOOST_CHECK(db.HasCode(codeHash));

    std::vector<uint8_t> read;
    BOOST_REQUIRE(db.ReadCode(codeHash, read));
    BOOST_CHECK_EQUAL_COLLECTIONS(read.begin(), read.end(),
                                  code.begin(), code.end());
}

BOOST_AUTO_TEST_CASE(db_storage_slot_write_read_erase)
{
    evm::CEvmStateDB db(/*nCacheSize=*/ 1 << 20, /*fMemory=*/ true);

    uint160 addr = MakeAddr(0x22);
    uint256 slot = MakeWord(0xDE);
    uint256 value = MakeWord(0xAD);

    uint256 read;
    BOOST_CHECK(!db.ReadStorage(addr, slot, read));

    BOOST_REQUIRE(db.WriteStorage(addr, slot, value));
    BOOST_REQUIRE(db.ReadStorage(addr, slot, read));
    BOOST_CHECK(read == value);

    BOOST_REQUIRE(db.EraseStorage(addr, slot));
    BOOST_CHECK(!db.ReadStorage(addr, slot, read));
}

// ----------------------------------------------------------------------------
// CEvmStateCache
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(cache_get_falls_through_to_db)
{
    evm::CEvmStateDB db(/*nCacheSize=*/ 1 << 20, /*fMemory=*/ true);
    uint160 addr = MakeAddr(0x33);

    evm::CEvmAccount persisted(11, MakeWord(0x10), MakeWord(0x20), MakeWord(0x30));
    BOOST_REQUIRE(db.WriteAccount(addr, persisted));

    evm::CEvmStateCache cache(db);

    BOOST_CHECK_EQUAL(cache.DirtyAccountCount(), 0U);

    evm::CEvmAccount read;
    BOOST_REQUIRE(cache.GetAccount(addr, read));
    BOOST_CHECK(read == persisted);

    // The read does NOT mark the entry dirty.
    BOOST_CHECK_EQUAL(cache.DirtyAccountCount(), 0U);
}

BOOST_AUTO_TEST_CASE(cache_set_then_get_returns_dirty_value)
{
    evm::CEvmStateDB db(/*nCacheSize=*/ 1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);

    uint160 addr = MakeAddr(0x44);
    evm::CEvmAccount a(1, MakeWord(0xAA), MakeWord(0xBB), MakeWord(0xCC));

    cache.SetAccount(addr, a);
    BOOST_CHECK_EQUAL(cache.DirtyAccountCount(), 1U);

    evm::CEvmAccount read;
    BOOST_REQUIRE(cache.GetAccount(addr, read));
    BOOST_CHECK(read == a);

    // Not yet in the DB until we Flush.
    BOOST_CHECK(!db.HasAccount(addr));
}

BOOST_AUTO_TEST_CASE(cache_flush_persists_to_db)
{
    evm::CEvmStateDB db(/*nCacheSize=*/ 1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);

    uint160 addr = MakeAddr(0x55);
    evm::CEvmAccount a(3, MakeWord(0x33), evm::CEvmAccount::EmptyCodeHash(),
                       evm::CEvmAccount::EmptyStorageRoot());
    cache.SetAccount(addr, a);

    BOOST_REQUIRE(cache.Flush());
    BOOST_CHECK_EQUAL(cache.DirtyAccountCount(), 0U);

    evm::CEvmAccount read;
    BOOST_REQUIRE(db.ReadAccount(addr, read));
    BOOST_CHECK(read == a);
}

BOOST_AUTO_TEST_CASE(cache_discard_leaves_db_untouched)
{
    evm::CEvmStateDB db(/*nCacheSize=*/ 1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);

    uint160 addr = MakeAddr(0x66);
    cache.SetAccount(addr, evm::CEvmAccount(9, MakeWord(0x09),
                                            MakeWord(0x09), MakeWord(0x09)));

    cache.Discard();
    BOOST_CHECK_EQUAL(cache.DirtyAccountCount(), 0U);
    BOOST_CHECK(!db.HasAccount(addr));

    // After discard a fresh read returns false (entry never existed).
    evm::CEvmAccount read;
    BOOST_CHECK(!cache.GetAccount(addr, read));
}

BOOST_AUTO_TEST_CASE(cache_delete_shadows_db_entry)
{
    evm::CEvmStateDB db(/*nCacheSize=*/ 1 << 20, /*fMemory=*/ true);
    uint160 addr = MakeAddr(0x77);
    BOOST_REQUIRE(db.WriteAccount(addr, evm::CEvmAccount(
        5, MakeWord(0x05), evm::CEvmAccount::EmptyCodeHash(),
        evm::CEvmAccount::EmptyStorageRoot())));

    evm::CEvmStateCache cache(db);
    BOOST_CHECK(cache.HasAccount(addr)); // sees the DB entry

    cache.DeleteAccount(addr);
    BOOST_CHECK(!cache.HasAccount(addr)); // shadow deletion

    evm::CEvmAccount read;
    BOOST_CHECK(!cache.GetAccount(addr, read));

    // Until Flush, the DB still has the entry.
    BOOST_CHECK(db.HasAccount(addr));

    BOOST_REQUIRE(cache.Flush());
    BOOST_CHECK(!db.HasAccount(addr));
}

BOOST_AUTO_TEST_CASE(cache_set_after_delete_supersedes)
{
    evm::CEvmStateDB db(/*nCacheSize=*/ 1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);

    uint160 addr = MakeAddr(0x88);
    cache.DeleteAccount(addr);
    evm::CEvmAccount a(2, MakeWord(0x02), evm::CEvmAccount::EmptyCodeHash(),
                       evm::CEvmAccount::EmptyStorageRoot());
    cache.SetAccount(addr, a);

    BOOST_CHECK(cache.HasAccount(addr));
    evm::CEvmAccount read;
    BOOST_REQUIRE(cache.GetAccount(addr, read));
    BOOST_CHECK(read == a);

    BOOST_REQUIRE(cache.Flush());
    BOOST_CHECK(db.HasAccount(addr));
}

BOOST_AUTO_TEST_CASE(cache_storage_flush_erases_zero_value)
{
    // SSTORE semantics: writing zero clears the slot. The cache honors
    // this on Flush: zero-valued dirty entries become EraseStorage calls
    // so the DB stays compact.
    evm::CEvmStateDB db(/*nCacheSize=*/ 1 << 20, /*fMemory=*/ true);
    uint160 addr = MakeAddr(0x99);
    uint256 slot = MakeWord(0x42);
    BOOST_REQUIRE(db.WriteStorage(addr, slot, MakeWord(0xFF))); // pre-existing value

    evm::CEvmStateCache cache(db);
    uint256 zero; // default-constructed = all zeros
    cache.SetStorage(addr, slot, zero);

    BOOST_REQUIRE(cache.Flush());

    uint256 read;
    // Slot is now absent — EraseStorage ran on Flush.
    BOOST_CHECK(!db.ReadStorage(addr, slot, read));
}

BOOST_AUTO_TEST_CASE(cache_code_write_and_read)
{
    evm::CEvmStateDB db(/*nCacheSize=*/ 1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);

    uint256 codeHash = MakeWord(0x55);
    std::vector<uint8_t> code = {0xF3, 0x00}; // RETURN, STOP

    BOOST_CHECK_EQUAL(cache.DirtyCodeCount(), 0U);

    cache.SetCode(codeHash, code);
    BOOST_CHECK_EQUAL(cache.DirtyCodeCount(), 1U);

    std::vector<uint8_t> read;
    BOOST_REQUIRE(cache.GetCode(codeHash, read));
    BOOST_CHECK_EQUAL_COLLECTIONS(read.begin(), read.end(),
                                  code.begin(), code.end());

    BOOST_REQUIRE(cache.Flush());
    BOOST_CHECK(db.HasCode(codeHash));
}

BOOST_AUTO_TEST_SUITE_END()
