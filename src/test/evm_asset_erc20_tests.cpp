// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <assets/assets.h>
#include <evm/balance.h>
#include <evm/hashing.h>
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

// ---- D4 mirror (M1) ledger test helpers ------------------------------
// These mirror the EXACT storage-slot layout in precompile_asset_erc20.cpp
// so a test can seed/inspect the EVM-side ledger directly. If the layout
// there changes, these must change in lockstep (the convergence test in
// M3 is the systemic backstop, but matching here keeps M1 self-contained).

evmc::address MakeAddr(uint8_t fill)
{
    evmc::address a{};
    for (int i = 0; i < 20; ++i) a.bytes[i] = fill;
    return a;
}

std::vector<uint8_t> Pad32Addr(const evmc::address& a)
{
    std::vector<uint8_t> v(32, 0);
    std::memcpy(v.data() + 12, a.bytes, 20);
    return v;
}

std::vector<uint8_t> Pad32Uint(uint64_t n)
{
    std::vector<uint8_t> v(32, 0);
    for (int i = 0; i < 8; ++i) v[31 - i] = static_cast<uint8_t>((n >> (8 * i)) & 0xFF);
    return v;
}

evmc::bytes32 ToBytes32(const uint256& u)
{
    evmc::bytes32 b{};
    std::memcpy(b.bytes, u.begin(), 32);
    return b;
}

// mapping(address=>uint256) at slot 0: key = keccak(pad(addr) ++ pad(0)).
evmc::bytes32 BalanceSlot(const evmc::address& holder)
{
    std::vector<uint8_t> buf = Pad32Addr(holder);
    const std::vector<uint8_t> s = Pad32Uint(0);
    buf.insert(buf.end(), s.begin(), s.end());
    return ToBytes32(evm::Keccak256(buf));
}

// mapping(address=>mapping(address=>uint256)) at slot 1:
//   inner = keccak(pad(owner) ++ pad(1)); key = keccak(pad(spender) ++ inner).
evmc::bytes32 AllowanceSlot(const evmc::address& owner, const evmc::address& spender)
{
    std::vector<uint8_t> ib = Pad32Addr(owner);
    const std::vector<uint8_t> s = Pad32Uint(1);
    ib.insert(ib.end(), s.begin(), s.end());
    const uint256 inner = evm::Keccak256(ib);
    std::vector<uint8_t> buf = Pad32Addr(spender);
    buf.insert(buf.end(), inner.begin(), inner.end());
    return ToBytes32(evm::Keccak256(buf));
}

// wrappedSupply scalar at slot 2.
evmc::bytes32 WrappedSupplySlot()
{
    return ToBytes32(uint256(Pad32Uint(2)));
}

void SeedStorageU64(evm::CEvmHost& host, const evmc::address& c,
                    const evmc::bytes32& slot, uint64_t amount)
{
    host.set_storage(c, slot, ToBytes32(evm::Uint256FromUint64(amount)));
}

uint64_t ReadStorageU64(evm::CEvmHost& host, const evmc::address& c,
                        const evmc::bytes32& slot)
{
    const evmc::bytes32 w = host.get_storage(c, slot);
    uint256 u;
    std::memcpy(u.begin(), w.bytes, 32);
    return evm::Uint256ToLowUint64(u);
}

// Call the precompile with an explicit msg.sender (and optional STATIC flag).
evmc::Result CallAssetFrom(evm::CEvmHost& host, const evmc::address& addr,
                           const std::vector<uint8_t>& input,
                           const evmc::address& sender,
                           uint32_t flags = 0, int64_t gas = 100000)
{
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.flags = flags;
    msg.gas = gas;
    msg.recipient = addr;
    msg.code_address = addr;
    msg.sender = sender;
    msg.value = evmc::uint256be{};
    msg.input_data = input.empty() ? nullptr : input.data();
    msg.input_size = input.size();
    evmc::Result out{};
    BOOST_REQUIRE(evm::ExecutePrecompile(host, msg, out));
    return out;
}

