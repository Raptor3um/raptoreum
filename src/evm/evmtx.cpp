// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/evmtx.h>

#include <assets/assets.h>
#include <assets/assetstype.h>
#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/validation.h>
#include <evm/apply.h>   // kWeisPerSatoshi
#include <evo/specialtx.h>
#include <tinyformat.h>
#include <update/update.h>

#include <map>
#include <set>
#include <string>

namespace evm {

// ----------------------------------------------------------------------------
// ToString() implementations (Phase 1: structure summary only).
// ----------------------------------------------------------------------------

std::string CEvmDeployTx::ToString() const
{
    return strprintf(
        "CEvmDeployTx(nVersion=%u, code.size=%u, gasLimit=%u, "
        "maxFeePerGas=%u, maxPriorityFeePerGas=%u, sender=%s, nonce=%u)",
        nVersion, code.size(), gasLimit, maxFeePerGas, maxPriorityFeePerGas,
        senderHash.ToString().substr(0, 8) + "..", nonce);
}

std::string CEvmCallTx::ToString() const
{
    return strprintf(
        "CEvmCallTx(nVersion=%u, to=%s, value=%u, data.size=%u, "
        "gasLimit=%u, maxFeePerGas=%u, maxPriorityFeePerGas=%u, "
        "sender=%s, nonce=%u)",
        nVersion, toAddress.ToString().substr(0, 8) + "..", value, data.size(),
        gasLimit, maxFeePerGas, maxPriorityFeePerGas,
        senderHash.ToString().substr(0, 8) + "..", nonce);
}

std::string CEvmSpendTx::ToString() const
{
    return strprintf(
        "CEvmSpendTx(nVersion=%u, from=%s, amount=%u, gasLimit=%u, "
        "maxFeePerGas=%u, maxPriorityFeePerGas=%u, nonce=%u)",
        nVersion, fromAddress.ToString().substr(0, 8) + "..", amount,
        gasLimit, maxFeePerGas, maxPriorityFeePerGas, nonce);
}

std::string CEvmFundTx::ToString() const
{
    return strprintf(
        "CEvmFundTx(nVersion=%u, to=%s, amount=%u)",
        nVersion, toAddress.ToString().substr(0, 8) + "..", amount);
}

std::string CWrapAssetTx::ToString() const
{
    return strprintf(
        "CWrapAssetTx(nVersion=%u, assetId=%s, evmRecipient=%s, amount=%u)",
        nVersion, assetId.substr(0, 12), evmRecipient.ToString().substr(0, 8) + "..",
        amount);
}

std::string CUnwrapAssetTx::ToString() const
{
    return strprintf(
        "CUnwrapAssetTx(nVersion=%u, assetId=%s, evmSender=%s, amount=%u)",
        nVersion, assetId.substr(0, 12), evmSender.ToString().substr(0, 8) + "..",
        amount);
}

// ----------------------------------------------------------------------------
// Validation entry points (Phase 1: structure-only + hard reject).
// ----------------------------------------------------------------------------
//
// In Phase 1 we register the tx-type dispatcher cases so that the network
// recognizes EVM transactions structurally. But until Phase 2 wires up
// execution and the UPDATE_EVM activation params are added to chainparams,
// these checks REJECT all EVM transactions.
//
// Reject codes used:
//   "evm-not-activated"        — Phase 2 has not landed yet
//   "bad-evm-tx-version"       — payload nVersion not supported
//   "bad-evm-tx-gaslimit"      — gasLimit is zero
//   "bad-evm-tx-fee"           — maxPriorityFeePerGas > maxFeePerGas (EIP-1559)
//   "bad-evm-deploy-payload"   — payload deserialization failure
//   "bad-evm-deploy-code"      — empty bytecode
//   "bad-evm-deploy-codesize"  — bytecode exceeds MAX_EVM_CONTRACT_CODE_SIZE
//   "bad-evm-call-payload"     — payload deserialization failure
//   "bad-evm-call-datasize"    — calldata exceeds MAX_EVM_CALLDATA_SIZE
//   "bad-evm-spend-payload"    — payload deserialization failure
//   "bad-evm-spend-amount"     — zero amount
//   "bad-evm-spend-script"     — empty outputScript

namespace {

/** Returns true once UPDATE_EVM is registered AND active for the given chain tip.
 *
 *  Phase 1.4: wired to the real UpdateManager. The EUpdate::EVM entry is
 *  declared in src/update/update.h but is NOT yet registered in
 *  src/chainparams.cpp for any network. As a consequence, IsActive() returns
 *  false on every chain — the safe default. EVM transactions are rejected
 *  with "evm-not-activated".
 *
 *  Activation per network is committed in a later phase (Phase 6+):
 *  chainparams.cpp will register Update(EUpdate::EVM, ..., heightActivated=X)
 *  with the chosen block height. Hard-fork-style activation per D2 — fixed
 *  height, not BIP9 version-bit voting. */
bool IsEvmActive(const CBlockIndex* pindexPrev)
{
    return Updates().IsEvmActive(pindexPrev);
}

// Sum per-assetId base-unit amounts across a tx's asset INPUTS (read from
// the spent coins in `view`) and asset OUTPUTS (tx.vout). Standalone mirror
// of the accounting in tx_verify.cpp::checkOutput, so wrap/unwrap can
// re-impose a constrained conservation while being exempt from the global
// checkAssetsOutputs rule.
void GatherAssetDeltas(const CTransaction& tx, const CCoinsViewCache& view,
                       std::map<std::string, CAmount>& vinSum,
                       std::map<std::string, CAmount>& voutSum)
{
    for (const auto& in : tx.vin) {
        const Coin& coin = view.AccessCoin(in.prevout);
        if (coin.IsSpent()) continue;
        if (!coin.out.scriptPubKey.IsAssetScript()) continue;
        CAssetTransfer t;
        if (!GetTransferAsset(coin.out.scriptPubKey, t)) continue;
        vinSum[t.assetId] += t.nAmount;
    }
    for (const auto& out : tx.vout) {
        if (!out.scriptPubKey.IsAssetScript()) continue;
        CAssetTransfer t;
        if (!GetTransferAsset(out.scriptPubKey, t)) continue;
        voutSum[t.assetId] += t.nAmount;
    }
}

// Re-impose a CONSTRAINED asset conservation: exactly `assetId` may have a
// net (vout - vin) delta of `expectedDelta`, and EVERY other asset touched
// by the tx must be balanced (vin == vout). For wrap, expectedDelta is
// negative (units burned from the UTXO side); for unwrap, positive (units
// minted back). This is what keeps the checkAssetsOutputs exemption from
// being abused to mint/burn arbitrary assets.
bool CheckConstrainedAssetDelta(const CTransaction& tx, const CCoinsViewCache& view,
                                CValidationState& state, const std::string& assetId,
                                CAmount expectedDelta, const char* rejectReason)
{
    std::map<std::string, CAmount> vinSum, voutSum;
    GatherAssetDeltas(tx, view, vinSum, voutSum);

    std::set<std::string> ids;
    for (const auto& kv : vinSum) ids.insert(kv.first);
    for (const auto& kv : voutSum) ids.insert(kv.first);

    for (const auto& id : ids) {
        const CAmount vin = vinSum.count(id) ? vinSum.at(id) : 0;
        const CAmount vout = voutSum.count(id) ? voutSum.at(id) : 0;
        const CAmount delta = vout - vin;
        const CAmount want = (id == assetId) ? expectedDelta : 0;
        if (delta != want) {
            return state.DoS(100, false, REJECT_INVALID, rejectReason);
        }
    }
    // If the target asset never appeared in the tx at all, its delta is 0,
    // which only satisfies a zero expectation.
    if (!ids.count(assetId) && expectedDelta != 0) {
        return state.DoS(100, false, REJECT_INVALID, rejectReason);
    }
    return true;
}

/** Common Phase-1 structural checks shared by all three EVM tx types. */
bool CheckEvmCommon(const CBlockIndex* pindexPrev,
                    CValidationState& state,
                    uint16_t nVersion,
                    uint64_t gasLimit,
                    uint64_t maxFeePerGas,
                    uint64_t maxPriorityFeePerGas)
{
    if (!IsEvmActive(pindexPrev)) {
        return state.DoS(10, false, REJECT_INVALID, "evm-not-activated");
    }
    if (nVersion != EVM_TX_PAYLOAD_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-tx-version");
    }
    if (gasLimit == 0) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-tx-gaslimit");
    }
    if (maxPriorityFeePerGas > maxFeePerGas) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-tx-fee");
    }
    return true;
}

} // anonymous namespace

