// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/validation.h>
#include <evm/account.h>
#include <evm/apply.h>
#include <evm/balance.h>
#include <evm/evmtx.h>
#include <evm/hashing.h>
#include <evm/rlp.h>
#include <evm/state_db.h>
#include <evo/specialtx.h>
#include <key.h>
#include <node/context.h>
#include <pubkey.h>
#include <primitives/transaction.h>
#include <rpc/server.h>
#include <rpc/request.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <test/test_raptoreum.h>
#include <txmempool.h>
#include <util/ref.h>
#include <util/strencodings.h>
#include <validation.h>

#include <univalue.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstring>
#include <vector>

/**
 * Full funded EVM e2e (permanent regression lock).
 *
 * Proves — and locks — the complete on-chain EVM usability path:
 *   1. evm::FUND moves RTM UTXO -> a specific EVM account (the one the
 *      eth key derives), through the production miner + validator.
 *   2. That account can now pay gas: a real EIP-155-signed Ethereum
 *      transaction from it is accepted via the eth_sendRawTransaction
 *      RPC, mined, and executed.
 *   3. eth_getTransactionReceipt resolves the receipt BY THE ETHEREUM
 *      TX HASH — the byte-order-sensitive cross-index path that was
 *      silently broken (receipts unreachable) until the ParseEthTxHash
 *      fix. This test would have caught it.
 */

namespace {

evm::RlpValue Str(std::vector<uint8_t> b)
{
    evm::RlpValue v; v.isList = false; v.bytes = std::move(b); return v;
}
evm::RlpValue Uint(uint64_t x)
{
    std::vector<uint8_t> be;
    for (int i = 7; i >= 0; --i) {
        const uint8_t byte = static_cast<uint8_t>((x >> (8 * i)) & 0xFF);
        if (be.empty() && byte == 0) continue;
        be.push_back(byte);
    }
    return Str(std::move(be));
}
evm::RlpValue List(std::vector<evm::RlpValue> items)
{
    evm::RlpValue v; v.isList = true; v.items = std::move(items); return v;
}
std::vector<uint8_t> StripLeadingZeros(const uint8_t* p, size_t n)
{
    size_t i = 0; while (i < n && p[i] == 0) ++i;
    return std::vector<uint8_t>(p + i, p + n);
}
uint160 EthAddressOf(const CKey& key)
{
    const CPubKey pub = key.GetPubKey();
    BOOST_REQUIRE(pub.size() == 65 && pub[0] == 0x04);
    const uint256 kh = evm::Keccak256(
        std::vector<uint8_t>(pub.begin() + 1, pub.begin() + 65));
    uint160 out;
    std::memcpy(out.begin(), kh.begin() + 12, 20);
    return out;
}

} // anonymous namespace

struct EthFundedE2ESetup : public TestChain100Setup {
    CKey ethKey;
    uint160 ethAddr;
    uint64_t chainId{0};

    EthFundedE2ESetup()
    {
        std::vector<unsigned char> secret(32, 0);
        secret[31] = 0x2A;  // deterministic key
        ethKey.Set(secret.begin(), secret.end(), /*fCompressed=*/false);
        BOOST_REQUIRE(ethKey.IsValid());
        ethAddr = EthAddressOf(ethKey);
        chainId = static_cast<uint64_t>(
            std::stoull(CallRpc("eth_chainId", UniValue(UniValue::VARR))
                            .get_str().substr(2), nullptr, 16));
    }

    UniValue CallRpc(const std::string& method, const UniValue& params)
    {
        util::Ref ctx{m_node};
        JSONRPCRequest req(ctx);
        req.strMethod = method;
        req.params = params;
        if (RPCIsInWarmup(nullptr)) SetRPCWarmupFinished();
        return tableRPC.execute(req);
    }

