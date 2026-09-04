// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <consensus/params.h>
#include <evm/host.h>
#include <evm/precompiles.h>
#include <evm/state_cache.h>
#include <evm/state_db.h>

#include <evmc/evmc.hpp>

#include <boost/test/unit_test.hpp>

#include <cstring>
#include <vector>

/**
 * Phase-4 RTM-native precompiles — ChainLocks (0x..0a03), LLMQ oracle
 * (0x..0a02), masternode registry (0x..0a04).
 *
 * Two layers of coverage:
 *
 *  (A) Selector-constant verification. Each precompile pins its
 *      4-byte method selectors as HAND-COMPUTED constants. A single
 *      wrong nibble silently mis-routes (or unreachables) a method,
 *      breaking Solidity/ethers compatibility with no other symptom.
 *      We recompute every selector via AbiFunctionSelector (proven
 *      against canonical ERC-20 values elsewhere) and assert it equals
 *      the documented constant. Pure — no managers, zero risk.
 *
 *  (B) Live dispatch + read/failure paths for the two precompiles whose
 *      backing managers answer deterministically from empty state in a
 *      bare node (ChainLocks: HasChainLock / IsLocked map lookups;
 *      LLMQ: HasRecoveredSigForId). On a fresh regtest node nothing is
 *      locked / signed, so the reads return false / 0 — which still
 *      exercises address recognition, dispatch routing, ABI decode and
 *      ABI encode. (MN getCount needs a DIP3 chain — its live path is
 *      left to an integration test; its selectors are pinned here.)
 */