bool CheckEvmDeployTx(const CTransaction& tx,
                     const CBlockIndex* pindexPrev,
                     CValidationState& state)
{
    CEvmDeployTx payload;
    if (!GetTxPayload(tx, payload)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-deploy-payload");
    }
    if (!CheckEvmCommon(pindexPrev, state,
                        payload.nVersion, payload.gasLimit,
                        payload.maxFeePerGas, payload.maxPriorityFeePerGas)) {
        return false;
    }
    if (payload.code.empty()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-deploy-code");
    }
    if (payload.code.size() > MAX_EVM_CONTRACT_CODE_SIZE) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-deploy-codesize");
    }
    return true;
}

bool CheckEvmCallTx(const CTransaction& tx,
                   const CBlockIndex* pindexPrev,
                   CValidationState& state)
{
    CEvmCallTx payload;
    if (!GetTxPayload(tx, payload)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-call-payload");
    }
    if (!CheckEvmCommon(pindexPrev, state,
                        payload.nVersion, payload.gasLimit,
                        payload.maxFeePerGas, payload.maxPriorityFeePerGas)) {
        return false;
    }
    if (payload.data.size() > MAX_EVM_CALLDATA_SIZE) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-call-datasize");
    }
    return true;
}

bool CheckEvmSpendTx(const CTransaction& tx,
                    const CBlockIndex* pindexPrev,
                    CValidationState& state)
{
    CEvmSpendTx payload;
    if (!GetTxPayload(tx, payload)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-spend-payload");
    }
    if (!CheckEvmCommon(pindexPrev, state,
                        payload.nVersion, payload.gasLimit,
                        payload.maxFeePerGas, payload.maxPriorityFeePerGas)) {
        return false;
    }
    if (payload.amount == 0) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-spend-amount");
    }
    if (payload.outputScript.empty()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-spend-script");
    }
    return true;
}

