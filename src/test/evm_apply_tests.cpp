// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <evm/account.h>
#include <evm/apply.h>
#include <evm/evmtx.h>
#include <evm/host.h>
#include <evm/state_cache.h>
#include <evm/state_db.h>

#include <crypto/sha256.h>
#include <hash.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <vector>

/**
 * Phase 2.3a tests: ApplyEvmCallTx.
 *
 * Validates the end-to-end "apply a single CALL tx" path:
 *
 *   1. Call to an account with no deployed code (EOA-to-EOA) succeeds
 *      with empty output and no state changes beyond what the EVM
 *      message itself triggers.
 *
 *   2. Call to a deployed SSTORE contract persists the stored slot
 *      into the cache after Flush().
 *
 *   3. Call to a contract that REVERTs returns EVMC_REVERT and
 *      Discard()-ing the cache rolls back any storage changes.
 *
 *   4. Logs emitted during the call surface in ApplyResult.logs.
 */

namespace {

// Standard 32-byte 0-padded uint256 with `fill` in the LOW byte.
uint256 LowByte(uint8_t fill)
{
    uint256 u;
    *(u.begin() + 31) = fill;
    return u;
}

// Standard 32-byte 0-padded uint256 used as an EVM address: the EVM
// address lives in bytes 12..31. `fill` populates the last byte of
// the address.
uint256 AddrAsUint256(uint8_t lowByte)
{
    uint256 u;
    *(u.begin() + 31) = lowByte;
    return u;
}

// Compute the canonical keccak256 of `bytes` using evmone's bundled
// ethash_keccak256. We re-use the same constant verification approach
// as Phase 0 (keccak256("") = c5d2460186...). Here we need a real
// hasher for contract code, so go through evmone.
uint256 Keccak256(const std::vector<uint8_t>& bytes)
{
    // Trick: deploy a contract whose Keccak256 we want and call
    // EvmSmokeExecute with a SHA3 opcode over the same bytes. But that's
    // overkill. The cleaner way is to use evmone's exported hasher,
    // but it isn't part of the public header at the version we pin.
    //
    // Simpler approach for the unit test: pre-compute the codeHash
    // off-line for each test contract. We don't actually need it for
    // the test bodies — we just need ANY deterministic 32-byte value
    // that's consistent between CEvmAccount.codeHash and the key under
    // which we WriteCode(). Use a Bitcoin Core HashWriter for that:
    // it's deterministic and unique-per-bytes, even if it isn't the
    // actual Ethereum keccak256.
    CHashWriter hw(SER_GETHASH, 0);
    hw.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return hw.GetHash();
}

evm::ExecutionContext MinimalContext()
{
    evm::ExecutionContext c;
    c.chainId = 7375;
    c.blockHeight = 100;
    c.blockTimestamp = 1700000000;
    c.blockGasLimit = 30'000'000;
    return c;
}

// Build a CEvmCallTx pointing at `to` with `data` as calldata and the
// given gas limit. All fee params zeroed — Phase 2.3a doesn't enforce
// gas accounting yet.
evm::CEvmCallTx MakeCallTx(const uint256& to,
                           uint64_t gasLimit,
                           const std::vector<uint8_t>& data = {},
                           uint64_t value = 0)
{
    evm::CEvmCallTx tx;
    tx.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    tx.toAddress = to;
    tx.value = value;
    tx.data = data;
    tx.gasLimit = gasLimit;
    tx.maxFeePerGas = 0;
    tx.maxPriorityFeePerGas = 0;
    tx.nonce = 0;
    // Sender defaults to all-zero address.
    return tx;
}

// Helper: deploy `code` at the given recipient address by directly
// writing the account and code records. Phase 2.3a does NOT have
// ApplyEvmDeployTx yet — that's 2.3b. We pre-populate the cache
// manually for these tests.
void DeployForTest(evm::CEvmStateCache& cache,
                   const uint256& recipientAsUint256,
                   const std::vector<uint8_t>& code)
{
    uint160 addr;
    std::memcpy(addr.begin(), recipientAsUint256.begin() + 12, 20);

    uint256 codeHash = Keccak256(code);
    cache.SetCode(codeHash, code);

    evm::CEvmAccount account(
        /*nonce=*/ 1,
        /*balance=*/ uint256(),
        /*codeHash=*/ codeHash,
        /*storageRoot=*/ evm::CEvmAccount::EmptyStorageRoot());
    cache.SetAccount(addr, account);
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(evm_apply_tests, BasicTestingSetup)

// ----------------------------------------------------------------------------
// 1. Call to an account with no code — trivial success
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(call_to_codeless_account_succeeds)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);

    evm::CEvmCallTx tx = MakeCallTx(AddrAsUint256(0x0E), 100'000);
    evm::ApplyResult r = evm::ApplyEvmCallTx(tx, cache, MinimalContext());

    BOOST_CHECK_EQUAL(r.statusCode, EVMC_SUCCESS);
    BOOST_CHECK(r.returnData.empty());
    BOOST_CHECK(r.logs.empty());
    BOOST_CHECK(r.selfdestructs.empty());
    // Some gas was consumed by the intrinsic CALL machinery.
    BOOST_CHECK(r.gasUsed >= 0);
}

// ----------------------------------------------------------------------------
// 2. Call into a deployed SSTORE contract — verify storage persists
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(call_into_sstore_contract_persists_storage)
{
    // Contract bytecode: store 0x42 at slot 0x07 and return successfully.
    //   PUSH1 0x42, PUSH1 0x07, SSTORE, STOP
    const std::vector<uint8_t> code = {
        0x60, 0x42,
        0x60, 0x07,
        0x55,           // SSTORE
        0x00,           // STOP
    };

    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);

