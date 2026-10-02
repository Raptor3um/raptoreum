// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <buspool.h>
#include <chain.h>
#include <coins.h>
#include <consensus/merkle.h>
#include <hash.h>
#include <consensus/consensus.h>
#include <evo/evodb.h>
#include <llmq/quorums_commitment.h>
#include <miner.h>
#include <pow.h>
#include <streams.h>
#include <txmempool.h>
#include <update/update.h>
#include <txdecoupling.h>
#include <validation.h>
#include <compat/endian.h>
#include <limits>
#include <map>
#include <set>
#include <chainparams.h>
#include <consensus/validation.h>
#include <evo/cbtx.h>
#include <evo/specialtx.h>
#include <test/test_raptoreum.h>
#include <util/system.h>

#include <boost/test/unit_test.hpp>

namespace {
struct DecouplingSetup : BasicTestingSetup {
    DecouplingSetup() : BasicTestingSetup(CBaseChainParams::REGTEST)
    {
        gArgs.ForceSetArg("-txdecoupling", "1");
        gArgs.ForceSetArg("-txdecouplingheight", "10");
        SelectParams(CBaseChainParams::REGTEST);
    }
    ~DecouplingSetup()
    {
        gArgs.ForceRemoveArg("-txdecoupling");
        gArgs.ForceRemoveArg("-txdecouplingheight");
        SelectParams(CBaseChainParams::REGTEST);
    }
};

CMutableTransaction CoinbasePayload(uint16_t version, int height)
{
    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_COINBASE;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vout.emplace_back(0, CScript() << OP_TRUE);
    CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
    payload << version << int32_t(height) << uint256() << uint256();
    if (version == 0x8001) WriteCompactSize(payload, 0);
    tx.vExtraPayload.assign(payload.begin(), payload.end());
    return tx;
}
}

BOOST_FIXTURE_TEST_SUITE(txdecoupling_tests, DecouplingSetup)

BOOST_AUTO_TEST_CASE(experimental_coinbase_activation)
{
    CBlockIndex parent;
    parent.nHeight = 8;
    CValidationState before;
    BOOST_CHECK(!CheckCbTx(CTransaction(CoinbasePayload(0x8001, 9)), &parent, before));

    parent.nHeight = 9;
    CValidationState active;
    BOOST_CHECK_MESSAGE(CheckCbTx(CTransaction(CoinbasePayload(0x8001, 10)), &parent, active),
                        active.GetRejectReason());
    CValidationState ordinary;
    BOOST_CHECK(CheckCbTx(CTransaction(CoinbasePayload(2, 10)), &parent, ordinary));
    CValidationState unassigned;
    BOOST_CHECK(!CheckCbTx(CTransaction(CoinbasePayload(3, 10)), &parent, unassigned));
}


BOOST_AUTO_TEST_CASE(experimental_options_and_manifest_limits)
{
    // Provider validation queries other networks without selecting them.
    BOOST_CHECK_NO_THROW(CreateChainParams(CBaseChainParams::MAIN));
    BOOST_CHECK_NO_THROW(CreateChainParams(CBaseChainParams::TESTNET));
    BOOST_CHECK_THROW(SelectParams(CBaseChainParams::MAIN), std::runtime_error);
    BOOST_CHECK_THROW(SelectParams(CBaseChainParams::TESTNET), std::runtime_error);
    BOOST_CHECK_EQUAL(Params().NetworkIDString(), CBaseChainParams::REGTEST);
    gArgs.ForceSetArg("-txdecouplingheight", "2147483648");
    BOOST_CHECK_THROW(CreateChainParams(CBaseChainParams::REGTEST), std::runtime_error);
    gArgs.ForceSetArg("-txdecouplingheight", "-1");
    BOOST_CHECK_THROW(CreateChainParams(CBaseChainParams::REGTEST), std::runtime_error);
    gArgs.ForceSetArg("-txdecouplingheight", "10");
    gArgs.ForceSetArg("-txdecoupling", "0");
    BOOST_CHECK_THROW(CreateChainParams(CBaseChainParams::REGTEST), std::runtime_error);
    gArgs.ForceSetArg("-txdecoupling", "1");
    gArgs.ForceSetArg("-prune", "550");
    BOOST_CHECK_THROW(CreateChainParams(CBaseChainParams::REGTEST), std::runtime_error);
    gArgs.ForceRemoveArg("-prune");

    CCbTx payload;
    payload.nVersion = CCbTx::TX_CERTIFICATE_VERSION;
    payload.nHeight = 10;
    payload.txCertificates.resize(CCbTx::MAX_CERTIFICATES);
    for (size_t i = 0; i < payload.txCertificates.size(); ++i) payload.txCertificates[i].index = i + 1;
    CMutableTransaction tx = CoinbasePayload(2, 10);
    SetTxPayload(tx, payload);
    BOOST_CHECK_LE(tx.vExtraPayload.size(), MAX_TX_EXTRA_PAYLOAD);
    CCbTx decoded;
    BOOST_REQUIRE(GetTxPayload(tx, decoded));
    BOOST_CHECK_EQUAL(decoded.txCertificates.size(), payload.txCertificates.size());
    CBlockIndex parent;
    parent.nHeight = 9;
    CValidationState good;
    BOOST_CHECK(CheckCbTx(CTransaction(tx), &parent, good));
    const auto reject = [&](CCbTx modified) {
        SetTxPayload(tx, modified);
        CValidationState state;
        BOOST_CHECK(!CheckCbTx(CTransaction(tx), &parent, state));
    };
    auto modified = payload;
    modified.txCertificates[0].index = 0;
    reject(modified);
    modified = payload;
    modified.txCertificates[1].index = 1;
    reject(modified);
    modified = payload;
    std::swap(modified.txCertificates[0], modified.txCertificates[1]);
    reject(modified);
    modified = payload;
    modified.txCertificates[0].certificate.result = CTxValidationCertificate::NEGATIVE;
    reject(modified);
    modified = payload;
    modified.txCertificates.emplace_back();
    BOOST_CHECK_THROW(SetTxPayload(tx, modified), std::ios_base::failure);
    tx = CoinbasePayload(0x8001, 10);
    tx.vExtraPayload.back() = 42;
    BOOST_CHECK(!GetTxPayload(tx, decoded));
    tx.vExtraPayload.back() = 253;
    tx.vExtraPayload.push_back(255);
    tx.vExtraPayload.push_back(255);
    BOOST_CHECK(!GetTxPayload(tx, decoded));

    gArgs.ForceRemoveArg("-txdecouplingheight");
    gArgs.ForceSetArg("-prune", "550");
    BOOST_CHECK_NO_THROW(CreateChainParams(CBaseChainParams::REGTEST));
    gArgs.ForceRemoveArg("-prune");
    SelectParams(CBaseChainParams::REGTEST);
    CValidationState disabled;
    BOOST_CHECK(!CheckCbTx(CTransaction(CoinbasePayload(0x8001, 10)), &parent, disabled));
    gArgs.ForceRemoveArg("-txdecoupling");
    SelectParams(CBaseChainParams::MAIN);
    CValidationState publicNetwork;
    BOOST_CHECK(!CheckCbTx(CTransaction(CoinbasePayload(0x8001, 10)), &parent, publicNetwork));
}

