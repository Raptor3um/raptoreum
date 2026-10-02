// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <blockencodings.h>
#include <decoupledblock.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/tx_check.h>
#include <consensus/validation.h>
#include <evo/specialtx.h>
#include <llmq/quorums_commitment.h>
#include <chainparams.h>
#include <pow.h>
#include <random.h>

#include <test/test_raptoreum.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <map>

std::vector <std::pair<uint256, CTransactionRef>> extra_txn;

BOOST_FIXTURE_TEST_SUITE(blockencodings_tests, RegTestingSetup
)

static CBlock BuildBlockTestCase() {
    CBlock block;
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig.resize(10);
    tx.vout.resize(1);
    tx.vout[0].nValue = 42;

    block.vtx.resize(3);
    block.vtx[0] = MakeTransactionRef(tx);
    block.nVersion = 42;
    block.hashPrevBlock = InsecureRand256();
    block.nBits = 0x207fffff;

    tx.vin[0].prevout.hash = InsecureRand256();
    tx.vin[0].prevout.n = 0;
    block.vtx[1] = MakeTransactionRef(tx);

    tx.vin.resize(10);
    for (size_t i = 0; i < tx.vin.size(); i++) {
        tx.vin[i].prevout.hash = InsecureRand256();
        tx.vin[i].prevout.n = 0;
    }
    block.vtx[2] = MakeTransactionRef(tx);

    bool mutated;
    block.hashMerkleRoot = BlockMerkleRoot(block, &mutated);
    assert(!mutated);
    while (!CheckProofOfWork(block.GetPOWHash(), block.nBits, Params().GetConsensus())) ++block.nNonce;
    return block;
}

// Number of shared use_counts we expect for a tx we haven't touched
// (block + mempool + our copy from the GetSharedTx call)
constexpr long SHARED_TX_OFFSET{3};

BOOST_AUTO_TEST_CASE(SimpleRoundTripTest)
        {
                CTxMemPool pool;
        TestMemPoolEntryHelper entry;
        CBlock block(BuildBlockTestCase());

        LOCK2(cs_main, pool.cs);
        pool.addUnchecked(entry.FromTx(block.vtx[2]));
        BOOST_CHECK_EQUAL(pool.mapTx.find(block.vtx[2]->GetHash())->GetSharedTx().use_count(), SHARED_TX_OFFSET + 0);

        // Do a simple ShortTxIDs RT
        {
            CBlockHeaderAndShortTxIDs shortIDs(block);

            CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
            stream << shortIDs;

            CBlockHeaderAndShortTxIDs shortIDs2;
            stream >> shortIDs2;

            PartiallyDownloadedBlock partialBlock(&pool);
            BOOST_CHECK(partialBlock.InitData(shortIDs2, extra_txn) == READ_STATUS_OK);
            BOOST_CHECK(partialBlock.IsTxAvailable(0));
            BOOST_CHECK(!partialBlock.IsTxAvailable(1));
            BOOST_CHECK(partialBlock.IsTxAvailable(2));

            BOOST_CHECK_EQUAL(pool.mapTx.find(block.vtx[2]->GetHash())->GetSharedTx().use_count(),
                              SHARED_TX_OFFSET + 1);

            size_t poolSize = pool.size();
            pool.removeRecursive(*block.vtx[2], MemPoolRemovalReason::MANUAL);
            BOOST_CHECK_EQUAL(pool.size(), poolSize - 1);

            CBlock block2;
            {
                PartiallyDownloadedBlock tmp = partialBlock;
                BOOST_CHECK(partialBlock.FillBlock(block2, {}) == READ_STATUS_INVALID); // No transactions
                partialBlock = tmp;
            }

            // Wrong transaction
            {
                PartiallyDownloadedBlock tmp = partialBlock;
                partialBlock.FillBlock(block2,
                                       {block.vtx[2]}); // Current implementation doesn't check txn here, but don't require that
                partialBlock = tmp;
            }
            bool mutated;
            BOOST_CHECK(block.hashMerkleRoot != BlockMerkleRoot(block2, &mutated));

            CBlock block3;
            BOOST_CHECK(partialBlock.FillBlock(block3, {block.vtx[1]}) == READ_STATUS_OK);
            BOOST_CHECK_EQUAL(block.GetHash().ToString(), block3.GetHash().ToString());
            BOOST_CHECK_EQUAL(block.hashMerkleRoot.ToString(), BlockMerkleRoot(block3, &mutated).ToString());
            BOOST_CHECK(!mutated);
        }
        }

class TestHeaderAndShortIDs {
    // Utility to encode custom CBlockHeaderAndShortTxIDs
public:
    CBlockHeader header;
    uint64_t nonce;
    std::vector <uint64_t> shorttxids;
    std::vector <PrefilledTransaction> prefilledtxn;

    explicit TestHeaderAndShortIDs(const CBlockHeaderAndShortTxIDs &orig) {
        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << orig;
        stream >> *this;
    }

    explicit TestHeaderAndShortIDs(const CBlock &block) :
            TestHeaderAndShortIDs(CBlockHeaderAndShortTxIDs(block)) {}

    uint64_t GetShortID(const uint256 &txhash) const {
        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << *this;
        CBlockHeaderAndShortTxIDs base;
        stream >> base;
        return base.GetShortID(txhash);
    }

    SERIALIZE_METHODS(TestHeaderAndShortIDs, obj
    ) { READWRITE(obj.header, obj.nonce, Using < VectorFormatter < CustomUintFormatter <
                                         CBlockHeaderAndShortTxIDs::SHORTTXIDS_LENGTH>>>(obj.shorttxids), obj.prefilledtxn);
    }
};

