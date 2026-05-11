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
#include <evm/parallel.h>
#include <evm/state_cache.h>
#include <evm/state_db.h>

#include <primitives/block.h>
#include <primitives/transaction.h>
#include <streams.h>
#include <version.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

/**
 * Phase 2.5 — parallel pre-flight worker pool tests.
 *
 * The MVP guarantee is byte-identical results to the serial
 * ProcessEvmTransactionsInBlock; tests pair-execute the same block
 * twice (serial + parallel) and compare aggregates + per-tx status.
 *
 * Stress coverage: a block with many EVM txs across many distinct
 * senders exercises real parallelism on multi-core builds.
 */

namespace {

evm::ExecutionContext MakeContext(uint64_t baseFee)
{
    evm::ExecutionContext c;
    c.chainId = 7375;
    c.blockHeight = 200;
    c.blockTimestamp = 1700000000;
    c.blockGasLimit = 30'000'000;
    c.baseFee = evm::Uint256FromUint64(baseFee);
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

void SeedSender(evm::CEvmStateCache& cache, uint8_t lowByte,
                uint64_t balance, uint64_t nonce = 0)
{
    cache.SetAccount(SenderAddr(lowByte), evm::CEvmAccount(
        nonce, evm::Uint256FromUint64(balance),
        evm::CEvmAccount::EmptyCodeHash(),
        evm::CEvmAccount::EmptyStorageRoot()));
}

template <typename T>
std::vector<unsigned char> Serialize(const T& obj)
{
    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << obj;
    return std::vector<unsigned char>(ds.begin(), ds.end());
}

CTransactionRef MakeEvmTx(uint16_t nType,
                          const std::vector<unsigned char>& payload)
{
    CMutableTransaction mtx;
    mtx.nVersion = 3;
    mtx.nType = nType;
    mtx.vExtraPayload = payload;
    return MakeTransactionRef(std::move(mtx));
}

CTransactionRef MakeNormalTx()
{
    CMutableTransaction mtx;
    mtx.nVersion = 3;
    mtx.nType = TRANSACTION_NORMAL;
    return MakeTransactionRef(std::move(mtx));
}

evm::CEvmCallTx MakeCall(uint8_t sender, uint8_t to, uint64_t value,
                        uint64_t gasLimit, uint64_t maxFee,
                        uint64_t priority, uint64_t nonce = 0)
{
    evm::CEvmCallTx p;
    p.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    uint256 toAddr;
    *(toAddr.begin() + 31) = to;
    p.toAddress = toAddr;
    p.value = value;
    p.gasLimit = gasLimit;
    p.maxFeePerGas = maxFee;
    p.maxPriorityFeePerGas = priority;
    p.senderHash = SenderHash(sender);
    p.nonce = nonce;
    return p;
}

// Re-seed an entire cache pair to identical state for paired runs.
void SeedTwoCaches(evm::CEvmStateCache& a, evm::CEvmStateCache& b)
{
    for (uint8_t s : {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7}) {
        a.SetAccount(SenderAddr(s), evm::CEvmAccount(
            0, evm::Uint256FromUint64(1'000'000'000),
            evm::CEvmAccount::EmptyCodeHash(),
            evm::CEvmAccount::EmptyStorageRoot()));
        b.SetAccount(SenderAddr(s), evm::CEvmAccount(
            0, evm::Uint256FromUint64(1'000'000'000),
            evm::CEvmAccount::EmptyCodeHash(),
            evm::CEvmAccount::EmptyStorageRoot()));
    }
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(evm_parallel_tests, BasicTestingSetup)

// ----------------------------------------------------------------------------
// Empty block: trivial agreement.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(empty_block_matches_serial)
{
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    CBlock block;
    block.vtx.push_back(MakeNormalTx());

    auto serial = evm::ProcessEvmTransactionsInBlock(block, nullptr, cache,
                                                    MakeContext(0));
    auto parallel = evm::ParallelProcessEvmTransactionsInBlock(
        block, nullptr, cache, MakeContext(0), {});

    BOOST_CHECK_EQUAL(serial.ok, parallel.ok);
    BOOST_CHECK_EQUAL(serial.totalBurned, parallel.totalBurned);
    BOOST_CHECK_EQUAL(serial.totalCoinbaseTip, parallel.totalCoinbaseTip);
    BOOST_CHECK_EQUAL(serial.txResults.size(), parallel.txResults.size());
}

// ----------------------------------------------------------------------------
// Single CALL: identical aggregates and per-tx status to serial.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(single_call_matches_serial)
{
    // Two caches, identical seed state. Run serial against one and
    // parallel against the other; compare.
    evm::CEvmStateDB dbA(1 << 20, true);
    evm::CEvmStateDB dbB(1 << 20, true);
    evm::CEvmStateCache cacheA(dbA);
    evm::CEvmStateCache cacheB(dbB);
    SeedTwoCaches(cacheA, cacheB);

    const auto ctx = MakeContext(/*baseFee=*/ 2);
    auto p = MakeCall(0xA0, 0x0E, 0, 100'000, 10, 1);
    CBlock block;
    block.vtx.push_back(MakeEvmTx(TRANSACTION_EVM_CALL, Serialize(p)));

    auto serial = evm::ProcessEvmTransactionsInBlock(block, nullptr, cacheA, ctx);
    auto parallel = evm::ParallelProcessEvmTransactionsInBlock(
        block, nullptr, cacheB, ctx, {});

    BOOST_REQUIRE_EQUAL(serial.txResults.size(), parallel.txResults.size());
    BOOST_CHECK_EQUAL(serial.txResults[0].apply.statusCode,
                     parallel.txResults[0].apply.statusCode);
    BOOST_CHECK_EQUAL(serial.txResults[0].apply.gasUsed,
                     parallel.txResults[0].apply.gasUsed);
    BOOST_CHECK_EQUAL(serial.totalCoinbaseTip, parallel.totalCoinbaseTip);
    BOOST_CHECK_EQUAL(serial.totalBurned, parallel.totalBurned);
}

// ----------------------------------------------------------------------------
// Many txs, many distinct senders: stress the worker pool, verify
// per-tx aggregates and final balances match the serial baseline.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(many_txs_match_serial)
{
    evm::CEvmStateDB dbA(1 << 20, true);
    evm::CEvmStateDB dbB(1 << 20, true);
    evm::CEvmStateCache cacheA(dbA);
    evm::CEvmStateCache cacheB(dbB);
    SeedTwoCaches(cacheA, cacheB);

    // Deploy an SSTORE contract at 0x77 on BOTH caches.
    const std::vector<uint8_t> code = {0x60, 0x42, 0x60, 0x01, 0x55, 0x00};
    const uint256 codeHash = evm::Keccak256(code);
    cacheA.SetCode(codeHash, code);
    cacheB.SetCode(codeHash, code);
    uint160 contractAddr;
    *(contractAddr.begin() + 19) = 0x77;
    cacheA.SetAccount(contractAddr, evm::CEvmAccount(
        1, uint256(), codeHash, evm::CEvmAccount::EmptyStorageRoot()));
    cacheB.SetAccount(contractAddr, evm::CEvmAccount(
        1, uint256(), codeHash, evm::CEvmAccount::EmptyStorageRoot()));

    // Build a block with 8 EVM CALLs from distinct senders (0xA0..0xA7),
    // interleaved with 2 non-EVM noise txs.
    CBlock block;
    block.vtx.push_back(MakeNormalTx());
    const auto ctx = MakeContext(/*baseFee=*/ 2);
    for (uint8_t i = 0; i < 8; ++i) {
        auto p = MakeCall(/*sender=*/ static_cast<uint8_t>(0xA0 + i),
                         /*to=*/ 0x77, /*value=*/ 0,
                         /*gasLimit=*/ 100'000, /*maxFee=*/ 10,
                         /*priority=*/ 1, /*nonce=*/ 0);
        block.vtx.push_back(MakeEvmTx(TRANSACTION_EVM_CALL, Serialize(p)));
        if (i == 3) {
            // Mid-block noise.
            block.vtx.push_back(MakeNormalTx());
        }
    }

    auto serial = evm::ProcessEvmTransactionsInBlock(block, nullptr, cacheA, ctx);
    auto parallel = evm::ParallelProcessEvmTransactionsInBlock(
        block, nullptr, cacheB, ctx, {});

    BOOST_REQUIRE(serial.ok);
    BOOST_REQUIRE(parallel.ok);
    BOOST_REQUIRE_EQUAL(serial.txResults.size(), parallel.txResults.size());
    BOOST_CHECK_EQUAL(serial.totalCoinbaseTip, parallel.totalCoinbaseTip);
    BOOST_CHECK_EQUAL(serial.totalBurned, parallel.totalBurned);

    // Per-tx status + gasUsed match.
    for (size_t i = 0; i < serial.txResults.size(); ++i) {
        BOOST_CHECK_EQUAL(serial.txResults[i].apply.statusCode,
                         parallel.txResults[i].apply.statusCode);
        BOOST_CHECK_EQUAL(serial.txResults[i].apply.gasUsed,
                         parallel.txResults[i].apply.gasUsed);
    }

    // Final state matches: every sender's balance and nonce is the
    // same in both caches.
    for (uint8_t s : {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7}) {
        evm::CEvmAccount accA;
        evm::CEvmAccount accB;
        BOOST_REQUIRE(cacheA.GetAccount(SenderAddr(s), accA));
        BOOST_REQUIRE(cacheB.GetAccount(SenderAddr(s), accB));
        BOOST_CHECK_EQUAL(accA.nonce, accB.nonce);
        BOOST_CHECK(accA.balance == accB.balance);
    }
}

// ----------------------------------------------------------------------------
// Pre-flight failure: both implementations report the same failedTxIndex.
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(preflight_failure_index_matches_serial)
{
    evm::CEvmStateDB dbA(1 << 20, true);
    evm::CEvmStateDB dbB(1 << 20, true);
    evm::CEvmStateCache cacheA(dbA);
    evm::CEvmStateCache cacheB(dbB);
    SeedTwoCaches(cacheA, cacheB);

    const auto ctx = MakeContext(2);
    // tx[0] valid; tx[1] with nonce mismatch.
    auto p0 = MakeCall(0xA0, 0x07, 0, 100'000, 10, 1, /*nonce=*/ 0);
    auto p1 = MakeCall(0xA0, 0x07, 0, 100'000, 10, 1, /*nonce=*/ 5);
    CBlock block;
    block.vtx.push_back(MakeEvmTx(TRANSACTION_EVM_CALL, Serialize(p0)));
    block.vtx.push_back(MakeEvmTx(TRANSACTION_EVM_CALL, Serialize(p1)));

    auto serial = evm::ProcessEvmTransactionsInBlock(block, nullptr, cacheA, ctx);
    auto parallel = evm::ParallelProcessEvmTransactionsInBlock(
        block, nullptr, cacheB, ctx, {});

    BOOST_CHECK(!serial.ok);
    BOOST_CHECK(!parallel.ok);
    BOOST_CHECK_EQUAL(serial.failedTxIndex, parallel.failedTxIndex);
}

BOOST_AUTO_TEST_SUITE_END()
