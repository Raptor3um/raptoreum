// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <evm/host.h>
#include <evm/precompiles.h>
#include <evm/state_cache.h>
#include <evm/state_db.h>

#include <evmc/evmc.hpp>

#include <boost/test/unit_test.hpp>

#include <cstring>
#include <vector>

/**
 * Phase 4.4 — masternode registry precompile (0x..0a04) LIVE path.
 *
 * Complements evm_phase4_precompiles_tests (which pins the MN selector
 * constants under a bare node). getCount/getByIndex read
 * CDeterministicMNManager::GetListAtChainTip(), which requires a
 * DIP3-active chain — hence TestChainDIP3Setup (431 blocks). This
 * exercises the real GetListAtChainTip + GetAllMNsCount + ABI encode
 * and the not-found / out-of-range failure paths, regardless of how
 * many MNs the base chain has.
 */

namespace {

evm::ExecutionContext Ctx()
{
    evm::ExecutionContext c;
    c.chainId = 7375;
    c.blockHeight = 431;
    c.blockTimestamp = 1700000000;
    c.blockGasLimit = 30'000'000;
    return c;
}

evmc::address MnPrecompile()
{
    evmc::address a{};
    a.bytes[18] = 0x0A;
    a.bytes[19] = 0x04;
    return a;
}

evmc::Result Call(evm::CEvmHost& host, const std::vector<uint8_t>& input)
{
    const evmc::address addr = MnPrecompile();
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = 100000;
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

BOOST_FIXTURE_TEST_SUITE(evm_mn_precompile_tests, TestChainDIP3Setup)

BOOST_AUTO_TEST_CASE(masternode_registry_live_reads)
{
    evm::CEvmStateDB db(1 << 20, true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, Ctx());

    // getCount() -> a valid uint256 (the live MN count at the tip).
    uint64_t count = 0;
    {
        const evmc::Result r = Call(host, Sel(0xA87D942C));
        BOOST_REQUIRE(r.status_code == EVMC_SUCCESS);
        std::vector<uint8_t> out(r.output_data, r.output_data + r.output_size);
        BOOST_REQUIRE(evm::AbiReadUint64(out, 0, count));
        // No assertion on the exact value — the base DIP3 chain may or
        // may not have registered MNs; the point is the live read +
        // encode works deterministically.
    }

    // isMasternode(<random 32-byte hash>) -> false (not a registered
    // proTxHash). Regardless of count, a random hash is not a MN.
    {
        std::vector<uint8_t> in = Sel(0x47AEBAB3);
        in.resize(4 + 32, 0);
        in[4 + 9] = 0x5A;  // arbitrary, vanishingly unlikely to be a MN
        const evmc::Result r = Call(host, in);
        BOOST_REQUIRE(r.status_code == EVMC_SUCCESS);
        std::vector<uint8_t> out(r.output_data, r.output_data + r.output_size);
        bool isMn = true;
        BOOST_REQUIRE(evm::AbiReadBool(out, 0, isMn));
        BOOST_CHECK(!isMn);
    }

    // getByIndex(count) -> failure (index == count is out of range).
    {
        std::vector<uint8_t> in = Sel(0x2D883A73);
        in.resize(4 + 32, 0);
        // Put `count` in the low 8 bytes of the uint256 arg.
        for (int i = 0; i < 8; ++i) {
            in[4 + 31 - i] = static_cast<uint8_t>((count >> (8 * i)) & 0xFF);
        }
        const evmc::Result r = Call(host, in);
        BOOST_CHECK(r.status_code == EVMC_FAILURE);
    }

    // getByProTxHash(<random>) -> failure (not found).
    {
        std::vector<uint8_t> in = Sel(0x0402E163);
        in.resize(4 + 32, 0);
        in[4 + 3] = 0x99;
        const evmc::Result r = Call(host, in);
        BOOST_CHECK(r.status_code == EVMC_FAILURE);
    }

    // Unknown selector + truncated args -> failure.
    BOOST_CHECK(Call(host, Sel(0xfeedface)).status_code == EVMC_FAILURE);
    BOOST_CHECK(Call(host, Sel(0x47AEBAB3)).status_code == EVMC_FAILURE);  // isMasternode w/o arg
}

BOOST_AUTO_TEST_SUITE_END()
