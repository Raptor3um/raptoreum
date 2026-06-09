// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/server.h>
#include <rpc/request.h>

#include <test/test_raptoreum.h>
#include <node/context.h>
#include <util/ref.h>

#include <boost/test/unit_test.hpp>

#include <univalue.h>

#include <string>

/**
 * eth_* JSON-RPC conformance.
 *
 * src/rpc/ethereum.cpp implements ~28 eth_/net_/web3_ methods but has
 * no spec-conformance coverage. The single biggest source of
 * ethers.js / viem / MetaMask incompatibility is the encoding
 * contract, not the business logic:
 *
 *   - QUANTITY values MUST be "0x"-prefixed, minimal (no leading
 *     zeros; zero is exactly "0x0"), lowercase hex.
 *   - DATA values MUST be "0x"-prefixed, even-length lowercase hex
 *     ("0x" for empty).
 *   - net_version is a DECIMAL string while eth_chainId is a hex
 *     QUANTITY — mixing these silently breaks wallets.
 *   - Block objects must carry the canonical field names.
 *   - Bad input must produce a clean JSON-RPC error, never a crash.
 *
 * These run under the regtest TestingSetup (chain at genesis,
 * cs_main available). The handlers degrade gracefully when the EVM
 * state DB is absent, so the encoding/error contract is exercised
 * without needing an EVM-activated chain. Deep behavioural coverage
 * (a real RLP-signed eth_sendRawTransaction round-trip) is a
 * follow-up layer.
 */

namespace {

bool IsHex(const std::string& s, size_t from)
{
    if (from >= s.size()) return false;
    for (size_t i = from; i < s.size(); ++i) {
        const char c = s[i];
        const bool ok = (c >= '0' && c <= '9') ||
                        (c >= 'a' && c <= 'f');
        if (!ok) return false;  // lowercase only, per spec
    }
    return true;
}

// Ethereum QUANTITY: "0x0", or "0x" + lowercase hex with no leading
// zero digit.
bool IsEthQuantity(const std::string& s)
{
    if (s.rfind("0x", 0) != 0) return false;
    if (s == "0x0") return true;
    if (s.size() < 3) return false;
    if (s[2] == '0') return false;       // no leading zeros
    return IsHex(s, 2);
}

// Ethereum DATA: "0x", or "0x" + even number of lowercase hex digits.
bool IsEthData(const std::string& s)
{
    if (s.rfind("0x", 0) != 0) return false;
    if (s == "0x") return true;
    if (((s.size() - 2) % 2) != 0) return false;
    return IsHex(s, 2);
}

UniValue Arr(std::initializer_list<UniValue> items)
{
    UniValue a(UniValue::VARR);
    for (const auto& it : items) a.push_back(it);
    return a;
}

} // anonymous namespace

struct EthRpcTestingSetup : public TestingSetup {
    UniValue CallEth(const std::string& method, const UniValue& params)
    {
        util::Ref context{m_node};
        JSONRPCRequest request(context);
        request.strMethod = method;
        request.params = params;
        request.fHelp = false;
        if (RPCIsInWarmup(nullptr)) SetRPCWarmupFinished();
        try {
            return tableRPC.execute(request);
        } catch (const UniValue& objError) {
            // Mirror rpc_tests.cpp: surface RPC errors as std::exception
            // so BOOST_CHECK_THROW(..., std::runtime_error) works and a
            // genuine crash is still distinguishable from a clean error.
            throw std::runtime_error(
                find_value(objError, "message").get_str());
        }
    }
};

BOOST_FIXTURE_TEST_SUITE(eth_rpc_conformance_tests, EthRpcTestingSetup)

// --- pure / deterministic ---------------------------------------------