bool CheckEvmFundTx(const CTransaction& tx,
                    const CBlockIndex* pindexPrev,
                    CValidationState& state)
{
    CEvmFundTx payload;
    if (!GetTxPayload(tx, payload)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-fund-payload");
    }
    // FUND carries no gas/fee fields, so it does not use CheckEvmCommon;
    // it gates on EVM activation + version + amount validity directly.
    if (!IsEvmActive(pindexPrev)) {
        return state.DoS(10, false, REJECT_INVALID, "evm-not-activated");
    }
    if (payload.nVersion != EVM_TX_PAYLOAD_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-tx-version");
    }
    if (payload.amount == 0) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-fund-amount");
    }
    // Must round-trip losslessly against the satoshi amount removed from
    // the UTXO side (consensus accounting is in satoshis).
    if (payload.amount % kWeisPerSatoshi != 0) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-fund-precision");
    }
    return true;
}

bool CheckWrapAssetTx(const CTransaction& tx,
                      const CBlockIndex* pindexPrev,
                      CValidationState& state,
                      const CCoinsViewCache& view,
                      CAssetsCache* assetsCache)
{
    CWrapAssetTx payload;
    if (!GetTxPayload(tx, payload)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-wrap-payload");
    }
    if (!IsEvmActive(pindexPrev)) {
        return state.DoS(10, false, REJECT_INVALID, "evm-not-activated");
    }
    if (payload.nVersion != EVM_TX_PAYLOAD_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-tx-version");
    }
    if (payload.amount == 0 || !MoneyRange(static_cast<CAmount>(payload.amount))) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-wrap-amount");
    }
    CAssetMetaData meta;
    if (payload.assetId.empty() || assetsCache == nullptr ||
        !assetsCache->GetAssetMetaData(payload.assetId, meta)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-wrap-asset");
    }
    // The wrapped asset must be burned from the UTXO side by EXACTLY amount
    // (inputs exceed outputs by amount); no other asset may be perturbed.
    if (!CheckConstrainedAssetDelta(tx, view, state, payload.assetId,
                                    -static_cast<CAmount>(payload.amount),
                                    "bad-evm-wrap-delta")) {
        return false;
    }
    return true;
}

bool CheckUnwrapAssetTx(const CTransaction& tx,
                        const CBlockIndex* pindexPrev,
                        CValidationState& state,
                        const CCoinsViewCache& view,
                        CAssetsCache* assetsCache)
{
    CUnwrapAssetTx payload;
    if (!GetTxPayload(tx, payload)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-unwrap-payload");
    }
    if (!IsEvmActive(pindexPrev)) {
        return state.DoS(10, false, REJECT_INVALID, "evm-not-activated");
    }
    if (payload.nVersion != EVM_TX_PAYLOAD_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-tx-version");
    }
    if (payload.amount == 0 || !MoneyRange(static_cast<CAmount>(payload.amount))) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-unwrap-amount");
    }
    CAssetMetaData meta;
    if (payload.assetId.empty() || assetsCache == nullptr ||
        !assetsCache->GetAssetMetaData(payload.assetId, meta)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-evm-unwrap-asset");
    }
    // The unwrapped asset must be minted onto the UTXO side by EXACTLY
    // amount (outputs exceed inputs by amount); no other asset perturbed.
    // The EVM-side debit that backs this mint (proving the sender holds the
    // wrapped units) is enforced in ApplyUnwrapAssetTx at ConnectBlock: a
    // shortfall rejects the whole block, so the mint can never stand alone.
    if (!CheckConstrainedAssetDelta(tx, view, state, payload.assetId,
                                    static_cast<CAmount>(payload.amount),
                                    "bad-evm-unwrap-delta")) {
        return false;
    }
    return true;
}

} // namespace evm
