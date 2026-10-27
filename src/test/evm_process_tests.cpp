// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <evm/account.h>
#include <evm/balance.h>
#include <evm/evmtx.h>
#include <evm/hashing.h>
#include <evm/host.h>
#include <evm/process.h>
#include <evm/state_cache.h>
#include <evm/state_db.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

/**
 * Phase 2.4 tests: ProcessEvm*Tx — fee accounting + sender debit/refund
 * + nonce bump around the Phase 2.3 Apply* dispatchers.
 *
 * Each test sets a fixed baseFee + maxFeePerGas + tip and verifies:
 *   - The sender's pre-tx balance is the budget for upfront-cost +
 *     value (or amount, for spend).
 *   - On success: balance debited by (gasUsed * effective + value),
 *     nonce bumped by 1.
 *   - The ProcessFee splits burn vs coinbase tip correctly.
 *   - On failure: state changes from the tx body are rolled back
 *     and the sender's balance is debited only by gasUsed * effective
 *     (value is refunded).
 */

namespace {

evm::ExecutionContext MakeContext(uint64_t baseFeeWeis)
{
    evm::ExecutionContext c;
    c.chainId = 7375;
    c.blockHeight = 100;
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
        nonce,
        evm::Uint256FromUint64(balanceWeis),
        evm::CEvmAccount::EmptyCodeHash(),
        evm::CEvmAccount::EmptyStorageRoot()));
}

uint64_t SenderBalance(evm::CEvmStateCache& cache, uint8_t lowByte)
{
    evm::CEvmAccount a;
    BOOST_REQUIRE(cache.GetAccount(SenderAddr(lowByte), a));
    return evm::Uint256ToLowUint64(a.balance);
}

uint64_t SenderNonce(evm::CEvmStateCache& cache, uint8_t lowByte)
{
    evm::CEvmAccount a;
    BOOST_REQUIRE(cache.GetAccount(SenderAddr(lowByte), a));
    return a.nonce;
}

// Build a CEvmCallTx aimed at the given recipient.
evm::CEvmCallTx MakeCall(uint8_t senderLow,
                         uint8_t recipientLow,
                         uint64_t value,
                         uint64_t gasLimit,
                         uint64_t maxFee,
                         uint64_t priorityFee,
                         uint64_t nonce = 0)
{
    evm::CEvmCallTx tx;
    tx.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    uint256 to;
    *(to.begin() + 31) = recipientLow;
    tx.toAddress = to;
    tx.value = value;
    tx.gasLimit = gasLimit;
    tx.maxFeePerGas = maxFee;
    tx.maxPriorityFeePerGas = priorityFee;
    tx.senderHash = SenderHash(senderLow);
    tx.nonce = nonce;
    return tx;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(evm_process_tests, BasicTestingSetup)

// ----------------------------------------------------------------------------
// Happy path: empty CALL with positive base fee + tip
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(call_success_debits_balance_and_bumps_nonce)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    const auto ctx = MakeContext(/*baseFee=*/ 5);

    const uint64_t balance0 = 1'000'000'000ULL;
    SeedSender(cache, /*lowByte=*/ 0xA0, balance0, /*nonce=*/ 0);

    // CALL to a codeless address — succeeds as no-op.
    const uint64_t gasLimit = 100'000;
    const uint64_t maxFee = 20;       // weis per gas
    const uint64_t priorityFee = 3;   // weis per gas
    auto tx = MakeCall(0xA0, 0x0E, /*value=*/ 0, gasLimit, maxFee, priorityFee);

    evm::ProcessResult r = evm::ProcessEvmCallTx(tx, cache, ctx);

    BOOST_REQUIRE(!r.preflightFailed);
    BOOST_CHECK_EQUAL(r.apply.statusCode, EVMC_SUCCESS);

    // Effective gas price = min(maxFee, baseFee + priority) = min(20, 8) = 8.
    // Burn per gas = baseFee = 5. Tip per gas = 3.
    // The codeless CALL returns gas_left = gas_limit (no work done),
    // so gasUsed = 0. With gasUsed=0 the fee split is zero too.
    BOOST_CHECK_EQUAL(r.apply.gasUsed, 0);
    BOOST_CHECK_EQUAL(r.fee.burned, 0U);
    BOOST_CHECK_EQUAL(r.fee.coinbaseTip, 0U);

    // Sender balance: pre-debit was gasLimit*maxFee = 100000*20 = 2M.
    // Refund = (gasLimit - gasUsed) * maxFee = 100000*20 = 2M.
    // Net change = 0. Balance unchanged.
    BOOST_CHECK_EQUAL(SenderBalance(cache, 0xA0), balance0);
    BOOST_CHECK_EQUAL(SenderNonce(cache, 0xA0), 1U);
}