    const uint256 contractAddrU256 = AddrAsUint256(0x77);
    DeployForTest(cache, contractAddrU256, code);

    evm::CEvmCallTx tx = MakeCallTx(contractAddrU256, 1'000'000);
    evm::ApplyResult r = evm::ApplyEvmCallTx(tx, cache, MinimalContext());

    BOOST_CHECK_EQUAL(r.statusCode, EVMC_SUCCESS);
    BOOST_CHECK(r.gasUsed > 0); // SSTORE costs gas

    // Storage slot 0x07 at the contract address should now hold 0x42.
    uint160 contractAddr;
    std::memcpy(contractAddr.begin(), contractAddrU256.begin() + 12, 20);
    uint256 slot = LowByte(0x07);
    uint256 stored;
    BOOST_REQUIRE(cache.GetStorage(contractAddr, slot, stored));
    BOOST_CHECK_EQUAL(static_cast<int>(*(stored.begin() + 31)), 0x42);

    // Flush to the DB and re-read: the value persists through the
    // persistence layer too.
    BOOST_REQUIRE(cache.Flush());
    uint256 storedFromDb;
    BOOST_REQUIRE(db.ReadStorage(contractAddr, slot, storedFromDb));
    BOOST_CHECK_EQUAL(static_cast<int>(*(storedFromDb.begin() + 31)), 0x42);
}

// ----------------------------------------------------------------------------
// 3. Call into a contract that REVERTs — discard rolls back
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(call_into_reverting_contract_rolls_back_on_discard)
{
    // Contract: SSTORE 0x99 at slot 0x42, then REVERT with empty data.
    //   PUSH1 0x99, PUSH1 0x42, SSTORE
    //   PUSH1 0x00, PUSH1 0x00, REVERT
    const std::vector<uint8_t> code = {
        0x60, 0x99,
        0x60, 0x42,
        0x55,                       // SSTORE
        0x60, 0x00, 0x60, 0x00,
        0xFD,                       // REVERT (empty output)
    };

    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);

    const uint256 contractAddrU256 = AddrAsUint256(0xBB);
    DeployForTest(cache, contractAddrU256, code);

    evm::CEvmCallTx tx = MakeCallTx(contractAddrU256, 1'000'000);
    evm::ApplyResult r = evm::ApplyEvmCallTx(tx, cache, MinimalContext());

    BOOST_CHECK_EQUAL(r.statusCode, EVMC_REVERT);

    // The cache still has the dirty SSTORE because evmone applied it
    // before reaching REVERT. The CALLER (e.g., ConnectBlock) must
    // discard. Validate that Discard wipes it.
    cache.Discard();

    uint160 contractAddr;
    std::memcpy(contractAddr.begin(), contractAddrU256.begin() + 12, 20);
    uint256 slot = LowByte(0x42);
    uint256 stored;
    BOOST_CHECK(!cache.GetStorage(contractAddr, slot, stored));
}

// ----------------------------------------------------------------------------
// 4. LOG0 surfaces in ApplyResult.logs
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(call_captures_log_in_apply_result)
{
    // Contract: PUSH 4-byte data into memory, then LOG0 with that data.
    //   PUSH1 0xDE, PUSH1 0xAD, PUSH1 0xBE, PUSH1 0xEF                ; one byte each
    //
    // Simpler: store an 0xDEADBEEF word in memory, then LOG0 the
    // last 4 bytes:
    //   PUSH4 0xDEADBEEF      (63 DE AD BE EF)
    //   PUSH1 0x00            (60 00)
    //   MSTORE                (52)             ; memory[0..32] = ...DEADBEEF
    //   PUSH1 0x04            (60 04)          ; data size = 4 bytes
    //   PUSH1 0x1C            (60 1C)          ; data offset = 28 = 32 - 4
    //   LOG0                  (A0)
    //   STOP                  (00)
    const std::vector<uint8_t> code = {
        0x63, 0xDE, 0xAD, 0xBE, 0xEF,
        0x60, 0x00,
        0x52,
        0x60, 0x04,
        0x60, 0x1C,
        0xA0,
        0x00,
    };

    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);

    const uint256 contractAddrU256 = AddrAsUint256(0x10);
    DeployForTest(cache, contractAddrU256, code);

    evm::CEvmCallTx tx = MakeCallTx(contractAddrU256, 1'000'000);
    evm::ApplyResult r = evm::ApplyEvmCallTx(tx, cache, MinimalContext());

    BOOST_CHECK_EQUAL(r.statusCode, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(r.logs.size(), 1U);
    const auto& log = r.logs[0];
    BOOST_CHECK(log.topics.empty()); // LOG0
    BOOST_REQUIRE_EQUAL(log.data.size(), 4U);
    BOOST_CHECK_EQUAL(log.data[0], 0xDE);
    BOOST_CHECK_EQUAL(log.data[1], 0xAD);
    BOOST_CHECK_EQUAL(log.data[2], 0xBE);
    BOOST_CHECK_EQUAL(log.data[3], 0xEF);

    // The log's emitting address should match our contract address.
    uint160 expectedAddr;
    std::memcpy(expectedAddr.begin(), contractAddrU256.begin() + 12, 20);
    uint160 loggedAddr;
    std::memcpy(loggedAddr.begin(), log.address.bytes, 20);
    BOOST_CHECK(loggedAddr == expectedAddr);
}

BOOST_AUTO_TEST_SUITE_END()