BOOST_AUTO_TEST_CASE(NonCoinbasePreforwardRTTest)
        {
                CTxMemPool pool;
        TestMemPoolEntryHelper entry;
        CBlock block(BuildBlockTestCase());

        LOCK2(cs_main, pool.cs);
        pool.addUnchecked(entry.FromTx(block.vtx[2]));
        BOOST_CHECK_EQUAL(pool.mapTx.find(block.vtx[2]->GetHash())->GetSharedTx().use_count(), SHARED_TX_OFFSET + 0);

        uint256 txhash;

        // Test with pre-forwarding tx 1, but not coinbase
        {
            TestHeaderAndShortIDs shortIDs(block);
            shortIDs.prefilledtxn.resize(1);
            shortIDs.prefilledtxn[0] = {1, block.vtx[1]};
            shortIDs.shorttxids.resize(2);
            shortIDs.shorttxids[0] = shortIDs.GetShortID(block.vtx[0]->GetHash());
            shortIDs.shorttxids[1] = shortIDs.GetShortID(block.vtx[2]->GetHash());

            CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
            stream << shortIDs;

            CBlockHeaderAndShortTxIDs shortIDs2;
            stream >> shortIDs2;

            PartiallyDownloadedBlock partialBlock(&pool);
            BOOST_CHECK(partialBlock.InitData(shortIDs2, extra_txn) == READ_STATUS_OK);
            BOOST_CHECK(!partialBlock.IsTxAvailable(0));
            BOOST_CHECK(partialBlock.IsTxAvailable(1));
            BOOST_CHECK(partialBlock.IsTxAvailable(2));

            BOOST_CHECK_EQUAL(pool.mapTx.find(block.vtx[2]->GetHash())->GetSharedTx().use_count(),
                              SHARED_TX_OFFSET + 1);

            CBlock block2;
            {
                PartiallyDownloadedBlock tmp = partialBlock;
                BOOST_CHECK(partialBlock.FillBlock(block2, {}) == READ_STATUS_INVALID); // No transactions
                partialBlock = tmp;
            }

            // Wrong transaction
            {
                PartiallyDownloadedBlock tmp = partialBlock;
                partialBlock.FillBlock(block2,
                                       {block.vtx[1]}); // Current implementation doesn't check txn here, but don't require that
                partialBlock = tmp;
            }
            BOOST_CHECK_EQUAL(pool.mapTx.find(block.vtx[2]->GetHash())->GetSharedTx().use_count(),
                              SHARED_TX_OFFSET + 2); // +2 because of partialBlock and block2
            bool mutated;
            BOOST_CHECK(block.hashMerkleRoot != BlockMerkleRoot(block2, &mutated));

            CBlock block3;
            PartiallyDownloadedBlock partialBlockCopy = partialBlock;
            BOOST_CHECK(partialBlock.FillBlock(block3, {block.vtx[0]}) == READ_STATUS_OK);
            BOOST_CHECK_EQUAL(block.GetHash().ToString(), block3.GetHash().ToString());
            BOOST_CHECK_EQUAL(block.hashMerkleRoot.ToString(), BlockMerkleRoot(block3, &mutated).ToString());
            BOOST_CHECK(!mutated);

            BOOST_CHECK_EQUAL(pool.mapTx.find(block.vtx[2]->GetHash())->GetSharedTx().use_count(),
                              SHARED_TX_OFFSET + 3); // +2 because of partialBlock and block2 and block3

            txhash = block.vtx[2]->GetHash();
            block.vtx.clear();
            block2.vtx.clear();
            block3.vtx.clear();
            BOOST_CHECK_EQUAL(pool.mapTx.find(txhash)->GetSharedTx().use_count(),
                              SHARED_TX_OFFSET + 1 - 1); // + 1 because of partialBlock; -1 because of block.
        }
        BOOST_CHECK_EQUAL(pool.mapTx.find(txhash)->GetSharedTx().use_count(), SHARED_TX_OFFSET - 1); // -1 because of block
        }

BOOST_AUTO_TEST_CASE(SufficientPreforwardRTTest)
        {
                CTxMemPool pool;
        TestMemPoolEntryHelper entry;
        CBlock block(BuildBlockTestCase());

        LOCK2(cs_main, pool.cs);
        pool.addUnchecked(entry.FromTx(block.vtx[1]));
        BOOST_CHECK_EQUAL(pool.mapTx.find(block.vtx[1]->GetHash())->GetSharedTx().use_count(), SHARED_TX_OFFSET + 0);

        uint256 txhash;

        // Test with pre-forwarding coinbase + tx 2 with tx 1 in mempool
        {
            TestHeaderAndShortIDs shortIDs(block);
            shortIDs.prefilledtxn.resize(2);
            shortIDs.prefilledtxn[0] = {0, block.vtx[0]};
            shortIDs.prefilledtxn[1] = {1, block.vtx[2]}; // id == 1 as it is 1 after index 1
            shortIDs.shorttxids.resize(1);
            shortIDs.shorttxids[0] = shortIDs.GetShortID(block.vtx[1]->GetHash());

            CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
            stream << shortIDs;

            CBlockHeaderAndShortTxIDs shortIDs2;
            stream >> shortIDs2;

            PartiallyDownloadedBlock partialBlock(&pool);
            BOOST_CHECK(partialBlock.InitData(shortIDs2, extra_txn) == READ_STATUS_OK);
            BOOST_CHECK(partialBlock.IsTxAvailable(0));
            BOOST_CHECK(partialBlock.IsTxAvailable(1));
            BOOST_CHECK(partialBlock.IsTxAvailable(2));

            BOOST_CHECK_EQUAL(pool.mapTx.find(block.vtx[1]->GetHash())->GetSharedTx().use_count(),
                              SHARED_TX_OFFSET + 1);

            CBlock block2;
            PartiallyDownloadedBlock partialBlockCopy = partialBlock;
            BOOST_CHECK(partialBlock.FillBlock(block2, {}) == READ_STATUS_OK);
            BOOST_CHECK_EQUAL(block.GetHash().ToString(), block2.GetHash().ToString());
            bool mutated;
            BOOST_CHECK_EQUAL(block.hashMerkleRoot.ToString(), BlockMerkleRoot(block2, &mutated).ToString());
            BOOST_CHECK(!mutated);

            txhash = block.vtx[1]->GetHash();
            block.vtx.clear();
            block2.vtx.clear();
            BOOST_CHECK_EQUAL(pool.mapTx.find(txhash)->GetSharedTx().use_count(),
                              SHARED_TX_OFFSET + 1 - 1); // + 1 because of partialBlock; -1 because of block
        }
        BOOST_CHECK_EQUAL(pool.mapTx.find(txhash)->GetSharedTx().use_count(), SHARED_TX_OFFSET - 1); // -1 because of block
        }

BOOST_AUTO_TEST_CASE(EmptyBlockRoundTripTest)
        {
                CTxMemPool pool;
        CMutableTransaction coinbase;
        coinbase.vin.resize(1);
        coinbase.vin[0].scriptSig.resize(10);
        coinbase.vout.resize(1);
        coinbase.vout[0].nValue = 42;

        CBlock block;
        block.vtx.resize(1);
        block.vtx[0] = MakeTransactionRef(std::move(coinbase));
        block.nVersion = 42;
        block.hashPrevBlock = InsecureRand256();
        block.nBits = 0x207fffff;

        bool mutated;
        block.hashMerkleRoot = BlockMerkleRoot(block, &mutated);
        assert(!mutated);
        while (!CheckProofOfWork(block.GetPOWHash(), block.nBits, Params().GetConsensus())) ++block.nNonce;

        // Test simple header round-trip with only coinbase
        {
            CBlockHeaderAndShortTxIDs shortIDs(block);

            CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
            stream << shortIDs;

            CBlockHeaderAndShortTxIDs shortIDs2;
            stream >> shortIDs2;

            PartiallyDownloadedBlock partialBlock(&pool);
            BOOST_CHECK(partialBlock.InitData(shortIDs2, extra_txn) == READ_STATUS_OK);
            BOOST_CHECK(partialBlock.IsTxAvailable(0));

            CBlock block2;
            std::vector <CTransactionRef> vtx_missing;
            BOOST_CHECK(partialBlock.FillBlock(block2, vtx_missing) == READ_STATUS_OK);
            BOOST_CHECK_EQUAL(block.GetHash().ToString(), block2.GetHash().ToString());
            BOOST_CHECK_EQUAL(block.hashMerkleRoot.ToString(), BlockMerkleRoot(block2, &mutated).ToString());
            BOOST_CHECK(!mutated);
        }
        }