// ----------------------------------------------------------------------------
// Successful CALL into a contract that does some real work (SSTORE) —
// gas is consumed, fee split is positive, refund is partial.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(call_into_sstore_charges_real_gas_and_splits_fee)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    const auto ctx = MakeContext(/*baseFee=*/ 2);

    // Deploy a small SSTORE contract at 0x77.
    const std::vector<uint8_t> code = {0x60, 0x42, 0x60, 0x01, 0x55, 0x00};
    const uint256 codeHash = evm::Keccak256(code);
    cache.SetCode(codeHash, code);
    uint160 contractAddr;
    *(contractAddr.begin() + 19) = 0x77;
    cache.SetAccount(contractAddr, evm::CEvmAccount(
        1, uint256(), codeHash, evm::CEvmAccount::EmptyStorageRoot()));

    const uint64_t balance0 = 10'000'000'000ULL; // plenty of budget
    SeedSender(cache, /*lowByte=*/ 0xA1, balance0);

    const uint64_t gasLimit = 100'000;
    const uint64_t maxFee = 10;
    const uint64_t priorityFee = 1;
    auto tx = MakeCall(0xA1, 0x77, /*value=*/ 0, gasLimit, maxFee, priorityFee);

    evm::ProcessResult r = evm::ProcessEvmCallTx(tx, cache, ctx);
    BOOST_REQUIRE(!r.preflightFailed);
    BOOST_CHECK_EQUAL(r.apply.statusCode, EVMC_SUCCESS);
    BOOST_CHECK(r.apply.gasUsed > 0);

    // Effective gas price = min(10, 2+1) = 3.
    // Burn per gas = 2. Tip per gas = 1.
    BOOST_CHECK_EQUAL(r.fee.burned,
                     static_cast<uint64_t>(r.apply.gasUsed) * 2);
    BOOST_CHECK_EQUAL(r.fee.coinbaseTip,
                     static_cast<uint64_t>(r.apply.gasUsed) * 1);

    // Sender pre-debit was gasLimit*maxFee = 100000*10 = 1M. Refund
    // was 1M - (gasUsed*3). Net debit = gasUsed * 3.
    const uint64_t expectedDebit =
        static_cast<uint64_t>(r.apply.gasUsed) * 3;
    BOOST_CHECK_EQUAL(SenderBalance(cache, 0xA1), balance0 - expectedDebit);
    BOOST_CHECK_EQUAL(SenderNonce(cache, 0xA1), 1U);
}

// ----------------------------------------------------------------------------
// Pre-flight failure: sender not in state.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(call_rejects_unknown_sender)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    const auto ctx = MakeContext(/*baseFee=*/ 1);

    auto tx = MakeCall(/*senderLow=*/ 0xFE, /*recipientLow=*/ 0x07,
                       /*value=*/ 0, /*gasLimit=*/ 21000,
                       /*maxFee=*/ 10, /*priority=*/ 1);
    evm::ProcessResult r = evm::ProcessEvmCallTx(tx, cache, ctx);

    BOOST_CHECK(r.preflightFailed);
}