// ABI input builders for the ERC-20 write surface.
std::vector<uint8_t> AbiTransfer(const evmc::address& to, uint64_t amount)
{
    std::vector<uint8_t> in = {0xa9, 0x05, 0x9c, 0xbb};
    const std::vector<uint8_t> a = Pad32Addr(to), v = Pad32Uint(amount);
    in.insert(in.end(), a.begin(), a.end());
    in.insert(in.end(), v.begin(), v.end());
    return in;
}
std::vector<uint8_t> AbiApprove(const evmc::address& spender, uint64_t amount)
{
    std::vector<uint8_t> in = {0x09, 0x5e, 0xa7, 0xb3};
    const std::vector<uint8_t> a = Pad32Addr(spender), v = Pad32Uint(amount);
    in.insert(in.end(), a.begin(), a.end());
    in.insert(in.end(), v.begin(), v.end());
    return in;
}
std::vector<uint8_t> AbiTransferFrom(const evmc::address& from,
                                     const evmc::address& to, uint64_t amount)
{
    std::vector<uint8_t> in = {0x23, 0xb8, 0x72, 0xdd};
    const std::vector<uint8_t> f = Pad32Addr(from), t = Pad32Addr(to),
                               v = Pad32Uint(amount);
    in.insert(in.end(), f.begin(), f.end());
    in.insert(in.end(), t.begin(), t.end());
    in.insert(in.end(), v.begin(), v.end());
    return in;
}
std::vector<uint8_t> AbiBalanceOf(const evmc::address& who)
{
    std::vector<uint8_t> in = {0x70, 0xa0, 0x82, 0x31};
    const std::vector<uint8_t> a = Pad32Addr(who);
    in.insert(in.end(), a.begin(), a.end());
    return in;
}
std::vector<uint8_t> AbiAllowance(const evmc::address& owner,
                                  const evmc::address& spender)
{
    std::vector<uint8_t> in = {0xdd, 0x62, 0xed, 0x3e};
    const std::vector<uint8_t> o = Pad32Addr(owner), s = Pad32Addr(spender);
    in.insert(in.end(), o.begin(), o.end());
    in.insert(in.end(), s.begin(), s.end());
    return in;
}

