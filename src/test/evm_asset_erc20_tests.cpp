// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <assets/assets.h>
#include <evm/host.h>
#include <evm/precompiles.h>
#include <evm/state_cache.h>
#include <evm/state_db.h>
#include <evo/providertx.h>
#include <hash.h>
#include <validation.h>

#include <evmc/evmc.hpp>

#include <boost/test/unit_test.hpp>

#include <cstring>
#include <string>
#include <vector>

/**
 * Phase 4.1 — Smart-Asset ERC-20 precompile read surface.
 *
 * The flagship RTM-EVM differentiator: every Smart Asset is callable
 * as a standard ERC-20 at a deterministic per-asset precompile
 * address (0xA55E70.. || hash160(assetId)[8:20]), so a Solidity
 * contract / MetaMask "Add Token" sees name / symbol / decimals /
 * totalSupply / balanceOf without any wrapping. This locks the MVP
 * read path through the real ExecutePrecompile dispatcher against a
 * seeded asset cache: address->assetId resolution, metadata reads,
 * ABI-encoded returns, and the failure paths (unknown selector,
 * unknown asset).
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

// Derive the asset's ERC-20 precompile address:
//   0xA55E70 00 00 00 00 00 (8-byte marker) || hash160(assetId)[8:20].
evmc::address AssetPrecompileAddress(const std::string& assetId)
{
    const uint160 h = Hash160(
        std::vector<unsigned char>(assetId.begin(), assetId.end()));
    evmc::address a{};
    const uint8_t marker[8] = {0xA5, 0x5E, 0x70, 0, 0, 0, 0, 0};
    std::memcpy(a.bytes, marker, 8);
    std::memcpy(a.bytes + 8, h.begin() + 8, 12);
    return a;
}

evmc::Result CallAsset(evm::CEvmHost& host, const evmc::address& addr,
                       const std::vector<uint8_t>& input, int64_t gas = 100000)
{
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

std::vector<uint8_t> Selector(uint32_t sel)
{
    return {static_cast<uint8_t>(sel >> 24), static_cast<uint8_t>(sel >> 16),
            static_cast<uint8_t>(sel >> 8),  static_cast<uint8_t>(sel)};
}

// Seed a root Smart Asset into the global asset cache.
void SeedAsset(const std::string& assetId, const std::string& name,
               uint8_t decimals)
{
    CNewAssetTx a;
    a.name = name;
    a.isRoot = true;
    a.updatable = false;
    a.isUnique = false;
    a.maxMintCount = 0;
    a.decimalPoint = decimals;
    a.referenceHash = "";
    a.fee = 0;
    a.type = 0;
    a.issueFrequency = 0;
    a.amount = 1000 * COIN;
    BOOST_REQUIRE(passetsCache->InsertAsset(a, assetId, /*nHeight=*/1));
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(evm_asset_erc20_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(asset_erc20_read_surface)
{
    const std::string assetId = "TESTASSETID0001";
    SeedAsset(assetId, "GOLD", /*decimals=*/8);

    evm::CEvmStateDB db(1 << 20, /*fMemory=*/true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, MinimalContext());

    const evmc::address addr = AssetPrecompileAddress(assetId);
    BOOST_REQUIRE(evm::IsPrecompileAddress(addr));

    // decimals() -> 8
    {
        const evmc::Result r =
            CallAsset(host, addr, Selector(0x313ce567));
        BOOST_REQUIRE(r.status_code == EVMC_SUCCESS);
        std::vector<uint8_t> out(r.output_data, r.output_data + r.output_size);
        uint8_t dec = 0;
        BOOST_REQUIRE(evm::AbiReadUint8(out, 0, dec));
        BOOST_CHECK_EQUAL(dec, 8);
    }

    // name() -> "GOLD"
    {
        const evmc::Result r =
            CallAsset(host, addr, Selector(0x06fdde03));
        BOOST_REQUIRE(r.status_code == EVMC_SUCCESS);
        std::vector<uint8_t> out(r.output_data, r.output_data + r.output_size);
        std::vector<uint8_t> nameBytes;
        BOOST_REQUIRE(evm::AbiReadDynamicBytes(out, 0, nameBytes));
        BOOST_CHECK_EQUAL(std::string(nameBytes.begin(), nameBytes.end()),
                          "GOLD");
    }

    // symbol() -> "GOLD" (Smart Assets surface name as symbol)
    {
        const evmc::Result r =
            CallAsset(host, addr, Selector(0x95d89b41));
        BOOST_REQUIRE(r.status_code == EVMC_SUCCESS);
        std::vector<uint8_t> out(r.output_data, r.output_data + r.output_size);
        std::vector<uint8_t> sym;
        BOOST_REQUIRE(evm::AbiReadDynamicBytes(out, 0, sym));
        BOOST_CHECK_EQUAL(std::string(sym.begin(), sym.end()), "GOLD");
    }

    // totalSupply() -> 0 (circulatingSupply starts at 0 pre-mint)
    {
        const evmc::Result r =
            CallAsset(host, addr, Selector(0x18160ddd));
        BOOST_REQUIRE(r.status_code == EVMC_SUCCESS);
        std::vector<uint8_t> out(r.output_data, r.output_data + r.output_size);
        uint64_t supply = 1;
        BOOST_REQUIRE(evm::AbiReadUint64(out, 0, supply));
        BOOST_CHECK_EQUAL(supply, 0U);
    }

    // balanceOf(addr) -> 0 (MVP, pre-mirror)
    {
        std::vector<uint8_t> input = Selector(0x70a08231);
        input.resize(4 + 32, 0);  // selector + zero-padded address arg
        const evmc::Result r = CallAsset(host, addr, input);
        BOOST_REQUIRE(r.status_code == EVMC_SUCCESS);
        std::vector<uint8_t> out(r.output_data, r.output_data + r.output_size);
        uint64_t bal = 1;
        BOOST_REQUIRE(evm::AbiReadUint64(out, 0, bal));
        BOOST_CHECK_EQUAL(bal, 0U);
    }
}

BOOST_AUTO_TEST_CASE(asset_erc20_failure_paths)
{
    const std::string assetId = "TESTASSETID0002";
    SeedAsset(assetId, "SILVER", 2);

    evm::CEvmStateDB db(1 << 20, /*fMemory=*/true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, MinimalContext());

    const evmc::address addr = AssetPrecompileAddress(assetId);

    // Unknown selector -> failure.
    {
        const evmc::Result r = CallAsset(host, addr, Selector(0xdeadbeef));
        BOOST_CHECK(r.status_code == EVMC_FAILURE);
    }

    // Asset-prefixed address with no matching asset -> failure.
    {
        evmc::address ghost{};
        const uint8_t marker[8] = {0xA5, 0x5E, 0x70, 0, 0, 0, 0, 0};
        std::memcpy(ghost.bytes, marker, 8);
        for (int i = 8; i < 20; ++i) ghost.bytes[i] = 0xEE;  // no such asset
        BOOST_REQUIRE(evm::IsPrecompileAddress(ghost));
        const evmc::Result r = CallAsset(host, ghost, Selector(0x313ce567));
        BOOST_CHECK(r.status_code == EVMC_FAILURE);
    }
}

BOOST_AUTO_TEST_SUITE_END()
