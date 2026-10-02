// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <buspool.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <llmq/quorums_instantsend.h>
#include <script/sign.h>
#include <test/test_raptoreum.h>
#include <txdecoupling.h>
#include <txmempool.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

namespace llmq {
struct InstantSendSigningTestAccess {
    static bool CanSign(CInstantSendManager& manager, const CTransaction& tx) {
        return manager.TrySignInputLocks(tx, false, Params().GetConsensus().llmqTypeInstantSend);
    }
    static void Track(CInstantSendManager& manager, const CTransactionRef& tx, const CBlockIndex* block) {
        manager.AddNonLockedTx(tx, block);
    }
};
} // namespace llmq

BOOST_FIXTURE_TEST_SUITE(buspool_tests, BasicTestingSetup)

static CTransactionRef Body(unsigned int tag)
{
    CMutableTransaction tx;
    tx.vin.emplace_back(uint256S("01"), tag);
    tx.vout.emplace_back(1, CScript() << OP_TRUE);
    return MakeTransactionRef(tx);
}

BOOST_AUTO_TEST_CASE(retention_bounds_and_shared_bodies)
{
    LOCK(cs_main);
    CTxMemPool pool;
    CBusPoolManager manager(pool, 2, 64 * 1024);
    const auto first = Body(1), second = Body(2), third = Body(3);
    const auto updates = pool.GetTransactionsUpdated();
    BOOST_REQUIRE(manager.RetainTransaction(first));
    BOOST_CHECK_GT(pool.GetTransactionsUpdated(), updates);
    const auto pinned = manager.GetTransaction(first->GetHash());
    const size_t one = manager.GetMemoryUsage();
    BOOST_CHECK_GT(one, first->GetTotalSize());
    BOOST_REQUIRE(manager.RetainTransaction(second));
    BOOST_CHECK_EQUAL(manager.GetCount(), 2U);
    BOOST_CHECK_EQUAL(manager.GetMemoryUsage(), 2 * one);
    const auto beforeDuplicate = pool.GetTransactionsUpdated();
    BOOST_REQUIRE(manager.RetainTransaction(first));
    BOOST_CHECK_EQUAL(pool.GetTransactionsUpdated(), beforeDuplicate);
    BOOST_CHECK_EQUAL(manager.GetCount(), 2U);
    BOOST_REQUIRE(manager.RetainTransaction(third));
    BOOST_CHECK(!manager.GetTransaction(first->GetHash()));
    BOOST_CHECK(manager.GetTransaction(second->GetHash()));
    BOOST_CHECK(manager.GetTransaction(third->GetHash()));
    BOOST_CHECK(pinned == first);
    BOOST_CHECK_EQUAL(manager.GetCount(), 2U);
    BOOST_CHECK_EQUAL(manager.GetMemoryUsage(), 2 * one);

    CBusPoolManager bytes(pool, 10, one);
    BOOST_REQUIRE(bytes.RetainTransaction(first));
    BOOST_REQUIRE(bytes.RetainTransaction(second));
    BOOST_CHECK_EQUAL(bytes.GetCount(), 1U);
    BOOST_CHECK_EQUAL(bytes.GetMemoryUsage(), one);
    BOOST_CHECK(!bytes.GetTransaction(first->GetHash()));
    CMutableTransaction large(*third);
    large.vin.front().scriptSig.resize(4096);
    BOOST_CHECK(!bytes.RetainTransaction(MakeTransactionRef(large)));
    CMutableTransaction payload(*third);
    payload.nVersion = 3;
    payload.nType = TRANSACTION_FUTURE;
    payload.vExtraPayload.resize(4096);
    BOOST_CHECK(!bytes.RetainTransaction(MakeTransactionRef(payload)));
    BOOST_CHECK(bytes.GetTransaction(second->GetHash()));
    BOOST_CHECK_EQUAL(bytes.GetCount(), 1U);

    CBusPoolManager leases(pool, 1, 64 * 1024);
    auto leased = Body(7);
    std::weak_ptr<const CTransaction> weak = leased;
    const auto leasedId = leased->GetHash();
    BOOST_REQUIRE(leases.RetainTransaction(leased));
    auto held = leases.GetTransaction(leasedId);
    leased.reset();
    BOOST_REQUIRE(leases.RetainTransaction(Body(8)));
    BOOST_CHECK(!leases.GetTransaction(leasedId));
    BOOST_CHECK(!weak.expired());
    BOOST_CHECK(held->GetHash() == leasedId);
    held.reset();
    BOOST_CHECK(weak.expired());
}

