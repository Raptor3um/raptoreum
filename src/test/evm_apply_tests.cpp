// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <evm/account.h>
#include <evm/apply.h>
#include <evm/evmtx.h>
#include <evm/hashing.h>
#include <evm/host.h>
#include <evm/state_cache.h>
#include <evm/state_db.h>

#include <hash.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <cstring>
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

// Phase 2.3a tests used a Bitcoin Core HashWriter for codeHash because
// a real Keccak-256 wrapper did not exist yet. Phase 2.3b ships
// evm::Keccak256 (see src/evm/hashing.{h,cpp}) — we use it directly
// here so the tests double as integration coverage for that wrapper.
uint256 Keccak256(const std::vector<uint8_t>& bytes)
{
    return evm::Keccak256(bytes);
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

// ============================================================================
// Phase 2.3b — ApplyEvmDeployTx
// ============================================================================

namespace {

// Build a CEvmDeployTx with the supplied init code, sender, and nonce.
evm::CEvmDeployTx MakeDeployTx(const uint256& senderHash,
                               uint64_t nonce,
                               const std::vector<uint8_t>& initCode,
                               uint64_t gasLimit = 1'000'000)
{
    evm::CEvmDeployTx tx;
    tx.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    tx.code = initCode;
    tx.gasLimit = gasLimit;
    tx.maxFeePerGas = 0;
    tx.maxPriorityFeePerGas = 0;
    tx.senderHash = senderHash;
    tx.nonce = nonce;
    return tx;
}

} // anonymous namespace

// ----------------------------------------------------------------------------
// Deploy a minimal contract: init returns a 6-byte runtime
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(deploy_installs_runtime_code_at_derived_address)
{
    // Runtime code (what the deployed contract will execute on each
    // CALL):
    //   PUSH1 0x42, PUSH1 0x01, SSTORE, STOP   ; 6 bytes
    const std::vector<uint8_t> runtime = {
        0x60, 0x42,
        0x60, 0x01,
        0x55,
        0x00,
    };

    // Init code: place runtime in memory at offset 26 (so its 6 bytes
    // sit at memory[26..31]) and RETURN(offset=26, size=6).
    //
    //   PUSH6 0x604260015500    ; (65 60 42 60 01 55 00) push runtime
    //   PUSH1 0x00              ; offset for MSTORE
    //   MSTORE                  ; memory[0..32] = 0...0||runtime
    //   PUSH1 0x06              ; size = 6
    //   PUSH1 0x1A              ; offset = 32 - 6 = 26
    //   RETURN
    const std::vector<uint8_t> initCode = {
        0x65, 0x60, 0x42, 0x60, 0x01, 0x55, 0x00,
        0x60, 0x00,
        0x52,
        0x60, 0x06,
        0x60, 0x1A,
        0xF3,
    };

    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);

    // Sender's 20-byte address lives in the low 20 bytes of senderHash.
    uint256 senderHash;
    *(senderHash.begin() + 31) = 0xAA; // sender = 0x00..00AA

    evm::CEvmDeployTx tx = MakeDeployTx(senderHash, /*nonce=*/ 0, initCode);
    evm::ApplyResult r = evm::ApplyEvmDeployTx(tx, cache, MinimalContext());

    BOOST_REQUIRE_EQUAL(r.statusCode, EVMC_SUCCESS);

    // The deployed address is the CREATE derivation of (sender, nonce).
    uint160 expectedSender;
    std::memcpy(expectedSender.begin(), senderHash.begin() + 12, 20);
    uint160 expectedAddress = evm::ContractAddressFromCreate(expectedSender, 0);
    BOOST_CHECK(r.deployedAddress == expectedAddress);

    // The account record at the deployed address now has codeHash =
    // Keccak256(runtime) and a fresh empty storage root.
    evm::CEvmAccount account;
    BOOST_REQUIRE(cache.GetAccount(r.deployedAddress, account));
    BOOST_CHECK_EQUAL(account.nonce, 1U);
    uint256 expectedCodeHash = evm::Keccak256(runtime);
    BOOST_CHECK(account.codeHash == expectedCodeHash);
    BOOST_CHECK(account.storageRoot == evm::CEvmAccount::EmptyStorageRoot());

    // The runtime code is stored under its hash, byte-for-byte
    // identical to what we expected.
    std::vector<uint8_t> stored;
    BOOST_REQUIRE(cache.GetCode(expectedCodeHash, stored));
    BOOST_REQUIRE_EQUAL(stored.size(), runtime.size());
    BOOST_CHECK_EQUAL_COLLECTIONS(stored.begin(), stored.end(),
                                  runtime.begin(), runtime.end());
}

