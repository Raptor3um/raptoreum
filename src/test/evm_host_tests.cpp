// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <evm/account.h>
#include <evm/hashing.h>
#include <evm/host.h>
#include <evm/state_cache.h>
#include <evm/state_db.h>

#include <evmc/evmc.hpp>
#include <evmone/evmone.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

/**
 * Phase 2.2 tests for CEvmHost:
 *
 *   - Execute real EVM bytecode against a host backed by CEvmStateCache.
 *   - Verify SSTORE/SLOAD round-trip through the cache.
 *   - Verify EIP-1153 transient storage (TSTORE/TLOAD).
 *   - Verify BALANCE reads from the cache.
 *   - Verify LOG0..LOG4 emit captured in the host.
 *   - Verify EIP-2929 access lists (cold then warm).
 *   - Verify nested CALL returns EVMC_REVERT (Phase 2.2 stub).
 */

namespace {

evmc::address MakeAddr(uint8_t fill)
{
    evmc::address a{};
    std::memset(a.bytes, fill, 20);
    return a;
}

evmc::bytes32 MakeWord(uint8_t fill)
{
    evmc::bytes32 b{};
    std::memset(b.bytes, fill, 32);
    return b;
}

evm::ExecutionContext MinimalContext()
{
    evm::ExecutionContext c;
    c.chainId = 7375; // regtest per docs/evm/PROPOSAL-FOR-CORE-TEAM.md
    c.blockHeight = 100;
    c.blockTimestamp = 1700000000;
    c.blockGasLimit = 30'000'000;
    return c;
}

// Run a single EVMC_CALL against the supplied host with the given bytecode.
// recipient defaults to 0x00..00. EVM revision: Cancun (D6).
evmc::Result Execute(evm::CEvmHost& host,
                     const std::vector<uint8_t>& bytecode,
                     int64_t gas = 1'000'000,
                     const evmc::address& recipient = evmc::address{})
{
    evmc::VM vm{evmc_create_evmone()};
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = gas;
    msg.recipient = recipient;
    msg.sender = evmc::address{};
    msg.code_address = recipient;
    msg.value = evmc::uint256be{};
    msg.input_data = nullptr;
    msg.input_size = 0;
    return vm.execute(host, EVMC_CANCUN, msg,
                      bytecode.empty() ? nullptr : bytecode.data(),
                      bytecode.size());
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(evm_host_tests, BasicTestingSetup)

// ----------------------------------------------------------------------------
// SSTORE + SLOAD round-trip through the state cache
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(sstore_then_sload_returns_value)
{
    // Bytecode:
    //   PUSH1 0x42, PUSH1 0x01, SSTORE     ; storage[1] = 0x42
    //   PUSH1 0x01, SLOAD                   ; stack <- storage[1]
    //   PUSH1 0x00, MSTORE                  ; memory[0..32] = stack top
    //   PUSH1 0x20, PUSH1 0x00, RETURN      ; return 32 bytes from memory[0]
    const std::vector<uint8_t> code = {
        0x60, 0x42,
        0x60, 0x01,
        0x55,
        0x60, 0x01,
        0x54,
        0x60, 0x00,
        0x52,
        0x60, 0x20, 0x60, 0x00,
        0xF3,
    };

    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, MinimalContext());

    evmc::Result r = Execute(host, code);

    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(r.output_size, 32U);
    // Last byte = 0x42, rest zero.
    BOOST_CHECK_EQUAL(static_cast<int>(r.output_data[31]), 0x42);
    for (size_t i = 0; i < 31; ++i) {
        BOOST_CHECK_EQUAL(static_cast<int>(r.output_data[i]), 0);
    }

    // Verify SSTORE persisted into the cache. Slot 0x01 at the recipient
    // address (0x00..00 in this test) should now read 0x42.
    //
    // The cache stores keys/values as uint256 byte arrays whose m_data
    // layout matches the on-wire big-endian form (matches evmc::bytes32
    // byte-for-byte). The last byte of m_data corresponds to the
    // least-significant byte of the 256-bit integer.
    uint256 stored;
    uint160 addr; // all zeros
    uint256 slot;
    *(slot.begin() + 31) = 0x01;
    BOOST_REQUIRE(cache.GetStorage(addr, slot, stored));
    BOOST_CHECK_EQUAL(static_cast<int>(*(stored.begin() + 31)), 0x42);
    // The other 31 bytes are zero.
    for (int i = 0; i < 31; ++i) {
        BOOST_CHECK_EQUAL(static_cast<int>(*(stored.begin() + i)), 0);
    }
}

// ----------------------------------------------------------------------------
// EIP-1153 transient storage round-trip
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(tstore_then_tload_returns_value)
{
    // Bytecode:
    //   PUSH1 0x77, PUSH1 0x05, TSTORE  (0x5D)  ; transient[5] = 0x77
    //   PUSH1 0x05, TLOAD  (0x5C)               ; load
    //   PUSH1 0x00, MSTORE
    //   PUSH1 0x20, PUSH1 0x00, RETURN
    const std::vector<uint8_t> code = {
        0x60, 0x77,
        0x60, 0x05,
        0x5D,           // TSTORE (Cancun)
        0x60, 0x05,
        0x5C,           // TLOAD (Cancun)
        0x60, 0x00,
        0x52,
        0x60, 0x20, 0x60, 0x00,
        0xF3,
    };

    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, MinimalContext());