BOOST_AUTO_TEST_CASE(chain_id_is_well_formed_hex_quantity)
{
    // The value is network-dependent (7373/7374/7375); assert the
    // CONTRACT, not a hardcoded network: a well-formed, non-zero hex
    // QUANTITY. (A zero chain id would break EIP-155 replay
    // protection.)
    const UniValue r = CallEth("eth_chainId", UniValue(UniValue::VARR));
    BOOST_REQUIRE(r.isStr());
    BOOST_CHECK(IsEthQuantity(r.get_str()));
    BOOST_CHECK(r.get_str() != "0x0");
    BOOST_CHECK(std::stoull(r.get_str().substr(2), nullptr, 16) > 0);
}

BOOST_AUTO_TEST_CASE(net_version_decimal_matches_chain_id_hex)
{
    // The wallet-breaking trap is INCONSISTENCY: net_version is a
    // DECIMAL string and eth_chainId a hex QUANTITY, but they MUST
    // denote the same id. ethers/viem cross-check these.
    const std::string chainHex =
        CallEth("eth_chainId", UniValue(UniValue::VARR)).get_str();
    const unsigned long long chainId =
        std::stoull(chainHex.substr(2), nullptr, 16);

    const UniValue nv = CallEth("net_version", UniValue(UniValue::VARR));
    BOOST_REQUIRE(nv.isStr());
    const std::string nvs = nv.get_str();
    BOOST_REQUIRE(!nvs.empty());
    for (char c : nvs) {
        BOOST_CHECK_MESSAGE(c >= '0' && c <= '9',
            "net_version must be a plain decimal string, got: " << nvs);
    }
    BOOST_CHECK_EQUAL(std::stoull(nvs), chainId);
}

BOOST_AUTO_TEST_CASE(block_number_is_quantity_zero_at_genesis)
{
    const UniValue r = CallEth("eth_blockNumber", UniValue(UniValue::VARR));
    BOOST_REQUIRE(r.isStr());
    BOOST_CHECK(IsEthQuantity(r.get_str()));
    BOOST_CHECK_EQUAL(r.get_str(), "0x0");
}

BOOST_AUTO_TEST_CASE(gas_price_and_priority_fee_are_quantities)
{
    BOOST_CHECK(IsEthQuantity(
        CallEth("eth_gasPrice", UniValue(UniValue::VARR)).get_str()));
    BOOST_CHECK(IsEthQuantity(
        CallEth("eth_maxPriorityFeePerGas",
                UniValue(UniValue::VARR)).get_str()));
}

BOOST_AUTO_TEST_CASE(client_version_and_protocol_are_strings)
{
    BOOST_CHECK(
        !CallEth("web3_clientVersion",
                 UniValue(UniValue::VARR)).get_str().empty());
    BOOST_CHECK(
        CallEth("eth_protocolVersion", UniValue(UniValue::VARR)).isStr());
}

BOOST_AUTO_TEST_CASE(node_status_methods_have_expected_types)
{
    BOOST_CHECK(
        CallEth("net_listening", UniValue(UniValue::VARR)).isBool());
    BOOST_CHECK(
        CallEth("eth_mining", UniValue(UniValue::VARR)).isBool());
    // eth_syncing: false or an object — must be defined, never throw.
    const UniValue s = CallEth("eth_syncing", UniValue(UniValue::VARR));
    BOOST_CHECK(s.isBool() || s.isObject());
    BOOST_CHECK(
        CallEth("eth_accounts", UniValue(UniValue::VARR)).isArray());
}

// --- state accessors: encoding contract on the default path -----------

BOOST_AUTO_TEST_CASE(state_accessors_obey_encoding_contract)
{
    const std::string addr = "0x000000000000000000000000000000000000000a";
    const std::string slot =
        "0x0000000000000000000000000000000000000000000000000000000000000001";

    BOOST_CHECK(IsEthQuantity(
        CallEth("eth_getBalance", Arr({addr, "latest"})).get_str()));
    BOOST_CHECK(IsEthQuantity(
        CallEth("eth_getTransactionCount",
                Arr({addr, "latest"})).get_str()));
    BOOST_CHECK(IsEthData(
        CallEth("eth_getCode", Arr({addr, "latest"})).get_str()));
    BOOST_CHECK(IsEthData(
        CallEth("eth_getStorageAt",
                Arr({addr, slot, "latest"})).get_str()));
}