BOOST_AUTO_TEST_CASE(successor_flags_predict_activation_without_temporary_indexes)
{
    LOCK(cs_main);
    Updates().Add(Update(EUpdate::DEPLOYMENT_V17, "v17", 0, 10, 0, 1, 10, 1, false,
                         VoteThreshold(95, 95, 5), VoteThreshold(0, 0, 1), false, 10));
    CBlockIndex parent;
    parent.nHeight = 8;
    parent.nTime = 1614369600;
    uint32_t flags = 0;
    BOOST_REQUIRE(GetTxValidationNextScriptFlags(&parent, Params().GetConsensus(), flags));
    BOOST_CHECK_EQUAL(flags & SCRIPT_ENABLE_DIP0020_OPCODES, 0U);
    parent.nHeight = 9;
    BOOST_REQUIRE(GetTxValidationNextScriptFlags(&parent, Params().GetConsensus(), flags));
    BOOST_CHECK(flags & SCRIPT_ENABLE_DIP0020_OPCODES);
    parent.nHeight = 8;
    BOOST_REQUIRE(GetTxValidationNextScriptFlags(&parent, Params().GetConsensus(), flags));
    BOOST_CHECK_EQUAL(flags & SCRIPT_ENABLE_DIP0020_OPCODES, 0U);
    // Successors of a parent with median time 1333238399 are all at or after the BIP16 switch.
    parent.nTime = 1333238399;
    BOOST_CHECK(GetTxValidationNextScriptFlags(&parent, Params().GetConsensus(), flags));
    parent.nTime = 1333238398;
    BOOST_CHECK(!GetTxValidationNextScriptFlags(&parent, Params().GetConsensus(), flags));
}

BOOST_AUTO_TEST_CASE(inactive_manifest_rejected_before_certificate_decoding)
{
    CCbTx payload;
    payload.nVersion = CCbTx::TX_CERTIFICATE_VERSION;
    payload.nHeight = 9;
    payload.txCertificates.resize(1);
    payload.txCertificates[0].index = 1;
    CMutableTransaction tx = CoinbasePayload(2, 9);
    SetTxPayload(tx, payload);
    // The last bytes are the certificate signature; these are not a BLS point.
    std::fill(tx.vExtraPayload.end() - CBLSSignature::SerSize, tx.vExtraPayload.end(), 0xff);
    CCbTx decoded;
    BOOST_REQUIRE(!GetTxPayload(tx, decoded));
    CBlockIndex parent;
    parent.nHeight = 8;
    CValidationState inactive;
    BOOST_CHECK(!CheckCbTx(CTransaction(tx), &parent, inactive));
    BOOST_CHECK_EQUAL(inactive.GetRejectReason(), "bad-cbtx-version");
    // The active rule still decodes every certificate.
    parent.nHeight = 9;
    CValidationState active;
    BOOST_CHECK(!CheckCbTx(CTransaction(tx), &parent, active));
    BOOST_CHECK_EQUAL(active.GetRejectReason(), "bad-cbtx-payload");
    tx.vExtraPayload.assign(1, 0x01);
    CValidationState truncated;
    BOOST_CHECK(!CheckCbTx(CTransaction(tx), &parent, truncated));
    BOOST_CHECK_EQUAL(truncated.GetRejectReason(), "bad-cbtx-payload");
}

BOOST_AUTO_TEST_CASE(relayed_proof_context_checked_before_signature)
{
    const uint256 tipHash = uint256S("02");
    CBlockIndex tip;
    tip.nHeight = 20;
    tip.phashBlock = &tipHash;
    CBLSSecretKey key;
    key.MakeNewKey();
    CTxValidationCertificate cert;
    cert.txid = uint256S("01");
    cert.parentHash = tipHash;
    cert.sig = key.Sign(uint256S("03"));
    size_t unread = 0;
    const auto read = [&](const CTxValidationCertificate& proof, bool malformed) {
        CDataStream wire(SER_NETWORK, PROTOCOL_VERSION);
        wire << proof;
        BOOST_REQUIRE_EQUAL(wire.size(), CTxValidationCertificate::V1_SIZE);
        if (malformed) std::fill(wire.end() - CBLSSignature::SerSize, wire.end(), char(0xff));
        const uint256 identity = Hash(wire.begin(), wire.end());
        CTxValidationCertificate decoded;
        const auto result = ReadRelayedProof(wire, identity, &tip, Params().GetConsensus(), decoded);
        unread = wire.size();
        return result;
    };
    BOOST_CHECK(read(cert, false) == RelayedProofRead::CURRENT);
    BOOST_CHECK_EQUAL(unread, 0U);
    // The handler penalizes this result, as it does other proof format failures.
    BOOST_CHECK(read(cert, true) == RelayedProofRead::MALFORMED);
    // A stale parent or an inactive rule returns with the signature still unread.
    auto stale = cert;
    stale.parentHash = uint256S("04");
    BOOST_CHECK(read(stale, true) == RelayedProofRead::STALE);
    BOOST_CHECK_EQUAL(unread, size_t{CBLSSignature::SerSize});
    tip.nHeight = 8;
    BOOST_CHECK(read(cert, true) == RelayedProofRead::STALE);
    BOOST_CHECK_EQUAL(unread, size_t{CBLSSignature::SerSize});
}

BOOST_AUTO_TEST_SUITE_END()