BOOST_AUTO_TEST_CASE(TransactionsRequestSerializationTest) {
        BlockTransactionsRequest req1;
        req1.blockhash = InsecureRand256();
        req1.indexes.resize(4);
        req1.indexes[0] = 0;
        req1.indexes[1] = 1;
        req1.indexes[2] = 3;
        req1.indexes[3] = 4;

        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << req1;

        BlockTransactionsRequest req2;
        stream >> req2;

        BOOST_CHECK_EQUAL(req1.blockhash.ToString(), req2.blockhash.ToString());
        BOOST_CHECK_EQUAL(req1.indexes.size(), req2.indexes.size());
        BOOST_CHECK_EQUAL(req1.indexes[0], req2.indexes[0]);
        BOOST_CHECK_EQUAL(req1.indexes[1], req2.indexes[1]);
        BOOST_CHECK_EQUAL(req1.indexes[2], req2.indexes[2]);
        BOOST_CHECK_EQUAL(req1.indexes[3], req2.indexes[3]);
}

BOOST_AUTO_TEST_CASE(TransactionsRequestDeserializationMaxTest) {
        // Check that the highest legal index is decoded correctly
        BlockTransactionsRequest req0;
        req0.blockhash = InsecureRand256();
        req0.indexes.resize(1);
        req0.indexes[0] = 0xffff;
        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << req0;

        BlockTransactionsRequest req1;
        stream >> req1;
        BOOST_CHECK_EQUAL(req0.indexes.size(), req1.indexes.size());
        BOOST_CHECK_EQUAL(req0.indexes[0], req1.indexes[0]);
}

BOOST_AUTO_TEST_CASE(TransactionsRequestDeserializationOverflowTest) {
        // Any set of index deltas that starts with N values that sum to (0x10000 - N)
        // causes the edge-case overflow that was originally not checked for. Such
        // a request cannot be created by serializing a real BlockTransactionsRequest
        // due to the overflow, so here we'll serialize from raw deltas.
        BlockTransactionsRequest req0;
        req0.blockhash = InsecureRand256();
        req0.indexes.resize(3);
        req0.indexes[0] = 0x7000;
        req0.indexes[1] = 0x10000 - 0x7000 - 2;
        req0.indexes[2] = 0;
        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << req0.blockhash;
        WriteCompactSize(stream, req0.indexes.size());
        WriteCompactSize(stream, req0.indexes[0]);
        WriteCompactSize(stream, req0.indexes[1]);
        WriteCompactSize(stream, req0.indexes[2]);

        BlockTransactionsRequest req1;
        try {
            stream >> req1;
            // before patch: deserialize above succeeds and this check fails, demonstrating the overflow
            BOOST_CHECK(req1.indexes[1] < req1.indexes[2]);
            // this shouldn't be reachable before or after patch
            BOOST_CHECK(0);
        } catch(std::ios_base::failure &) {
            // deserialize should fail
        }
}


BOOST_AUTO_TEST_CASE(DecoupledMixedRoundTrip)
{
    CBlock block(BuildBlockTestCase());
    CMutableTransaction child(*block.vtx[2]);
    child.vin.resize(1);
    child.vin[0].prevout = COutPoint(block.vtx[1]->GetHash(), 0);
    block.vtx[2] = MakeTransactionRef(child);
    child.vin[0].prevout = COutPoint(block.vtx[2]->GetHash(), 0);
    block.vtx.push_back(MakeTransactionRef(child));
    child.vin[0].prevout = COutPoint(block.vtx[3]->GetHash(), 0);
    block.vtx.push_back(MakeTransactionRef(child));
    block.hashMerkleRoot = BlockMerkleRoot(block);
    block.fChecked = true;

    CDecoupledBlock encoded(block, {block.vtx[0]->GetHash(), block.vtx[1]->GetHash(), block.vtx[3]->GetHash()});
    BOOST_REQUIRE_EQUAL(encoded.vtx.size(), 3U);
    BOOST_REQUIRE_EQUAL(encoded.vtxids.size(), 2U);
    BOOST_CHECK(encoded.vtx[0] == block.vtx[0]);
    BOOST_CHECK(encoded.vtx[1] == block.vtx[2]);
    BOOST_CHECK(encoded.vtx[2] == block.vtx[4]);
    BOOST_CHECK_EQUAL(encoded.vtxids[0].index, 1);
    BOOST_CHECK_EQUAL(encoded.vtxids[1].index, 3);
    BOOST_CHECK(encoded.vtxids[0].txid == block.vtx[1]->GetHash());
    BOOST_CHECK(encoded.vtxids[1].txid == block.vtx[3]->GetHash());

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION), expected(SER_NETWORK, PROTOCOL_VERSION);
    stream << encoded;
    expected << uint16_t(1) << block.GetBlockHeader();
    expected << std::vector<CTransactionRef>{block.vtx[0], block.vtx[2], block.vtx[4]};
    WriteCompactSize(expected, 2);
    expected << uint16_t(1) << block.vtx[1]->GetHash() << uint16_t(3) << block.vtx[3]->GetHash();
    BOOST_CHECK_EQUAL_COLLECTIONS(stream.begin(), stream.end(), expected.begin(), expected.end());

    CDecoupledBlock decoded;
    stream >> decoded;
    BOOST_CHECK(stream.empty());
    PartiallyDownloadedDecoupledBlock partial;
    BOOST_REQUIRE(partial.InitData(decoded, {}) == READ_STATUS_OK);
    const std::vector<uint16_t> missing{1, 3};
    BOOST_CHECK(partial.GetMissingIndexes() == missing);
    CBlock rebuilt;
    rebuilt.fChecked = true;
    BOOST_REQUIRE(partial.FillBlock(rebuilt, {block.vtx[1], block.vtx[3]}) == READ_STATUS_OK);
    BOOST_CHECK(!rebuilt.fChecked);
    BOOST_REQUIRE_EQUAL(rebuilt.vtx.size(), block.vtx.size());
    for (size_t i = 0; i < block.vtx.size(); ++i) {
        BOOST_CHECK(rebuilt.vtx[i]->GetHash() == block.vtx[i]->GetHash());
    }
    CDataStream originalBytes(SER_NETWORK, PROTOCOL_VERSION), rebuiltBytes(SER_NETWORK, PROTOCOL_VERSION);
    originalBytes << block;
    rebuiltBytes << rebuilt;
    BOOST_CHECK_EQUAL_COLLECTIONS(originalBytes.begin(), originalBytes.end(), rebuiltBytes.begin(), rebuiltBytes.end());
}

BOOST_AUTO_TEST_CASE(DecoupledFullAndCoinbaseRoundTrip)
{
    CBlock block(BuildBlockTestCase());
    for (size_t count : {size_t(3), size_t(1)}) {
        block.vtx.resize(count);
        block.hashMerkleRoot = BlockMerkleRoot(block);
        CDecoupledBlock encoded(block, {block.vtx[0]->GetHash()});
        BOOST_CHECK(encoded.vtxids.empty());
        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
        stream << encoded;
        CDecoupledBlock decoded;
        stream >> decoded;
        PartiallyDownloadedDecoupledBlock partial;
        BOOST_REQUIRE(partial.InitData(decoded, {}) == READ_STATUS_OK);
        BOOST_CHECK(partial.GetMissingIndexes().empty());
        CBlock rebuilt;
        BOOST_REQUIRE(partial.FillBlock(rebuilt, {}) == READ_STATUS_OK);
        BOOST_CHECK(rebuilt.GetHash() == block.GetHash());
        BOOST_CHECK(BlockMerkleRoot(rebuilt) == block.hashMerkleRoot);
        BOOST_CHECK(!rebuilt.fChecked);
    }
}

