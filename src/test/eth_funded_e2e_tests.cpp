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

BOOST_AUTO_TEST_SUITE_END()
