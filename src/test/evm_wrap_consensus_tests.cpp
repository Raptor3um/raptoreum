// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <assets/assets.h>
#include <assets/assetstype.h>
#include <coins.h>
#include <consensus/validation.h>
#include <evm/evmtx.h>
#include <evo/providertx.h>
#include <evo/specialtx.h>
#include <key_io.h>
#include <primitives/transaction.h>
#include <script/standard.h>
#include <uint256.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

/**
 * D4 mirror — consensus structural validation of wrap/unwrap
 * (CheckWrapAssetTx / CheckUnwrapAssetTx). The live regtest e2e proves
 * the happy path on-chain; this locks the SECURITY-CRITICAL guard in CI:
 * the CONSTRAINED asset-conservation that re-imposes a bound after the txs
 * are exempted from the global checkAssetsOutputs rule.
 *
 * The exemption (shared with MINT) is what lets wrap burn N from the UTXO
 * side and unwrap mint N back. The constraint is what stops that exemption
 * from being abused: for the declared assetId the (vout - vin) delta must
 * be exactly -amount (wrap) / +amount (unwrap), and EVERY other asset must
 * be balanced. A regression here would let a wrap/unwrap tx silently
 * create or destroy arbitrary asset units — a supply-integrity break.
 *
 * Runs under TestChain100Setup (regtest EVM active at height 0) so the
 * activation gate passes and we reach the structural checks.
 */