// ----------------------------------------------------------------------------
// Pre-flight failure: nonce mismatch.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(call_rejects_nonce_mismatch)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    const auto ctx = MakeContext(/*baseFee=*/ 1);

    SeedSender(cache, 0xA2, /*balance=*/ 1'000'000'000, /*nonce=*/ 5);

    // payload says nonce=0 but account is at nonce=5 → reject.
    auto tx = MakeCall(0xA2, 0x07, 0, 21000, 10, 1, /*nonce=*/ 0);
    evm::ProcessResult r = evm::ProcessEvmCallTx(tx, cache, ctx);

    BOOST_CHECK(r.preflightFailed);
    // Sender state untouched.
    BOOST_CHECK_EQUAL(SenderNonce(cache, 0xA2), 5U);
}

// ----------------------------------------------------------------------------
// Pre-flight failure: insufficient balance for upfront cost.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(call_rejects_insufficient_upfront_balance)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    const auto ctx = MakeContext(/*baseFee=*/ 1);

    // upfront cost = 100_000 * 10 + 5_000 = 1_005_000 weis.
    // Seed only 1M — short by 5000.
    SeedSender(cache, 0xA3, /*balance=*/ 1'000'000);

    auto tx = MakeCall(0xA3, 0x07, /*value=*/ 5'000,
                       /*gasLimit=*/ 100'000, /*maxFee=*/ 10, /*priority=*/ 1);
    evm::ProcessResult r = evm::ProcessEvmCallTx(tx, cache, ctx);

    BOOST_CHECK(r.preflightFailed);
    BOOST_CHECK_EQUAL(SenderBalance(cache, 0xA3), 1'000'000U);
    BOOST_CHECK_EQUAL(SenderNonce(cache, 0xA3), 0U);
}

// ----------------------------------------------------------------------------
// Pre-flight failure: maxPriorityFeePerGas > maxFeePerGas.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(call_rejects_priority_above_max_fee)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    const auto ctx = MakeContext(/*baseFee=*/ 1);

    SeedSender(cache, 0xA4, /*balance=*/ 1'000'000'000);

    auto tx = MakeCall(0xA4, 0x07, 0, 21000,
                       /*maxFee=*/ 5, /*priority=*/ 10);
    evm::ProcessResult r = evm::ProcessEvmCallTx(tx, cache, ctx);
    BOOST_CHECK(r.preflightFailed);
}

// ----------------------------------------------------------------------------
// Pre-flight failure: baseFee > maxFeePerGas.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(call_rejects_base_fee_above_max_fee)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    const auto ctx = MakeContext(/*baseFee=*/ 100);

    SeedSender(cache, 0xA5, 1'000'000'000);

    auto tx = MakeCall(0xA5, 0x07, 0, 21000,
                       /*maxFee=*/ 10, /*priority=*/ 1);
    evm::ProcessResult r = evm::ProcessEvmCallTx(tx, cache, ctx);
    BOOST_CHECK(r.preflightFailed);
}