    // Build + sign a legacy EIP-155 contract-creation tx from ethKey.
    std::string SignedLegacyCreate(uint64_t nonce, uint64_t gasPrice,
                                   uint64_t gasLimit,
                                   const std::vector<uint8_t>& data)
    {
        const std::vector<uint8_t> sigPayload = evm::RlpEncode(List({
            Uint(nonce), Uint(gasPrice), Uint(gasLimit), Str({}),
            Uint(0), Str(data), Uint(chainId), Str({}), Str({})}));
        std::vector<unsigned char> sig;
        BOOST_REQUIRE(ethKey.SignCompact(evm::Keccak256(sigPayload), sig));
        BOOST_REQUIRE_EQUAL(sig.size(), 65U);
        const int recid = (sig[0] - 27) & 3;
        const std::vector<uint8_t> r(sig.begin() + 1, sig.begin() + 33);
        const std::vector<uint8_t> s(sig.begin() + 33, sig.begin() + 65);
        const uint64_t v = chainId * 2 + 35 + static_cast<uint64_t>(recid);
        const std::vector<uint8_t> wire = evm::RlpEncode(List({
            Uint(nonce), Uint(gasPrice), Uint(gasLimit), Str({}),
            Uint(0), Str(data), Uint(v),
            Str(StripLeadingZeros(r.data(), r.size())),
            Str(StripLeadingZeros(s.data(), s.size()))}));
        return "0x" + HexStr(wire);
    }

    // FUND the eth key's EVM account with `fundSat` satoshis via a
    // coinbase spend + a mined block (same flow as the first test case).
    void FundEthAccount(const CScript& cbScript, CAmount fundSat)
    {
        const uint64_t kSatToWeis = 10'000'000'000ULL;
        const CAmount cbValue = m_coinbase_txns[0]->vout[0].nValue;
        CMutableTransaction ftx;
        ftx.nVersion = 3;
        ftx.nType = TRANSACTION_EVM_FUND;
        ftx.vin.resize(1);
        ftx.vin[0].prevout.hash = m_coinbase_txns[0]->GetHash();
        ftx.vin[0].prevout.n = 0;
        ftx.vout.resize(1);
        ftx.vout[0].nValue = cbValue - fundSat - 1000;
        ftx.vout[0].scriptPubKey = cbScript;
        evm::CEvmFundTx p;
        p.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
        p.toAddress.SetNull();
        std::memcpy(p.toAddress.begin() + 12, ethAddr.begin(), 20);
        p.amount = static_cast<uint64_t>(fundSat) * kSatToWeis;
        SetTxPayload(ftx, p);
        std::vector<unsigned char> sig;
        const uint256 h = SignatureHash(cbScript, ftx, 0, SIGHASH_ALL, 0,
                                        SigVersion::BASE);
        BOOST_REQUIRE(coinbaseKey.Sign(h, sig));
        sig.push_back(static_cast<unsigned char>(SIGHASH_ALL));
        ftx.vin[0].scriptSig = CScript() << sig;
        CreateAndProcessBlock({ftx}, cbScript);
    }

    // Drain the mempool into a block, ordering EVM wrapper txs by their
    // payload nonce (the EVM process layer requires sequential nonces
    // within a block, and queryHashes gives no ordering guarantee).
    void MineMempoolNonceOrdered(const CScript& cbScript)
    {
        std::vector<uint256> mh;
        m_node.mempool->queryHashes(mh);
        BOOST_REQUIRE(!mh.empty());
        std::vector<CMutableTransaction> txns;
        for (const uint256& hh : mh) {
            CTransactionRef t = m_node.mempool->get(hh);
            if (t) txns.push_back(CMutableTransaction(*t));
        }
        std::sort(txns.begin(), txns.end(),
                  [](const CMutableTransaction& a, const CMutableTransaction& b) {
                      evm::CEvmDeployTx pa, pb;
                      const bool oka = GetTxPayload(a.vExtraPayload, pa);
                      const bool okb = GetTxPayload(b.vExtraPayload, pb);
                      if (!oka || !okb) return oka && !okb;
                      return pa.nonce < pb.nonce;
                  });
        CreateAndProcessBlock(txns, cbScript);
    }
};

BOOST_FIXTURE_TEST_SUITE(eth_funded_e2e_tests, EthFundedE2ESetup)

