// Copyright (c) 2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation.h>
#include <txmempool.h>
#include <amount.h>
#include <consensus/validation.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/sign.h>
#include <script/standard.h>
#include <test/test_raptoreum.h>

#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_SUITE(txvalidation_tests)

//! Regression test for TxNeedsAssetsCache (validation.cpp), the predicate
//! AcceptToMemoryPoolWorker uses to decide whether a transaction is worth the
//! O(confirmed-assets) cost of copying the global asset cache. It must stay in
//! exact agreement with CheckSpecialTx's switch in evo/specialtx.cpp: true only
//! for the three asset transaction types, false for everything else, including
//! non-asset special types (provider/coinbase/quorum-commitment/future) whose
//! checkers don't take an assetsCache parameter at all.
BOOST_AUTO_TEST_CASE(tx_needs_assets_cache_predicate)
{
    auto tx = [](int nVersion, uint16_t nType) {
        CMutableTransaction mtx;
        mtx.nVersion = nVersion;
        mtx.nType = nType;
        return CTransaction(mtx);
    };

    BOOST_CHECK(!TxNeedsAssetsCache(tx(1, TRANSACTION_NORMAL)));
    BOOST_CHECK(!TxNeedsAssetsCache(tx(3, TRANSACTION_NORMAL)));
    BOOST_CHECK(!TxNeedsAssetsCache(tx(3, TRANSACTION_PROVIDER_REGISTER)));
    BOOST_CHECK(!TxNeedsAssetsCache(tx(3, TRANSACTION_COINBASE)));
    BOOST_CHECK(!TxNeedsAssetsCache(tx(3, TRANSACTION_QUORUM_COMMITMENT)));
    BOOST_CHECK(!TxNeedsAssetsCache(tx(3, TRANSACTION_FUTURE)));

    BOOST_CHECK(TxNeedsAssetsCache(tx(3, TRANSACTION_NEW_ASSET)));
    BOOST_CHECK(TxNeedsAssetsCache(tx(3, TRANSACTION_UPDATE_ASSET)));
    BOOST_CHECK(TxNeedsAssetsCache(tx(3, TRANSACTION_MINT_ASSET)));

    // An old-version transaction whose nType field happens to collide with an
    // asset type's numeric value must not be read as an asset transaction --
    // nVersion == 3 gates the whole nType field's meaning.
    BOOST_CHECK(!TxNeedsAssetsCache(tx(2, TRANSACTION_NEW_ASSET)));
}

/**
 * Ensure that the mempool won't accept coinbase transactions.
 */
BOOST_FIXTURE_TEST_CASE(tx_mempool_reject_coinbase, TestChain100Setup
)
{
CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
CMutableTransaction coinbaseTx;

coinbaseTx.
nVersion = 1;
coinbaseTx.vin.resize(1);
coinbaseTx.vout.resize(1);
coinbaseTx.vin[0].
scriptSig = CScript() << OP_11 << OP_EQUAL;
coinbaseTx.vout[0].
nValue = 1 * CENT;
coinbaseTx.vout[0].
scriptPubKey = scriptPubKey;

assert(CTransaction(coinbaseTx)
.

IsCoinBase()

);

CValidationState state;

LOCK(cs_main);

unsigned int initialPoolSize = m_node.mempool->size();

BOOST_CHECK_EQUAL(
false,
AcceptToMemoryPool(*m_node
.mempool, state,
MakeTransactionRef(coinbaseTx),
        nullptr /* pfMissingInputs */,
true /* bypass_limits */,
0 /* nAbsurdFee */));

// Check that the transaction hasn't been added to mempool.
BOOST_CHECK_EQUAL(m_node
.mempool->

size(), initialPoolSize

);

// Check that the validation state reflects the unsuccessful attempt.
BOOST_CHECK(state
.

IsInvalid()

);
BOOST_CHECK_EQUAL(state
.

GetRejectReason(),

"coinbase");

int nDoS;
BOOST_CHECK_EQUAL(state
.
IsInvalid(nDoS),
true);
BOOST_CHECK_EQUAL(nDoS,
100);
}

//! No test in this codebase exercises AcceptToMemoryPool's fDryRun=true path: its one
//! caller is CCoinJoin::IsCollateralValid (coinjoin/coinjoin.cpp), reached only from live
//! CoinJoin session/collateral traffic, which nothing in test/functional/rpc_coinjoin.py
//! triggers. Cover the ordinary-payment case directly here: it must still take the
//! "no assets cache needed" branch correctly and leave the mempool untouched.
BOOST_FIXTURE_TEST_CASE(tx_mempool_dry_run_ordinary_tx, TestChain100Setup)
{
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

    CMutableTransaction spend;
    spend.nVersion = 1;
    spend.vin.resize(1);
    spend.vin[0].prevout.hash = m_coinbase_txns[0]->GetHash();
    spend.vin[0].prevout.n = 0;
    spend.vout.resize(1);
    spend.vout[0].nValue = 11 * CENT;
    spend.vout[0].scriptPubKey = scriptPubKey;

    std::vector<unsigned char> vchSig;
    uint256 hash = SignatureHash(scriptPubKey, spend, 0, SIGHASH_ALL, 0, SigVersion::BASE);
    BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    spend.vin[0].scriptSig << vchSig;

    LOCK(cs_main);
    unsigned int initialPoolSize = m_node.mempool->size();

    CValidationState state;
    BOOST_CHECK(AcceptToMemoryPool(*m_node.mempool, state, MakeTransactionRef(spend),
                                   nullptr /* pfMissingInputs */, true /* bypass_limits */,
                                   0 /* nAbsurdFee */, true /* fDryRun */));

    // A dry run reports whether the transaction would be accepted but never adds it. The
    // dry run alone can't tell a genuinely-valid tx from one that merely didn't fail before
    // its (skipped) script check -- confirm it for real, with the same transaction, right
    // after: that only works if the dry run above left no state behind that would make this
    // fail (double-spend detection, coins_to_uncache, etc).
    BOOST_CHECK_EQUAL(m_node.mempool->size(), initialPoolSize);

    CValidationState state2;
    BOOST_CHECK(AcceptToMemoryPool(*m_node.mempool, state2, MakeTransactionRef(spend),
                                   nullptr /* pfMissingInputs */, true /* bypass_limits */,
                                   0 /* nAbsurdFee */, false /* fDryRun */));
    BOOST_CHECK_EQUAL(m_node.mempool->size(), initialPoolSize + 1);
}

BOOST_AUTO_TEST_SUITE_END()