// ----------------------------------------------------------------------------
// Charged failure: inner CALL reverts. The sender's state changes
// from the apply layer roll back; the gas portion is charged at the
// effective rate; value is refunded.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(call_revert_charges_gas_refunds_value)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    const auto ctx = MakeContext(/*baseFee=*/ 2);

    // Deploy a contract at 0xBB that REVERTs immediately.
    //   PUSH1 0x00, PUSH1 0x00, REVERT  (3 bytes)
    const std::vector<uint8_t> code = {0x60, 0x00, 0x60, 0x00, 0xFD};
    const uint256 codeHash = evm::Keccak256(code);
    cache.SetCode(codeHash, code);
    uint160 contractAddr;
    *(contractAddr.begin() + 19) = 0xBB;
    cache.SetAccount(contractAddr, evm::CEvmAccount(
        1, uint256(), codeHash, evm::CEvmAccount::EmptyStorageRoot()));

    const uint64_t balance0 = 10'000'000ULL;
    SeedSender(cache, /*lowByte=*/ 0xA6, balance0);

    const uint64_t gasLimit = 50'000;
    const uint64_t maxFee = 10;
    const uint64_t priorityFee = 1;
    const uint64_t value = 1234;
    auto tx = MakeCall(0xA6, 0xBB, value, gasLimit, maxFee, priorityFee);

    evm::ProcessResult r = evm::ProcessEvmCallTx(tx, cache, ctx);
    BOOST_REQUIRE(!r.preflightFailed);
    BOOST_CHECK_EQUAL(r.apply.statusCode, EVMC_REVERT);

    // Effective gas price = min(10, 2+1) = 3.
    // Pre-debit = gasLimit*maxFee + value = 500_000 + 1234.
    // Refund = (gasLimit*maxFee - gasUsed*effective) + value
    //        = 500_000 - gasUsed*3 + 1234.
    // Net = gasUsed * 3.
    const uint64_t expectedDebit =
        static_cast<uint64_t>(r.apply.gasUsed) * 3;
    BOOST_CHECK_EQUAL(SenderBalance(cache, 0xA6), balance0 - expectedDebit);
    // Nonce still bumps on charged failure.
    BOOST_CHECK_EQUAL(SenderNonce(cache, 0xA6), 1U);

    // Fee split.
    BOOST_CHECK_EQUAL(r.fee.burned,
                     static_cast<uint64_t>(r.apply.gasUsed) * 2);
    BOOST_CHECK_EQUAL(r.fee.coinbaseTip,
                     static_cast<uint64_t>(r.apply.gasUsed) * 1);
}

