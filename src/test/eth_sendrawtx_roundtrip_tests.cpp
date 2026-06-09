// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/server.h>
#include <rpc/request.h>

#include <evm/hashing.h>
#include <evm/rawtx.h>
#include <evm/rlp.h>
#include <evm/signing.h>

#include <key.h>
#include <pubkey.h>
#include <uint256.h>

#include <test/test_raptoreum.h>
#include <node/context.h>
#include <util/ref.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <univalue.h>

#include <string>
#include <vector>

/**
 * eth_sendRawTransaction behavioural round-trip.
 *
 * eth_sendRawTransaction is the single most security- and
 * compatibility-critical RPC: it ingests an externally-signed
 * Ethereum transaction, RLP-decodes it, verifies the secp256k1
 * signature, RECOVERS the sender, validates EIP-155 replay
 * protection, and maps it to a Raptoreum special tx. A bug here =
 * wrong sender / accepted forged or cross-chain tx / dApp
 * incompatibility. evm_rlp_tests covers the RLP codec in isolation;
 * nothing exercised the full external-signer → DecodeRawEthTx →
 * eth_sendRawTransaction path.
 *
 * This test acts as an independent Ethereum wallet: it builds and
 * signs a legacy EIP-155 tx, an EIP-1559 (type-2) tx, and a
 * contract-creation tx using the project's own secp256k1 + Keccac +
 * RLP, then asserts the production decoder recovers exactly the
 * signing address and every field, rejects a wrong chain id and a
 * tampered signature, and that the RPC handler maps + accepts a
 * well-formed tx without crashing.
 */

namespace {

evm::RlpValue Str(std::vector<uint8_t> b)
{
    evm::RlpValue v;
    v.isList = false;
    v.bytes = std::move(b);
    return v;
}

// RLP integer: minimal big-endian, zero == empty byte string.
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

evm::RlpValue Bytes20(const uint160& a)
{
    return Str(std::vector<uint8_t>(a.begin(), a.end()));
}

evm::RlpValue List(std::vector<evm::RlpValue> items)
{
    evm::RlpValue v;
    v.isList = true;
    v.items = std::move(items);
    return v;
}

// uint256 (big-endian word) -> minimal RLP integer bytes.
std::vector<uint8_t> StripLeadingZeros(const uint8_t* p, size_t n)
{
    size_t i = 0;
    while (i < n && p[i] == 0) ++i;
    return std::vector<uint8_t>(p + i, p + n);
}

// secp256k1 sign the Keccak-256 of `payload`; return r,s (32-byte
// big-endian) and the recovery id (0/1).
void SignKeccak(const CKey& key, const std::vector<uint8_t>& payload,
                std::vector<uint8_t>& r, std::vector<uint8_t>& s,
                int& recid)
{
    const uint256 h = evm::Keccak256(payload);
    std::vector<unsigned char> sig;
    BOOST_REQUIRE(key.SignCompact(h, sig));
    BOOST_REQUIRE_EQUAL(sig.size(), 65U);
    // Bitcoin compact header = 27 + recid (+4 if compressed pubkey).
    recid = (sig[0] - 27) & 3;
    r.assign(sig.begin() + 1, sig.begin() + 33);
    s.assign(sig.begin() + 33, sig.begin() + 65);
}

uint160 EthAddressOf(const CKey& key)
{
    // Ethereum address = last 20 bytes of keccak256(uncompressed
    // pubkey without the 0x04 prefix).
    const CPubKey pub = key.GetPubKey();
    BOOST_REQUIRE(pub.size() == 65 && pub[0] == 0x04);
    const uint256 kh = evm::Keccak256(
        std::vector<uint8_t>(pub.begin() + 1, pub.begin() + 65));
    uint160 out;
    std::memcpy(out.begin(), kh.begin() + 12, 20);
    return out;
}

std::vector<uint8_t> Concat(std::vector<uint8_t> a,
                            const std::vector<uint8_t>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

} // anonymous namespace

struct EthRawTxSetup : public TestingSetup {
    CKey key;
    uint160 ethAddr;
    uint64_t chainId{0};

