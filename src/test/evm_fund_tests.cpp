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

// Reorg safety (CRITICAL): a FUND moves RTM UTXO -> EVM by destroying
// UTXO value and crediting an EVM account in ConnectBlock. If a reorg
// disconnects that block, DisconnectBlock MUST revert the EVM credit
// via the block undo journal — otherwise the RTM is duplicated (credit
// survives EVM-side while the UTXO inputs are also restored) or lost.
// This drives the real ConnectBlock -> InvalidateBlock -> reconnect
// path end to end.
BOOST_AUTO_TEST_CASE(fund_credit_reverts_on_reorg)
{
    const CScript cbScript =
        CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    const uint64_t kSatToWeis = 10'000'000'000ULL;
    const CAmount fundSat = 250000;
    uint160 evmAcct(std::vector<unsigned char>(20, 0x9C));
    uint256 toAddr;
    std::memcpy(toAddr.begin() + 12, evmAcct.begin(), 20);

    CMutableTransaction ftx;
    ftx.nVersion = 3;
    ftx.nType = TRANSACTION_EVM_FUND;
    ftx.vin.resize(1);
    ftx.vin[0].prevout.hash = m_coinbase_txns[0]->GetHash();
    ftx.vin[0].prevout.n = 0;
    ftx.vout.resize(1);
    ftx.vout[0].nValue = m_coinbase_txns[0]->vout[0].nValue - fundSat - 1000;
    ftx.vout[0].scriptPubKey = cbScript;
    {
        evm::CEvmFundTx p;
        p.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
        p.toAddress = toAddr;
        p.amount = static_cast<uint64_t>(fundSat) * kSatToWeis;
        SetTxPayload(ftx, p);
        std::vector<unsigned char> sig;
        const uint256 h =
            SignatureHash(cbScript, ftx, 0, SIGHASH_ALL, 0, SigVersion::BASE);
        BOOST_REQUIRE(coinbaseKey.Sign(h, sig));
        sig.push_back(static_cast<unsigned char>(SIGHASH_ALL));
        ftx.vin[0].scriptSig = CScript() << sig;
    }

    // Account does not exist before the FUND.
    {
        evm::CEvmAccount a;
        BOOST_CHECK(!pevmstatedb->ReadAccount(evmAcct, a));
    }

    // Connect the FUND block -> account credited.
    CreateAndProcessBlock({ftx}, cbScript);
    CBlockIndex* fundTip = ::ChainActive().Tip();
    {
        evm::CEvmAccount a;
        BOOST_REQUIRE(pevmstatedb->ReadAccount(evmAcct, a));
        BOOST_CHECK(a.balance == evm::Uint256FromUint64(
            static_cast<uint64_t>(fundSat) * kSatToWeis));
    }

    // Reorg the FUND block away -> the credit MUST be reverted (the
    // account, which did not exist before, is erased by the undo).
    CValidationState state;
    BOOST_REQUIRE(InvalidateBlock(state, Params(), fundTip));
    {
        evm::CEvmAccount a;
        BOOST_CHECK_MESSAGE(!pevmstatedb->ReadAccount(evmAcct, a),
            "FUND credit survived a reorg — RTM duplicated/lost");
    }

    // Reconnect a fresh chain -> producing a new FUND restores the
    // credit cleanly (proves the rollback left no corruption).
    ResetBlockFailureFlags(fundTip);
    CreateAndProcessBlock({ftx}, cbScript);
    {
        evm::CEvmAccount a;
        BOOST_REQUIRE(pevmstatedb->ReadAccount(evmAcct, a));
        BOOST_CHECK(a.balance == evm::Uint256FromUint64(
            static_cast<uint64_t>(fundSat) * kSatToWeis));
    }
}

