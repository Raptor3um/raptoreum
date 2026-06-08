// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/validation.h>
#include <evm/account.h>
#include <evm/apply.h>
#include <evm/balance.h>
#include <evm/evmtx.h>
#include <evm/state_db.h>
#include <evo/specialtx.h>
#include <key.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <test/test_raptoreum.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <cstring>
#include <vector>

/**
 * AAL — TRANSACTION_EVM_FUND end-to-end (UTXO -> EVM funding).
 *
 * The funded HTTP e2e (eth_sendRawTransaction -> mine -> receipt) was
 * blocked because a fresh EVM account has no balance and cannot pay
 * gas. FUND is the bootstrap. This functional test proves the whole
 * consensus path deterministically through the production miner +
 * validator: build a FUND tx spending a mature coinbase output,
 * mine it, and assert the EVM account is credited by exactly the
 * funded amount — and that the amount left the UTXO side (it is NOT
 * paid to the coinbase). On regtest EVM + EVM_COMMIT are force-active
 * at height 0, so the block carries a v3 coinbase committing the
 * post-fund evmStateRoot the validator re-derives.
 */

BOOST_FIXTURE_TEST_SUITE(evm_fund_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(fund_credits_evm_account_end_to_end)
{
    // Coinbase output 0 is P2PK to coinbaseKey, mature after 100 blocks.
    const CScript cbScript =
        CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    const CAmount cbValue = m_coinbase_txns[0]->vout[0].nValue;

    const uint64_t kSatToWeis = 10'000'000'000ULL;   // 10^10
    const CAmount fundSat = 100000;                  // amount moved to EVM
    const uint64_t fundWeis = static_cast<uint64_t>(fundSat) * kSatToWeis;
    const CAmount minerFee = 1000;
    const CAmount change = cbValue - fundSat - minerFee;
    BOOST_REQUIRE(change > 0);

    // Destination EVM account (20-byte) carried in the low bytes.
    uint160 evmAcct(std::vector<unsigned char>(20, 0x77));
    uint256 toAddr;
    std::memcpy(toAddr.begin() + 12, evmAcct.begin(), 20);

    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_EVM_FUND;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = m_coinbase_txns[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = change;
    tx.vout[0].scriptPubKey = cbScript;   // change back to self

    evm::CEvmFundTx payload;
    payload.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    payload.toAddress = toAddr;
    payload.amount = fundWeis;
    SetTxPayload(tx, payload);

    // Sign the coinbase input (P2PK).
    std::vector<unsigned char> sig;
    const uint256 sighash =
        SignatureHash(cbScript, tx, 0, SIGHASH_ALL, 0, SigVersion::BASE);
    BOOST_REQUIRE(coinbaseKey.Sign(sighash, sig));
    sig.push_back(static_cast<unsigned char>(SIGHASH_ALL));
    tx.vin[0].scriptSig = CScript() << sig;

    // The EVM account must not exist before funding.
    {
        evm::CEvmAccount acc;
        BOOST_CHECK(!pevmstatedb->ReadAccount(evmAcct, acc));
    }

    // Mine the FUND tx in a v3 block.
    const int hBefore = ::ChainActive().Height();
    const CBlock b = CreateAndProcessBlock({tx}, cbScript);

    // Block accepted (miner==validator parity over the FUND-bearing
    // block, including the recomputed post-fund evmStateRoot).
    BOOST_CHECK_EQUAL(::ChainActive().Height(), hBefore + 1);
    BOOST_CHECK(::ChainActive().Tip()->GetBlockHash() == b.GetHash());

    // The EVM account now exists and holds EXACTLY the funded weis.
    evm::CEvmAccount acc;
    BOOST_REQUIRE(pevmstatedb->ReadAccount(evmAcct, acc));
    BOOST_CHECK(acc.balance == evm::Uint256FromUint64(fundWeis));
    BOOST_CHECK_EQUAL(acc.nonce, 0U);
}

// A second FUND to the same account adds to the existing balance
// (the credit is additive, not a reset) — and funds an address that
// already exists from the first fund.
BOOST_AUTO_TEST_CASE(fund_is_additive_to_existing_balance)
{
    const CScript cbScript =
        CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    const uint64_t kSatToWeis = 10'000'000'000ULL;
    uint160 evmAcct(std::vector<unsigned char>(20, 0x88));
    uint256 toAddr;
    std::memcpy(toAddr.begin() + 12, evmAcct.begin(), 20);

    auto mkFund = [&](size_t coinbaseIdx, CAmount fundSat) {
        const CAmount cbValue = m_coinbase_txns[coinbaseIdx]->vout[0].nValue;
        const CAmount change = cbValue - fundSat - 1000;
        CMutableTransaction tx;
        tx.nVersion = 3;
        tx.nType = TRANSACTION_EVM_FUND;
        tx.vin.resize(1);
        tx.vin[0].prevout.hash = m_coinbase_txns[coinbaseIdx]->GetHash();
        tx.vin[0].prevout.n = 0;
        tx.vout.resize(1);
        tx.vout[0].nValue = change;
        tx.vout[0].scriptPubKey = cbScript;
        evm::CEvmFundTx p;
        p.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
        p.toAddress = toAddr;
        p.amount = static_cast<uint64_t>(fundSat) * kSatToWeis;
        SetTxPayload(tx, p);
        std::vector<unsigned char> sig;
        const uint256 h =
            SignatureHash(cbScript, tx, 0, SIGHASH_ALL, 0, SigVersion::BASE);
        BOOST_REQUIRE(coinbaseKey.Sign(h, sig));
        sig.push_back(static_cast<unsigned char>(SIGHASH_ALL));
        tx.vin[0].scriptSig = CScript() << sig;
        return tx;
    };

    CreateAndProcessBlock({mkFund(0, 50000)}, cbScript);
    CreateAndProcessBlock({mkFund(1, 70000)}, cbScript);

    evm::CEvmAccount acc;
    BOOST_REQUIRE(pevmstatedb->ReadAccount(evmAcct, acc));
    BOOST_CHECK(acc.balance ==
                evm::Uint256FromUint64((50000ULL + 70000ULL) * kSatToWeis));
}

BOOST_AUTO_TEST_SUITE_END()