    evmc::Result r = Execute(host, code);

    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(r.output_size, 32U);
    BOOST_CHECK_EQUAL(static_cast<int>(r.output_data[31]), 0x77);

    // Transient storage does NOT bleed into persistent. After execution
    // the cache must have no entry for slot 0x05.
    uint160 addr;
    uint256 slot;
    *(slot.begin() + 31) = 0x05;
    uint256 value;
    BOOST_CHECK(!cache.GetStorage(addr, slot, value));
}

// ----------------------------------------------------------------------------
// BALANCE reads from the cache
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(balance_reads_account_from_cache)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);

    // Pre-populate an account with a known balance.
    uint160 targetAddr;
    *(targetAddr.begin() + 19) = 0x0B; // last byte = 0x0B
    uint256 balance;
    *(balance.begin() + 31) = 0x99;
    cache.SetAccount(targetAddr, evm::CEvmAccount(
        /*nonce=*/ 0,
        /*balance=*/ balance,
        evm::CEvmAccount::EmptyCodeHash(),
        evm::CEvmAccount::EmptyStorageRoot()));

    evm::CEvmHost host(cache, MinimalContext());

    // Sanity: direct host call returns 0x99 as last byte of balance.
    evmc::address evmAddr{};
    evmAddr.bytes[19] = 0x0B;
    evmc::uint256be bal = host.get_balance(evmAddr);
    BOOST_CHECK_EQUAL(static_cast<int>(bal.bytes[31]), 0x99);
    for (int i = 0; i < 31; ++i) {
        BOOST_CHECK_EQUAL(static_cast<int>(bal.bytes[i]), 0);
    }
}

// ----------------------------------------------------------------------------
// LOG0 captured in the host
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(emit_log_captured)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, MinimalContext());

    // Direct call (bypassing the EVM) is the cleanest unit test for the
    // host's emit_log: the EVM bytecode for LOG0 with arbitrary data
    // requires memory setup that's more verbose than the test value.
    evmc::address addr = MakeAddr(0xAB);
    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    host.emit_log(addr, data, sizeof(data), nullptr, 0);

    BOOST_REQUIRE_EQUAL(host.Logs().size(), 1U);
    const auto& log = host.Logs()[0];
    BOOST_CHECK_EQUAL(std::memcmp(log.address.bytes, addr.bytes, 20), 0);
    BOOST_CHECK_EQUAL(log.topics.size(), 0U);
    BOOST_REQUIRE_EQUAL(log.data.size(), 4U);
    BOOST_CHECK_EQUAL(log.data[0], 0x01);
    BOOST_CHECK_EQUAL(log.data[3], 0x04);
}

