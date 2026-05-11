// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <evm/evmtx.h>
#include <evo/specialtx.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <serialize.h>
#include <streams.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <vector>

/**
 * Phase 1 scaffolding tests for the three EVM special-transaction payloads:
 *
 *   - CEvmDeployTx
 *   - CEvmCallTx
 *   - CEvmSpendTx
 *
 * These tests validate:
 *   1. Round-trip serialization via CDataStream (the path used by
 *      vExtraPayload in a special transaction).
 *   2. GetTxPayload<T>() correctly extracts the struct from a fully-formed
 *      CTransaction with vExtraPayload set.
 *   3. Each field survives serialization byte-for-byte.
 *
 * They do NOT test validation logic (CheckEvm*Tx) — those are exercised
 * separately once activation gating allows accepting EVM txs (Phase 2+).
 */

namespace {

// Helper: round-trip any serializable type through CDataStream.
template <typename T>
T RoundTrip(const T& in)
{
    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << in;
    T out;
    ds >> out;
    BOOST_REQUIRE(ds.empty()); // No trailing bytes after deserialization.
    return out;
}

// Helper: build a "fake" CTransaction with vExtraPayload set to the
// serialized form of `payload`. Uses the existing SetTxPayload template
// from src/evo/specialtx.h to populate vExtraPayload exactly as the
// network would.
template <typename T>
CTransaction MakeTxWithPayload(uint16_t nType, const T& payload)
{
    CMutableTransaction mtx;
    mtx.nVersion = 3; // Special-tx version
    mtx.nType = nType;
    SetTxPayload(mtx, payload);
    return CTransaction(mtx);
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(evm_evmtx_tests, BasicTestingSetup)

// ----------------------------------------------------------------------------
// CEvmDeployTx round-trip
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(deploy_tx_round_trip)
{
    evm::CEvmDeployTx in;
    in.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    in.code = {0x60, 0x60, 0x60, 0x40, 0x52, 0x60, 0x00, 0xF3}; // arbitrary bytecode
    in.gasLimit = 1'234'567;
    in.maxFeePerGas = 100;
    in.maxPriorityFeePerGas = 2;
    in.senderHash = uint256S("0x1111111111111111111111111111111111111111111111111111111111111111");
    in.nonce = 42;

    evm::CEvmDeployTx out = RoundTrip(in);

    BOOST_CHECK_EQUAL(out.nVersion, in.nVersion);
    BOOST_CHECK_EQUAL_COLLECTIONS(out.code.begin(), out.code.end(),
                                  in.code.begin(), in.code.end());
    BOOST_CHECK_EQUAL(out.gasLimit, in.gasLimit);
    BOOST_CHECK_EQUAL(out.maxFeePerGas, in.maxFeePerGas);
    BOOST_CHECK_EQUAL(out.maxPriorityFeePerGas, in.maxPriorityFeePerGas);
    BOOST_CHECK(out.senderHash == in.senderHash);
    BOOST_CHECK_EQUAL(out.nonce, in.nonce);
}

BOOST_AUTO_TEST_CASE(deploy_tx_via_vextrapayload)
{
    evm::CEvmDeployTx in;
    in.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    in.code = {0xFE}; // single-byte bytecode
    in.gasLimit = 100'000;
    in.maxFeePerGas = 50;
    in.maxPriorityFeePerGas = 1;
    in.senderHash = uint256S("0xabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcd");
    in.nonce = 7;

    CTransaction tx = MakeTxWithPayload(TRANSACTION_EVM_DEPLOY, in);

    evm::CEvmDeployTx out;
    BOOST_REQUIRE(GetTxPayload(tx, out));

    BOOST_CHECK_EQUAL(out.nVersion, in.nVersion);
    BOOST_CHECK_EQUAL(out.code.size(), in.code.size());
    BOOST_CHECK_EQUAL(out.gasLimit, in.gasLimit);
    BOOST_CHECK_EQUAL(out.nonce, in.nonce);
    BOOST_CHECK(out.senderHash == in.senderHash);
}

// ----------------------------------------------------------------------------
// CEvmCallTx round-trip
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(call_tx_round_trip)
{
    evm::CEvmCallTx in;
    in.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    in.toAddress = uint256S("0x0000000000000000000000007777777777777777777777777777777777777777");
    in.value = 1'000'000'000'000ULL; // 1 micro-RTM in weis (10^12)
    in.data = {0x12, 0x34, 0x56, 0x78}; // 4-byte function selector
    in.gasLimit = 500'000;
    in.maxFeePerGas = 200;
    in.maxPriorityFeePerGas = 5;
    in.senderHash = uint256S("0x2222222222222222222222222222222222222222222222222222222222222222");
    in.nonce = 13;

    evm::CEvmCallTx out = RoundTrip(in);

    BOOST_CHECK_EQUAL(out.nVersion, in.nVersion);
    BOOST_CHECK(out.toAddress == in.toAddress);
    BOOST_CHECK_EQUAL(out.value, in.value);
    BOOST_CHECK_EQUAL_COLLECTIONS(out.data.begin(), out.data.end(),
                                  in.data.begin(), in.data.end());
    BOOST_CHECK_EQUAL(out.gasLimit, in.gasLimit);
    BOOST_CHECK_EQUAL(out.maxFeePerGas, in.maxFeePerGas);
    BOOST_CHECK_EQUAL(out.maxPriorityFeePerGas, in.maxPriorityFeePerGas);
    BOOST_CHECK(out.senderHash == in.senderHash);
    BOOST_CHECK_EQUAL(out.nonce, in.nonce);
}

BOOST_AUTO_TEST_CASE(call_tx_via_vextrapayload)
{
    evm::CEvmCallTx in;
    in.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    in.toAddress = uint256S("0x000000000000000000000000aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    in.value = 0; // contract call with no value
    in.data = std::vector<uint8_t>(32, 0xFF); // 32 bytes of 0xFF
    in.gasLimit = 21'000;
    in.maxFeePerGas = 10;
    in.maxPriorityFeePerGas = 1;
    in.senderHash = uint256S("0x3333333333333333333333333333333333333333333333333333333333333333");
    in.nonce = 0;

    CTransaction tx = MakeTxWithPayload(TRANSACTION_EVM_CALL, in);

    evm::CEvmCallTx out;
    BOOST_REQUIRE(GetTxPayload(tx, out));

    BOOST_CHECK_EQUAL(out.data.size(), 32U);
    BOOST_CHECK_EQUAL(static_cast<int>(out.data[0]), 0xFF);
    BOOST_CHECK_EQUAL(out.value, 0U);
    BOOST_CHECK_EQUAL(out.nonce, 0U);
}

// ----------------------------------------------------------------------------
// CEvmSpendTx round-trip
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(spend_tx_round_trip)
{
    evm::CEvmSpendTx in;
    in.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    in.fromAddress = uint256S("0x000000000000000000000000bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    in.amount = 5'000'000'000ULL;
    in.outputScript = CScript() << OP_DUP << OP_HASH160
                                 << std::vector<uint8_t>(20, 0x42)
                                 << OP_EQUALVERIFY << OP_CHECKSIG;
    in.gasLimit = 30'000;
    in.maxFeePerGas = 80;
    in.maxPriorityFeePerGas = 3;
    in.nonce = 99;

    evm::CEvmSpendTx out = RoundTrip(in);

    BOOST_CHECK_EQUAL(out.nVersion, in.nVersion);
    BOOST_CHECK(out.fromAddress == in.fromAddress);
    BOOST_CHECK_EQUAL(out.amount, in.amount);
    BOOST_CHECK(out.outputScript == in.outputScript);
    BOOST_CHECK_EQUAL(out.gasLimit, in.gasLimit);
    BOOST_CHECK_EQUAL(out.maxFeePerGas, in.maxFeePerGas);
    BOOST_CHECK_EQUAL(out.maxPriorityFeePerGas, in.maxPriorityFeePerGas);
    BOOST_CHECK_EQUAL(out.nonce, in.nonce);
}

BOOST_AUTO_TEST_CASE(spend_tx_via_vextrapayload)
{
    evm::CEvmSpendTx in;
    in.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    in.fromAddress = uint256S("0x000000000000000000000000cccccccccccccccccccccccccccccccccccccccc");
    in.amount = 1;
    in.outputScript = CScript() << OP_TRUE; // minimal script
    in.gasLimit = 21'000;
    in.maxFeePerGas = 1;
    in.maxPriorityFeePerGas = 0;
    in.nonce = 1;

    CTransaction tx = MakeTxWithPayload(TRANSACTION_EVM_SPEND, in);

    evm::CEvmSpendTx out;
    BOOST_REQUIRE(GetTxPayload(tx, out));

    BOOST_CHECK_EQUAL(out.amount, 1U);
    BOOST_CHECK_EQUAL(out.outputScript.size(), 1U);
}

// ----------------------------------------------------------------------------
// Defaults and limit constants
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(default_constructed_payloads_have_v1)
{
    evm::CEvmDeployTx d;
    evm::CEvmCallTx c;
    evm::CEvmSpendTx s;
    BOOST_CHECK_EQUAL(d.nVersion, evm::EVM_TX_PAYLOAD_VERSION);
    BOOST_CHECK_EQUAL(c.nVersion, evm::EVM_TX_PAYLOAD_VERSION);
    BOOST_CHECK_EQUAL(s.nVersion, evm::EVM_TX_PAYLOAD_VERSION);
}

BOOST_AUTO_TEST_CASE(size_limits_match_eip170)
{
    // EIP-170: contract code size limit is 24576 bytes.
    BOOST_CHECK_EQUAL(evm::MAX_EVM_CONTRACT_CODE_SIZE, 24576U);
    // Phase 1 calldata cap mirrors EIP-170 for now.
    BOOST_CHECK_EQUAL(evm::MAX_EVM_CALLDATA_SIZE, 24576U);
}

// ----------------------------------------------------------------------------
// Tx type values are stable (consensus-critical)
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(tx_type_values_stable)
{
    // These three values are reserved and consensus-critical. They must
    // never change without a hard fork and explicit coordination with
    // network operators. Test pins them.
    BOOST_CHECK_EQUAL(TRANSACTION_EVM_DEPLOY, 11);
    BOOST_CHECK_EQUAL(TRANSACTION_EVM_CALL, 12);
    BOOST_CHECK_EQUAL(TRANSACTION_EVM_SPEND, 13);
}

// ----------------------------------------------------------------------------
// Opcode values are stable (consensus-critical)
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(opcode_values_stable)
{
    BOOST_CHECK_EQUAL(static_cast<int>(OP_EVMCREATE), 0xbd);
    BOOST_CHECK_EQUAL(static_cast<int>(OP_EVMCALL), 0xbe);
    BOOST_CHECK_EQUAL(static_cast<int>(OP_EVMSPEND), 0xbf);
    BOOST_CHECK_EQUAL(MAX_OPCODE, static_cast<unsigned int>(OP_EVMSPEND));
}

// ----------------------------------------------------------------------------
// Script-interpreter gating of EVM opcodes
// ----------------------------------------------------------------------------
//
// EvalScript() must reject OP_EVMCREATE/CALL/SPEND when
// SCRIPT_ENABLE_EVM_OPCODES is NOT set, and accept them as no-ops when it IS.
// Phase 2 will add additional context-aware checks; Phase 1 only validates
// the gating.

namespace {

bool RunScriptWithFlag(opcodetype opcode, unsigned int flags, ScriptError& err)
{
    CScript script;
    script << opcode;
    std::vector<std::vector<unsigned char>> stack;
    BaseSignatureChecker checker; // returns false for everything; not exercised
    return EvalScript(stack, script, flags, checker, SigVersion::BASE, &err);
}

} // anonymous namespace

BOOST_AUTO_TEST_CASE(evm_opcodes_rejected_without_flag)
{
    ScriptError err;

    for (opcodetype op : {OP_EVMCREATE, OP_EVMCALL, OP_EVMSPEND}) {
        BOOST_TEST_INFO("opcode=" << GetOpName(op));
        bool ok = RunScriptWithFlag(op, SCRIPT_VERIFY_NONE, err);
        BOOST_CHECK(!ok);
        BOOST_CHECK_EQUAL(err, SCRIPT_ERR_BAD_OPCODE);
    }
}

BOOST_AUTO_TEST_CASE(evm_opcodes_accepted_with_flag)
{
    ScriptError err;

    for (opcodetype op : {OP_EVMCREATE, OP_EVMCALL, OP_EVMSPEND}) {
        BOOST_TEST_INFO("opcode=" << GetOpName(op));
        bool ok = RunScriptWithFlag(op, SCRIPT_ENABLE_EVM_OPCODES, err);
        // No-op: success, empty stack remaining. EvalScript itself returns true.
        BOOST_CHECK(ok);
        BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
    }
}

BOOST_AUTO_TEST_SUITE_END()