BOOST_AUTO_TEST_CASE(fund_then_deploy_then_receipt_by_eth_hash)
{
    const CScript cbScript =
        CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

    // 1. FUND the eth key's EVM account with ~1 RTM via a coinbase spend.
    const uint64_t kSatToWeis = 10'000'000'000ULL;
    const CAmount fundSat = 1'000'000;  // 0.01 RTM — covers gas
    const CAmount cbValue = m_coinbase_txns[0]->vout[0].nValue;
    {
        CMutableTransaction ftx;
        ftx.nVersion = 3;
        ftx.nType = TRANSACTION_EVM_FUND;
        ftx.vin.resize(1);
        ftx.vin[0].prevout.hash = m_coinbase_txns[0]->GetHash();
        ftx.vin[0].prevout.n = 0;
        ftx.vout.resize(1);
        ftx.vout[0].nValue = cbValue - fundSat - 1000;
        ftx.vout[0].scriptPubKey = cbScript;
        evm::CEvmFundTx p;
        p.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
        p.toAddress.SetNull();
        std::memcpy(p.toAddress.begin() + 12, ethAddr.begin(), 20);
        p.amount = static_cast<uint64_t>(fundSat) * kSatToWeis;
        SetTxPayload(ftx, p);
        std::vector<unsigned char> sig;
        const uint256 h = SignatureHash(cbScript, ftx, 0, SIGHASH_ALL, 0,
                                        SigVersion::BASE);
        BOOST_REQUIRE(coinbaseKey.Sign(h, sig));
        sig.push_back(static_cast<unsigned char>(SIGHASH_ALL));
        ftx.vin[0].scriptSig = CScript() << sig;
        CreateAndProcessBlock({ftx}, cbScript);
    }

    // The funded account exists with the credited balance.
    {
        evm::CEvmAccount acc;
        BOOST_REQUIRE(pevmstatedb->ReadAccount(ethAddr, acc));
        BOOST_CHECK(acc.balance == evm::Uint256FromUint64(
            static_cast<uint64_t>(fundSat) * kSatToWeis));
    }

    // 2. Submit a real EIP-155-signed contract creation FROM that
    //    account via eth_sendRawTransaction. gasPrice 1 gwei >= baseFee.
    const std::string raw = SignedLegacyCreate(
        /*nonce=*/0, /*gasPrice=*/1'000'000'000ULL, /*gasLimit=*/150000,
        /*data=*/{0x60, 0x05, 0x60, 0x04, 0x01});
    UniValue sp(UniValue::VARR);
    sp.push_back(raw);
    const UniValue sendRes = CallRpc("eth_sendRawTransaction", sp);
    BOOST_REQUIRE(sendRes.isStr());
    const std::string ethTxHash = sendRes.get_str();
    BOOST_CHECK_EQUAL(ethTxHash.size(), 66U);  // 0x + 32 bytes

    // 3. Mine the wrapper tx that eth_sendRawTransaction queued.
    std::vector<uint256> mh;
    m_node.mempool->queryHashes(mh);
    BOOST_REQUIRE(!mh.empty());
    std::vector<CMutableTransaction> mineTxns;
    for (const uint256& hh : mh) {
        CTransactionRef t = m_node.mempool->get(hh);
        if (t) mineTxns.push_back(CMutableTransaction(*t));
    }
    CreateAndProcessBlock(mineTxns, cbScript);

    // 4. The receipt resolves BY THE ETH TX HASH (byte-order path) and
    //    reflects a successful contract creation from the funded acct.
    UniValue rp(UniValue::VARR);
    rp.push_back(ethTxHash);
    const UniValue receipt = CallRpc("eth_getTransactionReceipt", rp);
    BOOST_REQUIRE_MESSAGE(receipt.isObject(),
        "receipt unreachable by eth tx hash (cross-index byte order)");
    BOOST_CHECK_EQUAL(find_value(receipt, "status").get_str(), "0x1");
    BOOST_CHECK_EQUAL(find_value(receipt, "transactionHash").get_str(),
                      ethTxHash);
    // from == the funded eth address (lowercase 0x hex).
    BOOST_CHECK_EQUAL(find_value(receipt, "from").get_str(),
                      "0x" + HexStr(std::vector<uint8_t>(
                          ethAddr.begin(), ethAddr.end())));
    // Contract creation populated a deployed address.
    const UniValue ca = find_value(receipt, "contractAddress");
    BOOST_CHECK(ca.isStr() && ca.get_str().rfind("0x", 0) == 0);

    // eth_getTransactionByHash also resolves by the eth hash, and now
    // projects the FULL Ethereum tx shape (FUP-3.4): a contract creation
    // has to=null, the deploy bytecode as `input`, the gas limit, nonce 0.
    UniValue bp(UniValue::VARR);
    bp.push_back(ethTxHash);
    const UniValue byHash = CallRpc("eth_getTransactionByHash", bp);
    BOOST_REQUIRE(byHash.isObject());
    BOOST_CHECK_EQUAL(find_value(byHash, "hash").get_str(), ethTxHash);
    BOOST_CHECK_EQUAL(find_value(byHash, "input").get_str(), "0x6005600401");
    BOOST_CHECK_EQUAL(find_value(byHash, "gas").get_str(), "0x249f0"); // 150000
    BOOST_CHECK_EQUAL(find_value(byHash, "nonce").get_str(), "0x0");
    BOOST_CHECK(find_value(byHash, "to").isNull());  // contract creation

    // 5. Cross-reference (FUP-3.7): the SAME eth tx hash must appear in the
    //    block listing — both the fullTx=true objects and the hash array.
    //    Before the fix the block emitted the reversed sha256d (and, for
    //    sendRawTransaction txs, never the keccak hash at all), so a dApp
    //    iterating block txs could not look up their receipts.
    const std::string blockHashStr =
        find_value(receipt, "blockHash").get_str();
    {
        UniValue gp(UniValue::VARR);
        gp.push_back(blockHashStr);
        gp.push_back(true);  // fullTx
        const UniValue blk = CallRpc("eth_getBlockByHash", gp);
        BOOST_REQUIRE(blk.isObject());
        // baseFeePerGas is present (post-London clients require it).
        BOOST_CHECK(find_value(blk, "baseFeePerGas").isStr());
        const UniValue& txObjs = find_value(blk, "transactions");
        BOOST_REQUIRE(txObjs.isArray());
        bool foundObj = false;
        for (size_t i = 0; i < txObjs.size(); ++i) {
            if (txObjs[i].isObject() &&
                find_value(txObjs[i], "hash").get_str() == ethTxHash) {
                foundObj = true;
                BOOST_CHECK_EQUAL(
                    find_value(txObjs[i], "input").get_str(), "0x6005600401");
            }
        }
        BOOST_CHECK_MESSAGE(foundObj,
            "block fullTx listing must carry the eth tx hash (cross-ref)");
    }
    {
        UniValue gp(UniValue::VARR);
        gp.push_back(blockHashStr);
        gp.push_back(false);  // hash-only listing
        const UniValue blk = CallRpc("eth_getBlockByHash", gp);
        const UniValue& hashes = find_value(blk, "transactions");
        BOOST_REQUIRE(hashes.isArray());
        bool foundHash = false;
        for (size_t i = 0; i < hashes.size(); ++i) {
            if (hashes[i].isStr() && hashes[i].get_str() == ethTxHash) {
                foundHash = true;
                break;
            }
        }
        BOOST_CHECK_MESSAGE(foundHash,
            "block hash-list must contain the eth tx hash (cross-ref)");
    }
}

