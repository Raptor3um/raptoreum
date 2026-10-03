// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <evm/apply.h>
#include <evm/connectblock.h>

#include <amount.h>
#include <primitives/transaction.h>
#include <script/script.h>

#include <boost/test/unit_test.hpp>

#include <vector>

/**
 * Phase 2.4 — EVM_SPEND → UTXO credit consensus rule.
 *
 * CheckCoinbaseRealisesSpendCredits is the security boundary that
 * lets a SPEND'd (and EVM-side destroyed) RTM amount reappear as a
 * UTXO without inflating supply or being redirected by the miner.
 * The ConnectBlock rule is: validators recompute the credit set from
 * re-execution, REQUIRE the coinbase to contain each credit
 * (multiset, exact script+amount), and raise the allowed coinbase
 * value by EXACTLY that recomputed sum. These tests pin that
 * function directly so the consensus invariant is regression-locked.
 */

namespace {

CScript Spk(uint8_t tag)
{
    return CScript() << OP_DUP << OP_HASH160
                     << std::vector<uint8_t>(20, tag)
                     << OP_EQUALVERIFY << OP_CHECKSIG;
}

evm::ApplyResult::UtxoCredit Credit(CAmount amount, uint8_t tag)
{
    evm::ApplyResult::UtxoCredit c;
    c.amount = amount;
    c.script = Spk(tag);
    return c;
}

// Build a coinbase-shaped tx with the given outputs.
CTransaction Coinbase(const std::vector<CTxOut>& vout)
{
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vout = vout;
    return CTransaction(mtx);
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(evm_spend_credit_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(no_credits_is_ok_and_zero_total)
{
    const CTransaction cb = Coinbase({CTxOut(50 * COIN, Spk(0x99))});
    CAmount total = -1;
    BOOST_CHECK(evm::CheckCoinbaseRealisesSpendCredits({}, cb, total));
    BOOST_CHECK_EQUAL(total, 0);
}

BOOST_AUTO_TEST_CASE(single_credit_present_sums_to_amount)
{
    const CTransaction cb = Coinbase({
        CTxOut(50 * COIN, Spk(0x99)),    // subsidy-ish, unrelated
        CTxOut(1234, Spk(0x11)),         // the SPEND credit
    });
    CAmount total = -1;
    BOOST_CHECK(evm::CheckCoinbaseRealisesSpendCredits(
        {Credit(1234, 0x11)}, cb, total));
    BOOST_CHECK_EQUAL(total, 1234);
}

BOOST_AUTO_TEST_CASE(multiple_distinct_credits_sum)
{
    const CTransaction cb = Coinbase({
        CTxOut(50 * COIN, Spk(0x99)),
        CTxOut(1000, Spk(0x11)),
        CTxOut(2000, Spk(0x22)),
    });
    CAmount total = -1;
    BOOST_CHECK(evm::CheckCoinbaseRealisesSpendCredits(
        {Credit(1000, 0x11), Credit(2000, 0x22)}, cb, total));
    BOOST_CHECK_EQUAL(total, 3000);
}

BOOST_AUTO_TEST_CASE(containment_ignores_extra_coinbase_outputs)
{
    // The coinbase legitimately also pays subsidy / smartnode /
    // founder; the credit need only be CONTAINED, not equal.
    const CTransaction cb = Coinbase({
        CTxOut(50 * COIN, Spk(0x99)),
        CTxOut(7 * COIN, Spk(0xAA)),
        CTxOut(500, Spk(0x33)),
        CTxOut(3 * COIN, Spk(0xBB)),
    });
    CAmount total = -1;
    BOOST_CHECK(evm::CheckCoinbaseRealisesSpendCredits(
        {Credit(500, 0x33)}, cb, total));
    BOOST_CHECK_EQUAL(total, 500);
}

BOOST_AUTO_TEST_CASE(duplicate_credits_need_distinct_outputs)
{
    // Two identical SPEND credits → require TWO matching outputs.
    const CTransaction one = Coinbase({CTxOut(900, Spk(0x44))});
    CAmount total = -1;
    BOOST_CHECK(!evm::CheckCoinbaseRealisesSpendCredits(
        {Credit(900, 0x44), Credit(900, 0x44)}, one, total));

    const CTransaction two = Coinbase({
        CTxOut(900, Spk(0x44)),
        CTxOut(900, Spk(0x44)),
    });
    total = -1;
    BOOST_CHECK(evm::CheckCoinbaseRealisesSpendCredits(
        {Credit(900, 0x44), Credit(900, 0x44)}, two, total));
    BOOST_CHECK_EQUAL(total, 1800);
}

BOOST_AUTO_TEST_CASE(missing_credit_wrong_script_rejected)
{
    const CTransaction cb = Coinbase({CTxOut(1234, Spk(0x11))});
    CAmount total = -1;
    // Right amount, wrong destination → miner tried to redirect it.
    BOOST_CHECK(!evm::CheckCoinbaseRealisesSpendCredits(
        {Credit(1234, 0x22)}, cb, total));
}

BOOST_AUTO_TEST_CASE(missing_credit_wrong_amount_rejected)
{
    const CTransaction cb = Coinbase({CTxOut(1234, Spk(0x11))});
    CAmount total = -1;
    BOOST_CHECK(!evm::CheckCoinbaseRealisesSpendCredits(
        {Credit(1233, 0x11)}, cb, total));
}

BOOST_AUTO_TEST_CASE(negative_or_out_of_range_amount_rejected)
{
    const CTransaction cb = Coinbase({CTxOut(50 * COIN, Spk(0x11))});
    CAmount total = -1;
    BOOST_CHECK(!evm::CheckCoinbaseRealisesSpendCredits(
        {Credit(-1, 0x11)}, cb, total));
    BOOST_CHECK(!evm::CheckCoinbaseRealisesSpendCredits(
        {Credit(MAX_MONEY + 1, 0x11)}, cb, total));
}

BOOST_AUTO_TEST_CASE(aggregate_out_of_range_rejected)
{
    // Each individually in range, sum overflows MoneyRange.
    const CAmount big = MAX_MONEY - 10;
    const CTransaction cb = Coinbase({
        CTxOut(big, Spk(0x11)),
        CTxOut(big, Spk(0x22)),
    });
    CAmount total = -1;
    BOOST_CHECK(!evm::CheckCoinbaseRealisesSpendCredits(
        {Credit(big, 0x11), Credit(big, 0x22)}, cb, total));
}

BOOST_AUTO_TEST_SUITE_END()
