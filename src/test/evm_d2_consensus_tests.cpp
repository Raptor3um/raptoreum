// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/validation.h>
#include <evm/account.h>
#include <evm/balance.h>
#include <evm/evmtx.h>
#include <evm/hashing.h>
#include <evm/mpt.h>
#include <evm/process.h>
#include <evm/receipt.h>
#include <evm/state_cache.h>
#include <evm/state_db.h>
#include <evo/cbtx.h>
#include <evo/specialtx.h>
#include <primitives/block.h>
#include <rpc/server.h>
#include <script/standard.h>
#include <test/test_raptoreum.h>
#include <util/ref.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <cstring>
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

BOOST_AUTO_TEST_CASE(chainid_network_parameters)
{
    BOOST_CHECK_EQUAL(CreateChainParams(CBaseChainParams::MAIN)->GetConsensus().evmChainId, 7373);
    BOOST_CHECK_EQUAL(CreateChainParams(CBaseChainParams::TESTNET)->GetConsensus().evmChainId, 7374);
    BOOST_CHECK_EQUAL(CreateChainParams(CBaseChainParams::REGTEST)->GetConsensus().evmChainId, 7375);
}

BOOST_AUTO_TEST_CASE(chainid_agrees_between_rpc_mining_and_validation)
{
    const uint160 sender(std::vector<unsigned char>(20, 0xa1));
    const uint160 contract(std::vector<unsigned char>(20, 0xb2));
    // CHAINID; duplicate, store at slot zero, and return the same 32-byte value.
    const std::vector<uint8_t> code = {
        0x46, 0x80, 0x60, 0x00, 0x55, 0x60, 0x00, 0x52,
        0x60, 0x20, 0x60, 0x00, 0xf3,
    };
    {
        evm::CEvmStateCache cache(*pevmstatedb);
        cache.SetAccount(sender, evm::CEvmAccount(
            0, evm::Uint256FromUint64(1'000'000'000'000'000ULL),
            evm::CEvmAccount::EmptyCodeHash(), evm::CEvmAccount::EmptyStorageRoot()));
        const uint256 codeHash = evm::Keccak256(code);
        cache.SetCode(codeHash, code);
        cache.SetAccount(contract, evm::CEvmAccount(
            1, uint256(), codeHash, evm::CEvmAccount::EmptyStorageRoot()));
        BOOST_REQUIRE(cache.Flush());
    }

    evm::CEvmCallTx payload;
    std::memcpy(payload.senderHash.begin() + 12, sender.begin(), 20);
    std::memcpy(payload.toAddress.begin() + 12, contract.begin(), 20);
    payload.gasLimit = 100'000;
    payload.maxFeePerGas = 1'000'000'000;
    CMutableTransaction call;
    call.nVersion = 3;
    call.nType = TRANSACTION_EVM_CALL;
    SetTxPayload(call, payload);

    const CScript spk = GetScriptForDestination(coinbaseKey.GetPubKey().GetID());
    const CBlock block = CreateAndProcessBlock({call}, spk);
    BOOST_REQUIRE(::ChainActive().Tip()->GetBlockHash() == block.GetHash());
    uint256 stored;
    BOOST_REQUIRE(pevmstatedb->ReadStorage(contract, uint256(), stored));
    BOOST_CHECK(stored == evm::Uint256FromUint64(7375));

    if (RPCIsInWarmup(nullptr)) SetRPCWarmupFinished();
    util::Ref context{m_node};
    JSONRPCRequest request(context);
    request.strMethod = "eth_chainId";
    request.params = UniValue(UniValue::VARR);
    BOOST_CHECK_EQUAL(tableRPC.execute(request).get_str(), "0x1ccf");

    UniValue object(UniValue::VOBJ);
    object.pushKV("to", "0x" + contract.GetHex());
    request.strMethod = "eth_call";
    request.params.push_back(object);
    request.params.push_back("latest");
    BOOST_CHECK_EQUAL(tableRPC.execute(request).get_str(),
                     "0x0000000000000000000000000000000000000000000000000000000000001ccf");
}

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