// ----------------------------------------------------------------------------
// EIP-2929 access lists: cold on first access, warm thereafter
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(access_account_cold_then_warm)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, MinimalContext());

    evmc::address a = MakeAddr(0xCC);
    BOOST_CHECK_EQUAL(host.access_account(a), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.access_account(a), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.access_account(a), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(access_storage_cold_then_warm)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, MinimalContext());

    evmc::address a = MakeAddr(0xCC);
    evmc::bytes32 k = MakeWord(0xDD);

    BOOST_CHECK_EQUAL(host.access_storage(a, k), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.access_storage(a, k), EVMC_ACCESS_WARM);

    // Different slot at same address: still cold.
    evmc::bytes32 k2 = MakeWord(0xEE);
    BOOST_CHECK_EQUAL(host.access_storage(a, k2), EVMC_ACCESS_COLD);
}

// ----------------------------------------------------------------------------
// Nested CALL to a codeless, zero-value target — succeeds as a no-op.
//
// Phase 2.2 returned EVMC_REVERT here because call() was a stub. Phase
// 2.3d wires the real dispatcher, so this case now succeeds (matches
// Ethereum semantics: CALL with no code is a value-only transfer, and
// zero-value adds no state, so the result is empty success).
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(call_codeless_zero_value_succeeds)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, MinimalContext());

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = 100'000;

    evmc::Result r = host.call(msg);
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
    // Gas is returned untouched: no code to execute and no
    // value-transfer work to do.
    BOOST_CHECK_EQUAL(r.gas_left, 100'000);
}

// ----------------------------------------------------------------------------
// get_tx_context plumbing
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(tx_context_returns_execution_context)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);

    evm::ExecutionContext c = MinimalContext();
    c.chainId = 7373;          // mainnet per design
    c.blockHeight = 999;
    c.blockTimestamp = 1730000000;
    *(c.baseFee.begin() + 31) = 0x07;
    c.coinbase = uint160{};
    *(c.coinbase.begin() + 19) = 0xCB;

    evm::CEvmHost host(cache, c);
    evmc_tx_context tx = host.get_tx_context();

    BOOST_CHECK_EQUAL(tx.block_number, 999);
    BOOST_CHECK_EQUAL(tx.block_timestamp, 1730000000);
    // chain_id is uint256be — last byte should be 7373 & 0xff = 0xCD,
    // and the next byte up should be 7373 >> 8 = 0x1C.
    BOOST_CHECK_EQUAL(static_cast<int>(tx.chain_id.bytes[31]), 0xCD);
    BOOST_CHECK_EQUAL(static_cast<int>(tx.chain_id.bytes[30]), 0x1C);
    BOOST_CHECK_EQUAL(static_cast<int>(tx.block_base_fee.bytes[31]), 0x07);
    BOOST_CHECK_EQUAL(static_cast<int>(tx.block_coinbase.bytes[19]), 0xCB);
}

// ----------------------------------------------------------------------------
// SELFDESTRUCT records the intent
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(selfdestruct_records_address)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, MinimalContext());

    evmc::address victim = MakeAddr(0xDE);
    evmc::address heir = MakeAddr(0xAD);

    BOOST_CHECK(host.Selfdestructs().empty());
    BOOST_CHECK(host.selfdestruct(victim, heir));
    BOOST_CHECK_EQUAL(host.Selfdestructs().size(), 1U);
    BOOST_CHECK(host.Selfdestructs().count(victim) == 1);

    // Idempotent: a second selfdestruct of the same address returns
    // false (already recorded) and does not duplicate.
    BOOST_CHECK(!host.selfdestruct(victim, heir));
    BOOST_CHECK_EQUAL(host.Selfdestructs().size(), 1U);
}

// ============================================================================
// Phase 2.3d — Nested CALL / DELEGATECALL / STATICCALL / CREATE end-to-end
// ============================================================================
//
// These tests run a real outer contract that issues the inner opcodes;
// evmone invokes CEvmHost::call() under the hood, which exercises the
// snapshot/revert + state-mutation paths added in this phase.