BOOST_AUTO_TEST_CASE(DecoupledCacheEvictionKeepsBodiesAlive)
{
    CBlock block(BuildBlockTestCase());
    const uint256 txid = block.vtx[1]->GetHash();
    std::weak_ptr<const CTransaction> retained = block.vtx[1];
    std::map<uint256, CTransactionRef> cache{{txid, block.vtx[1]}};
    CDecoupledBlock encoded(block, {txid});
    PartiallyDownloadedDecoupledBlock partial;
    BOOST_REQUIRE(partial.InitData(encoded, [&cache](const uint256& hash) {
        auto it = cache.find(hash);
        return it == cache.end() ? CTransactionRef() : it->second;
    }) == READ_STATUS_OK);
    BOOST_CHECK(partial.GetMissingIndexes().empty());
    cache.clear();
    block.vtx.clear();
    BOOST_CHECK(!retained.expired());
    CBlock rebuilt;
    BOOST_REQUIRE(partial.FillBlock(rebuilt, {}) == READ_STATUS_OK);
    BOOST_CHECK(rebuilt.vtx[1]->GetHash() == txid);
    BOOST_CHECK(rebuilt.vtx[1] == retained.lock());
    rebuilt.vtx.clear();
    BOOST_CHECK(retained.expired());
}