namespace {
void EnableCertificates()
{
    gArgs.ForceSetArg("-txdecoupling", "1");
    gArgs.ForceSetArg("-txdecouplingheight", "10");
    SelectParams(CBaseChainParams::REGTEST);
}

void DisableCertificates()
{
    gArgs.ForceRemoveArg("-txdecoupling");
    gArgs.ForceRemoveArg("-txdecouplingheight");
    SelectParams(CBaseChainParams::REGTEST);
}

// Public database fixture only: this does not simulate or replace the DKG functional scenario.
void StoreCommitment(const CBlockIndex* base, const CBlockIndex* mined, const CBLSSecretKey& key,
                     Consensus::LLMQType type = Consensus::LLMQ_5_60)
{
    llmq::CFinalCommitment commitment(Params().GetConsensus().llmqs.at(type), base->GetBlockHash());
    commitment.signers.assign(commitment.signers.size(), true);
    commitment.validMembers.assign(commitment.validMembers.size(), true);
    commitment.quorumPublicKey = key.GetPublicKey();
    evoDb->Write(std::make_pair(std::string("q_mc"), std::make_pair(type, base->GetBlockHash())),
                 std::make_pair(commitment, mined->GetBlockHash()));
    evoDb->Write(std::make_tuple(std::string("q_mcih"), type,
                  htobe32(std::numeric_limits<uint32_t>::max() - mined->nHeight)), base->nHeight);
}

CTxValidationCertificate Certify(const CTransaction& tx, const CCoinsViewCache& view,
                                const CBlockIndex* parent, const std::map<uint256, CBLSSecretKey>& keys)
{
    CTxValidationCertificate cert;
    cert.txid = tx.GetHash();
    cert.parentHash = parent->GetBlockHash();
    cert.policyFlags = TX_VALIDATION_POLICY_FLAGS_V1;
    CValidationState state;
    BOOST_REQUIRE(GetTxValidationNextScriptFlags(parent, Params().GetConsensus(), cert.consensusFlags));
    BOOST_REQUIRE(GetTxValidationPrevoutsDigest(tx, view, parent, cert.prevoutsDigest, state));
    CBLSPublicKey publicKey;
    BOOST_REQUIRE_MESSAGE(SelectTxValidationQuorum(parent, GetTxValidationRequestId(cert, Params().GetConsensus().hashGenesisBlock),
                                                   cert.quorumHash, publicKey, state), state.GetRejectReason());
    cert.sig = keys.at(cert.quorumHash).Sign(GetTxValidationSignHash(cert, Params().GetConsensus().hashGenesisBlock));
    return cert;
}

struct StatementSetup : RegTestingSetup {
    std::vector<CBlockIndex*> chain;
    std::map<uint256, CBLSSecretKey> keys;
    StatementSetup()
    {
        LOCK(cs_main);
        EnableCertificates();
        Updates().Add(Update(EUpdate::DEPLOYMENT_V17, "v17", 0, 10, 0, 1, 10, 1, false,
                             VoteThreshold(95, 95, 5), VoteThreshold(0, 0, 1), false, 10));
        chain.push_back(ChainActive().Genesis());
        for (int height = 1; height <= 100; ++height) {
            CBlockHeader header;
            header.nTime = chain.back()->nTime + 120;
            header.hashPrevBlock = chain.back()->GetBlockHash();
            auto index = m_node.chainman->m_blockman.InsertBlockIndex(header.GetHash());
            index->pprev = chain.back();
            index->nHeight = height;
            index->nTime = header.nTime;
            index->BuildSkip();
            chain.push_back(index);
        }
        for (const int height : {24, 48, 72, 96}) {
            CBLSSecretKey key;
            key.MakeNewKey();
            keys.emplace(chain[height]->GetBlockHash(), key);
            StoreCommitment(chain[height], chain[height + (height == 96 ? 1 : 6)], key);
        }
        evoDb->WriteBestBlock(chain.back()->GetBlockHash());
    }
    ~StatementSetup() { DisableCertificates(); }
};
}

BOOST_FIXTURE_TEST_CASE(txcertificate_signed_fields_prevouts_and_public_history, StatementSetup)
{
    LOCK(cs_main);
    const auto parent = chain.back();
    CCoinsView base;
    CCoinsViewCache view(&base);
    view.SetBestBlock(parent->GetBlockHash());
    const COutPoint input(uint256S("01"), 0);
    const Coin coin(CTxOut(COIN, CScript() << OP_FALSE), 50, false, TRANSACTION_NORMAL, {});
    view.AddCoin(input, Coin(coin), false);
    CMutableTransaction tx;
    tx.vin.emplace_back(input);
    tx.vout.emplace_back(COIN / 2, CScript() << OP_TRUE);
    const auto cert = Certify(CTransaction(tx), view, parent, keys);
    CValidationState valid;
    BOOST_REQUIRE(CheckTxValidationCertificate(cert, CTransaction(tx), view, parent, Params().GetConsensus(), valid));
    BOOST_CHECK(cert.quorumHash == chain[48]->GetBlockHash() || cert.quorumHash == chain[72]->GetBlockHash());
    const auto reject = [&](const CTxValidationCertificate& changed) {
        CValidationState state;
        BOOST_CHECK(!CheckTxValidationCertificate(changed, CTransaction(tx), view, parent, Params().GetConsensus(), state));
    };
    auto changed = cert;
    ++changed.version; reject(changed);
    changed = cert; changed.txid = uint256S("02"); reject(changed);
    changed = cert; changed.parentHash = chain[99]->GetBlockHash(); reject(changed);
    changed = cert; changed.prevoutsDigest = uint256(); reject(changed);
    changed = cert; changed.consensusFlags ^= SCRIPT_VERIFY_P2SH; reject(changed);
    changed = cert; changed.policyFlags ^= SCRIPT_VERIFY_LOW_S; reject(changed);
    changed = cert; changed.quorumType = Consensus::LLMQ_TEST_V17; reject(changed);
    changed = cert; changed.quorumHash = chain[24]->GetBlockHash();
    changed.sig = keys.at(changed.quorumHash).Sign(GetTxValidationSignHash(changed, Params().GetConsensus().hashGenesisBlock));
    reject(changed);
    changed = cert;
    changed.quorumHash = cert.quorumHash == chain[48]->GetBlockHash()
        ? chain[72]->GetBlockHash() : chain[48]->GetBlockHash();
    changed.sig = keys.at(changed.quorumHash).Sign(GetTxValidationSignHash(changed, Params().GetConsensus().hashGenesisBlock));
    reject(changed);
    changed = cert; changed.sig = CBLSSignature(); reject(changed);
    changed = cert; changed.result = CTxValidationCertificate::NEGATIVE;
    const auto genesis = Params().GetConsensus().hashGenesisBlock;
    BOOST_CHECK(GetTxValidationRequestId(cert, genesis) == GetTxValidationRequestId(changed, genesis));
    BOOST_CHECK(GetTxValidationMessageHash(cert) != GetTxValidationMessageHash(changed));
    changed.sig = keys.at(changed.quorumHash).Sign(GetTxValidationSignHash(changed, genesis));
    reject(changed);
    CValidationState diagnostic;
    BOOST_CHECK(CheckTxValidationStatement(changed, CTransaction(tx), view, parent, Params().GetConsensus(),
                                           CTxValidationCertificate::NEGATIVE, diagnostic));
    auto otherNetwork = Params().GetConsensus();
    otherNetwork.hashGenesisBlock = uint256S("04");
    CValidationState separated;
    BOOST_CHECK(!CheckTxValidationCertificate(cert, CTransaction(tx), view, parent, otherNetwork, separated));
    for (int field = 0; field < 6; ++field) {
        Coin changedCoin(coin);
        if (field == 0) changedCoin.out.nValue++;
        if (field == 1) changedCoin.out.scriptPubKey = CScript() << OP_TRUE;
        if (field == 2) changedCoin.nHeight++;
        if (field == 3) changedCoin.fCoinBase = true;
        if (field == 4) changedCoin.nType = TRANSACTION_PROVIDER_REGISTER;
        if (field == 5) changedCoin.vExtraPayload.push_back(1);
        view.AddCoin(input, std::move(changedCoin), true);
        reject(cert);
    }
    view.AddCoin(input, Coin(coin), true);
    evoDb->WriteBestBlock(chain[99]->GetBlockHash());
    CValidationState unrelated;
    BOOST_CHECK(!CheckTxValidationCertificate(cert, CTransaction(tx), view, parent, Params().GetConsensus(), unrelated));
    BOOST_CHECK(unrelated.IsError());
    evoDb->WriteBestBlock(parent->GetBlockHash());
    StoreCommitment(chain[72], chain[97], keys.at(chain[72]->GetBlockHash()));
    CValidationState wrongMined;
    BOOST_CHECK(!CheckTxValidationCertificate(cert, CTransaction(tx), view, parent, Params().GetConsensus(), wrongMined));
    BOOST_CHECK(wrongMined.IsError());
}