namespace {

// Pre-install runtime code + an account record at addr. Mirrors the
// DeployForTest helper in evm_apply_tests.cpp but uses the host's
// type conversions.
void InstallContract(evm::CEvmStateCache& cache,
                     uint8_t addrLowByte,
                     const std::vector<uint8_t>& code,
                     uint64_t balanceWeis = 0)
{
    uint160 addr;
    *(addr.begin() + 19) = addrLowByte;

    const uint256 codeHash = evm::Keccak256(code);
    cache.SetCode(codeHash, code);

    uint256 balance;
    for (int i = 0; i < 8; ++i) {
        *(balance.begin() + 31 - i) =
            static_cast<uint8_t>((balanceWeis >> (8 * i)) & 0xFF);
    }
    cache.SetAccount(addr, evm::CEvmAccount(
        /*nonce=*/ 1, balance,
        codeHash, evm::CEvmAccount::EmptyStorageRoot()));
}

// Run runtime code at `addrLowByte` and return the evmone result.
evmc::Result CallContract(evm::CEvmHost& host,
                         evm::CEvmStateCache& cache,
                         uint8_t addrLowByte,
                         int64_t gas = 5'000'000,
                         uint64_t valueWeis = 0)
{
    uint160 addr;
    *(addr.begin() + 19) = addrLowByte;

    evm::CEvmAccount acc;
    BOOST_REQUIRE(cache.GetAccount(addr, acc));
    std::vector<uint8_t> code;
    BOOST_REQUIRE(cache.GetCode(acc.codeHash, code));

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = gas;
    std::memcpy(msg.recipient.bytes, addr.begin(), 20);
    msg.code_address = msg.recipient;
    // value in big-endian: write the low 8 bytes.
    for (int i = 0; i < 8; ++i) {
        msg.value.bytes[31 - i] =
            static_cast<uint8_t>((valueWeis >> (8 * i)) & 0xFF);
    }

    evmc::VM vm{evmc_create_evmone()};
    return vm.execute(host, EVMC_CANCUN, msg,
                      code.empty() ? nullptr : code.data(), code.size());
}

// Bytecode of "outer" contract that CALLs address 0x07 with no value
// or args, then STOPs. Sufficient to exercise the inner CALL frame.
std::vector<uint8_t> OuterCallsByte07Bytecode()
{
    return {
        0x60, 0x00,                  // PUSH1 0 (retSize)
        0x60, 0x00,                  // PUSH1 0 (retOffset)
        0x60, 0x00,                  // PUSH1 0 (argsSize)
        0x60, 0x00,                  // PUSH1 0 (argsOffset)
        0x60, 0x00,                  // PUSH1 0 (value)
        0x60, 0x07,                  // PUSH1 0x07 (address)
        0x62, 0x0F, 0x42, 0x40,      // PUSH3 0x0F4240 (gas = 1_000_000)
        0xF1,                        // CALL
        0x00,                        // STOP
    };
}

// Inner contract: SSTORE 0x42 at slot 0x01, then STOP (or REVERT).
std::vector<uint8_t> InnerSstoreThenStop()
{
    return { 0x60, 0x42, 0x60, 0x01, 0x55, 0x00 };
}
std::vector<uint8_t> InnerSstoreThenRevert()
{
    return {
        0x60, 0x42, 0x60, 0x01, 0x55, // PUSH1 0x42; PUSH1 0x01; SSTORE
        0x60, 0x00, 0x60, 0x00,       // PUSH1 0x00; PUSH1 0x00 (offset/size for REVERT)
        0xFD,                         // REVERT
    };
}

} // anonymous namespace

// ----------------------------------------------------------------------------
// Nested CALL succeeds — inner SSTORE persists to the cache after the
// outer frame completes.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(nested_call_persists_inner_sstore)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, MinimalContext());

    InstallContract(cache, 0xAA, OuterCallsByte07Bytecode());
    InstallContract(cache, 0x07, InnerSstoreThenStop());

    evmc::Result r = CallContract(host, cache, 0xAA);
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);

    uint160 innerAddr;
    *(innerAddr.begin() + 19) = 0x07;
    uint256 slot1;
    *(slot1.begin() + 31) = 0x01;
    uint256 stored;
    BOOST_REQUIRE(cache.GetStorage(innerAddr, slot1, stored));
    BOOST_CHECK_EQUAL(static_cast<int>(*(stored.begin() + 31)), 0x42);
}

