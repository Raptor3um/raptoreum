// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <evm/account.h>
#include <evm/balance.h>
#include <evm/connectblock.h>
#include <evm/evmtx.h>
#include <evm/hashing.h>
#include <evm/host.h>
#include <evm/state_cache.h>
#include <evm/state_db.h>

#include <primitives/block.h>
#include <primitives/transaction.h>
#include <streams.h>
#include <version.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

/**
 * Phase 2.4e — block-level EVM tx iterator tests.
 *
 * Each test builds a CBlock by hand containing one or more EVM-typed
 * CTransactions and dispatches via ProcessEvmTransactionsInBlock.
 * We verify:
 *   - Mixed EVM + non-EVM blocks: non-EVM txs are silently skipped.
 *   - Multi-EVM-tx blocks: fees/credits aggregate correctly.
 *   - Pre-flight failures abort processing and report the failing
 *     tx index.
 *   - Charged failures (REVERT) are recorded but do not abort the
 *     block.
 *   - UTXO credits from SPEND txs surface in the aggregated result.
 */

namespace {

evm::ExecutionContext MakeContext(uint64_t baseFeeWeis)
{
    evm::ExecutionContext c;
    c.chainId = 7375;
    c.blockHeight = 200;
    c.blockTimestamp = 1700000000;
    c.blockGasLimit = 30'000'000;
    c.baseFee = evm::Uint256FromUint64(baseFeeWeis);
    return c;
}

uint256 SenderHash(uint8_t lowByte)
{
    uint256 u;
    *(u.begin() + 31) = lowByte;
    return u;
}

uint160 SenderAddr(uint8_t lowByte)
{
    uint160 a;
    *(a.begin() + 19) = lowByte;
    return a;
}

void SeedSender(evm::CEvmStateCache& cache,
                uint8_t lowByte,
                uint64_t balanceWeis,
                uint64_t nonce = 0)
{
    cache.SetAccount(SenderAddr(lowByte), evm::CEvmAccount(
        nonce, evm::Uint256FromUint64(balanceWeis),
        evm::CEvmAccount::EmptyCodeHash(),
        evm::CEvmAccount::EmptyStorageRoot()));
}

template <typename T>
std::vector<unsigned char> SerializePayload(const T& obj)
{
    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << obj;
    return std::vector<unsigned char>(ds.begin(), ds.end());
}

// Build a CTransaction with the given nType and serialized payload.
// Other fields are minimum-viable defaults: a single zero-input,
// no outputs, version 3 (the smart-tx version used by Dash-style
// special transactions in the existing code).
CTransactionRef MakeEvmTx(uint16_t nType,
                          const std::vector<unsigned char>& payload)
{
    CMutableTransaction mtx;
    mtx.nVersion = 3;
    mtx.nType = nType;
    mtx.vExtraPayload = payload;
    return MakeTransactionRef(std::move(mtx));
}

// A vanilla "normal" tx for the no-skip test (non-EVM type → should
// be ignored by the iterator).
CTransactionRef MakeNormalTx()
{
    CMutableTransaction mtx;
    mtx.nVersion = 3;
    mtx.nType = TRANSACTION_NORMAL;
    return MakeTransactionRef(std::move(mtx));
}

evm::CEvmCallTx MakeCallPayload(uint8_t senderLow,
                                uint8_t toLow,
                                uint64_t value,
                                uint64_t gasLimit,
                                uint64_t maxFee,
                                uint64_t priorityFee,
                                uint64_t nonce = 0)
{
    evm::CEvmCallTx p;
    p.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    uint256 to;
    *(to.begin() + 31) = toLow;
    p.toAddress = to;
    p.value = value;
    p.gasLimit = gasLimit;
    p.maxFeePerGas = maxFee;
    p.maxPriorityFeePerGas = priorityFee;
    p.senderHash = SenderHash(senderLow);
    p.nonce = nonce;
    return p;
}

evm::CEvmSpendTx MakeSpendPayload(uint8_t senderLow,
                                  uint64_t weisAmount,
                                  uint64_t gasLimit,
                                  uint64_t maxFee,
                                  uint64_t priorityFee,
                                  uint64_t nonce = 0)
{
    evm::CEvmSpendTx p;
    p.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    p.fromAddress = SenderHash(senderLow);
    p.amount = weisAmount;
    p.outputScript = CScript() << OP_DUP << OP_HASH160
                               << std::vector<unsigned char>(20, 0xAB)
                               << OP_EQUALVERIFY << OP_CHECKSIG;
    p.gasLimit = gasLimit;
    p.maxFeePerGas = maxFee;
    p.maxPriorityFeePerGas = priorityFee;
    p.nonce = nonce;
    return p;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(evm_connectblock_tests, BasicTestingSetup)

// ----------------------------------------------------------------------------
// A block with no EVM txs: result is the trivial zero-everything.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(empty_block_yields_empty_result)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    CBlock block;
    block.vtx.push_back(MakeNormalTx()); // a non-EVM tx; should be ignored

    auto r = evm::ProcessEvmTransactionsInBlock(block, nullptr, cache,
                                               MakeContext(/*baseFee=*/ 0));
    BOOST_CHECK(r.ok);
    BOOST_CHECK_EQUAL(r.txResults.size(), 0U);
    BOOST_CHECK_EQUAL(r.totalCoinbaseTip, 0U);
    BOOST_CHECK_EQUAL(r.totalBurned, 0U);
    BOOST_CHECK_EQUAL(r.utxoCredits.size(), 0U);
}

// ----------------------------------------------------------------------------
// Single CALL tx in a block, with non-EVM noise around it: only the
// CALL is processed; aggregates reflect the single tx.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(single_call_tx_aggregates_correctly)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    SeedSender(cache, 0xA0, /*balance=*/ 1'000'000'000ULL);