// ----------------------------------------------------------------------------
// ProcessEvmDeployTx happy path: deploy a minimal contract; sender
// debited at the effective rate.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(deploy_success_debits_sender_and_installs_contract)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    const auto ctx = MakeContext(/*baseFee=*/ 2);

    // Standard 15-byte init code returning the 6-byte SSTORE runtime.
    const std::vector<uint8_t> initCode = {
        0x65, 0x60, 0x42, 0x60, 0x01, 0x55, 0x00,
        0x60, 0x00, 0x52,
        0x60, 0x06, 0x60, 0x1A, 0xF3,
    };

    SeedSender(cache, /*lowByte=*/ 0xD0, /*balance=*/ 1'000'000'000ULL);

    evm::CEvmDeployTx tx;
    tx.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    tx.code = initCode;
    tx.gasLimit = 200'000;
    tx.maxFeePerGas = 10;
    tx.maxPriorityFeePerGas = 1;
    tx.senderHash = SenderHash(0xD0);
    tx.nonce = 0;

    evm::ProcessResult r = evm::ProcessEvmDeployTx(tx, cache, ctx);

    BOOST_REQUIRE(!r.preflightFailed);
    BOOST_CHECK_EQUAL(r.apply.statusCode, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(SenderNonce(cache, 0xD0), 1U);

    // Effective = min(10, 3) = 3. Gas charged: gasUsed * 3.
    BOOST_CHECK(r.apply.gasUsed > 0);
    BOOST_CHECK_EQUAL(SenderBalance(cache, 0xD0),
                     1'000'000'000ULL - static_cast<uint64_t>(r.apply.gasUsed) * 3);

    // The deployed contract record exists at the derived address.
    evm::CEvmAccount deployed;
    BOOST_REQUIRE(cache.GetAccount(r.apply.deployedAddress, deployed));
    BOOST_CHECK_EQUAL(deployed.nonce, 1U);
}

// ----------------------------------------------------------------------------
// ProcessEvmSpendTx happy path: fixed 21000 gas, value transferred
// to UTXO credit, balance debited.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(spend_success_charges_21000_gas_and_records_utxo)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    const auto ctx = MakeContext(/*baseFee=*/ 2);

    // Seed sender 0xE0 with enough to cover gas + 5 satoshis worth of
    // weis (5 * 10^10 = 5*10^10 weis).
    const uint64_t seedWeis = 1'000'000'000'000ULL; // 100 sat worth
    SeedSender(cache, /*lowByte=*/ 0xE0, seedWeis);

    evm::CEvmSpendTx tx;
    tx.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    tx.fromAddress = SenderHash(0xE0);
    tx.amount = 5 * evm::kWeisPerSatoshi; // 5 satoshis worth
    // Non-empty dest script.
    tx.outputScript = CScript() << OP_DUP << OP_HASH160
                                << std::vector<unsigned char>(20, 0xCD)
                                << OP_EQUALVERIFY << OP_CHECKSIG;
    tx.gasLimit = 21'000;
    tx.maxFeePerGas = 10;
    tx.maxPriorityFeePerGas = 1;
    tx.nonce = 0;

    evm::ProcessResult r = evm::ProcessEvmSpendTx(tx, cache, ctx);
    BOOST_REQUIRE(!r.preflightFailed);
    BOOST_CHECK_EQUAL(r.apply.statusCode, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(r.apply.gasUsed, 21000);

    // UTXO credit recorded.
    BOOST_REQUIRE_EQUAL(r.apply.utxoCredits.size(), 1U);
    BOOST_CHECK_EQUAL(r.apply.utxoCredits[0].amount, 5);

    // Balance debited by spend amount + gas at effective rate (3).
    const uint64_t expectedDebit =
        5ULL * evm::kWeisPerSatoshi + 21000ULL * 3;
    BOOST_CHECK_EQUAL(SenderBalance(cache, 0xE0), seedWeis - expectedDebit);
    BOOST_CHECK_EQUAL(SenderNonce(cache, 0xE0), 1U);

    // Fee split.
    BOOST_CHECK_EQUAL(r.fee.burned, 21000ULL * 2);
    BOOST_CHECK_EQUAL(r.fee.coinbaseTip, 21000ULL * 1);
}

// D2 increment 5 — EIP-1559 base-fee derivation. The committed
// evmBaseFee is recompute-validated against this exact function, so
// pin its spec properties: unchanged at target, rises above (>=+1),
// falls below, floors at 0, saturates at u64 max, max change ~1/8.
BOOST_AUTO_TEST_CASE(eip1559_base_fee_derivation)
{
    using evm::ComputeNextBaseFee;
    const uint64_t L = 30'000'000;       // gas limit
    const uint64_t T = L / 2;            // target (elasticity 2) = 15M
    const uint64_t B = 1'000'000'000ULL; // 1 gwei

    // Exactly at target → unchanged.
    BOOST_CHECK_EQUAL(ComputeNextBaseFee(B, T, L), B);
    // Empty block (used 0 < target) → decreases.
    BOOST_CHECK_LT(ComputeNextBaseFee(B, 0, L), B);
    // Full block (used == limit > target) → increases by max ~1/8.
    const uint64_t up = ComputeNextBaseFee(B, L, L);
    BOOST_CHECK_GT(up, B);
    BOOST_CHECK_EQUAL(up, B + B / 8);     // delta = B*(L-T)/T/8 = B/8
    // Just above target → at least +1 even when the formula rounds
    // to zero.
    BOOST_CHECK_GE(ComputeNextBaseFee(B, T + 1, L), B + 1);
    // Below target by the symmetric amount → -1/8.
    BOOST_CHECK_EQUAL(ComputeNextBaseFee(B, 0, L), B - B / 8);
    // Floors at 0, never negative/underflow.
    BOOST_CHECK_EQUAL(ComputeNextBaseFee(1, 0, L), 1);  // B/8==0 → unchanged-ish
    BOOST_CHECK_EQUAL(ComputeNextBaseFee(0, 0, L), 0);
    // Saturates at u64 max on the way up.
    const uint64_t MAXU = std::numeric_limits<uint64_t>::max();
    BOOST_CHECK_EQUAL(ComputeNextBaseFee(MAXU, L, L), MAXU);
    // Degenerate gas limit → unchanged (guard, no div-by-zero).
    BOOST_CHECK_EQUAL(ComputeNextBaseFee(B, 123, 0), B);
}

BOOST_AUTO_TEST_SUITE_END()