// ----------------------------------------------------------------------------
// Nested CALL that REVERTs rolls back its writes via the host's
// snapshot mechanism. The outer frame still completes normally.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(nested_call_revert_rolls_back_inner_sstore)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, MinimalContext());

    InstallContract(cache, 0xAB, OuterCallsByte07Bytecode());
    InstallContract(cache, 0x07, InnerSstoreThenRevert());

    evmc::Result r = CallContract(host, cache, 0xAB);
    // Outer frame succeeds — the inner CALL's revert is contained.
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);

    // The inner SSTORE was rolled back by the savepoint.
    uint160 innerAddr;
    *(innerAddr.begin() + 19) = 0x07;
    uint256 slot1;
    *(slot1.begin() + 31) = 0x01;
    uint256 stored;
    BOOST_CHECK(!cache.GetStorage(innerAddr, slot1, stored));
}

// ----------------------------------------------------------------------------
// STATICCALL into a contract that attempts SSTORE: evmone enforces the
// static flag and the inner frame fails. Outer succeeds. Storage is
// not written.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(nested_staticcall_blocks_inner_sstore)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, MinimalContext());

    // Outer contract: STATICCALL 0x07. STATICCALL pops 6 args (no value).
    //   PUSH1 0 (retSize)
    //   PUSH1 0 (retOffset)
    //   PUSH1 0 (argsSize)
    //   PUSH1 0 (argsOffset)
    //   PUSH1 0x07 (address)
    //   PUSH3 0x0F4240 (gas)
    //   STATICCALL (0xFA)
    //   STOP
    const std::vector<uint8_t> outer = {
        0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0x60, 0x00,
        0x60, 0x07,
        0x62, 0x0F, 0x42, 0x40,
        0xFA,
        0x00,
    };

    InstallContract(cache, 0xAC, outer);
    InstallContract(cache, 0x07, InnerSstoreThenStop());

    evmc::Result r = CallContract(host, cache, 0xAC);
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);

    // Storage at the inner contract is untouched — STATICCALL +
    // SSTORE is a state-mutation violation that evmone refuses.
    uint160 innerAddr;
    *(innerAddr.begin() + 19) = 0x07;
    uint256 slot1;
    *(slot1.begin() + 31) = 0x01;
    uint256 stored;
    BOOST_CHECK(!cache.GetStorage(innerAddr, slot1, stored));
}

// ----------------------------------------------------------------------------
// CALL with value: balance transfers from caller to callee. Caller is
// pre-funded so the inner debit can succeed.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(nested_call_with_value_transfers_balance)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, MinimalContext());

    // Outer: CALL 0x07 with value=1000 (PUSH2 0x03E8).
    //   PUSH1 0       (retSize)
    //   PUSH1 0       (retOffset)
    //   PUSH1 0       (argsSize)
    //   PUSH1 0       (argsOffset)
    //   PUSH2 0x03E8  (value = 1000)
    //   PUSH1 0x07    (address)
    //   PUSH3 0x0F4240(gas)
    //   CALL          (0xF1)
    //   STOP          (0x00)
    const std::vector<uint8_t> outer = {
        0x60, 0x00, 0x60, 0x00, 0x60, 0x00, 0x60, 0x00,
        0x61, 0x03, 0xE8,
        0x60, 0x07,
        0x62, 0x0F, 0x42, 0x40,
        0xF1, 0x00,
    };

    // Pre-fund the outer contract with 2000 weis.
    InstallContract(cache, 0xAD, outer, /*balanceWeis=*/ 2000);
    // Inner: do-nothing STOP.
    InstallContract(cache, 0x07, {0x00});

    evmc::Result r = CallContract(host, cache, 0xAD);
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);

    uint160 outerAddr;
    *(outerAddr.begin() + 19) = 0xAD;
    uint160 innerAddr;
    *(innerAddr.begin() + 19) = 0x07;

    evm::CEvmAccount outerAcc;
    BOOST_REQUIRE(cache.GetAccount(outerAddr, outerAcc));
    evm::CEvmAccount innerAcc;
    BOOST_REQUIRE(cache.GetAccount(innerAddr, innerAcc));

    // Caller debited by 1000; callee credited by 1000.
    BOOST_CHECK_EQUAL(static_cast<int>(*(outerAcc.balance.begin() + 30)), 0x03);
    BOOST_CHECK_EQUAL(static_cast<int>(*(outerAcc.balance.begin() + 31)), 0xE8);
    BOOST_CHECK_EQUAL(static_cast<int>(*(innerAcc.balance.begin() + 30)), 0x03);
    BOOST_CHECK_EQUAL(static_cast<int>(*(innerAcc.balance.begin() + 31)), 0xE8);
}