namespace {

evm::ExecutionContext MinimalContext()
{
    evm::ExecutionContext c;
    c.chainId = 7375;
    c.blockHeight = 100;
    c.blockTimestamp = 1700000000;
    c.blockGasLimit = 30'000'000;
    return c;
}

evmc::address FixedPrecompile(uint8_t suffix)
{
    evmc::address a{};
    a.bytes[18] = 0x0A;
    a.bytes[19] = suffix;
    return a;
}

evmc::Result Call(evm::CEvmHost& host, uint8_t suffix,
                  const std::vector<uint8_t>& input, int64_t gas = 100000)
{
    const evmc::address addr = FixedPrecompile(suffix);
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = gas;
    msg.recipient = addr;
    msg.code_address = addr;
    msg.sender = evmc::address{};
    msg.value = evmc::uint256be{};
    msg.input_data = input.empty() ? nullptr : input.data();
    msg.input_size = input.size();
    evmc::Result out{};
    BOOST_REQUIRE(evm::ExecutePrecompile(host, msg, out));
    return out;
}

std::vector<uint8_t> Sel(uint32_t s)
{
    return {static_cast<uint8_t>(s >> 24), static_cast<uint8_t>(s >> 16),
            static_cast<uint8_t>(s >> 8), static_cast<uint8_t>(s)};
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(evm_phase4_precompiles_tests, TestingSetup)

// (A) Every precompile's pinned selector constant == keccak256(sig)[0:4].
BOOST_AUTO_TEST_CASE(all_precompile_selectors_match_signatures)
{
    // ChainLocks
    BOOST_CHECK_EQUAL(
        evm::AbiFunctionSelector("isChainLocked(uint32,bytes32)"), 0x59D66E82U);
    BOOST_CHECK_EQUAL(
        evm::AbiFunctionSelector("isTxInstantLocked(bytes32)"), 0x51C37046U);
    BOOST_CHECK_EQUAL(
        evm::AbiFunctionSelector("latestChainLockedHeight()"), 0x2A433BE9U);
    // LLMQ oracle
    BOOST_CHECK_EQUAL(
        evm::AbiFunctionSelector("hasSignature(uint8,bytes32)"), 0x76F08249U);
    BOOST_CHECK_EQUAL(
        evm::AbiFunctionSelector("getSignature(uint8,bytes32)"), 0x4E640CB2U);
    BOOST_CHECK_EQUAL(
        evm::AbiFunctionSelector(
            "verifySignature(uint8,uint32,bytes32,bytes32,bytes)"),
        0x1805CE6BU);
    // Masternode registry
    BOOST_CHECK_EQUAL(evm::AbiFunctionSelector("getCount()"), 0xA87D942CU);
    BOOST_CHECK_EQUAL(
        evm::AbiFunctionSelector("isMasternode(bytes32)"), 0x47AEBAB3U);
    BOOST_CHECK_EQUAL(
        evm::AbiFunctionSelector("getByProTxHash(bytes32)"), 0x0402E163U);
    BOOST_CHECK_EQUAL(
        evm::AbiFunctionSelector("getByIndex(uint256)"), 0x2D883A73U);
}

// (B) ChainLocks live read + failure paths (nothing locked on a fresh node).
BOOST_AUTO_TEST_CASE(chainlocks_read_and_failure_paths)
{
    evm::CEvmStateDB db(1 << 20, true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, MinimalContext());

    // isChainLocked(123, <hash>) -> false
    {
        std::vector<uint8_t> in = Sel(0x59D66E82);
        in.resize(4 + 64, 0);
        in[4 + 31] = 123;  // uint32 height in the low byte of slot 0
        const evmc::Result r = Call(host, 0x03, in);
        BOOST_REQUIRE(r.status_code == EVMC_SUCCESS);
        std::vector<uint8_t> out(r.output_data, r.output_data + r.output_size);
        bool v = true;
        BOOST_REQUIRE(evm::AbiReadBool(out, 0, v));
        BOOST_CHECK(!v);
    }
    // isTxInstantLocked(<txid>) -> false
    {
        std::vector<uint8_t> in = Sel(0x51C37046);
        in.resize(4 + 32, 0);
        in[4 + 5] = 0xAB;  // arbitrary txid
        const evmc::Result r = Call(host, 0x03, in);
        BOOST_REQUIRE(r.status_code == EVMC_SUCCESS);
        std::vector<uint8_t> out(r.output_data, r.output_data + r.output_size);
        bool v = true;
        BOOST_REQUIRE(evm::AbiReadBool(out, 0, v));
        BOOST_CHECK(!v);
    }
    // latestChainLockedHeight() -> 0 (no chainlocks on a fresh node)
    {
        const evmc::Result r = Call(host, 0x03, Sel(0x2A433BE9));
        BOOST_REQUIRE(r.status_code == EVMC_SUCCESS);
        std::vector<uint8_t> out(r.output_data, r.output_data + r.output_size);
        uint64_t h = 1;
        BOOST_REQUIRE(evm::AbiReadUint64(out, 0, h));
        BOOST_CHECK_EQUAL(h, 0U);
    }
    // Unknown selector -> failure.
    BOOST_CHECK(Call(host, 0x03, Sel(0xdeadbeef)).status_code == EVMC_FAILURE);
    // Truncated args -> failure.
    BOOST_CHECK(Call(host, 0x03, Sel(0x59D66E82)).status_code == EVMC_FAILURE);
}

// (B) LLMQ oracle live read + failure paths.
BOOST_AUTO_TEST_CASE(llmq_read_and_failure_paths)
{
    evm::CEvmStateDB db(1 << 20, true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, MinimalContext());

    // hasSignature(validType, id) -> false (no recovered sigs).
    {
        std::vector<uint8_t> in = Sel(0x76F08249);
        in.resize(4 + 64, 0);
        in[4 + 31] = static_cast<uint8_t>(Consensus::LLMQ_50_60);  // uint8 type
        in[4 + 32 + 7] = 0x42;  // arbitrary id
        const evmc::Result r = Call(host, 0x02, in);
        BOOST_REQUIRE(r.status_code == EVMC_SUCCESS);
        std::vector<uint8_t> out(r.output_data, r.output_data + r.output_size);
        bool v = true;
        BOOST_REQUIRE(evm::AbiReadBool(out, 0, v));
        BOOST_CHECK(!v);
    }
    // Unknown LLMQ type -> failure.
    {
        std::vector<uint8_t> in = Sel(0x76F08249);
        in.resize(4 + 64, 0);
        in[4 + 31] = 0xFE;  // not a known llmq type
        BOOST_CHECK(Call(host, 0x02, in).status_code == EVMC_FAILURE);
    }
    // Truncated args -> failure.
    BOOST_CHECK(Call(host, 0x02, Sel(0x76F08249)).status_code == EVMC_FAILURE);
    // Unknown selector -> failure.
    BOOST_CHECK(Call(host, 0x02, Sel(0x12345678)).status_code == EVMC_FAILURE);
}

// Reserved fixed-precompile suffix 0x01 is not implemented -> failure.
BOOST_AUTO_TEST_CASE(reserved_precompile_0a01_fails)
{
    evm::CEvmStateDB db(1 << 20, true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, MinimalContext());
    BOOST_CHECK(Call(host, 0x01, Sel(0xA87D942C)).status_code == EVMC_FAILURE);
}

BOOST_AUTO_TEST_SUITE_END()
