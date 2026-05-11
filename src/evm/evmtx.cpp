// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/evmtx.h>

#include <chain.h>
#include <consensus/validation.h>
#include <evo/specialtx.h>
#include <tinyformat.h>

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

/** Returns false until Phase 2 registers UPDATE_EVM in chainparams.cpp. */
bool IsEvmActive(const CBlockIndex* /*pindexPrev*/)
{
    // PHASE 1: hard-coded false.
    // PHASE 2: replace with Params().GetUpdateManager().IsActive(
    //   EUpdate::EVM, pindexPrev) once UPDATE_EVM is wired into
    //   src/update/update.h and src/chainparams.cpp.
    return false;
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

} // namespace evm