// ----------------------------------------------------------------------------
// Nested CREATE: outer contract uses CREATE to deploy a child whose
// runtime executes SSTORE. After the outer frame completes, the child
// account exists at the derived address with the correct runtime code.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(nested_create_installs_child_at_derived_address)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, MinimalContext());

    // Outer contract code layout:
    //   header(15 bytes) || init_code(15 bytes)
    //
    // header copies init_code into memory[0..15] via CODECOPY and then
    // invokes CREATE(value=0, offset=0, length=15).
    //
    //   PUSH1 0x0F  (length=15)
    //   PUSH1 0x0F  (codeOffset = 15 = end of header)
    //   PUSH1 0x00  (destOffset = 0)
    //   CODECOPY    (0x39)
    //   PUSH1 0x0F  (length for CREATE)
    //   PUSH1 0x00  (offset for CREATE)
    //   PUSH1 0x00  (value for CREATE)
    //   CREATE      (0xF0)
    //   STOP
    //
    // init code returns the standard 6-byte runtime (SSTORE 0x42 at
    // slot 0x01; STOP).
    const std::vector<uint8_t> outer = {
        // header (15 bytes)
        0x60, 0x0F,
        0x60, 0x0F,
        0x60, 0x00,
        0x39,
        0x60, 0x0F,
        0x60, 0x00,
        0x60, 0x00,
        0xF0,
        0x00,
        // init code (15 bytes): runtime 60 42 60 01 55 00
        0x65, 0x60, 0x42, 0x60, 0x01, 0x55, 0x00,
        0x60, 0x00, 0x52,
        0x60, 0x06, 0x60, 0x1A, 0xF3,
    };

    InstallContract(cache, 0xCA, outer);

    evmc::Result r = CallContract(host, cache, 0xCA);
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);

    // Derived child address = CREATE(0xCA-padded-to-20-bytes, nonce=1).
    uint160 outerAddr;
    *(outerAddr.begin() + 19) = 0xCA;
    const uint160 childAddr = evm::ContractAddressFromCreate(outerAddr, 1);

    evm::CEvmAccount childAcc;
    BOOST_REQUIRE(cache.GetAccount(childAddr, childAcc));
    BOOST_CHECK_EQUAL(childAcc.nonce, 1U);

    // Verify the runtime code stored under the child's codeHash matches
    // the expected 6-byte SSTORE-and-STOP runtime.
    const std::vector<uint8_t> expectedRuntime = {0x60, 0x42, 0x60, 0x01, 0x55, 0x00};
    BOOST_CHECK(childAcc.codeHash == evm::Keccak256(expectedRuntime));
    std::vector<uint8_t> storedRuntime;
    BOOST_REQUIRE(cache.GetCode(childAcc.codeHash, storedRuntime));
    BOOST_CHECK_EQUAL_COLLECTIONS(storedRuntime.begin(), storedRuntime.end(),
                                  expectedRuntime.begin(), expectedRuntime.end());

    // Outer contract's nonce was bumped by the CREATE.
    evm::CEvmAccount outerAcc;
    BOOST_REQUIRE(cache.GetAccount(outerAddr, outerAcc));
    BOOST_CHECK_EQUAL(outerAcc.nonce, 2U);
}

BOOST_AUTO_TEST_SUITE_END()