// --- block object shape -----------------------------------------------

BOOST_AUTO_TEST_CASE(genesis_block_object_shape)
{
    const UniValue b =
        CallEth("eth_getBlockByNumber", Arr({"0x0", UniValue(false)}));
    BOOST_REQUIRE(b.isObject());

    const UniValue& num = find_value(b, "number");
    BOOST_REQUIRE(num.isStr());
    BOOST_CHECK_EQUAL(num.get_str(), "0x0");

    for (const char* f : {"hash", "parentHash"}) {
        const UniValue& v = find_value(b, f);
        BOOST_REQUIRE_MESSAGE(v.isStr(), f << " missing/!str");
        // 32-byte hash as DATA: "0x" + 64 hex chars.
        BOOST_CHECK_MESSAGE(IsEthData(v.get_str()) &&
                                v.get_str().size() == 66,
                            f << " not a 32-byte 0x-hash");
    }
    for (const char* f : {"gasLimit", "gasUsed", "timestamp"}) {
        const UniValue& v = find_value(b, f);
        BOOST_REQUIRE_MESSAGE(v.isStr(), f << " missing/!str");
        BOOST_CHECK_MESSAGE(IsEthQuantity(v.get_str()),
                            f << " not a QUANTITY");
    }
    BOOST_CHECK(find_value(b, "transactions").isArray());
}

BOOST_AUTO_TEST_CASE(latest_tag_resolves_to_tip)
{
    const std::string bn =
        CallEth("eth_blockNumber", UniValue(UniValue::VARR)).get_str();
    const UniValue b =
        CallEth("eth_getBlockByNumber", Arr({"latest", UniValue(false)}));
    BOOST_REQUIRE(b.isObject());
    BOOST_CHECK_EQUAL(find_value(b, "number").get_str(), bn);
}

BOOST_AUTO_TEST_CASE(unknown_block_number_returns_null_not_crash)
{
    const UniValue b = CallEth(
        "eth_getBlockByNumber", Arr({"0x7fffffff", UniValue(false)}));
    BOOST_CHECK(b.isNull());
}

// --- error contract: bad input must be a clean RPC error --------------

BOOST_AUTO_TEST_CASE(malformed_address_is_clean_error)
{
    // 0x123 is not a 20-byte address — must be a JSON-RPC error,
    // never a crash / uncaught throw.
    BOOST_CHECK_THROW(
        CallEth("eth_getBalance", Arr({"0x123", "latest"})),
        std::runtime_error);
}

BOOST_AUTO_TEST_CASE(malformed_raw_transaction_is_clean_error)
{
    BOOST_CHECK_THROW(
        CallEth("eth_sendRawTransaction", Arr({"0xdeadbeef"})),
        std::runtime_error);
}

BOOST_AUTO_TEST_CASE(missing_required_params_is_clean_error)
{
    BOOST_CHECK_THROW(
        CallEth("eth_getBalance", UniValue(UniValue::VARR)),
        std::runtime_error);
}