uint64_t DecodeU64(const evmc::Result& r)
{
    std::vector<uint8_t> out(r.output_data, r.output_data + r.output_size);
    uint64_t v = 0;
    BOOST_REQUIRE(evm::AbiReadUint64(out, 0, v));
    return v;
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

// D4 mirror (M1): the EVM-side ERC-20 ledger. Seeds a balance directly
// into the precompile's own storage trie (the same trie wrap/unwrap will
// credit in M2) and exercises the full write surface through the real
// ExecutePrecompile dispatch: transfer, approve, allowance, transferFrom,
// plus the STATIC-call write guard and the insufficient-balance/allowance
// failure paths. balanceOf/totalSupply now read this ledger.
BOOST_AUTO_TEST_CASE(asset_erc20_ledger_writes)
{
    const std::string assetId = "TESTASSETID0003";
    SeedAsset(assetId, "BRONZE", 4);

    evm::CEvmStateDB db(1 << 20, /*fMemory=*/true);
    evm::CEvmStateCache cache(db);
    evm::CEvmHost host(cache, MinimalContext());

    const evmc::address addr = AssetPrecompileAddress(assetId);
    const evmc::address alice   = MakeAddr(0xA1);
    const evmc::address bob      = MakeAddr(0xB2);
    const evmc::address carol    = MakeAddr(0xC3);

    // Seed Alice with 1000 wrapped units (as wrap/unwrap would in M2).
    SeedStorageU64(host, addr, BalanceSlot(alice), 1000);
    SeedStorageU64(host, addr, WrappedSupplySlot(), 1000);

    // balanceOf(alice) == 1000, balanceOf(bob) == 0.
    BOOST_CHECK_EQUAL(DecodeU64(CallAssetFrom(host, addr, AbiBalanceOf(alice), alice)), 1000U);
    BOOST_CHECK_EQUAL(DecodeU64(CallAssetFrom(host, addr, AbiBalanceOf(bob), alice)), 0U);

    // transfer(alice -> bob, 400): balances move, returns true.
    {
        const evmc::Result r = CallAssetFrom(host, addr, AbiTransfer(bob, 400), alice);
        BOOST_REQUIRE(r.status_code == EVMC_SUCCESS);
        BOOST_CHECK_EQUAL(DecodeU64(r), 1U);  // ERC-20 returns bool true
    }
    BOOST_CHECK_EQUAL(ReadStorageU64(host, addr, BalanceSlot(alice)), 600U);
    BOOST_CHECK_EQUAL(ReadStorageU64(host, addr, BalanceSlot(bob)), 400U);

    // totalSupply unchanged by a transfer (conserved).
    {
        const evmc::Result r = CallAssetFrom(host, addr, Selector(0x18160ddd), alice);
        BOOST_REQUIRE(r.status_code == EVMC_SUCCESS);
        BOOST_CHECK_EQUAL(DecodeU64(r), 1000U);
    }

    // transfer with insufficient balance fails, leaves state untouched.
    {
        const evmc::Result r = CallAssetFrom(host, addr, AbiTransfer(carol, 100000), bob);
        BOOST_CHECK(r.status_code == EVMC_FAILURE);
    }
    BOOST_CHECK_EQUAL(ReadStorageU64(host, addr, BalanceSlot(bob)), 400U);

    // STATIC-call write is rejected (no mutation under EVMC_STATIC).
    {
        const evmc::Result r =
            CallAssetFrom(host, addr, AbiTransfer(bob, 1), alice, EVMC_STATIC);
        BOOST_CHECK(r.status_code == EVMC_FAILURE);
    }
    BOOST_CHECK_EQUAL(ReadStorageU64(host, addr, BalanceSlot(alice)), 600U);

    // approve(alice -> carol, 250): allowance set, returns true.
    {
        const evmc::Result r = CallAssetFrom(host, addr, AbiApprove(carol, 250), alice);
        BOOST_REQUIRE(r.status_code == EVMC_SUCCESS);
        BOOST_CHECK_EQUAL(DecodeU64(r), 1U);
    }
    BOOST_CHECK_EQUAL(
        DecodeU64(CallAssetFrom(host, addr, AbiAllowance(alice, carol), carol)), 250U);

    // transferFrom(alice -> bob, 150) by carol: moves funds, decrements allowance.
    {
        const evmc::Result r =
            CallAssetFrom(host, addr, AbiTransferFrom(alice, bob, 150), carol);
        BOOST_REQUIRE(r.status_code == EVMC_SUCCESS);
        BOOST_CHECK_EQUAL(DecodeU64(r), 1U);
    }
    BOOST_CHECK_EQUAL(ReadStorageU64(host, addr, BalanceSlot(alice)), 450U);
    BOOST_CHECK_EQUAL(ReadStorageU64(host, addr, BalanceSlot(bob)), 550U);
    BOOST_CHECK_EQUAL(
        DecodeU64(CallAssetFrom(host, addr, AbiAllowance(alice, carol), carol)), 100U);

    // transferFrom beyond remaining allowance (100) fails.
    {
        const evmc::Result r =
            CallAssetFrom(host, addr, AbiTransferFrom(alice, bob, 101), carol);
        BOOST_CHECK(r.status_code == EVMC_FAILURE);
    }
    // Allowance and balances unchanged by the failed transferFrom.
    BOOST_CHECK_EQUAL(
        DecodeU64(CallAssetFrom(host, addr, AbiAllowance(alice, carol), carol)), 100U);
    BOOST_CHECK_EQUAL(ReadStorageU64(host, addr, BalanceSlot(alice)), 450U);

    // approve is gated by STATIC too.
    {
        const evmc::Result r =
            CallAssetFrom(host, addr, AbiApprove(carol, 1), alice, EVMC_STATIC);
        BOOST_CHECK(r.status_code == EVMC_FAILURE);
    }
}

BOOST_AUTO_TEST_SUITE_END()