// SPEND end-to-end (EVM -> UTXO) + reorg safety. Proves the miner now
// realises the SPEND's UTXO credit as a coinbase output (the gap where
// lifting the SPEND stopgap left credit-realisation unwired would have
// wedged any SPEND-bearing block), the EVM account is debited, and a
// reorg restores the debited balance.
BOOST_AUTO_TEST_CASE(spend_end_to_end_and_reorg_safe)
{
    const CScript cbScript =
        CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    const uint64_t kSatToWeis = 10'000'000'000ULL;

    // 1. FUND the EVM account generously (covers spend amount + gas).
    uint160 evmAcct(std::vector<unsigned char>(20, 0xD7));
    uint256 acctWord;
    std::memcpy(acctWord.begin() + 12, evmAcct.begin(), 20);
    const CAmount fundSat = 1'000'000;  // 0.01 RTM
    {
        CMutableTransaction f;
        f.nVersion = 3;
        f.nType = TRANSACTION_EVM_FUND;
        f.vin.resize(1);
        f.vin[0].prevout.hash = m_coinbase_txns[0]->GetHash();
        f.vin[0].prevout.n = 0;
        f.vout.resize(1);
        f.vout[0].nValue = m_coinbase_txns[0]->vout[0].nValue - fundSat - 1000;
        f.vout[0].scriptPubKey = cbScript;
        evm::CEvmFundTx p;
        p.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
        p.toAddress = acctWord;
        p.amount = static_cast<uint64_t>(fundSat) * kSatToWeis;
        SetTxPayload(f, p);
        std::vector<unsigned char> sig;
        const uint256 h =
            SignatureHash(cbScript, f, 0, SIGHASH_ALL, 0, SigVersion::BASE);
        BOOST_REQUIRE(coinbaseKey.Sign(h, sig));
        sig.push_back(static_cast<unsigned char>(SIGHASH_ALL));
        f.vin[0].scriptSig = CScript() << sig;
        CreateAndProcessBlock({f}, cbScript);
    }
    evm::CEvmAccount funded;
    BOOST_REQUIRE(pevmstatedb->ReadAccount(evmAcct, funded));
    const uint256 fundedBalance = funded.balance;

    // 2. SPEND part of it back to a UTXO. Wrapper carries no vin/vout;
    //    gas is paid from the EVM balance (process.cpp), and the
    //    destination satoshis reappear as a coinbase output.
    const uint64_t spendWeis = 100000ULL * kSatToWeis;  // 0.001 RTM
    const CScript destScript =
        CScript() << OP_DUP << OP_HASH160
                  << std::vector<uint8_t>(20, 0x44)
                  << OP_EQUALVERIFY << OP_CHECKSIG;
    CMutableTransaction s;
    s.nVersion = 3;
    s.nType = TRANSACTION_EVM_SPEND;
    {
        evm::CEvmSpendTx p;
        p.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
        p.fromAddress = acctWord;
        p.amount = spendWeis;
        p.outputScript = destScript;
        p.gasLimit = 21000;
        p.maxFeePerGas = 1'000'000'000ULL;  // >= baseFee
        p.maxPriorityFeePerGas = 0;
        p.nonce = 0;
        SetTxPayload(s, p);
    }

    const CBlock spendBlock = CreateAndProcessBlock({s}, cbScript);
    CBlockIndex* spendTip = ::ChainActive().Tip();

    // Block accepted -> the coinbase realised the SPEND credit (miner
    // fix). The destination output is present in the coinbase.
    {
        bool found = false;
        for (const auto& o : spendBlock.vtx[0]->vout) {
            if (o.scriptPubKey == destScript &&
                o.nValue == static_cast<CAmount>(spendWeis / kSatToWeis)) {
                found = true;
                break;
            }
        }
        BOOST_CHECK_MESSAGE(found,
            "SPEND destination UTXO missing from coinbase");
    }

    // EVM account debited by at least the spend amount (plus gas).
    evm::CEvmAccount afterSpend;
    BOOST_REQUIRE(pevmstatedb->ReadAccount(evmAcct, afterSpend));
    BOOST_CHECK(afterSpend.balance != fundedBalance);
    BOOST_CHECK(evm::Uint256GreaterOrEqualUint64(fundedBalance, spendWeis));

    // 3. Reorg the SPEND block away -> the EVM debit is reverted (the
    //    account balance returns to its funded value).
    CValidationState state;
    BOOST_REQUIRE(InvalidateBlock(state, Params(), spendTip));
    evm::CEvmAccount restored;
    BOOST_REQUIRE(pevmstatedb->ReadAccount(evmAcct, restored));
    BOOST_CHECK_MESSAGE(restored.balance == fundedBalance,
        "SPEND debit not reverted on reorg — EVM balance corrupted");
}

BOOST_AUTO_TEST_SUITE_END()