    const auto ctx = MakeContext(/*baseFee=*/ 2);
    auto callPayload = MakeCallPayload(/*sender=*/ 0xA0, /*to=*/ 0x0E,
                                       /*value=*/ 0,
                                       /*gasLimit=*/ 100'000,
                                       /*maxFee=*/ 10,
                                       /*priority=*/ 1);
    auto serialized = SerializePayload(callPayload);

    CBlock block;
    block.vtx.push_back(MakeNormalTx());                       // noise
    block.vtx.push_back(MakeEvmTx(TRANSACTION_EVM_CALL, serialized));
    block.vtx.push_back(MakeNormalTx());                       // noise

    auto r = evm::ProcessEvmTransactionsInBlock(block, nullptr, cache, ctx);
    BOOST_REQUIRE(r.ok);
    BOOST_REQUIRE_EQUAL(r.txResults.size(), 1U);
    BOOST_CHECK_EQUAL(r.txResults[0].apply.statusCode, EVMC_SUCCESS);
    // Codeless call → gas_used=0; both burn and tip are zero.
    BOOST_CHECK_EQUAL(r.totalBurned, 0U);
    BOOST_CHECK_EQUAL(r.totalCoinbaseTip, 0U);
}

// ----------------------------------------------------------------------------
// Two EVM txs from the same sender with sequential nonces: both
// succeed, aggregates sum, sender nonce ends at 2.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(two_calls_aggregate_fees_and_advance_nonce)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    SeedSender(cache, 0xA1, /*balance=*/ 10'000'000'000ULL, /*nonce=*/ 0);

    // Deploy a SSTORE contract at 0x77 so call(s) actually do work.
    const std::vector<uint8_t> code = {0x60, 0x42, 0x60, 0x01, 0x55, 0x00};
    const uint256 codeHash = evm::Keccak256(code);
    cache.SetCode(codeHash, code);
    uint160 contractAddr;
    *(contractAddr.begin() + 19) = 0x77;
    cache.SetAccount(contractAddr, evm::CEvmAccount(
        1, uint256(), codeHash, evm::CEvmAccount::EmptyStorageRoot()));

    const auto ctx = MakeContext(/*baseFee=*/ 2);

    auto p0 = MakeCallPayload(0xA1, 0x77, 0, 100'000, 10, 1, /*nonce=*/ 0);
    auto p1 = MakeCallPayload(0xA1, 0x77, 0, 100'000, 10, 1, /*nonce=*/ 1);

    CBlock block;
    block.vtx.push_back(MakeEvmTx(TRANSACTION_EVM_CALL, SerializePayload(p0)));
    block.vtx.push_back(MakeEvmTx(TRANSACTION_EVM_CALL, SerializePayload(p1)));

    auto r = evm::ProcessEvmTransactionsInBlock(block, nullptr, cache, ctx);
    BOOST_REQUIRE(r.ok);
    BOOST_REQUIRE_EQUAL(r.txResults.size(), 2U);
    BOOST_CHECK_EQUAL(r.txResults[0].apply.statusCode, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(r.txResults[1].apply.statusCode, EVMC_SUCCESS);

    // Aggregates: each tx's burn = gasUsed * 2, tip = gasUsed * 1.
    const uint64_t g0 = static_cast<uint64_t>(r.txResults[0].apply.gasUsed);
    const uint64_t g1 = static_cast<uint64_t>(r.txResults[1].apply.gasUsed);
    BOOST_CHECK_EQUAL(r.totalBurned, (g0 + g1) * 2);
    BOOST_CHECK_EQUAL(r.totalCoinbaseTip, (g0 + g1) * 1);

    // Sender nonce ended at 2.
    evm::CEvmAccount sender;
    BOOST_REQUIRE(cache.GetAccount(SenderAddr(0xA1), sender));
    BOOST_CHECK_EQUAL(sender.nonce, 2U);
}

// ----------------------------------------------------------------------------
// Pre-flight failure aborts processing and reports failed index.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(preflight_failure_aborts_block)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    SeedSender(cache, 0xA2, /*balance=*/ 1'000'000'000ULL, /*nonce=*/ 0);

    const auto ctx = MakeContext(/*baseFee=*/ 2);

    // tx[0] valid.
    auto p0 = MakeCallPayload(0xA2, 0x07, 0, 100'000, 10, 1, /*nonce=*/ 0);
    // tx[1] uses a wrong nonce (says 5, but account is at nonce=1 after p0).
    auto p1 = MakeCallPayload(0xA2, 0x07, 0, 100'000, 10, 1, /*nonce=*/ 5);

    CBlock block;
    block.vtx.push_back(MakeEvmTx(TRANSACTION_EVM_CALL, SerializePayload(p0)));
    block.vtx.push_back(MakeEvmTx(TRANSACTION_EVM_CALL, SerializePayload(p1)));

    auto r = evm::ProcessEvmTransactionsInBlock(block, nullptr, cache, ctx);
    BOOST_CHECK(!r.ok);
    BOOST_CHECK_EQUAL(r.failedTxIndex, 1);
    // The first tx still executed (its result is in txResults).
    BOOST_REQUIRE_EQUAL(r.txResults.size(), 1U);
    BOOST_CHECK_EQUAL(r.txResults[0].apply.statusCode, EVMC_SUCCESS);
}

// ----------------------------------------------------------------------------
// Charged failure (REVERT) is included in the block — block still ok.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(charged_failure_included_block_ok)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    SeedSender(cache, 0xA3, /*balance=*/ 1'000'000'000ULL, /*nonce=*/ 0);

    // Deploy a revert-immediate contract at 0xBB.
    const std::vector<uint8_t> code = {0x60, 0x00, 0x60, 0x00, 0xFD};
    const uint256 codeHash = evm::Keccak256(code);
    cache.SetCode(codeHash, code);
    uint160 contractAddr;
    *(contractAddr.begin() + 19) = 0xBB;
    cache.SetAccount(contractAddr, evm::CEvmAccount(
        1, uint256(), codeHash, evm::CEvmAccount::EmptyStorageRoot()));

    const auto ctx = MakeContext(/*baseFee=*/ 2);
    auto p = MakeCallPayload(0xA3, 0xBB, 0, 50'000, 10, 1, 0);
    CBlock block;
    block.vtx.push_back(MakeEvmTx(TRANSACTION_EVM_CALL, SerializePayload(p)));

    auto r = evm::ProcessEvmTransactionsInBlock(block, nullptr, cache, ctx);
    BOOST_CHECK(r.ok);
    BOOST_REQUIRE_EQUAL(r.txResults.size(), 1U);
    BOOST_CHECK_EQUAL(r.txResults[0].apply.statusCode, EVMC_REVERT);
    // Fee still recorded for the gas the failed tx consumed.
    BOOST_CHECK(r.totalCoinbaseTip > 0U);
    BOOST_CHECK(r.totalBurned > 0U);
}

// ----------------------------------------------------------------------------
// SPEND tx surfaces a UtxoCredit in the aggregated result.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(spend_tx_produces_aggregated_utxo_credit)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    // Seed plenty: 100 sat worth of weis (10^12).
    SeedSender(cache, 0xE1, /*balance=*/ 1'000'000'000'000ULL);

    const auto ctx = MakeContext(/*baseFee=*/ 2);
    auto p = MakeSpendPayload(/*sender=*/ 0xE1,
                              /*weisAmount=*/ 3 * evm::kWeisPerSatoshi,
                              /*gasLimit=*/ 21'000,
                              /*maxFee=*/ 10,
                              /*priority=*/ 1);
    CBlock block;
    block.vtx.push_back(MakeEvmTx(TRANSACTION_EVM_SPEND, SerializePayload(p)));

    auto r = evm::ProcessEvmTransactionsInBlock(block, nullptr, cache, ctx);
    BOOST_REQUIRE(r.ok);
    BOOST_REQUIRE_EQUAL(r.utxoCredits.size(), 1U);
    BOOST_CHECK_EQUAL(r.utxoCredits[0].amount, 3);
    BOOST_CHECK_EQUAL(r.totalCoinbaseTip, 21000ULL * 1);
    BOOST_CHECK_EQUAL(r.totalBurned, 21000ULL * 2);
}

// ----------------------------------------------------------------------------
// Mixed block: DEPLOY tx followed by CALL tx that targets the
// just-deployed contract via nonce-derived address. Verifies state
// composes across txs in the same block.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(deploy_then_call_in_same_block)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    SeedSender(cache, 0xD0, /*balance=*/ 1'000'000'000'000ULL, /*nonce=*/ 0);

    const auto ctx = MakeContext(/*baseFee=*/ 2);

    // Deploy tx: standard 15-byte init code → 6-byte SSTORE runtime.
    evm::CEvmDeployTx dp;
    dp.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    dp.code = {
        0x65, 0x60, 0x42, 0x60, 0x01, 0x55, 0x00,
        0x60, 0x00, 0x52,
        0x60, 0x06, 0x60, 0x1A, 0xF3,
    };
    dp.gasLimit = 200'000;
    dp.maxFeePerGas = 10;
    dp.maxPriorityFeePerGas = 1;
    dp.senderHash = SenderHash(0xD0);
    dp.nonce = 0;

    // The deployed address will be ContractAddressFromCreate(0xD0, 0).
    const uint160 deployedAddr =
        evm::ContractAddressFromCreate(SenderAddr(0xD0), 0);

    // Call tx targeting that derived address.
    uint256 toAddress;
    std::memcpy(toAddress.begin() + 12, deployedAddr.begin(), 20);
    evm::CEvmCallTx cp;
    cp.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    cp.toAddress = toAddress;
    cp.value = 0;
    cp.gasLimit = 100'000;
    cp.maxFeePerGas = 10;
    cp.maxPriorityFeePerGas = 1;
    cp.senderHash = SenderHash(0xD0);
    cp.nonce = 1; // sender nonce is now 1 after the deploy

    CBlock block;
    block.vtx.push_back(MakeEvmTx(TRANSACTION_EVM_DEPLOY, SerializePayload(dp)));
    block.vtx.push_back(MakeEvmTx(TRANSACTION_EVM_CALL, SerializePayload(cp)));

    auto r = evm::ProcessEvmTransactionsInBlock(block, nullptr, cache, ctx);
    BOOST_REQUIRE(r.ok);
    BOOST_REQUIRE_EQUAL(r.txResults.size(), 2U);
    BOOST_CHECK_EQUAL(r.txResults[0].apply.statusCode, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(r.txResults[1].apply.statusCode, EVMC_SUCCESS);
    BOOST_CHECK(r.txResults[0].apply.deployedAddress == deployedAddr);

    // The call did SSTORE 0x42 at slot 0x01 of the deployed contract.
    uint256 slot1;
    *(slot1.begin() + 31) = 0x01;
    uint256 stored;
    BOOST_REQUIRE(cache.GetStorage(deployedAddr, slot1, stored));
    BOOST_CHECK_EQUAL(static_cast<int>(*(stored.begin() + 31)), 0x42);
}

BOOST_AUTO_TEST_SUITE_END()
