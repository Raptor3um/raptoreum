// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <assets/assets.h>
#include <evm/apply.h>
#include <evm/asset_ledger.h>
#include <evm/evmtx.h>
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
 * D4 Smart-Asset mirror (M2) — wrap/unwrap apply path + the bidirectional
 * mirror invariant.
 *
 * These lock the EVM-SIDE half of the bridge at the apply layer (the UTXO
 * half — burn on wrap, mint on unwrap — is consensus-validated by
 * CheckWrap/UnwrapAssetTx and exercised in the block-level tests):
 *
 *   - ApplyWrapAssetTx credits the asset's EVM ERC-20 ledger and bumps
 *     wrappedSupply, and the credit is THE EXACT slot the precompile's
 *     balanceOf()/totalSupply() read (mirror convergence at the unit level).
 *   - ApplyUnwrapAssetTx debits the ledger, refuses an over-debit without
 *     mutating state, and round-trips wrap→unwrap to a conserved balance.
 *
 * The conservation law mirrored here is
 *     sum(UTXO asset balances) + EVM wrappedSupply == circulatingSupply,
 * so a wrap that moves N units across the bridge must leave wrappedSupply
 * exactly N higher and balanceOf exactly N higher.
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

evmc::address MakeAddr(uint8_t fill)
{
    evmc::address a{};
    for (int i = 0; i < 20; ++i) a.bytes[i] = fill;
    return a;
}

uint160 ToU160(const evmc::address& a)
{
    uint160 o;
    std::memcpy(o.begin(), a.bytes, 20);
    return o;
}

uint256 ToWord160(const evmc::address& a)
{
    uint256 w;
    w.SetNull();
    std::memcpy(w.begin() + 12, a.bytes, 20);
    return w;
}

void SeedAsset(const std::string& assetId, const std::string& name, uint8_t decimals)
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

evm::CWrapAssetTx WrapPayload(const std::string& assetId,
                              const evmc::address& recipient, uint64_t amount)
{
    evm::CWrapAssetTx p;
    p.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    p.assetId = assetId;
    p.evmRecipient = ToWord160(recipient);
    p.amount = amount;
    return p;
}

evm::CUnwrapAssetTx UnwrapPayload(const std::string& assetId,
                                  const evmc::address& sender, uint64_t amount)
{
    evm::CUnwrapAssetTx p;
    p.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    p.assetId = assetId;
    p.evmSender = ToWord160(sender);
    p.amount = amount;
    return p;
}

// Drive the asset precompile's balanceOf(holder) over the SAME state cache,
// proving the wrap credit lands exactly where balanceOf reads.
uint64_t PrecompileBalanceOf(evm::CEvmHost& host, const std::string& assetId,
                             const evmc::address& holder)
{
    const evmc::address addr = evm::AssetErc20Address(assetId);
    std::vector<uint8_t> input = {0x70, 0xa0, 0x82, 0x31};  // balanceOf(address)
    std::vector<uint8_t> arg(32, 0);
    std::memcpy(arg.data() + 12, holder.bytes, 20);
    input.insert(input.end(), arg.begin(), arg.end());

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = 100000;
    msg.recipient = addr;
    msg.code_address = addr;
    msg.input_data = input.data();
    msg.input_size = input.size();
    evmc::Result out{};
    BOOST_REQUIRE(evm::ExecutePrecompile(host, msg, out));
    BOOST_REQUIRE(out.status_code == EVMC_SUCCESS);
    std::vector<uint8_t> o(out.output_data, out.output_data + out.output_size);
    uint64_t bal = 0;
    BOOST_REQUIRE(evm::AbiReadUint64(o, 0, bal));
    return bal;
}