    EthRawTxSetup()
    {
        // Deterministic, uncompressed (Ethereum needs the 65-byte
        // pubkey for address derivation + recoverable sigs).
        std::vector<unsigned char> secret(32, 0);
        secret[31] = 0x2A;  // private key = 42
        key.Set(secret.begin(), secret.end(), /*fCompressed=*/false);
        BOOST_REQUIRE(key.IsValid());
        ethAddr = EthAddressOf(key);

        util::Ref ctx{m_node};
        JSONRPCRequest req(ctx);
        req.strMethod = "eth_chainId";
        req.params = UniValue(UniValue::VARR);
        if (RPCIsInWarmup(nullptr)) SetRPCWarmupFinished();
        const std::string hx = tableRPC.execute(req).get_str();
        chainId = std::stoull(hx.substr(2), nullptr, 16);
    }

    // legacy EIP-155 signed wire bytes.
    std::vector<uint8_t> BuildLegacy(uint64_t nonce, const uint160* to,
                                     uint64_t value, uint64_t gasPrice,
                                     uint64_t gasLimit,
                                     const std::vector<uint8_t>& data)
    {
        const evm::RlpValue toV =
            to ? Bytes20(*to) : Str({});  // empty == contract creation
        // EIP-155 signing payload: [nonce,gasPrice,gas,to,value,data,
        //                           chainId,0,0]
        const std::vector<uint8_t> sigPayload = evm::RlpEncode(List({
            Uint(nonce), Uint(gasPrice), Uint(gasLimit), toV,
            Uint(value), Str(data), Uint(chainId), Str({}), Str({})}));
        std::vector<uint8_t> r, s;
        int recid = 0;
        SignKeccak(key, sigPayload, r, s, recid);
        const uint64_t v = chainId * 2 + 35 + static_cast<uint64_t>(recid);
        return evm::RlpEncode(List({
            Uint(nonce), Uint(gasPrice), Uint(gasLimit), toV,
            Uint(value), Str(data), Uint(v),
            Str(StripLeadingZeros(r.data(), r.size())),
            Str(StripLeadingZeros(s.data(), s.size()))}));
    }

    // EIP-1559 (type 0x02) signed wire bytes.
    std::vector<uint8_t> Build1559(uint64_t nonce, const uint160& to,
                                   uint64_t value, uint64_t maxPrio,
                                   uint64_t maxFee, uint64_t gasLimit,
                                   const std::vector<uint8_t>& data)
    {
        const evm::RlpValue accessList = List({});
        const std::vector<uint8_t> body = evm::RlpEncode(List({
            Uint(chainId), Uint(nonce), Uint(maxPrio), Uint(maxFee),
            Uint(gasLimit), Bytes20(to), Uint(value), Str(data),
            accessList}));
        const std::vector<uint8_t> sigPayload = Concat({0x02}, body);
        std::vector<uint8_t> r, s;
        int recid = 0;
        SignKeccak(key, sigPayload, r, s, recid);
        const std::vector<uint8_t> signed_ = evm::RlpEncode(List({
            Uint(chainId), Uint(nonce), Uint(maxPrio), Uint(maxFee),
            Uint(gasLimit), Bytes20(to), Uint(value), Str(data),
            accessList, Uint(static_cast<uint64_t>(recid)),
            Str(StripLeadingZeros(r.data(), r.size())),
            Str(StripLeadingZeros(s.data(), s.size()))}));
        return Concat({0x02}, signed_);
    }