BOOST_AUTO_TEST_CASE(DecoupledInvalidLayout)
{
    const CBlock block(BuildBlockTestCase());
    const CDecoupledBlock valid(block, {block.vtx[1]->GetHash(), block.vtx[2]->GetHash()});
    const auto invalid = [&valid, &block](const CDecoupledBlock& encoded) {
        PartiallyDownloadedDecoupledBlock partial;
        BOOST_REQUIRE(partial.InitData(valid, {}) == READ_STATUS_OK);
        BOOST_CHECK(partial.InitData(encoded, [](const uint256&) {
            BOOST_ERROR("Invalid layouts must be rejected before body lookup");
            return CTransactionRef();
        }) == READ_STATUS_INVALID);
        BOOST_CHECK(partial.GetMissingIndexes().empty());
        CBlock rebuilt;
        BOOST_CHECK(partial.FillBlock(rebuilt, {block.vtx[1], block.vtx[2]}) == READ_STATUS_FAILED);
        BOOST_REQUIRE(partial.InitData(valid, {}) == READ_STATUS_OK);
        BOOST_CHECK(partial.FillBlock(rebuilt, {block.vtx[1], block.vtx[2]}) == READ_STATUS_OK);

        // Bypass the encoder to exercise untrusted wire layouts independently.
        if (std::all_of(encoded.vtx.begin(), encoded.vtx.end(), [](const CTransactionRef& tx) { return bool(tx); })) {
            CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
            stream << encoded.version << encoded.header << encoded.vtx << encoded.vtxids;
            CDecoupledBlock decoded;
            BOOST_CHECK_THROW(stream >> decoded, std::ios_base::failure);
        }
    };
    CDecoupledBlock bad = valid;
    bad.header.SetNull();
    invalid(bad);
    bad = valid;
    bad.version = 2;
    invalid(bad);
    bad = valid;
    bad.vtx.clear();
    invalid(bad);
    bad = valid;
    bad.vtxids[0].index = 0;
    invalid(bad);
    bad = valid;
    bad.vtxids[1].index = 1;
    invalid(bad);
    bad = valid;
    std::swap(bad.vtxids[0], bad.vtxids[1]);
    invalid(bad);
    bad = valid;
    bad.vtxids[1].index = 3;
    invalid(bad);
    bad = valid;
    bad.vtxids[1].txid = bad.vtxids[0].txid;
    invalid(bad);
    bad = valid;
    bad.vtxids[0].txid = block.vtx[0]->GetHash();
    invalid(bad);
    bad = valid;
    bad.vtx[0] = block.vtx[1];
    invalid(bad);
    bad = valid;
    bad.vtx[0].reset();
    invalid(bad);
    bad = CDecoupledBlock(block, {});
    bad.vtx[1].reset();
    invalid(bad);
    bad = CDecoupledBlock(block, {});
    bad.vtx[1] = block.vtx[0];
    invalid(bad);
    bad = valid;
    bad.vtxids.resize(65535);
    invalid(bad);
    CBlock oversized = block;
    oversized.vtx.resize(65536, block.vtx[1]);
    BOOST_CHECK_THROW(CDecoupledBlock(oversized, {}), std::invalid_argument);
    CBlock empty;
    BOOST_CHECK_THROW(CDecoupledBlock(empty, {}), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(DecoupledCountsCheckedBeforeAllocation)
{
    const CBlock block(BuildBlockTestCase());
    CDataStream bodies(SER_NETWORK, PROTOCOL_VERSION);
    bodies << uint16_t(1) << block.GetBlockHeader();
    WriteCompactSize(bodies, 65536);
    CDecoupledBlock decoded;
    BOOST_CHECK_THROW(bodies >> decoded, std::ios_base::failure);
    BOOST_CHECK_EQUAL(decoded.vtx.capacity(), 0U);
    BOOST_CHECK_EQUAL(decoded.vtxids.capacity(), 0U);

    CDataStream refs(SER_NETWORK, PROTOCOL_VERSION);
    refs << uint16_t(1) << block.GetBlockHeader() << std::vector<CTransactionRef>{block.vtx[0]};
    WriteCompactSize(refs, 65535);
    BOOST_CHECK_THROW(refs >> decoded, std::ios_base::failure);
    BOOST_CHECK_EQUAL(decoded.vtxids.capacity(), 0U);

    CDataStream unknownVersion(SER_NETWORK, PROTOCOL_VERSION);
    unknownVersion << uint16_t(2);
    BOOST_CHECK_THROW(unknownVersion >> decoded, std::ios_base::failure);

    CDecoupledBlock boundary(block, {});
    boundary.vtx.resize(1);
    for (uint32_t i = 1; i < 65535; ++i) {
        boundary.vtxids.push_back({uint16_t(i), uint256S(strprintf("%064x", i))});
    }
    CDataStream wire(SER_NETWORK, PROTOCOL_VERSION);
    wire << boundary;
    wire >> decoded;
    PartiallyDownloadedDecoupledBlock partial;
    BOOST_REQUIRE(partial.InitData(decoded, {}) == READ_STATUS_OK);
    const auto missing = partial.GetMissingIndexes();
    BOOST_REQUIRE_EQUAL(missing.size(), 65534U);
    BOOST_CHECK_EQUAL(missing.front(), 1);
    BOOST_CHECK_EQUAL(missing.back(), 65534);
}

BOOST_AUTO_TEST_CASE(DecoupledResponsesRequireExactBodies)
{
    const CBlock block(BuildBlockTestCase());
    const CDecoupledBlock encoded(block, {block.vtx[1]->GetHash(), block.vtx[2]->GetHash()});
    PartiallyDownloadedDecoupledBlock partial;
    BOOST_REQUIRE(partial.InitData(encoded, {}) == READ_STATUS_OK);
    CBlock rebuilt = block;
    rebuilt.fChecked = true;
    const std::vector<std::vector<CTransactionRef>> invalidResponses{
        {}, {block.vtx[1]}, {block.vtx[2], block.vtx[1]},
        {block.vtx[1], CTransactionRef()}, {block.vtx[1], MakeTransactionRef()},
        {block.vtx[1], block.vtx[2], block.vtx[2]}
    };
    for (const auto& response : invalidResponses) {
        BOOST_CHECK(partial.FillBlock(rebuilt, response) == READ_STATUS_FAILED);
        BOOST_CHECK(rebuilt.fChecked);
        BOOST_CHECK(rebuilt.vtx == block.vtx);
        const std::vector<uint16_t> missing{1, 2};
        BOOST_CHECK(partial.GetMissingIndexes() == missing);
    }
    BOOST_REQUIRE(partial.FillBlock(rebuilt, {block.vtx[1], block.vtx[2]}) == READ_STATUS_OK);
    BOOST_CHECK(rebuilt.vtx == block.vtx);
    BOOST_CHECK(!rebuilt.fChecked);
    BOOST_CHECK(partial.FillBlock(rebuilt, {}) == READ_STATUS_FAILED);
}

BOOST_AUTO_TEST_CASE(DecoupledLookupRequiresExactTxid)
{
    const CBlock block(BuildBlockTestCase());
    const CDecoupledBlock encoded(block, {block.vtx[1]->GetHash()});
    PartiallyDownloadedDecoupledBlock partial;
    BOOST_REQUIRE(partial.InitData(encoded, {}) == READ_STATUS_OK);
    BOOST_CHECK(partial.InitData(encoded, [&block](const uint256&) { return block.vtx[2]; }) == READ_STATUS_FAILED);
    BOOST_CHECK(partial.GetMissingIndexes().empty());
    CBlock rebuilt;
    BOOST_CHECK(partial.FillBlock(rebuilt, {block.vtx[1]}) == READ_STATUS_FAILED);
    BOOST_REQUIRE(partial.InitData(encoded, [&block](const uint256&) { return block.vtx[1]; }) == READ_STATUS_OK);
    BOOST_REQUIRE(partial.FillBlock(rebuilt, {}) == READ_STATUS_OK);
    BOOST_CHECK(rebuilt.vtx == block.vtx);
}

BOOST_AUTO_TEST_CASE(DecoupledChecksBytesAndMerkleWithoutContextualValidation)
{
    CBlock block(BuildBlockTestCase());
    block.nBits = 0x01003456; // Invalid proof-of-work target belongs to normal block validation.
    PartiallyDownloadedDecoupledBlock partial;
    CBlock rebuilt;
    BOOST_REQUIRE(partial.InitData(CDecoupledBlock(block, {}), {}) == READ_STATUS_OK);
    BOOST_REQUIRE(partial.FillBlock(rebuilt, {}) == READ_STATUS_OK);
    BOOST_CHECK(!rebuilt.fChecked);

    CDecoupledBlock corrupted(block, {});
    corrupted.header.hashMerkleRoot.SetNull();
    BOOST_REQUIRE(partial.InitData(corrupted, {}) == READ_STATUS_OK);
    BOOST_CHECK(partial.FillBlock(rebuilt, {}) == READ_STATUS_FAILED);

    CMutableTransaction large(*block.vtx[1]);
    large.vin[0].scriptSig.resize(MaxBlockSize());
    block.vtx[1] = MakeTransactionRef(large);
    block.hashMerkleRoot = BlockMerkleRoot(block);
    BOOST_REQUIRE(partial.InitData(CDecoupledBlock(block, {block.vtx[1]->GetHash()}), {}) == READ_STATUS_OK);
    BOOST_CHECK(partial.FillBlock(rebuilt, {block.vtx[1]}) == READ_STATUS_FAILED);
}


static CBlock BuildQuorumCommitmentBlockTestCase()
{
    CBlock block(BuildBlockTestCase());
    llmq::CFinalCommitmentTxPayload payload;
    payload.nHeight = 1;
    payload.commitment = llmq::CFinalCommitment(
        Params().GetConsensus().llmqs.at(Consensus::LLMQ_5_60), Params().GenesisBlock().GetHash());
    BOOST_REQUIRE(payload.commitment.VerifyNull());
    CMutableTransaction commitment;
    commitment.nVersion = 3;
    commitment.nType = TRANSACTION_QUORUM_COMMITMENT;
    SetTxPayload(commitment, payload);
    block.vtx[1] = MakeTransactionRef(commitment);
    block.hashMerkleRoot = BlockMerkleRoot(block);
    BOOST_REQUIRE(block.vtx[1]->IsNull());
    CValidationState state;
    BOOST_REQUIRE(CheckTransaction(*block.vtx[1], state, payload.nHeight, 0));
    while (!CheckProofOfWork(block.GetPOWHash(), block.nBits, Params().GetConsensus())) ++block.nNonce;
    return block;
}

BOOST_AUTO_TEST_CASE(CompactPrefilledQuorumCommitmentRoundTrip)
{
    CBlock block(BuildQuorumCommitmentBlockTestCase());
    TestHeaderAndShortIDs encoded(block);
    encoded.prefilledtxn = {{0, block.vtx[0]}, {0, block.vtx[1]}};
    encoded.shorttxids = {encoded.GetShortID(block.vtx[2]->GetHash())};
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << encoded;
    CBlockHeaderAndShortTxIDs decoded;
    stream >> decoded;
    BOOST_REQUIRE(stream.empty());

    CTxMemPool pool;
    PartiallyDownloadedBlock partial(&pool);
    BOOST_REQUIRE(partial.InitData(decoded, {}) == READ_STATUS_OK);
    BOOST_CHECK(partial.IsTxAvailable(0));
    BOOST_CHECK(partial.IsTxAvailable(1));
    BOOST_CHECK(!partial.IsTxAvailable(2));
    CBlock rebuilt;
    BOOST_REQUIRE(partial.FillBlock(rebuilt, {block.vtx[2]}) == READ_STATUS_OK);
    CDataStream originalBytes(SER_NETWORK, PROTOCOL_VERSION), rebuiltBytes(SER_NETWORK, PROTOCOL_VERSION);
    originalBytes << block;
    rebuiltBytes << rebuilt;
    BOOST_CHECK_EQUAL_COLLECTIONS(originalBytes.begin(), originalBytes.end(), rebuiltBytes.begin(), rebuiltBytes.end());
}

BOOST_AUTO_TEST_CASE(CompactRejectsEmptyPrefilledTransaction)
{
    CBlock block(BuildBlockTestCase());
    CTxMemPool pool;
    // Only the version-3 quorum commitment shape may be structurally empty.
    CMutableTransaction normalV3, commitmentV2;
    normalV3.nVersion = 3;
    commitmentV2.nVersion = 2;
    commitmentV2.nType = TRANSACTION_QUORUM_COMMITMENT;
    for (const CTransactionRef& tx : {MakeTransactionRef(CMutableTransaction()), MakeTransactionRef(normalV3),
                                      MakeTransactionRef(commitmentV2), CTransactionRef{}}) {
        block.vtx[0] = tx;
        PartiallyDownloadedBlock partial(&pool);
        BOOST_CHECK(partial.InitData(CBlockHeaderAndShortTxIDs(block), {}) == READ_STATUS_INVALID);
    }
}

BOOST_AUTO_TEST_CASE(DecoupledQuorumCommitmentRoundTrip)
{
    CBlock block(BuildQuorumCommitmentBlockTestCase());
    CDecoupledBlock encoded(block, {block.vtx[2]->GetHash()});
    BOOST_REQUIRE_EQUAL(encoded.vtx.size(), 2U);
    BOOST_CHECK(encoded.vtx[1] == block.vtx[1]);
    BOOST_REQUIRE_EQUAL(encoded.vtxids.size(), 1U);
    BOOST_CHECK_EQUAL(encoded.vtxids[0].index, 2);
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << encoded;
    CDecoupledBlock decoded;
    stream >> decoded;
    BOOST_CHECK(stream.empty());
    PartiallyDownloadedDecoupledBlock partial;
    BOOST_REQUIRE(partial.InitData(decoded, {}) == READ_STATUS_OK);
    const std::vector<uint16_t> missing{2};
    BOOST_CHECK(partial.GetMissingIndexes() == missing);
    CBlock rebuilt;
    BOOST_REQUIRE(partial.FillBlock(rebuilt, {block.vtx[2]}) == READ_STATUS_OK);
    BOOST_CHECK(!rebuilt.fChecked);
    CDataStream originalBytes(SER_NETWORK, PROTOCOL_VERSION), rebuiltBytes(SER_NETWORK, PROTOCOL_VERSION);
    originalBytes << block;
    rebuiltBytes << rebuilt;
    BOOST_CHECK_EQUAL_COLLECTIONS(originalBytes.begin(), originalBytes.end(), rebuiltBytes.begin(), rebuiltBytes.end());
}

BOOST_AUTO_TEST_CASE(DecoupledDefersEmptyTransactionConsensus)
{
    CBlock block(BuildBlockTestCase());
    block.vtx[1] = MakeTransactionRef(CMutableTransaction());
    BOOST_REQUIRE(block.vtx[1]->IsNull());
    block.hashMerkleRoot = BlockMerkleRoot(block);
    CDecoupledBlock encoded(block, {block.vtx[2]->GetHash()});
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << encoded;
    CDecoupledBlock decoded;
    stream >> decoded;
    PartiallyDownloadedDecoupledBlock partial;
    BOOST_REQUIRE(partial.InitData(decoded, {}) == READ_STATUS_OK);
    CBlock rebuilt;
    BOOST_REQUIRE(partial.FillBlock(rebuilt, {block.vtx[2]}) == READ_STATUS_OK);
    BOOST_CHECK(!rebuilt.fChecked);
    CValidationState state;
    BOOST_CHECK(!CheckTransaction(*rebuilt.vtx[1], state, 1, 0));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-vin-empty");
}


// Untrusted counts must be refused without growing the target vectors.
BOOST_AUTO_TEST_CASE(DecoupledBudgetRejectsTruncatedCountsBeforeAllocation)
{
    const auto exhausted = [](const std::ios_base::failure& e) {
        return std::string(e.what()).find("decoupled allocation budget exceeded") != std::string::npos;
    };
    for (bool input : {true, false}) {
        CDataStream wire(SER_NETWORK, PROTOCOL_VERSION);
        wire << int32_t(1);
        if (!input) WriteCompactSize(wire, 0);
        WriteCompactSize(wire, MAX_SIZE);
        CMutableTransaction tx;
        CDecoupledReadBudget budget(1024);
        CDecoupledBudgetedReader<CDataStream> reader(wire, budget);
        BOOST_CHECK_EXCEPTION(reader >> tx, std::ios_base::failure, exhausted);
        BOOST_CHECK_EQUAL(tx.vin.capacity(), 0U);
        BOOST_CHECK_EQUAL(tx.vout.capacity(), 0U);
    }
    CDataStream scriptWire(SER_NETWORK, PROTOCOL_VERSION);
    WriteCompactSize(scriptWire, MAX_SIZE);
    CScript script;
    const auto inlineCapacity = script.capacity();
    CDecoupledReadBudget scriptBudget(1024);
    CDecoupledBudgetedReader<CDataStream> scriptReader(scriptWire, scriptBudget);
    BOOST_CHECK_EXCEPTION(scriptReader >> script, std::ios_base::failure, exhausted);
    BOOST_CHECK_EQUAL(script.capacity(), inlineCapacity);

    CDataStream payloadWire(SER_NETWORK, PROTOCOL_VERSION);
    WriteCompactSize(payloadWire, MAX_SIZE);
    std::vector<unsigned char> payload;
    CDecoupledReadBudget payloadBudget(1024);
    CDecoupledBudgetedReader<CDataStream> payloadReader(payloadWire, payloadBudget);
    BOOST_CHECK_EXCEPTION(payloadReader >> payload, std::ios_base::failure, exhausted);
    BOOST_CHECK_EQUAL(payload.capacity(), 0U);

    const CBlock block(BuildBlockTestCase());
    CDataStream bodies(SER_NETWORK, PROTOCOL_VERSION);
    bodies << uint16_t(1) << block.GetBlockHeader();
    WriteCompactSize(bodies, 65535);
    CDecoupledReadBudget bodyBudget(1024);
    CDecoupledBudgetedReader<CDataStream> bodyReader(bodies, bodyBudget);
    CDecoupledBlock decoded;
    BOOST_CHECK_EXCEPTION(bodyReader >> decoded, std::ios_base::failure, exhausted);
    BOOST_CHECK_EQUAL(decoded.vtx.capacity(), 0U);

    CDataStream references(SER_NETWORK, PROTOCOL_VERSION);
    references << uint16_t(1) << block.GetBlockHeader() << std::vector<CTransactionRef>{block.vtx[0]};
    WriteCompactSize(references, 65534);
    CDecoupledReadBudget refBudget(1024);
    CDecoupledBudgetedReader<CDataStream> refReader(references, refBudget);
    CDecoupledBlock decodedRefs;
    BOOST_CHECK_EXCEPTION(refReader >> decodedRefs, std::ios_base::failure, exhausted);
    BOOST_CHECK_EQUAL(decodedRefs.vtxids.capacity(), 0U);
}

BOOST_AUTO_TEST_CASE(DecoupledBudgetChargesSharedBodiesBeforeConstruction)
{
    const CBlock block(BuildBlockTestCase());
    CDataStream wire(SER_NETWORK, PROTOCOL_VERSION);
    wire << block.vtx[0];
    const auto unread = wire.size();
    CDecoupledReadBudget budget(0);
    CDecoupledBudgetedReader<CDataStream> reader(wire, budget);
    CTransactionRef tx;
    BOOST_CHECK_THROW(reader >> tx, std::ios_base::failure);
    BOOST_CHECK(!tx);
    BOOST_CHECK_EQUAL(wire.size(), unread);
}

BOOST_AUTO_TEST_CASE(DecoupledBudgetStopsManyMinimalBodies)
{
    const CBlock block(BuildBlockTestCase());
    CDataStream wire(SER_NETWORK, PROTOCOL_VERSION);
    wire << uint16_t(1) << block.GetBlockHeader();
    WriteCompactSize(wire, 5000);
    wire << block.vtx[0];
    CMutableTransaction minimal;
    for (uint32_t i = 1; i < 5000; ++i) {
        minimal.nLockTime = i;
        wire << minimal;
    }
    WriteCompactSize(wire, 0);
    BOOST_REQUIRE_LT(wire.size(), 64U * 1024);
    CDecoupledReadBudget budget(512 * 1024);
    CDecoupledBudgetedReader<CDataStream> reader(wire, budget);
    CDecoupledBlock decoded;
    BOOST_CHECK_THROW(reader >> decoded, std::ios_base::failure);
    const auto constructed = std::count_if(decoded.vtx.begin(), decoded.vtx.end(),
                                           [](const CTransactionRef& tx) { return bool(tx); });
    BOOST_CHECK_GT(constructed, 1);
    BOOST_CHECK_LT(constructed, 5000);
    BOOST_CHECK(!wire.empty());
    BOOST_CHECK_LE(budget.GetUsed(), 512U * 1024);
}

BOOST_AUTO_TEST_CASE(DecoupledBudgetReservesExtraPayloadCopy)
{
    // CTransaction's move constructor currently copies the mutable extra payload.
    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_QUORUM_COMMITMENT;
    tx.vExtraPayload.resize(6000);
    CDataStream wire(SER_NETWORK, PROTOCOL_VERSION);
    wire << tx;
    CDecoupledReadBudget budget(10000);
    CDecoupledBudgetedReader<CDataStream> reader(wire, budget);
    CTransactionRef decoded;
    BOOST_CHECK_THROW(reader >> decoded, std::ios_base::failure);
    BOOST_CHECK(!decoded);
    BOOST_CHECK_EQUAL(wire.size(), 6000U);

    CDataStream complete(SER_NETWORK, PROTOCOL_VERSION);
    complete << tx;
    CDecoupledReadBudget sufficientBudget(25000);
    CDecoupledBudgetedReader<CDataStream> completeReader(complete, sufficientBudget);
    completeReader >> decoded;
    BOOST_REQUIRE(decoded);
    BOOST_CHECK(complete.empty());
    BOOST_CHECK(decoded->vExtraPayload == tx.vExtraPayload);
    BOOST_CHECK(decoded->GetHash() == tx.GetHash());
}

BOOST_AUTO_TEST_CASE(DecoupledBudgetChargesLayoutScratch)
{
    const CBlock block(BuildBlockTestCase());
    CDecoupledBlock encoded(block, {});
    encoded.vtx.resize(1);
    for (uint16_t i = 1; i <= 1000; ++i) {
        encoded.vtxids.push_back({i, uint256S(strprintf("%064x", i))});
    }
    CDataStream wire(SER_NETWORK, PROTOCOL_VERSION);
    wire << encoded;
    CDecoupledReadBudget budget(50 * 1024);
    CDecoupledBudgetedReader<CDataStream> reader(wire, budget);
    CDecoupledBlock decoded;
    BOOST_CHECK_THROW(reader >> decoded, std::ios_base::failure);
    BOOST_CHECK_EQUAL(decoded.vtxids.size(), 1000U);

    CDecoupledReadBudget partialBudget(0);
    PartiallyDownloadedDecoupledBlock partial;
    size_t lookups = 0;
    BOOST_CHECK_THROW(partial.InitData(encoded, [&lookups](const uint256&) {
        ++lookups;
        return CTransactionRef();
    }, &partialBudget), std::ios_base::failure);
    BOOST_CHECK_EQUAL(lookups, 0U);
}

BOOST_AUTO_TEST_CASE(DecoupledBudgetChargesExtraIndexBeforeAllocation)
{
    const CBlock block(BuildBlockTestCase());
    std::vector<std::pair<uint256, CTransactionRef>> extra(1000);
    extra[0] = {block.vtx[1]->GetHash(), block.vtx[1]};
    extra[1] = {block.vtx[2]->GetHash(), block.vtx[2]};
    const size_t node = memusage::MallocUsage(
        sizeof(memusage::stl_tree_node<std::pair<const uint256, CTransactionRef>>));
    const size_t cost = memusage::MallocUsage(extra.size() * node);

    // Without references no index is built or charged.
    CDecoupledReadBudget none(0);
    BOOST_CHECK(IndexExtraTransactions(extra, CDecoupledBlock(block, {}), none).empty());

    // Every cache slot is charged, filled or not, before the first node exists.
    const CDecoupledBlock encoded(block, {block.vtx[1]->GetHash()});
    CDecoupledReadBudget tight(cost - 1);
    BOOST_CHECK_THROW(IndexExtraTransactions(extra, encoded, tight), CDecoupledReadBudget::Exceeded);
    BOOST_CHECK_EQUAL(tight.GetUsed(), 0U);
    CDecoupledReadBudget enough(cost);
    const auto index = IndexExtraTransactions(extra, encoded, enough);
    BOOST_CHECK_EQUAL(enough.GetUsed(), cost);
    BOOST_CHECK_EQUAL(index.size(), 2U);
    BOOST_CHECK(index.at(block.vtx[1]->GetHash()) == block.vtx[1]);
}

BOOST_AUTO_TEST_CASE(DecoupledBudgetChargesRetainedBodiesAndReconstruction)
{
    CBlock block(BuildBlockTestCase());
    CMutableTransaction special;
    special.nVersion = 3;
    special.nType = TRANSACTION_QUORUM_COMMITMENT;
    special.vExtraPayload.resize(64 * 1024);
    block.vtx[1] = MakeTransactionRef(special);
    block.hashMerkleRoot = BlockMerkleRoot(block);
    CDecoupledBlock encoded(block, {block.vtx[1]->GetHash(), block.vtx[2]->GetHash()});
    const auto owners = block.vtx[1].use_count();
    CDecoupledReadBudget budget(4096);
    PartiallyDownloadedDecoupledBlock partial;
    BOOST_CHECK_THROW(partial.InitData(encoded, [&block](const uint256&) {
        return block.vtx[1];
    }, &budget), std::ios_base::failure);
    BOOST_CHECK_EQUAL(block.vtx[1].use_count(), owners);
    CBlock rebuilt;
    BOOST_CHECK(partial.FillBlock(rebuilt, {}) == READ_STATUS_FAILED);

    BOOST_REQUIRE(partial.InitData(encoded, {}) == READ_STATUS_OK);
    CDecoupledReadBudget missingBudget(0);
    BOOST_CHECK_THROW(partial.GetMissingIndexes(&missingBudget), std::ios_base::failure);
    CDecoupledReadBudget fillBudget(4096);
    BOOST_CHECK_THROW(partial.FillBlock(rebuilt, {block.vtx[1], block.vtx[2]}, &fillBudget),
                      std::ios_base::failure);
    BOOST_CHECK(rebuilt.vtx.empty());
    BOOST_CHECK(partial.GetMissingIndexes() == std::vector<uint16_t>({1, 2}));
}

BOOST_AUTO_TEST_CASE(DecoupledBudgetChargesMerkleScratchAndGrowth)
{
    const CBlock block(BuildBlockTestCase());
    const CDecoupledBlock encoded(block, {});
    // 64 bytes allow the three shared references; 200 also allow the first
    // three-leaf array, but not the extra allocation for an odd leaf count.
    for (size_t limit : {64U, 200U}) {
        PartiallyDownloadedDecoupledBlock partial;
        BOOST_REQUIRE(partial.InitData(encoded, {}) == READ_STATUS_OK);
        CDecoupledReadBudget budget(limit);
        CBlock rebuilt;
        BOOST_CHECK_THROW(partial.FillBlock(rebuilt, {}, &budget), std::ios_base::failure);
        BOOST_CHECK(rebuilt.vtx.empty());
    }
}

BOOST_AUTO_TEST_CASE(DecoupledBudgetedRoundTripsKeepBodiesAlive)
{
    for (bool references : {false, true}) {
        CBlock block(BuildBlockTestCase());
        const CDecoupledBlock encoded(block, references ? std::set<uint256>{block.vtx[1]->GetHash(), block.vtx[2]->GetHash()}
                                                       : std::set<uint256>{});
        CDataStream expected(SER_NETWORK, PROTOCOL_VERSION), transport(SER_NETWORK, PROTOCOL_VERSION);
        expected << block;
        transport << encoded;
        const std::vector<unsigned char> bytes(transport.begin(), transport.end());
        VectorReader source(SER_NETWORK, PROTOCOL_VERSION, bytes, 0);
        CDecoupledReadBudget budget(1024 * 1024);
        budget.ChargeArray(bytes.capacity(), sizeof(unsigned char));
        CDecoupledBudgetedReader<VectorReader> reader(source, budget);
        CDecoupledBlock decoded;
        reader >> decoded;
        BOOST_CHECK(source.empty());
        PartiallyDownloadedDecoupledBlock partial;
        std::weak_ptr<const CTransaction> retained = block.vtx[1];
        BOOST_REQUIRE(partial.InitData(decoded, [&block](const uint256& hash) {
            return hash == block.vtx[1]->GetHash() ? block.vtx[1] : CTransactionRef();
        }, &budget) == READ_STATUS_OK);
        const auto missing = partial.GetMissingIndexes(&budget);
        BOOST_CHECK(missing == (references ? std::vector<uint16_t>{2} : std::vector<uint16_t>{}));
        const std::vector<CTransactionRef> response = references ? std::vector<CTransactionRef>{block.vtx[2]}
                                                                : std::vector<CTransactionRef>{};
        block.vtx.clear();
        if (references) BOOST_CHECK(!retained.expired());
        CBlock rebuilt;
        BOOST_REQUIRE(partial.FillBlock(rebuilt, response, &budget) == READ_STATUS_OK);
        BOOST_CHECK(!rebuilt.fChecked);
        CDataStream actual(SER_NETWORK, PROTOCOL_VERSION);
        actual << rebuilt;
        BOOST_CHECK_EQUAL_COLLECTIONS(actual.begin(), actual.end(), expected.begin(), expected.end());
        BOOST_CHECK_GT(budget.GetUsed(), bytes.size());
        BOOST_CHECK_LT(budget.GetUsed(), 1024U * 1024);
    }
}

BOOST_AUTO_TEST_CASE(DecoupledBudgetRejectsOverflow)
{
    CDecoupledReadBudget budget(std::numeric_limits<size_t>::max());
    BOOST_CHECK_THROW(budget.ChargeArray(std::numeric_limits<size_t>::max(), 2), std::ios_base::failure);
    BOOST_CHECK_THROW(budget.ChargeArray(1, std::numeric_limits<size_t>::max()), std::ios_base::failure);
    BOOST_CHECK_EQUAL(budget.GetUsed(), 0U);
    budget.ChargeBytes(64);
    BOOST_CHECK_THROW(budget.SetLimit(63), CDecoupledReadBudget::Exceeded);
    budget.SetLimit(64);
    BOOST_CHECK_THROW(budget.ChargeBytes(1), CDecoupledReadBudget::Exceeded);
    budget.SetLimit(128);
    budget.ChargeBytes(64);
    BOOST_CHECK_EQUAL(budget.GetUsed(), 128U);
}

// A valid block that fills the size limit with the most memory-expensive
// shape per serialized byte: minimal transactions, or 100 kB transactions with
// empty-script outputs.
static CBlock BuildMaximumCostBlock(size_t outputsPerTx)
{
    CBlock block;
    block.nBits = 0x207fffff;
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].scriptSig = CScript() << OP_TRUE << OP_TRUE;
    coinbase.vout.resize(1);
    block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
    size_t size = ::GetSerializeSize(CBlockHeader(), SER_NETWORK, PROTOCOL_VERSION) + 3 +
        ::GetSerializeSize(*block.vtx[0], SER_NETWORK, PROTOCOL_VERSION);
    for (uint32_t n = 0; block.vtx.size() < CDecoupledBlock::MAX_TRANSACTIONS; ++n) {
        CMutableTransaction tx;
        tx.vin.emplace_back(COutPoint(ArithToUint256(arith_uint256(n + 1)), 0));
        tx.vout.resize(outputsPerTx);
        const size_t txSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
        if (size + txSize > MAX_DIP0001_BLOCK_SIZE - 1000) break;
        size += txSize;
        block.vtx.push_back(MakeTransactionRef(std::move(tx)));
    }
    block.hashMerkleRoot = BlockMerkleRoot(block);
    return block;
}

// Mirrors submitdecoupledblock: RPC hex, decode, lookup and reconstruction share one budget.
static size_t SubmissionBudgetUse(const CBlock& block, bool referenced)
{
    std::set<uint256> references;
    if (referenced) {
        for (size_t i = 1; i < block.vtx.size(); ++i) references.insert(block.vtx[i]->GetHash());
    }
    const CDecoupledBlock encoded(block, references);
    CDataStream wire(SER_NETWORK, PROTOCOL_VERSION);
    wire << encoded;
    const std::vector<unsigned char> bytes(wire.begin(), wire.end());
    CDecoupledReadBudget budget(std::numeric_limits<size_t>::max());
    budget.ChargeArray(2 * bytes.size() + 1, sizeof(char));
    VectorReader source(SER_NETWORK, PROTOCOL_VERSION, bytes, 0);
    CDecoupledBudgetedReader<VectorReader> reader(source, budget);
    CDecoupledBlock decoded;
    reader >> decoded;
    BOOST_REQUIRE(source.empty());
    budget.ChargeShared(sizeof(CBlock));
    std::map<uint256, CTransactionRef> bodies;
    for (const auto& tx : block.vtx) bodies.emplace(tx->GetHash(), tx);
    PartiallyDownloadedDecoupledBlock partial;
    BOOST_REQUIRE(partial.InitData(decoded, [&bodies](const uint256& txid) {
        const auto body = bodies.find(txid);
        return body == bodies.end() ? CTransactionRef() : body->second;
    }, &budget) == READ_STATUS_OK);
    BOOST_REQUIRE(partial.GetMissingIndexes(&budget).empty());
    CBlock rebuilt;
    BOOST_REQUIRE(partial.FillBlock(rebuilt, {}, &budget) == READ_STATUS_OK);
    BOOST_CHECK(rebuilt.GetHash() == block.GetHash());
    return budget.GetUsed();
}

BOOST_AUTO_TEST_CASE(DecoupledSubmissionBudgetCoversMaximumBlocks)
{
    // 1: minimal transactions; 11100: about 100 kB of empty-script outputs each.
    for (const size_t outputs : {size_t{1}, size_t{11100}}) {
        const CBlock block = BuildMaximumCostBlock(outputs);
        BOOST_REQUIRE_GT(::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION), MAX_DIP0001_BLOCK_SIZE - 110000);
        BOOST_REQUIRE_LE(::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION), MAX_DIP0001_BLOCK_SIZE);
        for (const bool referenced : {false, true}) {
            const size_t used = SubmissionBudgetUse(block, referenced);
            BOOST_TEST_MESSAGE("outputs=" << outputs << " referenced=" << referenced << " transactions="
                               << block.vtx.size() << " budget=" << used);
            BOOST_CHECK_LE(used, MAX_DECOUPLED_SUBMISSION_BYTES);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