uint64_t PrecompileTotalSupply(evm::CEvmHost& host, const std::string& assetId)
{
    const evmc::address addr = evm::AssetErc20Address(assetId);
    std::vector<uint8_t> input = {0x18, 0x16, 0x0d, 0xdd};  // totalSupply()
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = 100000;
    msg.recipient = addr;
    msg.code_address = addr;
    msg.input_data = input.data();
    msg.input_size = input.size();
    evmc::Result out{};
    BOOST_REQUIRE(evm::ExecutePrecompile(host, msg, out));
    BOOST_REQUIRE(out.status_code == EVMC_SUCCESS);
    std::vector<uint8_t> o(out.output_data, out.output_data + out.output_size);
    uint64_t s = 0;
    BOOST_REQUIRE(evm::AbiReadUint64(o, 0, s));
    return s;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(evm_asset_wrap_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(wrap_credits_ledger_and_mirrors_precompile)
{
    const std::string assetId = "WRAPASSETID00001";
    SeedAsset(assetId, "MIRROR", 8);

    evm::CEvmStateDB db(1 << 20, /*fMemory=*/true);
    evm::CEvmStateCache cache(db);
    const evm::ExecutionContext ctx = MinimalContext();

    const evmc::address alice = MakeAddr(0xA1);

    // Wrap 700 units to Alice.
    const evm::ApplyResult r =
        evm::ApplyWrapAssetTx(WrapPayload(assetId, alice, 700), cache, ctx);
    BOOST_REQUIRE(r.statusCode == EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(r.gasUsed, 0);

    // Ledger reads via the shared module.
    BOOST_CHECK_EQUAL(evm::AssetLedgerBalanceOf(cache, assetId, ToU160(alice)), 700U);
    BOOST_CHECK_EQUAL(evm::AssetLedgerWrappedSupply(cache, assetId), 700U);

    // The MIRROR: the precompile's balanceOf/totalSupply over the same cache
    // read exactly what wrap credited.
    evm::CEvmHost host(cache, ctx);
    BOOST_CHECK_EQUAL(PrecompileBalanceOf(host, assetId, alice), 700U);
    BOOST_CHECK_EQUAL(PrecompileTotalSupply(host, assetId), 700U);
}

BOOST_AUTO_TEST_CASE(wrap_accumulates_across_holders)
{
    const std::string assetId = "WRAPASSETID00002";
    SeedAsset(assetId, "GOLD", 8);

    evm::CEvmStateDB db(1 << 20, /*fMemory=*/true);
    evm::CEvmStateCache cache(db);
    const evm::ExecutionContext ctx = MinimalContext();

    const evmc::address alice = MakeAddr(0xA1);
    const evmc::address bob = MakeAddr(0xB2);

    BOOST_REQUIRE(evm::ApplyWrapAssetTx(WrapPayload(assetId, alice, 300), cache, ctx)
                      .statusCode == EVMC_SUCCESS);
    BOOST_REQUIRE(evm::ApplyWrapAssetTx(WrapPayload(assetId, bob, 200), cache, ctx)
                      .statusCode == EVMC_SUCCESS);
    BOOST_REQUIRE(evm::ApplyWrapAssetTx(WrapPayload(assetId, alice, 150), cache, ctx)
                      .statusCode == EVMC_SUCCESS);

    BOOST_CHECK_EQUAL(evm::AssetLedgerBalanceOf(cache, assetId, ToU160(alice)), 450U);
    BOOST_CHECK_EQUAL(evm::AssetLedgerBalanceOf(cache, assetId, ToU160(bob)), 200U);
    // sum(balanceOf) == wrappedSupply (ERC-20 invariant).
    BOOST_CHECK_EQUAL(evm::AssetLedgerWrappedSupply(cache, assetId), 650U);
}

BOOST_AUTO_TEST_CASE(unwrap_debits_and_refuses_overdraw)
{
    const std::string assetId = "WRAPASSETID00003";
    SeedAsset(assetId, "SILVER", 8);

    evm::CEvmStateDB db(1 << 20, /*fMemory=*/true);
    evm::CEvmStateCache cache(db);
    const evm::ExecutionContext ctx = MinimalContext();

    const evmc::address alice = MakeAddr(0xA1);

    // Wrap 1000, then unwrap 400 — balance and supply drop to 600.
    BOOST_REQUIRE(evm::ApplyWrapAssetTx(WrapPayload(assetId, alice, 1000), cache, ctx)
                      .statusCode == EVMC_SUCCESS);
    BOOST_REQUIRE(evm::ApplyUnwrapAssetTx(UnwrapPayload(assetId, alice, 400), cache, ctx)
                      .statusCode == EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(evm::AssetLedgerBalanceOf(cache, assetId, ToU160(alice)), 600U);
    BOOST_CHECK_EQUAL(evm::AssetLedgerWrappedSupply(cache, assetId), 600U);

    // Over-debit (601 > 600) fails WITHOUT mutating any slot.
    const evm::ApplyResult bad =
        evm::ApplyUnwrapAssetTx(UnwrapPayload(assetId, alice, 601), cache, ctx);
    BOOST_CHECK(bad.statusCode == EVMC_FAILURE);
    BOOST_CHECK_EQUAL(evm::AssetLedgerBalanceOf(cache, assetId, ToU160(alice)), 600U);
    BOOST_CHECK_EQUAL(evm::AssetLedgerWrappedSupply(cache, assetId), 600U);

    // Unwrapping a holder with zero balance also fails.
    const evmc::address mallory = MakeAddr(0xCC);
    const evm::ApplyResult none =
        evm::ApplyUnwrapAssetTx(UnwrapPayload(assetId, mallory, 1), cache, ctx);
    BOOST_CHECK(none.statusCode == EVMC_FAILURE);
}

BOOST_AUTO_TEST_CASE(wrap_unwrap_roundtrip_conserves)
{
    const std::string assetId = "WRAPASSETID00004";
    SeedAsset(assetId, "BRONZE", 8);

    evm::CEvmStateDB db(1 << 20, /*fMemory=*/true);
    evm::CEvmStateCache cache(db);
    const evm::ExecutionContext ctx = MinimalContext();

    const evmc::address alice = MakeAddr(0xA1);

    // A sequence of wraps and unwraps; the net of (wrapped - unwrapped) must
    // equal both balanceOf(alice) and wrappedSupply at every step.
    int64_t net = 0;
    const std::vector<int64_t> ops = {500, -200, 1000, -700, -100, 50};
    for (int64_t op : ops) {
        if (op > 0) {
            BOOST_REQUIRE(
                evm::ApplyWrapAssetTx(WrapPayload(assetId, alice, op), cache, ctx)
                    .statusCode == EVMC_SUCCESS);
        } else {
            BOOST_REQUIRE(
                evm::ApplyUnwrapAssetTx(UnwrapPayload(assetId, alice, -op), cache, ctx)
                    .statusCode == EVMC_SUCCESS);
        }
        net += op;
        BOOST_CHECK_EQUAL(evm::AssetLedgerBalanceOf(cache, assetId, ToU160(alice)),
                          static_cast<uint64_t>(net));
        BOOST_CHECK_EQUAL(evm::AssetLedgerWrappedSupply(cache, assetId),
                          static_cast<uint64_t>(net));
    }
    BOOST_CHECK_EQUAL(net, 550);  // 500-200+1000-700-100+50
}

BOOST_AUTO_TEST_SUITE_END()