BOOST_FIXTURE_TEST_CASE(txcertificate_eligibility_and_manifest_bind_the_block, StatementSetup)
{
    LOCK(cs_main);
    const auto parent = chain.back();
    CCoinsView base;
    CCoinsViewCache view(&base);
    view.SetBestBlock(parent->GetBlockHash());
    const COutPoint input(uint256S("01"), 0);
    const Coin coin(CTxOut(COIN, CScript() << OP_FALSE), 50, false, TRANSACTION_NORMAL, {});
    view.AddCoin(input, Coin(coin), false);
    CMutableTransaction tx;
    tx.vin.emplace_back(input);
    tx.vout.emplace_back(COIN / 2, CScript() << OP_TRUE);
    const auto cert = Certify(CTransaction(tx), view, parent, keys);
    CBlock block;
    auto coinbase = CoinbasePayload(0x8001, 101);
    CCbTx payload;
    BOOST_REQUIRE(GetTxPayload(coinbase, payload));
    payload.txCertificates.push_back({1, cert});
    SetTxPayload(coinbase, payload);
    block.vtx = {MakeTransactionRef(coinbase), MakeTransactionRef(tx)};
    std::set<uint16_t> certified;
    CValidationState good;
    BOOST_REQUIRE(CheckBlockTxCertificates(block, view, parent, Params().GetConsensus(), cert.consensusFlags, certified, good));
    BOOST_CHECK(certified == std::set<uint16_t>{1});
    CValidationState wrongFlags;
    BOOST_CHECK(!CheckBlockTxCertificates(block, view, parent, Params().GetConsensus(), cert.consensusFlags ^ SCRIPT_VERIFY_P2SH,
                                          certified, wrongFlags));
    payload.txCertificates[0].index = 2;
    SetTxPayload(coinbase, payload);
    block.vtx[0] = MakeTransactionRef(coinbase);
    CValidationState outside;
    BOOST_CHECK(!CheckBlockTxCertificates(block, view, parent, Params().GetConsensus(), cert.consensusFlags, certified, outside));

    for (int kind = 0; kind < 5; ++kind) {
        Coin changedCoin(coin);
        CMutableTransaction candidate = tx;
        if (kind == 0) changedCoin.nHeight = parent->nHeight + 1;
        if (kind == 1) changedCoin.nType = TRANSACTION_FUTURE;
        if (kind == 2) candidate.nType = TRANSACTION_FUTURE;
        CScript asset;
        asset.assign(32, OP_0);
        asset[25] = OP_ASSET_ID;
        asset[27] = 'r';
        asset[28] = 't';
        asset[29] = 'm';
        if (kind == 3) changedCoin.out.scriptPubKey = asset;
        if (kind == 4) candidate.vout[0].scriptPubKey = asset;
        view.AddCoin(input, std::move(changedCoin), true);
        uint256 digest;
        CValidationState state;
        BOOST_CHECK(!GetTxValidationPrevoutsDigest(CTransaction(candidate), view, parent, digest, state));
    }
    view.SpendCoin(input);
    uint256 digest;
    CValidationState spent;
    BOOST_CHECK(!GetTxValidationPrevoutsDigest(CTransaction(tx), view, parent, digest, spent));
}

BOOST_FIXTURE_TEST_CASE(txcertificate_equal_height_branches, StatementSetup)
{
    LOCK(cs_main);
    std::vector<CBlockIndex*> fork(chain.begin(), chain.begin() + 70);
    for (int height = 70; height <= 100; ++height) {
        CBlockHeader header;
        header.nTime = fork.back()->nTime + 120;
        header.nNonce = 1;
        header.hashPrevBlock = fork.back()->GetBlockHash();
        auto index = m_node.chainman->m_blockman.InsertBlockIndex(header.GetHash());
        index->pprev = fork.back();
        index->nHeight = height;
        index->nTime = header.nTime;
        index->BuildSkip();
        fork.push_back(index);
    }
    uint256 selected;
    CBLSPublicKey publicKey;
    CValidationState unavailable;
    BOOST_CHECK(!SelectTxValidationQuorum(fork.back(), uint256S("01"), selected, publicKey, unavailable));
    BOOST_CHECK(unavailable.IsError());
    CBLSSecretKey forkKey;
    forkKey.MakeNewKey();
    StoreCommitment(fork[72], fork[78], forkKey);
    evoDb->WriteBestBlock(fork.back()->GetBlockHash());
    CValidationState oldBranch;
    BOOST_CHECK(!SelectTxValidationQuorum(chain.back(), uint256S("01"), selected, publicKey, oldBranch));
    BOOST_CHECK(oldBranch.IsError());
    bool selectedFork = false;
    for (int i = 0; i < 32; ++i) {
        CValidationState state;
        const auto request = (CHashWriter(SER_GETHASH, 0) << i).GetHash();
        BOOST_REQUIRE(SelectTxValidationQuorum(fork.back(), request, selected, publicKey, state));
        BOOST_CHECK(selected == fork[72]->GetBlockHash() || selected == chain[48]->GetBlockHash());
        selectedFork |= selected == fork[72]->GetBlockHash();
    }
    BOOST_CHECK(selectedFork);
    // A mined commitment from the other branch must never be accepted at this anchor.
    StoreCommitment(fork[72], chain[78], forkKey);
    CValidationState wrongAncestry;
    BOOST_CHECK(!SelectTxValidationQuorum(fork.back(), uint256S("01"), selected, publicKey, wrongAncestry));
    BOOST_CHECK(wrongAncestry.IsError());
}