BOOST_AUTO_TEST_CASE(configuration_and_provenance)
{
    LOCK(cs_main);
    CTxMemPool pool;
    BOOST_CHECK_THROW(CBusPoolManager(pool, 0, 1024), std::invalid_argument);
    BOOST_CHECK_THROW(CBusPoolManager(pool, 100001, 1024), std::invalid_argument);
    BOOST_CHECK_THROW(CBusPoolManager(pool, 1, 0), std::invalid_argument);
    BOOST_CHECK_THROW(CBusPoolManager(pool, 1, 1024ULL * 1024 * 1024 + 1), std::invalid_argument);

    CTxMemPoolEntry entry(Body(1), 1000, 0, 0, 1, false, 0, LockPoints());
    BOOST_CHECK(entry.AreScriptsLocallyValidated());
    entry.SetScriptsLocallyValidated(false);
    BOOST_CHECK(!entry.AreScriptsLocallyValidated());
    entry.SetScriptsLocallyValidated(true);
    BOOST_CHECK(entry.AreScriptsLocallyValidated());
}

BOOST_FIXTURE_TEST_CASE(signer_checks_scripts_after_ordinary_dry_run, TestChain100Setup)
{
    LOCK(cs_main);
    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.vin.emplace_back(m_coinbase_txns[0]->GetHash(), 0);
    const CScript script = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    tx.vout.emplace_back(11 * CENT, script);
    CValidationState dryRun;
    BOOST_REQUIRE(AcceptToMemoryPool(*m_node.mempool, dryRun, MakeTransactionRef(tx), nullptr, true, 0, true));
    CValidationState missingSignature;
    BOOST_CHECK(!CheckTxForCertificate(CTransaction(tx), missingSignature, TX_VALIDATION_POLICY_FLAGS_V1));
    BOOST_CHECK_EQUAL(missingSignature.GetRejectReason(), "txcert-script-failed");

    std::vector<unsigned char> signature;
    BOOST_REQUIRE(coinbaseKey.Sign(SignatureHash(script, tx, 0, SIGHASH_ALL, 0, SigVersion::BASE), signature));
    signature.push_back(SIGHASH_ALL);
    tx.vin[0].scriptSig << signature;
    CValidationState valid;
    BOOST_CHECK(CheckTxForCertificate(CTransaction(tx), valid, TX_VALIDATION_POLICY_FLAGS_V1));
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 0U);
}

BOOST_FIXTURE_TEST_CASE(instant_send_requires_local_or_active_chain_context, TestChain100Setup)
{
    LOCK(cs_main);
    auto& consensus = const_cast<Consensus::Params&>(Params().GetConsensus());
    struct RestoreContext {
        Consensus::Params& consensus;
        const int height;
        CBlockIndex* tip;
        ~RestoreContext() { consensus.nTxDecouplingHeight = height; ChainActive().SetTip(tip); }
    } restore{consensus, consensus.nTxDecouplingHeight, ChainActive().Tip()};
    consensus.nTxDecouplingHeight = 1;
    auto& pool = *m_node.mempool;
    llmq::CInstantSendManager manager(pool, *m_node.connman, true, true);
    const auto tx = Body(30);
    const auto canSign = [&] { return llmq::InstantSendSigningTestAccess::CanSign(manager, *tx); };
    BOOST_CHECK(!canSign());
    CTxMemPoolEntry entry(tx, 1000, 0, 0, 1, false, 0, LockPoints());
    entry.SetScriptsLocallyValidated(false);
    pool.addUnchecked(entry);
    BOOST_CHECK(!canSign());
    {
        LOCK(pool.cs);
        pool.mapTx.modify(pool.mapTx.find(tx->GetHash()), [](CTxMemPoolEntry& item) {
            item.SetScriptsLocallyValidated(true);
        });
    }
    BOOST_CHECK(canSign());
    pool.removeRecursive(*tx, MemPoolRemovalReason::MANUAL);
    llmq::InstantSendSigningTestAccess::Track(manager, tx, restore.tip);
    BOOST_CHECK(canSign());
    ChainActive().SetTip(restore.tip->pprev);
    BOOST_CHECK(!canSign());
    ChainActive().SetTip(restore.tip);
    BOOST_CHECK(canSign());
    consensus.nTxDecouplingHeight = -1;
    llmq::InstantSendSigningTestAccess::Track(manager, tx, nullptr);
    BOOST_CHECK(canSign());
}

BOOST_AUTO_TEST_SUITE_END()