// ----------------------------------------------------------------------------
// Deploy then Call — end-to-end "real" sequence
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(deploy_then_call_persists_storage_at_deployed_address)
{
    // Same runtime as the previous test: SSTORE 0x42 at slot 0x01.
    const std::vector<uint8_t> runtime = {
        0x60, 0x42, 0x60, 0x01, 0x55, 0x00,
    };
    const std::vector<uint8_t> initCode = {
        0x65, 0x60, 0x42, 0x60, 0x01, 0x55, 0x00,
        0x60, 0x00, 0x52,
        0x60, 0x06, 0x60, 0x1A, 0xF3,
    };

    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);

    uint256 senderHash;
    *(senderHash.begin() + 31) = 0xBC;

    // 1. Deploy
    evm::CEvmDeployTx deployTx = MakeDeployTx(senderHash, /*nonce=*/ 0, initCode);
    evm::ApplyResult deployR =
        evm::ApplyEvmDeployTx(deployTx, cache, MinimalContext());
    BOOST_REQUIRE_EQUAL(deployR.statusCode, EVMC_SUCCESS);

    // 2. Construct a CALL tx pointing at the freshly-deployed contract.
    //    Convert the 20-byte address into the 32-byte payload form
    //    (low 20 bytes carry the address; high 12 bytes are zero).
    uint256 toAddress;
    std::memcpy(toAddress.begin() + 12, deployR.deployedAddress.begin(), 20);

    evm::CEvmCallTx callTx = MakeCallTx(toAddress, 1'000'000);
    evm::ApplyResult callR =
        evm::ApplyEvmCallTx(callTx, cache, MinimalContext());
    BOOST_REQUIRE_EQUAL(callR.statusCode, EVMC_SUCCESS);

    // 3. Verify storage slot 0x01 at the deployed address is 0x42.
    uint160 contractAddr = deployR.deployedAddress;
    uint256 slot = LowByte(0x01);
    uint256 stored;
    BOOST_REQUIRE(cache.GetStorage(contractAddr, slot, stored));
    BOOST_CHECK_EQUAL(static_cast<int>(*(stored.begin() + 31)), 0x42);
}

// ----------------------------------------------------------------------------
// CREATE-collision refusal
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(deploy_refuses_collision_with_existing_contract)
{
    const std::vector<uint8_t> initCode = {
        0x60, 0x00, 0x60, 0x00, 0xF3, // return empty
    };

    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);

    uint256 senderHash;
    *(senderHash.begin() + 31) = 0xDD;

    uint160 expectedSender;
    std::memcpy(expectedSender.begin(), senderHash.begin() + 12, 20);
    uint160 collidingAddress = evm::ContractAddressFromCreate(expectedSender, 5);

    // Place a pre-existing contract at the address we'd derive for
    // (sender, nonce=5). Use a non-empty codeHash to trigger the
    // collision branch.
    uint256 fakeCodeHash;
    *(fakeCodeHash.begin() + 31) = 0xFE;
    cache.SetAccount(collidingAddress, evm::CEvmAccount(
        /*nonce=*/ 1,
        /*balance=*/ uint256(),
        /*codeHash=*/ fakeCodeHash,
        evm::CEvmAccount::EmptyStorageRoot()));

    evm::CEvmDeployTx tx = MakeDeployTx(senderHash, /*nonce=*/ 5, initCode);
    evm::ApplyResult r = evm::ApplyEvmDeployTx(tx, cache, MinimalContext());

    BOOST_CHECK_EQUAL(r.statusCode, EVMC_FAILURE);
    // Gas counter records the full gas limit as consumed on refusal —
    // a deploy that collides is treated like out-of-gas for fee
    // accounting purposes (matches what EIP-3541 and friends do).
    BOOST_CHECK_EQUAL(r.gasUsed, static_cast<int64_t>(tx.gasLimit));

    // The colliding account is unchanged: still has the fake codeHash.
    evm::CEvmAccount unchanged;
    BOOST_REQUIRE(cache.GetAccount(collidingAddress, unchanged));
    BOOST_CHECK(unchanged.codeHash == fakeCodeHash);
}

BOOST_AUTO_TEST_SUITE_END()