namespace {
struct CertificateBlockSetup : TestChain100Setup {
    std::map<uint256, CBLSSecretKey> keys;
    CertificateBlockSetup()
    {
        LOCK(cs_main);
        EnableCertificates();
        CBLSSecretKey key;
        key.MakeNewKey();
        keys.emplace(ChainActive()[24]->GetBlockHash(), key);
        auto dbTx = evoDb->BeginTransaction();
        StoreCommitment(ChainActive()[24], ChainActive()[30], key);
        // Template checks roll back the current transaction; public history must already be committed.
        dbTx->Commit();
    }
    ~CertificateBlockSetup() { DisableCertificates(); }

    CBlock CertifiedBlock(const std::vector<CMutableTransaction>& txs)
    {
        CBlock block = CreateBlock(txs, CScript() << OP_TRUE);
        CCbTx payload;
        BOOST_REQUIRE(GetTxPayload(*block.vtx[0], payload));
        payload.nVersion = CCbTx::TX_CERTIFICATE_VERSION;
        for (size_t i = block.vtx.size() - txs.size(); i < block.vtx.size(); ++i) {
            payload.txCertificates.push_back({uint16_t(i), Certify(*block.vtx[i], ChainstateActive().CoinsTip(),
                                                                   ChainActive().Tip(), keys)});
        }
        CMutableTransaction coinbase(*block.vtx[0]);
        SetTxPayload(coinbase, payload);
        block.vtx[0] = MakeTransactionRef(coinbase);
        block.hashMerkleRoot = BlockMerkleRoot(block);
        block.fChecked = false;
        return block;
    }
};
}

BOOST_FIXTURE_TEST_CASE(txcertificate_v17_round_transition, CertificateBlockSetup)
{
    LOCK(cs_main);
    Updates().Add(Update(EUpdate::DEPLOYMENT_V17, "v17", 0, 10, 0, 1, 9, 1, false,
                         VoteThreshold(0, 0, 1), VoteThreshold(0, 0, 1)));
    uint32_t flags;
    BOOST_REQUIRE(GetTxValidationNextScriptFlags(ChainActive()[18], Params().GetConsensus(), flags));
    BOOST_CHECK_EQUAL(flags & SCRIPT_ENABLE_DIP0020_OPCODES, 0U);
    BOOST_REQUIRE(GetTxValidationNextScriptFlags(ChainActive()[19], Params().GetConsensus(), flags));
    BOOST_CHECK(flags & SCRIPT_ENABLE_DIP0020_OPCODES);
    BOOST_REQUIRE(GetTxValidationNextScriptFlags(ChainActive()[29], Params().GetConsensus(), flags));
    BOOST_CHECK(flags & SCRIPT_ENABLE_DIP0020_OPCODES);
    BOOST_REQUIRE(GetTxValidationNextScriptFlags(ChainActive()[18], Params().GetConsensus(), flags));
    BOOST_CHECK_EQUAL(flags & SCRIPT_ENABLE_DIP0020_OPCODES, 0U);
}

BOOST_FIXTURE_TEST_CASE(txcertificate_block_preserves_non_script_rules, CertificateBlockSetup)
{
    LOCK(cs_main);
    CMutableTransaction payment;
    payment.nVersion = 3;
    payment.vin.emplace_back(m_coinbase_txns[0]->GetHash(), 0);
    const CAmount inputValue = ChainstateActive().CoinsTip().AccessCoin(payment.vin[0].prevout).out.nValue;
    payment.vout.emplace_back(inputValue - 1000, CScript() << OP_TRUE);
    // The input deliberately has no ECDSA signature; only the committed certificate authorizes omission.
    const CBlock certified = CertifiedBlock({payment});
    CValidationState accepted;
    BOOST_REQUIRE_MESSAGE(TestBlockValidity(accepted, Params(), certified, ChainActive().Tip(), false, true),
                          accepted.GetRejectReason());
    CBlock ordinary = certified;
    CCbTx payload;
    BOOST_REQUIRE(GetTxPayload(*ordinary.vtx[0], payload));
    payload.nVersion = 2;
    payload.txCertificates.clear();
    CMutableTransaction coinbase(*ordinary.vtx[0]);
    SetTxPayload(coinbase, payload);
    ordinary.vtx[0] = MakeTransactionRef(coinbase);
    ordinary.hashMerkleRoot = BlockMerkleRoot(ordinary);
    ordinary.fChecked = false;
    // Script failures surface as the check queue's generic reason, or directly without it.
    const auto scriptFailure = [](const CValidationState& state) {
        const std::string reason = state.GetRejectReason();
        return reason == "block-validation-failed" || reason.find("mandatory-script-verify-flag-failed") == 0;
    };
    CValidationState uncached;
    BOOST_CHECK(!TestBlockValidity(uncached, Params(), ordinary, ChainActive().Tip(), false, true));
    BOOST_CHECK_MESSAGE(scriptFailure(uncached), uncached.GetRejectReason());

    // A manifest authorizes only its own positions: an uncertified transaction in the
    // same block still executes scripts. Its only input is unspendable (OP_FALSE).
    CMutableTransaction locked = payment;
    locked.vout[0].scriptPubKey = CScript() << OP_FALSE;
    CMutableTransaction uncertified;
    uncertified.nVersion = 3;
    uncertified.vin.emplace_back(locked.GetHash(), 0);
    uncertified.vout.emplace_back(locked.vout[0].nValue - 1000, CScript() << OP_TRUE);
    CBlock mixed = CertifiedBlock({locked});
    mixed.vtx.push_back(MakeTransactionRef(uncertified));
    mixed.hashMerkleRoot = BlockMerkleRoot(mixed);
    mixed.fChecked = false;
    CValidationState mixedState;
    BOOST_CHECK(!TestBlockValidity(mixedState, Params(), mixed, ChainActive().Tip(), false, true));
    BOOST_CHECK_MESSAGE(scriptFailure(mixedState), mixedState.GetRejectReason());
    // Control: the certified position alone is valid, so the uncertified script caused the failure.
    CBlock lockedOnly = CertifiedBlock({locked});
    CValidationState lockedState;
    BOOST_CHECK_MESSAGE(TestBlockValidity(lockedState, Params(), lockedOnly, ChainActive().Tip(), false, true),
                        lockedState.GetRejectReason());

    const auto reject = [&](const std::vector<CMutableTransaction>& txs, const std::string& reason) {
        CBlock block = CertifiedBlock(txs);
        CValidationState state;
        BOOST_CHECK(!TestBlockValidity(state, Params(), block, ChainActive().Tip(), false, true));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), reason);
    };
    CMutableTransaction overspend = payment;
    overspend.vout[0].nValue = inputValue + 1;
    reject({overspend}, "bad-txns-in-belowout");
    CMutableTransaction immature = payment;
    immature.vin[0].prevout.hash = m_coinbase_txns.back()->GetHash();
    immature.vout[0].nValue = 1;
    reject({immature}, "bad-txns-premature-spend-of-coinbase");
    CMutableTransaction sequence = payment;
    sequence.vin[0].nSequence = 1000;
    reject({sequence}, "bad-txns-nonfinal");
    CMutableTransaction conflict = payment;
    conflict.vout[0].nValue--;
    reject({payment, conflict}, "bad-txns-inputs-missingorspent");
    CMutableTransaction sigops = payment;
    sigops.vout[0].scriptPubKey = CScript();
    sigops.vout[0].scriptPubKey.assign(50001, OP_CHECKSIG);
    reject({sigops}, "bad-blk-sigops");

    CBlock corrupted = certified;
    BOOST_REQUIRE(GetTxPayload(*corrupted.vtx[0], payload));
    payload.txCertificates[0].certificate.sig = CBLSSignature();
    coinbase = CMutableTransaction(*corrupted.vtx[0]);
    SetTxPayload(coinbase, payload);
    corrupted.vtx[0] = MakeTransactionRef(coinbase);
    corrupted.hashMerkleRoot = BlockMerkleRoot(corrupted);
    corrupted.fChecked = false;
    CValidationState invalidProof;
    BOOST_CHECK(!TestBlockValidity(invalidProof, Params(), corrupted, ChainActive().Tip(), false, true));
    BOOST_CHECK_EQUAL(invalidProof.GetRejectReason(), "bad-txcert-signature");
    // The manifest is committed by the block hash, so the block carries a full DoS score.
    int score = 0;
    BOOST_CHECK(invalidProof.IsInvalid(score));
    BOOST_CHECK_EQUAL(score, 100);

    DisableCertificates();
    CValidationState inactive;
    BOOST_CHECK(!TestBlockValidity(inactive, Params(), certified, ChainActive().Tip(), false, true));
    BOOST_CHECK_EQUAL(inactive.GetRejectReason(), "bad-cbtx-version");
}

