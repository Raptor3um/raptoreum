// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/validation.h>
#include <evm/mpt.h>
#include <evm/process.h>
#include <evm/receipt.h>
#include <evo/cbtx.h>
#include <evo/specialtx.h>
#include <primitives/block.h>
#include <script/standard.h>
#include <test/test_raptoreum.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <vector>

/**
 * D2 hard-fork — permanent live-path regression lock.
 *
 * Increment 6 made the EVM-commitment hard fork LIVE on regtest:
 * the miner produces a CCbTx v3 coinbase and every validator
 * recomputes + enforces the five committed values. Until now that
 * miner==validator parity was only proven by manual regtest runs —
 * a future change could silently break it and nothing automated
 * would catch the divergence (the worst class of consensus bug).
 *
 * TestChainSetup mines real blocks through the production
 * miner (CreateNewBlock) and connects them through the production
 * validator (ConnectBlock). On regtest EVM_COMMIT is force-active at
 * height 0, so EVERY block here is a v3 block: the mere fact that
 * CreateAndProcessBlock returns a connected block is itself proof
 * that the miner produced a v3 coinbase the validator accepted
 * (parity). These cases additionally pin the committed contents and
 * the EIP-1559 base-fee recurrence, and exercise a v3-block reorg.
 */

namespace {

struct D2ChainSetup : public TestChainSetup {
    D2ChainSetup() : TestChainSetup(1) {}
};

CCbTx CoinbaseCb(const CBlock& b)
{
    CCbTx cb;
    BOOST_REQUIRE(!b.vtx.empty());
    BOOST_REQUIRE(GetTxPayload(*b.vtx[0], cb));
    return cb;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(evm_d2_consensus_tests, D2ChainSetup)

// Mining a chain of v3 blocks: each connects (miner==validator
// parity), commits a v3 coinbase with the expected empty-block roots,
// and the committed base fee follows the canonical EIP-1559
// recurrence off the parent's committed (baseFee, gasUsed).
BOOST_AUTO_TEST_CASE(v3_coinbase_committed_and_eip1559_recurrence)
{
    const CScript spk =
        GetScriptForDestination(coinbaseKey.GetPubKey().GetID());
    const std::vector<CMutableTransaction> noTxns;
    const uint256 emptyReceiptsRoot = evm::ComputeReceiptsRoot({});

    bool havePrev = false;
    uint64_t prevBaseFee = 0, prevGasUsed = 0;

    for (int i = 0; i < 8; ++i) {
        const CBlock b = CreateAndProcessBlock(noTxns, spk);
        const CCbTx cb = CoinbaseCb(b);

        // Connected → the validator accepted this miner-built v3
        // coinbase. Pin the shape.
        BOOST_CHECK_EQUAL((int)cb.nVersion,
                          (int)CCbTx::EVM_COMMIT_VERSION);
        BOOST_CHECK_EQUAL(cb.evmGasUsed, 0u);  // no EVM txs
        BOOST_CHECK(cb.evmReceiptsRoot == emptyReceiptsRoot);
        BOOST_CHECK(cb.evmExecTime > 0);

        // EIP-1559: each block's committed base fee is exactly the
        // canonical function of the PARENT's committed (baseFee,
        // gasUsed). Empty blocks (gasUsed 0 < target) decay it toward
        // zero. The chain connected, so the validator already
        // enforced this — re-deriving locks the formula permanently.
        if (havePrev) {
            const uint64_t expect = evm::ComputeNextBaseFee(
                prevBaseFee, prevGasUsed, /*gasLimit=*/30'000'000);
            BOOST_CHECK_EQUAL(cb.evmBaseFee, expect);
        }
        havePrev = true;
        prevBaseFee = cb.evmBaseFee;
        prevGasUsed = cb.evmGasUsed;
    }
}

// A v3-block reorg: invalidating the tip disconnects a v3 block
// (running the EVM undo journal under the LIVE hard fork), then a
// longer competing branch of v3 blocks reconnects cleanly.
BOOST_AUTO_TEST_CASE(v3_block_reorg_reconnects)
{
    const CScript spk =
        GetScriptForDestination(coinbaseKey.GetPubKey().GetID());
    const std::vector<CMutableTransaction> noTxns;

    CreateAndProcessBlock(noTxns, spk);
    CreateAndProcessBlock(noTxns, spk);
    const int hBefore = ::ChainActive().Height();
    CBlockIndex* tip = ::ChainActive().Tip();
    BOOST_REQUIRE(tip != nullptr);

    // Disconnect the tip v3 block (EVM undo journal must unwind it).
    CValidationState state;
    BOOST_REQUIRE(InvalidateBlock(state, Params(), tip));
    BOOST_CHECK_EQUAL(::ChainActive().Height(), hBefore - 1);

    // Build a longer competing branch off the now-best parent; the
    // node reorgs onto it and every new block is a valid v3 block.
    ResetBlockFailureFlags(tip);
    CreateAndProcessBlock(noTxns, spk);
    const CBlock nb = CreateAndProcessBlock(noTxns, spk);
    BOOST_CHECK(::ChainActive().Height() >= hBefore);
    BOOST_CHECK_EQUAL((int)CoinbaseCb(nb).nVersion,
                      (int)CCbTx::EVM_COMMIT_VERSION);
}

BOOST_AUTO_TEST_SUITE_END()