namespace {

// 25-byte P2PKH base so the appended OP_ASSET_ID lands at the index
// IsAssetScript expects; BuildAssetTransaction appends the asset payload.
CScript AssetScript(const std::string& assetId, CAmount nAmount, uint8_t addrFill)
{
    CScript s = GetScriptForDestination(CKeyID(uint160(std::vector<unsigned char>(20, addrFill))));
    CAssetTransfer(assetId, nAmount).BuildAssetTransaction(s);
    return s;
}

uint256 ToWord160(uint8_t fill)
{
    uint256 w;
    w.SetNull();
    for (int i = 12; i < 32; ++i) *(w.begin() + i) = fill;
    return w;
}

void SeedAsset(const std::string& assetId, const std::string& name)
{
    CNewAssetTx a;
    a.name = name;
    a.isRoot = true;
    a.updatable = false;
    a.isUnique = false;
    a.maxMintCount = 0;
    a.decimalPoint = 8;
    a.referenceHash = "";
    a.fee = 0;
    a.type = 0;
    a.issueFrequency = 0;
    a.amount = 1000 * COIN;
    BOOST_REQUIRE(passetsCache->InsertAsset(a, assetId, /*nHeight=*/1));
}

// Add an asset UTXO to `view` and return its outpoint (for use as a tx input).
COutPoint AddAssetUtxo(CCoinsViewCache& view, const std::string& assetId,
                       CAmount nAmount, uint8_t addrFill, const uint256& txid, uint32_t n)
{
    CTxOut out(0, AssetScript(assetId, nAmount, addrFill));
    view.AddCoin(COutPoint(txid, n), Coin(std::move(out), 100, false, 0, {}), false);
    return COutPoint(txid, n);
}

template <typename Payload>
CTransaction MakeTx(uint16_t nType, const Payload& payload,
                    const std::vector<COutPoint>& vin,
                    const std::vector<CTxOut>& vout)
{
    CMutableTransaction mtx;
    mtx.nVersion = 3;
    mtx.nType = nType;
    for (const auto& o : vin) mtx.vin.emplace_back(o);
    for (const auto& o : vout) mtx.vout.push_back(o);
    SetTxPayload(mtx, payload);
    return CTransaction(mtx);
}

evm::CWrapAssetTx WrapPayload(const std::string& id, uint8_t evmFill, uint64_t amount)
{
    evm::CWrapAssetTx p;
    p.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    p.assetId = id;
    p.evmRecipient = ToWord160(evmFill);
    p.amount = amount;
    return p;
}

evm::CUnwrapAssetTx UnwrapPayload(const std::string& id, uint8_t evmFill, uint64_t amount)
{
    evm::CUnwrapAssetTx p;
    p.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    p.assetId = id;
    p.evmSender = ToWord160(evmFill);
    p.amount = amount;
    return p;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(evm_wrap_consensus_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(wrap_delta_binding)
{
    const std::string A = "WRAPCONSENSUSIDA";
    SeedAsset(A, "ALPHA");
    const CBlockIndex* tip = ::ChainActive().Tip();

    CCoinsView base;
    CCoinsViewCache view(&base);
    const uint256 t1 = uint256S("01");
    // 100 ALPHA input; 60 ALPHA change output => burn 40.
    const COutPoint in = AddAssetUtxo(view, A, 100 * COIN, 0xA1, t1, 0);
    const CTxOut change(0, AssetScript(A, 60 * COIN, 0xA1));

    // Valid: declared amount == burned delta (40).
    {
        CValidationState st;
        BOOST_CHECK(evm::CheckWrapAssetTx(
            MakeTx(TRANSACTION_WRAP_ASSET, WrapPayload(A, 0xAA, 40 * COIN), {in}, {change}),
            tip, st, view, passetsCache.get()));
    }

    // Wrong declared amount (30 != actual 40) -> rejected.
    {
        CValidationState st;
        BOOST_CHECK(!evm::CheckWrapAssetTx(
            MakeTx(TRANSACTION_WRAP_ASSET, WrapPayload(A, 0xAA, 30 * COIN), {in}, {change}),
            tip, st, view, passetsCache.get()));
        BOOST_CHECK_EQUAL(st.GetRejectReason(), "bad-evm-wrap-delta");
    }

    // Zero amount -> rejected before the delta check.
    {
        CValidationState st;
        BOOST_CHECK(!evm::CheckWrapAssetTx(
            MakeTx(TRANSACTION_WRAP_ASSET, WrapPayload(A, 0xAA, 0), {in}, {change}),
            tip, st, view, passetsCache.get()));
        BOOST_CHECK_EQUAL(st.GetRejectReason(), "bad-evm-wrap-amount");
    }

    // Unknown asset id -> rejected.
    {
        CValidationState st;
        BOOST_CHECK(!evm::CheckWrapAssetTx(
            MakeTx(TRANSACTION_WRAP_ASSET, WrapPayload("NOSUCHASSETIDXX", 0xAA, 40 * COIN),
                   {in}, {change}),
            tip, st, view, passetsCache.get()));
        BOOST_CHECK_EQUAL(st.GetRejectReason(), "bad-evm-wrap-asset");
    }
}

BOOST_AUTO_TEST_CASE(wrap_rejects_other_asset_perturbation)
{
    // The exemption must NOT let a wrap silently destroy a DIFFERENT asset.
    const std::string A = "WRAPCONSENSUSIDA";
    const std::string B = "WRAPCONSENSUSIDB";
    SeedAsset(A, "ALPHA");
    SeedAsset(B, "BETA");
    const CBlockIndex* tip = ::ChainActive().Tip();

    CCoinsView base;
    CCoinsViewCache view(&base);
    const uint256 t1 = uint256S("02");
    // Inputs: 100 ALPHA + 50 BETA. Output: 60 ALPHA change only.
    // ALPHA delta = -40 (the declared wrap), but BETA delta = -50 (silently
    // burned with no output) -> must be rejected.
    const COutPoint inA = AddAssetUtxo(view, A, 100 * COIN, 0xA1, t1, 0);
    const COutPoint inB = AddAssetUtxo(view, B, 50 * COIN, 0xB1, t1, 1);
    const CTxOut changeA(0, AssetScript(A, 60 * COIN, 0xA1));

    CValidationState st;
    BOOST_CHECK(!evm::CheckWrapAssetTx(
        MakeTx(TRANSACTION_WRAP_ASSET, WrapPayload(A, 0xAA, 40 * COIN), {inA, inB}, {changeA}),
        tip, st, view, passetsCache.get()));
    BOOST_CHECK_EQUAL(st.GetRejectReason(), "bad-evm-wrap-delta");
}

BOOST_AUTO_TEST_CASE(unwrap_delta_binding)
{
    const std::string A = "WRAPCONSENSUSIDA";
    SeedAsset(A, "ALPHA");
    const CBlockIndex* tip = ::ChainActive().Tip();

    CCoinsView base;
    CCoinsViewCache view(&base);
    // Unwrap mints to the UTXO side: no asset inputs, a 40 ALPHA mint output.
    const CTxOut mint(0, AssetScript(A, 40 * COIN, 0xA1));

    // Valid: declared amount == minted delta (+40).
    {
        CValidationState st;
        BOOST_CHECK(evm::CheckUnwrapAssetTx(
            MakeTx(TRANSACTION_UNWRAP_ASSET, UnwrapPayload(A, 0xAA, 40 * COIN), {}, {mint}),
            tip, st, view, passetsCache.get()));
    }

    // Wrong declared amount (50 != minted 40) -> rejected.
    {
        CValidationState st;
        BOOST_CHECK(!evm::CheckUnwrapAssetTx(
            MakeTx(TRANSACTION_UNWRAP_ASSET, UnwrapPayload(A, 0xAA, 50 * COIN), {}, {mint}),
            tip, st, view, passetsCache.get()));
        BOOST_CHECK_EQUAL(st.GetRejectReason(), "bad-evm-unwrap-delta");
    }

    // Zero amount -> rejected.
    {
        CValidationState st;
        BOOST_CHECK(!evm::CheckUnwrapAssetTx(
            MakeTx(TRANSACTION_UNWRAP_ASSET, UnwrapPayload(A, 0xAA, 0), {}, {mint}),
            tip, st, view, passetsCache.get()));
        BOOST_CHECK_EQUAL(st.GetRejectReason(), "bad-evm-unwrap-amount");
    }
}

BOOST_AUTO_TEST_SUITE_END()