BOOST_FIXTURE_TEST_CASE(txcertificate_mining_certificate_capacity, CertificateBlockSetup)
{
    BOOST_REQUIRE(CCbTx::MAX_CERTIFICATES == 41);
    const size_t fillerCount = CCbTx::MAX_CERTIFICATES - 1;
    const CScript script = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    const auto sign = [&](CMutableTransaction& tx) {
        for (size_t i = 0; i < tx.vin.size(); ++i) {
            std::vector<unsigned char> signature;
            BOOST_REQUIRE(coinbaseKey.Sign(SignatureHash(script, tx, i, SIGHASH_ALL, 0, SigVersion::BASE), signature));
            signature.push_back(SIGHASH_ALL);
            tx.vin[i].scriptSig << signature;
        }
    };

    // Confirm independent outputs: v1 certificates cannot authorize unconfirmed inputs.
    CMutableTransaction funding;
    funding.nVersion = 3;
    funding.vin.emplace_back(m_coinbase_txns[0]->GetHash(), 0);
    CAmount value;
    {
        LOCK(cs_main);
        value = (ChainstateActive().CoinsTip().AccessCoin(funding.vin[0].prevout).out.nValue - 1000) /
                (fillerCount + 5);
    }
    BOOST_REQUIRE_GT(value, 100000);
    funding.vout.assign(fillerCount + 5, CTxOut(value, script));
    sign(funding);
    const CBlock fundingBlock = CreateAndProcessBlock({funding}, script);

    LOCK(cs_main);
    BOOST_REQUIRE(ChainActive().Tip()->GetBlockHash() == fundingBlock.GetHash());
    auto& pool = *m_node.mempool;
    struct RestoreBusPool {
        std::shared_ptr<CBusPoolManager> previous;
        ~RestoreBusPool() { busPoolManager = std::move(previous); }
    } restore{busPoolManager};
    busPoolManager = std::make_shared<CBusPoolManager>(pool);

    const auto add = [&](const CTransactionRef& tx, bool local, bool certified) {
        CTxValidationCertificate certificate;
        if (certified) certificate = Certify(*tx, ChainstateActive().CoinsTip(), ChainActive().Tip(), keys);
        CValidationState admitted;
        // Bypass fee limits to retain zero-fee parents for CPFP.
        BOOST_REQUIRE_MESSAGE(AcceptToMemoryPool(pool, admitted, tx, nullptr, true, 0, false,
                                                 local ? nullptr : &certificate), admitted.GetRejectReason());
        if (certified) {
            CValidationState retained;
            BOOST_REQUIRE_MESSAGE(busPoolManager->SubmitTransaction(tx, certificate, retained), retained.GetRejectReason());
        }
        LOCK(pool.cs);
        BOOST_REQUIRE(pool.mapTx.find(tx->GetHash()) != pool.mapTx.end());
        BOOST_CHECK_EQUAL(pool.mapTx.find(tx->GetHash())->AreScriptsLocallyValidated(), local);
    };
    unsigned int output = 0;
    const auto parent = [&](CAmount fee, bool local, bool certified) {
        CMutableTransaction tx;
        tx.nVersion = 3;
        tx.vin.emplace_back(funding.GetHash(), output++);
        tx.vout.emplace_back(value - fee, script);
        if (local) sign(tx); // Remote parents deliberately lack an ECDSA signature.
        const auto result = MakeTransactionRef(tx);
        add(result, local, certified);
        return result;
    };
    const auto child = [&](const std::vector<CTransactionRef>& parents, CAmount fee) {
        CMutableTransaction tx;
        tx.nVersion = 3;
        CAmount inputs = 0;
        for (const auto& txParent : parents) {
            tx.vin.emplace_back(txParent->GetHash(), 0);
            inputs += txParent->vout[0].nValue;
        }
        tx.vout.emplace_back(inputs - fee, script);
        sign(tx);
        const auto result = MakeTransactionRef(tx);
        add(result, true, false);
        return result;
    };

    std::vector<CTransactionRef> fillers;
    for (size_t i = 0; i < fillerCount; ++i) fillers.push_back(parent(100000, false, true));
    const auto twoA = parent(0, false, true);
    const auto twoB = parent(0, false, true);
    const auto twoChild = child({twoA, twoB}, 60000);
    const auto oneParent = parent(0, false, true);
    const auto oneChild = child({oneParent}, 10000);
    const auto localCertificate = parent(1000, true, true);
    const auto ordinary = parent(500, true, false);
    BOOST_CHECK_EQUAL(output, funding.vout.size());

    BlockAssembler::Options options;
    options.blockMinFeeRate = CFeeRate(1000);
    const auto checkTemplate = [&](bool includeTwo) {
        // Body fee rates order fillers, the two-parent package, the one-parent
        // package, then the two local candidates. Manifest bytes remain overhead.
        const auto result = BlockAssembler(pool, Params(), options).CreateNewBlock(script, true);
        const auto& block = result->block; // CreateNewBlock also runs TestBlockValidity.
        CCbTx payload;
        BOOST_REQUIRE(GetTxPayload(*block.vtx[0], payload));
        BOOST_CHECK(payload.nVersion == CCbTx::TX_CERTIFICATE_VERSION);
        BOOST_REQUIRE_EQUAL(payload.txCertificates.size(), 41U);
        BOOST_CHECK_EQUAL(block.vtx[0]->vExtraPayload.size(), 9829U); // 70 + 1 + 41 * 238.
        BOOST_CHECK_LE(block.vtx[0]->vExtraPayload.size(), MAX_TX_EXTRA_PAYLOAD);

        std::set<uint256> expectedCertificates;
        for (const auto& tx : fillers) expectedCertificates.insert(tx->GetHash());
        if (includeTwo) {
            expectedCertificates.insert(twoA->GetHash());
            expectedCertificates.insert(twoB->GetHash());
        } else {
            expectedCertificates.insert(oneParent->GetHash());
        }
        auto expected = expectedCertificates;
        const auto& includedChild = includeTwo ? twoChild : oneChild;
        expected.insert(includedChild->GetHash());
        expected.insert(localCertificate->GetHash());
        expected.insert(ordinary->GetHash());
        std::set<uint256> actual;
        std::map<uint256, size_t> positions;
        for (size_t i = 1; i < block.vtx.size(); ++i) {
            positions.emplace(block.vtx[i]->GetHash(), i);
            if (block.vtx[i]->nType != TRANSACTION_QUORUM_COMMITMENT) actual.insert(block.vtx[i]->GetHash());
        }
        BOOST_REQUIRE(actual == expected);
        std::set<uint256> actualCertificates;
        uint16_t lastIndex = 0;
        for (const auto& entry : payload.txCertificates) {
            BOOST_REQUIRE_LT(entry.index, block.vtx.size());
            BOOST_CHECK_GT(entry.index, lastIndex);
            BOOST_CHECK(entry.certificate.txid == block.vtx[entry.index]->GetHash());
            BOOST_CHECK(entry.certificate.parentHash == block.hashPrevBlock);
            actualCertificates.insert(entry.certificate.txid);
            lastIndex = entry.index;
        }
        BOOST_CHECK(actualCertificates == expectedCertificates);
        if (includeTwo) {
            BOOST_CHECK_LT(positions.at(twoA->GetHash()), positions.at(twoChild->GetHash()));
            BOOST_CHECK_LT(positions.at(twoB->GetHash()), positions.at(twoChild->GetHash()));
            BOOST_CHECK_EQUAL(positions.at(twoA->GetHash()) < positions.at(twoB->GetHash()),
                              twoA->GetHash() < twoB->GetHash());
        } else {
            BOOST_CHECK_LT(positions.at(oneParent->GetHash()), positions.at(oneChild->GetHash()));
        }
        BOOST_CHECK_GT(positions.at(localCertificate->GetHash()), positions.at(includedChild->GetHash()));
        BOOST_CHECK_GT(positions.at(ordinary->GetHash()), positions.at(includedChild->GetHash()));
    };

    // One free certificate slot rejects the two-parent package atomically,
    // admits the one-parent package and still admits both local candidates.
    checkTemplate(false);
    pool.removeRecursive(*fillers.back(), MemPoolRemovalReason::MANUAL);
    fillers.pop_back();
    // Two free slots now admit both remote parents before their local child.
    // The lower-fee remote package must be omitted once all 41 slots are used.
    checkTemplate(true);

    const size_t candidates = pool.size();
    const auto ordinaryTemplate = BlockAssembler(pool, Params(), options).CreateNewBlock(script, false);
    CCbTx ordinaryPayload;
    BOOST_REQUIRE(GetTxPayload(*ordinaryTemplate->block.vtx[0], ordinaryPayload));
    BOOST_CHECK(ordinaryPayload.nVersion == CCbTx::CURRENT_VERSION);
    BOOST_CHECK(ordinaryPayload.txCertificates.empty());
    std::set<uint256> ordinaryIDs;
    for (const auto& tx : ordinaryTemplate->block.vtx) {
        if (!tx->IsCoinBase() && tx->nType != TRANSACTION_QUORUM_COMMITMENT) ordinaryIDs.insert(tx->GetHash());
    }
    const std::set<uint256> expectedOrdinary{localCertificate->GetHash(), ordinary->GetHash()};
    BOOST_CHECK(ordinaryIDs == expectedOrdinary);
    BOOST_CHECK_EQUAL(pool.size(), candidates);
    LOCK(pool.cs);
    for (const auto& entry : pool.mapTx) {
        const bool local = entry.GetTx().GetHash() == twoChild->GetHash() ||
                           entry.GetTx().GetHash() == oneChild->GetHash() ||
                           expectedOrdinary.count(entry.GetTx().GetHash()) != 0;
        BOOST_CHECK_EQUAL(entry.AreScriptsLocallyValidated(), local);
    }
}

