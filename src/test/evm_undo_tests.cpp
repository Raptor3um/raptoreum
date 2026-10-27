// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <evm/account.h>
#include <evm/balance.h>
#include <evm/state_cache.h>
#include <evm/state_db.h>
#include <evm/undo.h>

#include <serialize.h>
#include <streams.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <vector>

/**
 * Phase 2.6 — reorg journal tests.
 *
 *   1. Build undo from a cache with mixed writes; verify all pre-block
 *      values are captured (and existedBefore reflects DB presence).
 *   2. Apply undo restores the DB to the pre-block state byte-for-byte.
 *   3. Round-trip serialization of CEvmStateUndo.
 *   4. addedCodeHashes contains only NEWLY-added code blobs.
 *   5. Deleted-account case: the prior account is restored.
 */

namespace {

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

evm::CEvmAccount AccountWithBalance(uint8_t fill, uint64_t balanceWeis,
                                    uint64_t nonce = 0)
{
    return evm::CEvmAccount(
        nonce,
        evm::Uint256FromUint64(balanceWeis),
        evm::CEvmAccount::EmptyCodeHash(),
        evm::CEvmAccount::EmptyStorageRoot());
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(evm_undo_tests, BasicTestingSetup)

// ----------------------------------------------------------------------------
// BuildUndoFromCache captures pre-block account state.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(build_undo_records_pre_block_account)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    // Seed DB with a pre-existing account at addr 0x11.
    const uint160 addr = MakeAddr(0x11);
    const evm::CEvmAccount before = AccountWithBalance(0x11, 1000);
    BOOST_REQUIRE(db.WriteAccount(addr, before));

    // Cache modifies the account.
    evm::CEvmStateCache cache(db);
    evm::CEvmAccount after = AccountWithBalance(0x11, 2000, /*nonce=*/ 5);
    cache.SetAccount(addr, after);

    auto undo = evm::BuildUndoFromCache(cache, db);
    BOOST_REQUIRE_EQUAL(undo.accountChanges.size(), 1U);
    BOOST_CHECK(undo.accountChanges[0].existedBefore);
    BOOST_CHECK(undo.accountChanges[0].before.balance == before.balance);
    BOOST_CHECK_EQUAL(undo.accountChanges[0].before.nonce, before.nonce);
}

// ----------------------------------------------------------------------------
// BuildUndoFromCache marks NEW accounts (no DB entry) correctly.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(build_undo_marks_new_account_as_not_existed)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);

    const uint160 addr = MakeAddr(0x22);
    cache.SetAccount(addr, AccountWithBalance(0x22, 500));

    auto undo = evm::BuildUndoFromCache(cache, db);
    BOOST_REQUIRE_EQUAL(undo.accountChanges.size(), 1U);
    BOOST_CHECK(!undo.accountChanges[0].existedBefore);
}

// ----------------------------------------------------------------------------
// Storage slot pre-block value capture.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(build_undo_captures_storage_pre_state)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    const uint160 addr = MakeAddr(0x33);
    const uint256 slot = MakeWord(0x07);
    // Pre-block: slot has value 0xAA.
    BOOST_REQUIRE(db.WriteStorage(addr, slot, MakeWord(0xAA)));

    evm::CEvmStateCache cache(db);
    cache.SetStorage(addr, slot, MakeWord(0xBB));

    auto undo = evm::BuildUndoFromCache(cache, db);
    BOOST_REQUIRE_EQUAL(undo.storageChanges.size(), 1U);
    BOOST_CHECK(undo.storageChanges[0].existedBefore);
    BOOST_CHECK(undo.storageChanges[0].before == MakeWord(0xAA));

    // A second slot that was empty: existedBefore=false.
    const uint256 slot2 = MakeWord(0x08);
    cache.SetStorage(addr, slot2, MakeWord(0xCC));
    auto undo2 = evm::BuildUndoFromCache(cache, db);
    BOOST_REQUIRE_EQUAL(undo2.storageChanges.size(), 2U);
    // Find the slot2 entry.
    bool foundSlot2 = false;
    for (const auto& s : undo2.storageChanges) {
        if (s.slot == slot2) {
            foundSlot2 = true;
            BOOST_CHECK(!s.existedBefore);
        }
    }
    BOOST_CHECK(foundSlot2);
}

