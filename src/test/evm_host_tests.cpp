// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

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
// Nested CALL returns EVMC_REVERT (Phase 2.2 stub)
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(call_stub_returns_revert)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, MinimalContext());

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = 100'000;

    evmc::Result r = host.call(msg);
    BOOST_CHECK_EQUAL(r.status_code, EVMC_REVERT);
    BOOST_CHECK_EQUAL(r.gas_left, 0);
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

BOOST_AUTO_TEST_SUITE_END()