// Successor activation is cached per parent hash. Two branches at the same height
// with different miner votes must never share a result.
BOOST_FIXTURE_TEST_CASE(txcertificate_successor_activation_is_per_branch, TestChain100Setup)
{
    const Update update(EUpdate::DEPLOYMENT_V17, "v17", 0, 10, 100, 1, 9, 1, false,
                        VoteThreshold(100, 100, 1), VoteThreshold(0, 0, 1));
    {
        LOCK(cs_main);
        Updates().Add(update);
    }
    const CScript script = CScript() << OP_TRUE;
    const auto mine = [&](bool signal) {
        CBlock block = CreateBlock({}, script);
        block.nVersion = signal ? (block.nVersion | update.BitMask()) : (block.nVersion & ~update.BitMask());
        while (!CheckProofOfWork(block.GetPOWHash(), block.nBits, Params().GetConsensus())) ++block.nNonce;
        BOOST_REQUIRE(g_chainman.ProcessNewBlock(Params(), std::make_shared<const CBlock>(block), true, nullptr));
        LOCK(cs_main);
        BOOST_REQUIRE(ChainActive().Tip()->GetBlockHash() == block.GetHash());
        return ChainActive().Tip();
    };
    std::vector<const CBlockIndex*> signalling, silent;
    for (int i = 0; i < 40; ++i) signalling.push_back(mine(true));
    {
        CValidationState state;
        BOOST_REQUIRE(InvalidateBlock(state, Params(), const_cast<CBlockIndex*>(signalling.front())));
    }
    for (int i = 0; i < 40; ++i) silent.push_back(mine(false));

    LOCK(cs_main);
    bool differed = false;
    for (size_t i = 0; i < signalling.size(); ++i) {
        BOOST_REQUIRE_EQUAL(signalling[i]->nHeight, silent[i]->nHeight);
        std::vector<bool> results;
        for (const CBlockIndex* parent : {signalling[i], silent[i]}) {
            // Oracle: a fresh manager without any cached state from either branch.
            CBlockIndex next;
            next.pprev = const_cast<CBlockIndex*>(parent);
            next.nHeight = parent->nHeight + 1;
            next.BuildSkip();
            UpdateManager fresh;
            fresh.Add(update);
            const bool expected = fresh.IsActive(EUpdate::DEPLOYMENT_V17, &next);
            BOOST_CHECK_EQUAL(Updates().IsActiveForNextBlock(EUpdate::DEPLOYMENT_V17, parent), expected);
            results.push_back(expected);
        }
        differed |= results[0] != results[1];
    }
    BOOST_CHECK(differed);
}