// ----------------------------------------------------------------------------
// addedCodeHashes lists only blobs that are NEW to the DB.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(build_undo_lists_only_new_code_hashes)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    // Pre-block: an existing code blob at hash 0xCC...CC.
    const uint256 existingHash = MakeWord(0xCC);
    const std::vector<uint8_t> existingCode = {0xFE, 0xED, 0xFA, 0xCE};
    BOOST_REQUIRE(db.WriteCode(existingHash, existingCode));

    evm::CEvmStateCache cache(db);
    // Cache "writes" the same hash (no-op for the DB).
    cache.SetCode(existingHash, existingCode);
    // Cache adds a NEW hash.
    const uint256 newHash = MakeWord(0xDD);
    cache.SetCode(newHash, {0xDE, 0xAD, 0xBE, 0xEF});

    auto undo = evm::BuildUndoFromCache(cache, db);
    BOOST_REQUIRE_EQUAL(undo.addedCodeHashes.size(), 1U);
    BOOST_CHECK(undo.addedCodeHashes[0] == newHash);
}

// ----------------------------------------------------------------------------
// ApplyUndoToDB restores pre-block state for accounts, storage, code.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(apply_undo_restores_pre_block_state)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);

    // Pre-block state:
    //   account at 0xAA exists with balance 1000
    //   account at 0xBB does NOT exist
    //   storage[0xAA][slot 0x01] = 0x42
    //   code blob at hash 0xEE exists
    const uint160 a1 = MakeAddr(0xAA);
    const uint160 a2 = MakeAddr(0xBB);
    const uint256 slot1 = MakeWord(0x01);
    const uint256 codeHash = MakeWord(0xEE);
    const std::vector<uint8_t> codeBytes = {0x42};
    BOOST_REQUIRE(db.WriteAccount(a1, AccountWithBalance(0xAA, 1000)));
    BOOST_REQUIRE(db.WriteStorage(a1, slot1, MakeWord(0x42)));
    BOOST_REQUIRE(db.WriteCode(codeHash, codeBytes));

    // Apply block: modify a1, create a2, change the slot, add a new code blob.
    evm::CEvmStateCache cache(db);
    cache.SetAccount(a1, AccountWithBalance(0xAA, 99999, /*nonce=*/ 7));
    cache.SetAccount(a2, AccountWithBalance(0xBB, 555));
    cache.SetStorage(a1, slot1, MakeWord(0xFF));
    const uint256 newCodeHash = MakeWord(0xFE);
    cache.SetCode(newCodeHash, {0x99});

    auto undo = evm::BuildUndoFromCache(cache, db);
    BOOST_REQUIRE(cache.Flush());

    // After flush, DB is in the post-block state. Sanity: a1 has the
    // new balance.
    evm::CEvmAccount peek;
    BOOST_REQUIRE(db.ReadAccount(a1, peek));
    BOOST_CHECK(peek.balance == evm::Uint256FromUint64(99999));

    // Apply the undo.
    BOOST_REQUIRE(evm::ApplyUndoToDB(undo, db));

    // a1 restored to the pre-block 1000.
    BOOST_REQUIRE(db.ReadAccount(a1, peek));
    BOOST_CHECK(peek.balance == evm::Uint256FromUint64(1000));
    BOOST_CHECK_EQUAL(peek.nonce, 0U);

    // a2 erased (it didn't exist before the block).
    evm::CEvmAccount missing;
    BOOST_CHECK(!db.ReadAccount(a2, missing));

    // Storage slot restored to 0x42.
    uint256 slotVal;
    BOOST_REQUIRE(db.ReadStorage(a1, slot1, slotVal));
    BOOST_CHECK(slotVal == MakeWord(0x42));

    // Pre-existing code blob still there.
    std::vector<uint8_t> codeRead;
    BOOST_REQUIRE(db.ReadCode(codeHash, codeRead));
    BOOST_CHECK_EQUAL_COLLECTIONS(codeRead.begin(), codeRead.end(),
                                  codeBytes.begin(), codeBytes.end());

    // Newly-added code blob erased.
    std::vector<uint8_t> dead;
    BOOST_CHECK(!db.ReadCode(newCodeHash, dead));
}

// ----------------------------------------------------------------------------
// Deleted-account case: undo restores the prior account.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(apply_undo_restores_deleted_account)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    const uint160 addr = MakeAddr(0x44);
    const evm::CEvmAccount original = AccountWithBalance(0x44, 7777, /*nonce=*/ 3);
    BOOST_REQUIRE(db.WriteAccount(addr, original));

    evm::CEvmStateCache cache(db);
    cache.DeleteAccount(addr);
    auto undo = evm::BuildUndoFromCache(cache, db);
    BOOST_REQUIRE(cache.Flush());

    // After flush, account is gone.
    evm::CEvmAccount peek;
    BOOST_CHECK(!db.ReadAccount(addr, peek));

    BOOST_REQUIRE(evm::ApplyUndoToDB(undo, db));

    BOOST_REQUIRE(db.ReadAccount(addr, peek));
    BOOST_CHECK_EQUAL(peek.nonce, 3U);
    BOOST_CHECK(peek.balance == evm::Uint256FromUint64(7777));
}