// eth_estimateGas must return a COMPLETE tx gasLimit — the intrinsic gas
// (21000 + EIP-2028 calldata cost) plus execution gas — not just the
// execution slice. A code-less recipient runs no EVM code, so the estimate
// is exactly the intrinsic: a client that funded a real tx with this value
// would NOT hit out-of-gas. (0x..ff is a plain EOA, outside the precompile
// and asset-token address ranges.)
BOOST_AUTO_TEST_CASE(estimate_gas_includes_intrinsic)
{
    const std::string eoa = "0x00000000000000000000000000000000000000ff";
    auto estimate = [&](const std::string& data) -> uint64_t {
        UniValue call(UniValue::VOBJ);
        call.pushKV("to", eoa);
        call.pushKV("data", data);
        const UniValue r = CallEth("eth_estimateGas", Arr({call, "latest"}));
        BOOST_REQUIRE(r.isStr());
        BOOST_CHECK(IsEthQuantity(r.get_str()));
        return std::stoull(r.get_str().substr(2), nullptr, 16);
    };

    // Empty calldata: pure 21000 intrinsic, no execution.
    BOOST_CHECK_EQUAL(estimate("0x"), 21000u);
    // One non-zero calldata byte: +16 (EIP-2028).
    BOOST_CHECK_EQUAL(estimate("0x01"), 21016u);
    // One zero calldata byte: +4.
    BOOST_CHECK_EQUAL(estimate("0x00"), 21004u);
    // Mixed: two non-zero + one zero => 21000 + 16 + 16 + 4.
    BOOST_CHECK_EQUAL(estimate("0xa1b200"), 21036u);
}

// EIP-1559 fee surface: maxPriorityFeePerGas must be a non-zero tip (so
// wallets don't build a zero-tip tx the mempool drops), and gasPrice must
// be baseFee + tip >= the tip. Reporting 0x0 (the old behaviour) broke
// MetaMask fee sizing once the base fee went live.
BOOST_AUTO_TEST_CASE(eip1559_fee_surface_is_nonzero)
{
    const UniValue tip = CallEth("eth_maxPriorityFeePerGas",
                                 UniValue(UniValue::VARR));
    BOOST_REQUIRE(tip.isStr());
    BOOST_CHECK(IsEthQuantity(tip.get_str()));
    // 1 gwei suggested tip.
    BOOST_CHECK_EQUAL(tip.get_str(), "0x3b9aca00");

    const UniValue gp = CallEth("eth_gasPrice", UniValue(UniValue::VARR));
    BOOST_REQUIRE(gp.isStr());
    BOOST_CHECK(IsEthQuantity(gp.get_str()));
    const uint64_t gasPrice = std::stoull(gp.get_str().substr(2), nullptr, 16);
    const uint64_t tipWei = std::stoull(tip.get_str().substr(2), nullptr, 16);
    // gasPrice = baseFee + tip, so it is at least the tip.
    BOOST_CHECK(gasPrice >= tipWei);
}

// The block object must carry the post-London fields ethers/viem require:
// baseFeePerGas (sourced from the D2 commitment) and a real gasUsed.
BOOST_AUTO_TEST_CASE(block_object_has_eip1559_and_evm_fields)
{
    const UniValue blk = CallEth("eth_getBlockByNumber", Arr({"0x0", false}));
    BOOST_REQUIRE(blk.isObject());
    for (const char* field : {"baseFeePerGas", "gasUsed", "stateRoot",
                              "receiptsRoot"}) {
        const UniValue& v = find_value(blk, field);
        BOOST_CHECK_MESSAGE(!v.isNull(),
            "block object missing required field: " << field);
        BOOST_REQUIRE(v.isStr());
    }
    BOOST_CHECK(IsEthQuantity(find_value(blk, "baseFeePerGas").get_str()));
    BOOST_CHECK(IsEthQuantity(find_value(blk, "gasUsed").get_str()));
    BOOST_CHECK(IsEthData(find_value(blk, "stateRoot").get_str()));
    BOOST_CHECK(IsEthData(find_value(blk, "receiptsRoot").get_str()));
    // difficulty / totalDifficulty are QUANTITY-encoded (totalDifficulty is
    // the cumulative nChainWork, so non-zero past genesis).
    BOOST_CHECK(IsEthQuantity(find_value(blk, "difficulty").get_str()));
    BOOST_CHECK(IsEthQuantity(find_value(blk, "totalDifficulty").get_str()));
}

BOOST_AUTO_TEST_SUITE_END()