namespace {
// Delivers a tip notification as the validation interface would.
struct BusPoolTipAccess : CBusPoolManager {
    using CBusPoolManager::CBusPoolManager;
    using CBusPoolManager::SynchronousUpdatedBlockTip;
};
}

// A started node's first notification can report the loaded tip unchanged, for
// example when a new block is rejected during connection.
BOOST_FIXTURE_TEST_CASE(txcertificate_unchanged_loaded_tip_keeps_certificates, CertificateBlockSetup)
{
    LOCK(cs_main);
    // The buspool is constructed before the chain is loaded and started afterwards.
    BusPoolTipAccess manager(*m_node.mempool);
    manager.InitializeCurrentBlockTip();
    CMutableTransaction payment;
    payment.nVersion = 3;
    payment.vin.emplace_back(m_coinbase_txns[0]->GetHash(), 0);
    const CAmount inputValue = ChainstateActive().CoinsTip().AccessCoin(payment.vin[0].prevout).out.nValue;
    payment.vout.emplace_back(inputValue - 100000, CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG);
    // No ECDSA signature: only the remote certificate admits this candidate.
    const auto tx = MakeTransactionRef(payment);
    const auto certificate = Certify(*tx, ChainstateActive().CoinsTip(), ChainActive().Tip(), keys);
    CValidationState state;
    BOOST_REQUIRE_MESSAGE(manager.SubmitTransaction(tx, certificate, state), state.GetRejectReason());
    manager.SynchronousUpdatedBlockTip(ChainActive().Tip(), nullptr, false);
    CTxValidationCertificate retained;
    BOOST_CHECK(manager.GetCertificate(tx->GetHash(), ChainActive().Tip(), retained));
    BOOST_CHECK(SerializeHash(retained) == SerializeHash(certificate));
    BOOST_CHECK(m_node.mempool->exists(tx->GetHash()));
}

struct UpdateManagerTestAccess {
    static uint64_t RoundsWalked(const UpdateManager& manager) { return manager.roundsWalked; }
    static void EvictParents(UpdateManager& manager) { manager.nextBlockActive.clear(); }
    static void EvictAll(UpdateManager& manager)
    {
        manager.nextBlockActive.clear();
        manager.nextBlockFinal.clear();
    }
};

// Once successor activation is final on a branch, a new parent must not walk the
// round history again. Results still match a fresh manager on both branches.
BOOST_FIXTURE_TEST_CASE(txcertificate_successor_activation_work_stops_at_finality, TestChain100Setup)
{
    // Voting ends at 120: signalling activates by 130, silence fails at 130.
    const Update update(EUpdate::DEPLOYMENT_V17, "v17", 0, 10, 100, 1, 3, 1, false,
                        VoteThreshold(100, 100, 1), VoteThreshold(0, 0, 1));
    {
        LOCK(cs_main);
        Updates().Add(update);
    }
    const CScript script = CScript() << OP_TRUE;
    const auto mine = [&](bool signal) {
        CBlock block = CreateBlock({}, script);
        block.nVersion = signal ? (block.nVersion | update.BitMask()) : (block.nVersion & ~update.BitMask());
        while (!CheckProofOfWork(block.GetPOWHash(), block.nBits, Params().GetConsensus())) ++block.nNonce;
        BOOST_REQUIRE(g_chainman.ProcessNewBlock(Params(), std::make_shared<const CBlock>(block), true, nullptr));
        LOCK(cs_main);
        BOOST_REQUIRE(ChainActive().Tip()->GetBlockHash() == block.GetHash());
        return ChainActive().Tip();
    };
    std::vector<const CBlockIndex*> signalling, silent;
    for (int i = 0; i < 45; ++i) signalling.push_back(mine(true));
    {
        CValidationState state;
        BOOST_REQUIRE(InvalidateBlock(state, Params(), const_cast<CBlockIndex*>(signalling.front())));
    }
    for (int i = 0; i < 45; ++i) silent.push_back(mine(false));

    LOCK(cs_main);
    const auto oracle = [&](const CBlockIndex* parent) {
        CBlockIndex next;
        next.pprev = const_cast<CBlockIndex*>(parent);
        next.nHeight = parent->nHeight + 1;
        next.BuildSkip();
        UpdateManager fresh;
        fresh.Add(update);
        return fresh.IsActive(EUpdate::DEPLOYMENT_V17, &next);
    };
    const auto check = [&](const std::vector<const CBlockIndex*>& branch) {
        uint64_t settled = 0;
        for (const CBlockIndex* parent : branch) {
            BOOST_CHECK_EQUAL(Updates().IsActiveForNextBlock(EUpdate::DEPLOYMENT_V17, parent), oracle(parent));
            if (parent->nHeight == 134) settled = UpdateManagerTestAccess::RoundsWalked(Updates());
        }
        // Each later parent is a new hash, yet none walks a round.
        BOOST_CHECK_EQUAL(UpdateManagerTestAccess::RoundsWalked(Updates()), settled);
    };
    BOOST_REQUIRE_EQUAL(signalling.back()->nHeight, 145);
    BOOST_REQUIRE(oracle(signalling.back()) && !oracle(silent.back()));
    check(signalling);
    check(silent);
    UpdateManagerTestAccess::EvictParents(Updates());
    check(signalling);
    check(silent);
    UpdateManagerTestAccess::EvictAll(Updates());
    check(silent);
    check(signalling);
}