// ----------------------------------------------------------------------------
// Round-trip serialization of CEvmStateUndo.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(undo_serialization_round_trip)
{
    evm::CEvmStateUndo in;
    {
        evm::AccountChangeUndo a;
        a.address = MakeAddr(0x55);
        a.existedBefore = true;
        a.before = AccountWithBalance(0x55, 1234, /*nonce=*/ 9);
        in.accountChanges.push_back(a);

        evm::AccountChangeUndo b;
        b.address = MakeAddr(0x56);
        b.existedBefore = false;
        in.accountChanges.push_back(b);

        evm::StorageChangeUndo s;
        s.address = MakeAddr(0x55);
        s.slot = MakeWord(0x77);
        s.existedBefore = true;
        s.before = MakeWord(0xAB);
        in.storageChanges.push_back(s);

        in.addedCodeHashes.push_back(MakeWord(0xC0));
        in.addedCodeHashes.push_back(MakeWord(0xC1));
    }

    CDataStream ds(SER_DISK, CLIENT_VERSION);
    ds << in;
    evm::CEvmStateUndo out;
    ds >> out;
    BOOST_REQUIRE(ds.empty());

    BOOST_REQUIRE_EQUAL(out.accountChanges.size(), 2U);
    BOOST_CHECK(out.accountChanges[0].existedBefore);
    BOOST_CHECK_EQUAL(out.accountChanges[0].before.nonce, 9U);
    BOOST_CHECK(!out.accountChanges[1].existedBefore);
    BOOST_REQUIRE_EQUAL(out.storageChanges.size(), 1U);
    BOOST_CHECK(out.storageChanges[0].existedBefore);
    BOOST_CHECK(out.storageChanges[0].before == MakeWord(0xAB));
    BOOST_REQUIRE_EQUAL(out.addedCodeHashes.size(), 2U);
    BOOST_CHECK(out.addedCodeHashes[0] == MakeWord(0xC0));
}

// ----------------------------------------------------------------------------
// State DB persistence: write/read/erase a block-undo blob.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(db_block_undo_persistence)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    const uint256 blockHash = MakeWord(0xAA);

    std::vector<uint8_t> bytesIn = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    BOOST_REQUIRE(db.WriteBlockUndoBytes(blockHash, bytesIn));

    std::vector<uint8_t> bytesOut;
    BOOST_REQUIRE(db.ReadBlockUndoBytes(blockHash, bytesOut));
    BOOST_CHECK_EQUAL_COLLECTIONS(bytesOut.begin(), bytesOut.end(),
                                  bytesIn.begin(), bytesIn.end());

    BOOST_REQUIRE(db.EraseBlockUndo(blockHash));
    std::vector<uint8_t> nothing;
    BOOST_CHECK(!db.ReadBlockUndoBytes(blockHash, nothing));
}

// ----------------------------------------------------------------------------
// Full round-trip: build undo, flush, persist, read back, apply.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(full_round_trip_via_db_persistence)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    const uint160 addr = MakeAddr(0x66);
    BOOST_REQUIRE(db.WriteAccount(addr, AccountWithBalance(0x66, 100)));

    // Block: modify the account.
    evm::CEvmStateCache cache(db);
    cache.SetAccount(addr, AccountWithBalance(0x66, 999, /*nonce=*/ 2));
    auto undoBuilt = evm::BuildUndoFromCache(cache, db);
    BOOST_REQUIRE(cache.Flush());

    // Persist undo blob keyed by a fake block hash.
    const uint256 blockHash = MakeWord(0xBE);
    CDataStream ds(SER_DISK, CLIENT_VERSION);
    ds << undoBuilt;
    std::vector<uint8_t> bytes(ds.begin(), ds.end());
    BOOST_REQUIRE(db.WriteBlockUndoBytes(blockHash, bytes));

    // Later: read back and deserialize.
    std::vector<uint8_t> readBack;
    BOOST_REQUIRE(db.ReadBlockUndoBytes(blockHash, readBack));
    CDataStream ds2(readBack, SER_DISK, CLIENT_VERSION);
    evm::CEvmStateUndo undoFromDisk;
    ds2 >> undoFromDisk;

    // Apply: account restored to balance 100.
    BOOST_REQUIRE(evm::ApplyUndoToDB(undoFromDisk, db));
    evm::CEvmAccount peek;
    BOOST_REQUIRE(db.ReadAccount(addr, peek));
    BOOST_CHECK(peek.balance == evm::Uint256FromUint64(100));
    BOOST_CHECK_EQUAL(peek.nonce, 0U);
}

BOOST_AUTO_TEST_SUITE_END()