// eth_getLogs regression set requested in PR review before testnet:
// the two canonical filter shapes, blockHash/range mutual exclusion,
// spec ordering (blockNumber, transactionIndex, block-cumulative
// logIndex), and reorg consistency (disconnect + reconnect).
BOOST_AUTO_TEST_CASE(getlogs_filters_ordering_and_reorg)
{
    const CScript cbScript =
        CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    FundEthAccount(cbScript, /*fundSat=*/1'000'000);

    // Two log-emitting contract creations mined in ONE block:
    //   tx A (nonce 0): LOG2(topics 0x..aa, 0x..bb) then LOG0  -> 2 logs
    //   tx B (nonce 1): LOG0                                    -> 1 log
    // Spec logIndex is the position within the BLOCK, so the three logs
    // must report 0x0, 0x1, 0x2 — a per-receipt counter would emit
    // 0x0, 0x1, 0x0 and break indexers.
    const std::vector<uint8_t> codeA = {
        0x60, 0xbb, 0x60, 0xaa, 0x60, 0x00, 0x60, 0x00, 0xa2,  // LOG2
        0x60, 0x00, 0x60, 0x00, 0xa0,                          // LOG0
        0x00};                                                 // STOP
    const std::vector<uint8_t> codeB = {
        0x60, 0x00, 0x60, 0x00, 0xa0,                          // LOG0
        0x00};                                                 // STOP
    UniValue sp(UniValue::VARR);
    sp.push_back(SignedLegacyCreate(0, 1'000'000'000ULL, 150000, codeA));
    const std::string hashA = CallRpc("eth_sendRawTransaction", sp).get_str();
    sp.setArray();
    sp.push_back(SignedLegacyCreate(1, 1'000'000'000ULL, 150000, codeB));
    CallRpc("eth_sendRawTransaction", sp);
    MineMempoolNonceOrdered(cbScript);

    // Coordinates + contract address from tx A's receipt.
    UniValue rp(UniValue::VARR);
    rp.push_back(hashA);
    const UniValue receiptA = CallRpc("eth_getTransactionReceipt", rp);
    BOOST_REQUIRE(receiptA.isObject());
    BOOST_REQUIRE_EQUAL(find_value(receiptA, "status").get_str(), "0x1");
    const std::string blockHashStr = find_value(receiptA, "blockHash").get_str();
    const std::string contractA = find_value(receiptA, "contractAddress").get_str();
    const uint64_t bn = std::stoull(
        find_value(receiptA, "blockNumber").get_str().substr(2), nullptr, 16);

    const std::string topicAA = "0x" + std::string(62, '0') + "aa";
    const std::string topicBB = "0x" + std::string(62, '0') + "bb";
    const std::string topicCC = "0x" + std::string(62, '0') + "cc";

    auto getLogs = [&](const UniValue& f) {
        UniValue p(UniValue::VARR);
        p.push_back(f);
        return CallRpc("eth_getLogs", p);
    };
    auto getLogsThrows = [&](const UniValue& f) -> bool {
        try { getLogs(f); return false; }
        catch (const UniValue&) { return true; }
        catch (const std::exception&) { return true; }
    };

    // --- Regression shape 1: range + address array + [null, [or-set]] ---
    // {"fromBlock":..,"toBlock":..,"address":["0x.."],"topics":[null,["0x..","0x.."]]}
    {
        UniValue f(UniValue::VOBJ);
        f.pushKV("fromBlock", strprintf("0x%x", bn - 1));
        f.pushKV("toBlock", strprintf("0x%x", bn));
        UniValue addrs(UniValue::VARR);
        addrs.push_back(contractA);
        f.pushKV("address", addrs);
        UniValue topics(UniValue::VARR);
        topics.push_back(UniValue());          // position 0: wildcard
        UniValue orSet(UniValue::VARR);        // position 1: BB or CC
        orSet.push_back(topicBB);
        orSet.push_back(topicCC);
        topics.push_back(orSet);
        f.pushKV("topics", topics);

        const UniValue r = getLogs(f);
        BOOST_REQUIRE(r.isArray());
        // Only tx A's LOG2 matches: position 1 == 0x..bb; the LOG0s have
        // no topics and tx B is a different address anyway.
        BOOST_REQUIRE_EQUAL(r.size(), 1U);
        BOOST_CHECK_EQUAL(find_value(r[0], "address").get_str(), contractA);
        BOOST_CHECK_EQUAL(find_value(r[0], "logIndex").get_str(), "0x0");
        const UniValue& ts = find_value(r[0], "topics");
        BOOST_REQUIRE_EQUAL(ts.size(), 2U);
        BOOST_CHECK_EQUAL(ts[0].get_str(), topicAA);
        BOOST_CHECK_EQUAL(ts[1].get_str(), topicBB);
    }

    // --- Regression shape 2: {"blockHash":"0x..","topics":[]} ------------
    // Empty topics array = no constraint; all three logs of the block, in
    // (blockNumber, transactionIndex, logIndex) order with BLOCK-cumulative
    // logIndex 0x0, 0x1, 0x2.
    {
        UniValue f(UniValue::VOBJ);
        f.pushKV("blockHash", blockHashStr);
        f.pushKV("topics", UniValue(UniValue::VARR));

        const UniValue r = getLogs(f);
        BOOST_REQUIRE(r.isArray());
        BOOST_REQUIRE_EQUAL(r.size(), 3U);
        uint64_t prevTx = 0, prevLi = 0;
        for (size_t i = 0; i < 3; ++i) {
            BOOST_CHECK_EQUAL(find_value(r[i], "blockHash").get_str(),
                              blockHashStr);
            BOOST_CHECK_EQUAL(find_value(r[i], "logIndex").get_str(),
                              strprintf("0x%x", i));
            const uint64_t txi = std::stoull(
                find_value(r[i], "transactionIndex").get_str().substr(2),
                nullptr, 16);
            const uint64_t li = std::stoull(
                find_value(r[i], "logIndex").get_str().substr(2), nullptr, 16);
            if (i > 0) {
                BOOST_CHECK_MESSAGE(
                    txi > prevTx || (txi == prevTx && li > prevLi),
                    "logs must be ordered by (transactionIndex, logIndex)");
            }
            prevTx = txi; prevLi = li;
        }
        // tx A carries logs 0,1; tx B carries log 2 — different txIndex.
        BOOST_CHECK(find_value(r[0], "transactionIndex").get_str() ==
                    find_value(r[1], "transactionIndex").get_str());
        BOOST_CHECK(find_value(r[1], "transactionIndex").get_str() !=
                    find_value(r[2], "transactionIndex").get_str());
    }

    // --- blockHash is mutually exclusive with fromBlock/toBlock ----------
    {
        UniValue f(UniValue::VOBJ);
        f.pushKV("blockHash", blockHashStr);
        f.pushKV("fromBlock", "0x0");
        BOOST_CHECK(getLogsThrows(f));
        UniValue g(UniValue::VOBJ);
        g.pushKV("blockHash", blockHashStr);
        g.pushKV("toBlock", "latest");
        BOOST_CHECK(getLogsThrows(g));
    }

    // --- Reorg consistency: disconnect, then reconnect -------------------
    CBlockIndex* logTip = nullptr;
    {
        LOCK(cs_main);
        logTip = ::ChainActive().Tip();
    }
    BOOST_REQUIRE(logTip != nullptr);
    {
        CValidationState st;
        BOOST_REQUIRE(InvalidateBlock(st, Params(), logTip));
    }
    // The orphaned block's logs must be an explicit error by hash (the
    // indexer's roll-back signal), and absent from range queries.
    {
        UniValue f(UniValue::VOBJ);
        f.pushKV("blockHash", blockHashStr);
        BOOST_CHECK(getLogsThrows(f));
        UniValue g(UniValue::VOBJ);
        g.pushKV("fromBlock", strprintf("0x%x", bn - 1));
        g.pushKV("toBlock", "latest");
        const UniValue r = getLogs(g);
        BOOST_REQUIRE(r.isArray());
        BOOST_CHECK_EQUAL(r.size(), 0U);
    }
    // Reconnect: the same block returns to the canonical chain and the
    // exact same three logs are served again.
    {
        {
            LOCK(cs_main);
            ResetBlockFailureFlags(logTip);
        }
        CValidationState st;
        BOOST_REQUIRE(ActivateBestChain(st, Params()));
        UniValue f(UniValue::VOBJ);
        f.pushKV("blockHash", blockHashStr);
        const UniValue r = getLogs(f);
        BOOST_REQUIRE(r.isArray());
        BOOST_CHECK_EQUAL(r.size(), 3U);
        BOOST_CHECK_EQUAL(find_value(r[2], "logIndex").get_str(), "0x2");
    }
}

BOOST_AUTO_TEST_SUITE_END()
