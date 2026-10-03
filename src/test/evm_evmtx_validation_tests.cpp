// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <consensus/validation.h>
#include <evm/evmtx.h>
#include <evo/specialtx.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

/**
 * Consensus structural validation of EVM transactions — the
 * CheckEvm{Deploy,Call,Spend,Fund}Tx entry points reached from
 * CheckSpecialTx for EVERY EVM tx. These reject malformed payloads
 * (bad version, zero gas, inverted EIP-1559 fees, over-size code /
 * calldata, zero / non-round amounts, empty output scripts) before
 * execution. They had no direct coverage; a regression that let a
 * malformed EVM tx into a block would be a consensus defect. Run
 * under regtest (EVM active at height 0), so the activation gate
 * passes and we exercise the structural checks against the chain tip.
 */

namespace {

template <typename T>
CTransaction MakeTx(uint16_t nType, const T& payload)
{
    CMutableTransaction mtx;
    mtx.nVersion = 3;
    mtx.nType = nType;
    SetTxPayload(mtx, payload);
    return CTransaction(mtx);
}

evm::CEvmDeployTx ValidDeploy()
{
    evm::CEvmDeployTx d;
    d.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    d.code = {0x60, 0x00};
    d.gasLimit = 100000;
    d.maxFeePerGas = 1000;
    d.maxPriorityFeePerGas = 10;
    d.nonce = 0;
    return d;
}
evm::CEvmCallTx ValidCall()
{
    evm::CEvmCallTx c;
    c.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    c.value = 0;
    c.data = {0x01};
    c.gasLimit = 50000;
    c.maxFeePerGas = 1000;
    c.maxPriorityFeePerGas = 10;
    c.nonce = 0;
    return c;
}
evm::CEvmSpendTx ValidSpend()
{
    evm::CEvmSpendTx s;
    s.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    s.amount = 100000;
    s.outputScript = CScript() << OP_TRUE;
    s.gasLimit = 21000;
    s.maxFeePerGas = 1000;
    s.maxPriorityFeePerGas = 0;
    s.nonce = 0;
    return s;
}
evm::CEvmFundTx ValidFund()
{
    evm::CEvmFundTx f;
    f.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    f.amount = 10'000'000'000ULL;  // exactly 1 satoshi worth
    return f;
}

} // anonymous namespace

// TestChain100Setup (not bare TestingSetup): EVM activates against a
// real mined tip, so the activation gate passes and we reach the
// structural checks. (At genesis the EVM update gate reads inactive.)
BOOST_FIXTURE_TEST_SUITE(evm_evmtx_validation_tests, TestChain100Setup)

#define CHK(fn, payload, ntype) [&]{ \
    CValidationState st; \
    return evm::fn(MakeTx(ntype, payload), ::ChainActive().Tip(), st); }()

BOOST_AUTO_TEST_CASE(valid_payloads_pass)
{
    BOOST_CHECK(CHK(CheckEvmDeployTx, ValidDeploy(), TRANSACTION_EVM_DEPLOY));
    BOOST_CHECK(CHK(CheckEvmCallTx,   ValidCall(),   TRANSACTION_EVM_CALL));
    BOOST_CHECK(CHK(CheckEvmSpendTx,  ValidSpend(),  TRANSACTION_EVM_SPEND));
    BOOST_CHECK(CHK(CheckEvmFundTx,   ValidFund(),   TRANSACTION_EVM_FUND));
}

BOOST_AUTO_TEST_CASE(deploy_rejections)
{
    auto d = ValidDeploy(); d.code.clear();                 // empty code
    BOOST_CHECK(!CHK(CheckEvmDeployTx, d, TRANSACTION_EVM_DEPLOY));
    d = ValidDeploy();
    d.code.assign(evm::MAX_EVM_CONTRACT_CODE_SIZE + 1, 0x60);  // oversize
    BOOST_CHECK(!CHK(CheckEvmDeployTx, d, TRANSACTION_EVM_DEPLOY));
    d = ValidDeploy(); d.gasLimit = 0;                      // zero gas
    BOOST_CHECK(!CHK(CheckEvmDeployTx, d, TRANSACTION_EVM_DEPLOY));
    d = ValidDeploy(); d.maxPriorityFeePerGas = d.maxFeePerGas + 1;  // bad fee
    BOOST_CHECK(!CHK(CheckEvmDeployTx, d, TRANSACTION_EVM_DEPLOY));
    d = ValidDeploy(); d.nVersion = 0;                      // bad version
    BOOST_CHECK(!CHK(CheckEvmDeployTx, d, TRANSACTION_EVM_DEPLOY));
}

BOOST_AUTO_TEST_CASE(call_rejections)
{
    auto c = ValidCall();
    c.data.assign(evm::MAX_EVM_CALLDATA_SIZE + 1, 0x00);    // oversize calldata
    BOOST_CHECK(!CHK(CheckEvmCallTx, c, TRANSACTION_EVM_CALL));
    c = ValidCall(); c.gasLimit = 0;                        // zero gas
    BOOST_CHECK(!CHK(CheckEvmCallTx, c, TRANSACTION_EVM_CALL));
    c = ValidCall(); c.maxPriorityFeePerGas = c.maxFeePerGas + 1;  // bad fee
    BOOST_CHECK(!CHK(CheckEvmCallTx, c, TRANSACTION_EVM_CALL));
}

BOOST_AUTO_TEST_CASE(spend_rejections)
{
    auto s = ValidSpend(); s.amount = 0;                    // zero amount
    BOOST_CHECK(!CHK(CheckEvmSpendTx, s, TRANSACTION_EVM_SPEND));
    s = ValidSpend(); s.outputScript = CScript();          // empty script
    BOOST_CHECK(!CHK(CheckEvmSpendTx, s, TRANSACTION_EVM_SPEND));
    s = ValidSpend(); s.gasLimit = 0;                      // zero gas
    BOOST_CHECK(!CHK(CheckEvmSpendTx, s, TRANSACTION_EVM_SPEND));
}

BOOST_AUTO_TEST_CASE(fund_rejections)
{
    CValidationState st1;
    evm::CEvmFundTx z = ValidFund(); z.amount = 0;         // zero amount
    BOOST_CHECK(!evm::CheckEvmFundTx(
        MakeTx(TRANSACTION_EVM_FUND, z), ::ChainActive().Tip(), st1));
    BOOST_CHECK_EQUAL(st1.GetRejectReason(), "bad-evm-fund-amount");

    CValidationState st2;
    evm::CEvmFundTx p = ValidFund(); p.amount = 10'000'000'001ULL;  // not %1e10
    BOOST_CHECK(!evm::CheckEvmFundTx(
        MakeTx(TRANSACTION_EVM_FUND, p), ::ChainActive().Tip(), st2));
    BOOST_CHECK_EQUAL(st2.GetRejectReason(), "bad-evm-fund-precision");

    evm::CEvmFundTx bv = ValidFund(); bv.nVersion = 0;     // bad version
    BOOST_CHECK(!CHK(CheckEvmFundTx, bv, TRANSACTION_EVM_FUND));
}

#undef CHK

BOOST_AUTO_TEST_SUITE_END()