    UniValue SendRaw(const std::vector<uint8_t>& wire)
    {
        util::Ref ctx{m_node};
        JSONRPCRequest req(ctx);
        req.strMethod = "eth_sendRawTransaction";
        UniValue p(UniValue::VARR);
        p.push_back("0x" + HexStr(wire));
        req.params = p;
        if (RPCIsInWarmup(nullptr)) SetRPCWarmupFinished();
        try {
            return tableRPC.execute(req);
        } catch (const UniValue& e) {
            throw std::runtime_error(find_value(e, "message").get_str());
        }
    }
};

BOOST_FIXTURE_TEST_SUITE(eth_sendrawtx_roundtrip_tests, EthRawTxSetup)

BOOST_AUTO_TEST_CASE(legacy_eip155_decodes_to_signing_address)
{
    uint160 to;
    for (int i = 0; i < 20; ++i) *(to.begin() + i) = uint8_t(0x11 * (i & 1));
    const std::vector<uint8_t> data = {0xde, 0xad, 0xbe, 0xef};
    const auto wire = BuildLegacy(7, &to, 123456, 1000, 90000, data);

    evm::DecodedRawTx d;
    BOOST_REQUIRE(evm::DecodeRawEthTx(wire, chainId, d));
    BOOST_CHECK(d.sender == ethAddr);          // signature recovery
    BOOST_CHECK_EQUAL(d.txType, 0);
    BOOST_CHECK_EQUAL(d.nonce, 7U);
    BOOST_CHECK_EQUAL(d.value, 123456U);
    BOOST_CHECK_EQUAL(d.gasLimit, 90000U);
    BOOST_CHECK(!d.emptyTo);
    BOOST_CHECK(d.to == to);
    BOOST_CHECK(d.data == data);
}

BOOST_AUTO_TEST_CASE(eip1559_type2_decodes_to_signing_address)
{
    uint160 to;
    for (int i = 0; i < 20; ++i) *(to.begin() + i) = uint8_t(0xA0 + i);
    const std::vector<uint8_t> data = {0x60, 0x00, 0x60, 0x00};
    const auto wire = Build1559(3, to, 1, 2, 1000000000ULL, 21000, data);

    evm::DecodedRawTx d;
    BOOST_REQUIRE(evm::DecodeRawEthTx(wire, chainId, d));
    BOOST_CHECK(d.sender == ethAddr);
    BOOST_CHECK_EQUAL(d.txType, 2);
    BOOST_CHECK_EQUAL(d.nonce, 3U);
    BOOST_CHECK_EQUAL(d.value, 1U);
    BOOST_CHECK_EQUAL(d.maxPriorityFeePerGas, 2U);
    BOOST_CHECK_EQUAL(d.maxFeePerGas, 1000000000ULL);
    BOOST_CHECK_EQUAL(d.gasLimit, 21000U);
    BOOST_CHECK(d.to == to);
    BOOST_CHECK(d.data == data);
}

BOOST_AUTO_TEST_CASE(contract_creation_sets_empty_to)
{
    const std::vector<uint8_t> initCode = {0x60, 0x0a, 0x60, 0x00};
    const auto wire = BuildLegacy(0, nullptr, 0, 1000, 200000, initCode);

    evm::DecodedRawTx d;
    BOOST_REQUIRE(evm::DecodeRawEthTx(wire, chainId, d));
    BOOST_CHECK(d.sender == ethAddr);
    BOOST_CHECK(d.emptyTo);
    BOOST_CHECK(d.data == initCode);
}

BOOST_AUTO_TEST_CASE(wrong_chain_id_is_rejected)
{
    uint160 to;
    const auto wire = BuildLegacy(1, &to, 0, 1000, 90000, {});
    evm::DecodedRawTx d;
    // EIP-155 replay protection: a tx signed for chainId must not
    // decode under a different chain id.
    BOOST_CHECK(!evm::DecodeRawEthTx(wire, chainId + 1, d));
}

BOOST_AUTO_TEST_CASE(tampered_signature_does_not_recover_signer)
{
    uint160 to;
    auto wire = BuildLegacy(1, &to, 0, 1000, 90000, {0x01});
    // Flip a byte near the end (inside r/s) and require that the
    // decoder either rejects it or recovers a DIFFERENT address —
    // never silently accepts it as ours.
    wire[wire.size() - 5] ^= 0xFF;
    evm::DecodedRawTx d;
    const bool ok = evm::DecodeRawEthTx(wire, chainId, d);
    BOOST_CHECK(!(ok && d.sender == ethAddr));
}

BOOST_AUTO_TEST_CASE(rpc_handler_maps_and_does_not_crash)
{
    uint160 to;
    for (int i = 0; i < 20; ++i) *(to.begin() + i) = uint8_t(0x22);
    const auto wire = BuildLegacy(0, &to, 0, 1000, 90000, {0x00});

    // The handler decodes, maps to a TRANSACTION_EVM_CALL and
    // broadcasts. Mempool acceptance depends on EVM-account funding
    // (out of scope for an in-process unit test), so accept either a
    // well-formed 0x + 64-hex tx hash OR a CLEAN rpc error — never an
    // uncaught throw / crash. This proves the decode→map→submit wiring
    // is reachable and safe.
    try {
        const UniValue r = SendRaw(wire);
        BOOST_REQUIRE(r.isStr());
        const std::string h = r.get_str();
        BOOST_CHECK(h.rfind("0x", 0) == 0);
        BOOST_CHECK_EQUAL(h.size(), 66U);  // 0x + 32-byte keccak
    } catch (const std::runtime_error&) {
        // Clean, mapped RPC error — acceptable (e.g. unfunded sender).
    }
}

BOOST_AUTO_TEST_CASE(rpc_rejects_garbage_wire_cleanly)
{
    BOOST_CHECK_THROW(SendRaw({0xde, 0xad, 0xbe, 0xef}),
                      std::runtime_error);
}

// The PRODUCTION evm::SignLegacyTx primitive (FUP-5.4) must produce a wire
// blob the production decoder recovers to the exact signing address and
// fields — the same guarantee SignEip1559Tx already had, now for legacy
// (EIP-155, type 0x0) txs that older tooling / hardware wallets emit.
BOOST_AUTO_TEST_CASE(sign_legacy_primitive_round_trips)
{
    uint160 to;
    for (int i = 0; i < 20; ++i) *(to.begin() + i) = uint8_t(0x33);

    evm::LegacyTxFields f;
    f.chainId = chainId;
    f.nonce = 9;
    f.gasPrice = 1'000'000'000ULL;
    f.gasLimit = 90000;
    f.emptyTo = false;
    f.to = to;
    f.value = 555;
    f.data = {0xca, 0xfe};

    const std::vector<uint8_t> wire = evm::SignLegacyTx(key, f);
    BOOST_REQUIRE(!wire.empty());
    evm::DecodedRawTx d;
    BOOST_REQUIRE(evm::DecodeRawEthTx(wire, chainId, d));
    BOOST_CHECK(d.sender == ethAddr);   // signature recovery -> signer
    BOOST_CHECK_EQUAL(d.txType, 0);     // legacy
    BOOST_CHECK_EQUAL(d.nonce, 9U);
    BOOST_CHECK_EQUAL(d.gasLimit, 90000U);
    BOOST_CHECK_EQUAL(d.value, 555U);
    BOOST_CHECK(!d.emptyTo);
    BOOST_CHECK(d.to == to);
    BOOST_CHECK(d.data == f.data);

    // Contract creation (emptyTo) round-trips too.
    evm::LegacyTxFields c = f;
    c.emptyTo = true;
    c.data = {0x60, 0x0a, 0x60, 0x00};
    const std::vector<uint8_t> wireC = evm::SignLegacyTx(key, c);
    BOOST_REQUIRE(!wireC.empty());
    evm::DecodedRawTx dc;
    BOOST_REQUIRE(evm::DecodeRawEthTx(wireC, chainId, dc));
    BOOST_CHECK(dc.sender == ethAddr);
    BOOST_CHECK(dc.emptyTo);
    BOOST_CHECK(dc.data == c.data);

    // EIP-155 replay protection: the tx must NOT decode under another chain.
    {
        evm::DecodedRawTx wrong;
        BOOST_CHECK(!evm::DecodeRawEthTx(wire, chainId + 1, wrong));
    }
    // chainId 0 is refused (this build mandates EIP-155 replay protection).
    {
        evm::LegacyTxFields z = f;
        z.chainId = 0;
        BOOST_CHECK(evm::SignLegacyTx(key, z).empty());
    }
}

BOOST_AUTO_TEST_SUITE_END()
