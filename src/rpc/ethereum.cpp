// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <evm/account.h>
#include <evm/balance.h>
#include <evm/smoke.h>
#include <evm/state_db.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <util/strencodings.h>
#include <validation.h>

#include <univalue.h>

#include <cstdint>
#include <string>
#include <vector>

/**
 * RPC namespace serving the EVM-related JSON-RPC.
 *
 * Two complementary surfaces live here:
 *
 *   - evm_*  : Raptoreum-native developer probes (e.g.,
 *              evm_executeReadOnly from Phase 0).
 *
 *   - eth_*  : Ethereum JSON-RPC compatibility layer (Phase 3),
 *              implemented to the extent that MetaMask / ethers.js /
 *              viem / web3.js can connect to a raptoreumd instance and
 *              transparently read EVM-side balance, nonce, code and
 *              storage. Phase 3 ships the read-only subset first;
 *              eth_sendRawTransaction (which requires an RLP decoder
 *              and EIP-155 signature verification) and receipt /
 *              filter endpoints land in follow-up commits.
 *
 * Both are routed through RegisterEthereumRPCCommands.
 */

namespace {

// ----------------------------------------------------------------------
// Hex helpers shared by the entire namespace.
// ----------------------------------------------------------------------

// Strip an optional "0x" / "0X" prefix from a hex string.
std::string StripHexPrefix(const std::string& hex)
{
    if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        return hex.substr(2);
    }
    return hex;
}

// Ethereum "quantity" format: 0x-prefixed lowercase hex with no
// unnecessary leading zeros; zero serialises as "0x0".
std::string ToEthQuantity(uint64_t v)
{
    if (v == 0) return "0x0";
    std::string raw = strprintf("%x", v);
    return "0x" + raw;
}

// Ethereum "data" format: 0x-prefixed lowercase hex with the natural
// byte length (leading zero bytes preserved). Empty data is "0x".
std::string ToEthData(const std::vector<uint8_t>& bytes)
{
    if (bytes.empty()) return "0x";
    return "0x" + HexStr(bytes);
}

std::string ToEthData(const uint256& word)
{
    return std::string("0x") + HexStr(Span<const uint8_t>(word.begin(), 32));
}

std::string ToEthData(const uint160& addr)
{
    return std::string("0x") + HexStr(Span<const uint8_t>(addr.begin(), 20));
}

// Parse a 0x-prefixed 20-byte hex address into uint160. Throws
// JSONRPCError on any deviation from the canonical form.
uint160 ParseEthAddress(const UniValue& v, const std::string& name)
{
    if (!v.isStr()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                          name + " must be a 0x-prefixed 20-byte hex string");
    }
    const std::string stripped = StripHexPrefix(v.get_str());
    if (stripped.size() != 40 || !IsHex(stripped)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                          name + " must be a 0x-prefixed 20-byte hex string");
    }
    std::vector<unsigned char> raw = ParseHex(stripped);
    return uint160(raw);
}

// Parse a 0x-prefixed 32-byte hex word into uint256.
uint256 ParseEthWord(const UniValue& v, const std::string& name)
{
    if (!v.isStr()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                          name + " must be a 0x-prefixed 32-byte hex string");
    }
    const std::string stripped = StripHexPrefix(v.get_str());
    if (stripped.size() != 64 || !IsHex(stripped)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                          name + " must be a 0x-prefixed 32-byte hex string");
    }
    std::vector<unsigned char> raw = ParseHex(stripped);
    return uint256(raw);
}

// Validate a block tag. We currently only support "latest"; any other
// tag (including hex block numbers, "earliest", "pending") returns an
// error. Historical state queries become available alongside the
// receipt/log indexer in a follow-up commit.
void RequireLatestBlockTag(const UniValue& tag, const std::string& name)
{
    if (tag.isNull()) return; // default = latest
    if (!tag.isStr()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                          name + " must be a string block tag");
    }
    const std::string s = tag.get_str();
    if (s == "latest" || s == "pending") return;
    throw JSONRPCError(RPC_INVALID_PARAMETER,
                      "only 'latest'/'pending' block tags are supported in "
                      "this build; historical EVM queries land with the "
                      "log/receipt indexer in a follow-up commit");
}

// Format a uint256 carrying a balance as an Ethereum-style quantity
// hex string. For balances that fit in uint64 the output is the
// short canonical form; for larger values (currently unreachable on
// the chain — total RTM supply < 2^64) the result drops leading-zero
// bytes.
std::string BalanceAsEthQuantity(const uint256& balance)
{
    // Fast path: the value fits in uint64 (always true today).
    bool highIsZero = true;
    for (int i = 0; i < 24; ++i) {
        if (*(balance.begin() + i) != 0) { highIsZero = false; break; }
    }
    if (highIsZero) {
        return ToEthQuantity(evm::Uint256ToLowUint64(balance));
    }
    // Generic path: drop leading-zero bytes from the 32-byte BE form.
    int firstNonZero = 0;
    for (int i = 0; i < 32; ++i) {
        if (*(balance.begin() + i) != 0) { firstNonZero = i; break; }
    }
    std::string hex = HexStr(Span<const uint8_t>(balance.begin() + firstNonZero,
                                                32 - firstNonZero));
    // HexStr returns lowercase; trim a leading '0' nibble if present.
    if (!hex.empty() && hex.front() == '0') hex.erase(hex.begin());
    return "0x" + hex;
}

// ----------------------------------------------------------------------
// Phase 0 — evm_executeReadOnly  (developer probe, no consensus impact)
// ----------------------------------------------------------------------

UniValue evm_executeReadOnly(const JSONRPCRequest& request)
{
    RPCHelpMan{"evm_executeReadOnly",
        "\nExecute EVM bytecode against an empty world state and return the result.\n"
        "\nPHASE 0 SMOKE TEST — validates evmone integration. Not consensus-relevant.\n"
        "Has no chain state access (no storage, no balance, no nested calls).\n",
        {
            {"bytecode_hex", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
             "The EVM bytecode to execute, as a hex string (with or without 0x prefix)."},
            {"calldata_hex", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
             "The calldata to pass to the contract, as a hex string (with or without 0x prefix). Pass \"0x\" or \"\" for none."},
            {"gas_limit", RPCArg::Type::NUM, /* default */ "1000000",
             "The gas limit for execution."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::NUM, "status_code", "evmc_status_code (0 = success)"},
                {RPCResult::Type::NUM, "gas_used", "Gas consumed by the execution"},
                {RPCResult::Type::STR_HEX, "return_data", "Bytes returned by the contract via RETURN, hex-encoded"},
            },
        },
        RPCExamples{
            HelpExampleCli("evm_executeReadOnly", "\"600560040160005260206000F3\" \"0x\"")
            + HelpExampleRpc("evm_executeReadOnly", "\"600560040160005260206000F3\", \"0x\"")
        },
    }.Check(request);

    const std::string bytecode_str = StripHexPrefix(request.params[0].get_str());
    const std::string calldata_str = StripHexPrefix(request.params[1].get_str());

    if (!bytecode_str.empty() && !IsHex(bytecode_str)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "bytecode_hex is not a valid hex string");
    }
    if (!calldata_str.empty() && !IsHex(calldata_str)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "calldata_hex is not a valid hex string");
    }

    int64_t gas_limit = 1000000;
    if (!request.params[2].isNull()) {
        gas_limit = request.params[2].get_int64();
        if (gas_limit <= 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "gas_limit must be positive");
        }
    }

    const std::vector<uint8_t> bytecode = ParseHex(bytecode_str);
    const std::vector<uint8_t> calldata = ParseHex(calldata_str);

    const evm::SmokeResult r = evm::EvmSmokeExecute(bytecode, calldata, gas_limit);

    UniValue out(UniValue::VOBJ);
    out.pushKV("status_code", static_cast<int64_t>(r.status_code));
    out.pushKV("gas_used", r.gas_used);
    out.pushKV("return_data", HexStr(r.return_data));
    return out;
}

// ----------------------------------------------------------------------
// Phase 3 — eth_*  Ethereum-compatible JSON-RPC (read-only subset)
// ----------------------------------------------------------------------
//
// The Phase 3 deliverable is MetaMask + ethers.js + viem connecting to
// raptoreumd and seeing correct EVM balances, nonces, code, and
// storage. The methods below cover the read-only portion of the
// namespace and rely on the global pevmstatedb plus the active chain.
//
// All accessors require pevmstatedb to be initialised. Pre-EVM nodes
// (or unit-test contexts without it) return zero values gracefully
// where the EVM convention does so, and an explicit RPC error
// otherwise.

// EVM chain IDs reserved for Raptoreum per docs/evm/PROPOSAL-FOR-
// CORE-TEAM.md. Hardcoded for now; FUP-1 in the project memory tracks
// the migration into Consensus::Params.
constexpr int64_t kRtmEvmChainIdMainnet = 7373;
constexpr int64_t kRtmEvmChainIdTestnet = 7374;
constexpr int64_t kRtmEvmChainIdRegtest = 7375;
constexpr int64_t kRtmEvmChainIdDefault = kRtmEvmChainIdMainnet;

// Pick the active chain id from the runtime CChainParams. We key off
// the network's BIP70 string to avoid coupling to the chainparams
// internals.
int64_t ActiveEvmChainId()
{
    const std::string& net = Params().NetworkIDString();
    if (net == "main") return kRtmEvmChainIdMainnet;
    if (net == "test") return kRtmEvmChainIdTestnet;
    if (net == "regtest") return kRtmEvmChainIdRegtest;
    return kRtmEvmChainIdDefault;
}

UniValue eth_chainId(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_chainId",
        "\nReturns the EVM chain id for the network the node is running on.\n"
        "Used by wallets (MetaMask et al.) for EIP-155 replay protection.\n",
        {},
        RPCResult{RPCResult::Type::STR, "chainId", "Chain id as a 0x-prefixed hex quantity"},
        RPCExamples{
            HelpExampleCli("eth_chainId", "")
            + HelpExampleRpc("eth_chainId", "")
        },
    }.Check(request);

    return ToEthQuantity(static_cast<uint64_t>(ActiveEvmChainId()));
}

UniValue eth_blockNumber(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_blockNumber",
        "\nReturns the height of the active chain tip as a 0x-prefixed hex quantity.\n",
        {},
        RPCResult{RPCResult::Type::STR, "blockNumber", "Active chain tip height"},
        RPCExamples{
            HelpExampleCli("eth_blockNumber", "")
            + HelpExampleRpc("eth_blockNumber", "")
        },
    }.Check(request);

    LOCK(cs_main);
    const int height = ::ChainActive().Height();
    return ToEthQuantity(static_cast<uint64_t>(std::max(0, height)));
}

UniValue eth_gasPrice(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_gasPrice",
        "\nReturns a recommended gas price in weis.\n"
        "\nUntil EIP-1559 base-fee dynamics activate (FUP-1/FUP-2), this\n"
        "always returns 0x0 — the active fee is the priority component only.\n",
        {},
        RPCResult{RPCResult::Type::STR, "gasPrice", "Recommended gas price as a quantity hex"},
        RPCExamples{
            HelpExampleCli("eth_gasPrice", "")
            + HelpExampleRpc("eth_gasPrice", "")
        },
    }.Check(request);

    return ToEthQuantity(0);
}

UniValue eth_getBalance(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_getBalance",
        "\nReturns the balance of the given EVM account in weis.\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO,
             "20-byte EVM address, 0x-prefixed."},
            {"block", RPCArg::Type::STR, /* default */ "\"latest\"",
             "Block tag. Only 'latest'/'pending' are supported in this build."},
        },
        RPCResult{RPCResult::Type::STR, "balance", "Account balance in weis (0x-prefixed quantity)"},
        RPCExamples{
            HelpExampleCli("eth_getBalance", "\"0x000000000000000000000000000000000000000a\" \"latest\"")
            + HelpExampleRpc("eth_getBalance", "\"0x000000000000000000000000000000000000000a\", \"latest\"")
        },
    }.Check(request);

    const uint160 addr = ParseEthAddress(request.params[0], "address");
    RequireLatestBlockTag(request.params[1], "block");

    if (!pevmstatedb) {
        // Pre-EVM build / not initialised: balance is conventionally zero.
        return ToEthQuantity(0);
    }

    evm::CEvmAccount account;
    if (!pevmstatedb->ReadAccount(addr, account)) {
        return ToEthQuantity(0);
    }
    return BalanceAsEthQuantity(account.balance);
}

UniValue eth_getTransactionCount(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_getTransactionCount",
        "\nReturns the transaction count (nonce) for an EVM account.\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO,
             "20-byte EVM address, 0x-prefixed."},
            {"block", RPCArg::Type::STR, /* default */ "\"latest\"",
             "Block tag. Only 'latest'/'pending' are supported in this build."},
        },
        RPCResult{RPCResult::Type::STR, "nonce", "Account nonce (0x-prefixed quantity)"},
        RPCExamples{
            HelpExampleCli("eth_getTransactionCount", "\"0x000000000000000000000000000000000000000a\" \"latest\"")
            + HelpExampleRpc("eth_getTransactionCount", "\"0x000000000000000000000000000000000000000a\", \"latest\"")
        },
    }.Check(request);

    const uint160 addr = ParseEthAddress(request.params[0], "address");
    RequireLatestBlockTag(request.params[1], "block");

    if (!pevmstatedb) return ToEthQuantity(0);

    evm::CEvmAccount account;
    if (!pevmstatedb->ReadAccount(addr, account)) {
        return ToEthQuantity(0);
    }
    return ToEthQuantity(account.nonce);
}

UniValue eth_getCode(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_getCode",
        "\nReturns the runtime bytecode at the given EVM address.\n"
        "\nFor accounts with no deployed code (EOAs or never-deployed addresses)\n"
        "the result is '0x'.\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO,
             "20-byte EVM address, 0x-prefixed."},
            {"block", RPCArg::Type::STR, /* default */ "\"latest\"",
             "Block tag. Only 'latest'/'pending' are supported in this build."},
        },
        RPCResult{RPCResult::Type::STR_HEX, "code", "Bytecode (0x-prefixed hex)"},
        RPCExamples{
            HelpExampleCli("eth_getCode", "\"0x000000000000000000000000000000000000000a\" \"latest\"")
            + HelpExampleRpc("eth_getCode", "\"0x000000000000000000000000000000000000000a\", \"latest\"")
        },
    }.Check(request);

    const uint160 addr = ParseEthAddress(request.params[0], "address");
    RequireLatestBlockTag(request.params[1], "block");

    if (!pevmstatedb) return std::string("0x");

    evm::CEvmAccount account;
    if (!pevmstatedb->ReadAccount(addr, account)) return std::string("0x");
    if (account.codeHash == evm::CEvmAccount::EmptyCodeHash()) return std::string("0x");

    std::vector<uint8_t> code;
    if (!pevmstatedb->ReadCode(account.codeHash, code)) return std::string("0x");
    return ToEthData(code);
}

UniValue eth_getStorageAt(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_getStorageAt",
        "\nReturns the value of a storage slot at the given EVM address.\n"
        "\nUnset slots read as the canonical zero word per EVM semantics.\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO,
             "20-byte EVM address, 0x-prefixed."},
            {"slot", RPCArg::Type::STR, RPCArg::Optional::NO,
             "32-byte slot key, 0x-prefixed hex."},
            {"block", RPCArg::Type::STR, /* default */ "\"latest\"",
             "Block tag. Only 'latest'/'pending' are supported in this build."},
        },
        RPCResult{RPCResult::Type::STR_HEX, "value", "Storage value (32-byte hex word)"},
        RPCExamples{
            HelpExampleCli("eth_getStorageAt", "\"0x000000000000000000000000000000000000000a\" \"0x0000000000000000000000000000000000000000000000000000000000000001\" \"latest\"")
            + HelpExampleRpc("eth_getStorageAt", "\"0x000000000000000000000000000000000000000a\", \"0x0000000000000000000000000000000000000000000000000000000000000001\", \"latest\"")
        },
    }.Check(request);

    const uint160 addr = ParseEthAddress(request.params[0], "address");
    const uint256 slot = ParseEthWord(request.params[1], "slot");
    RequireLatestBlockTag(request.params[2], "block");

    const uint256 zero;
    if (!pevmstatedb) return ToEthData(zero);

    uint256 value;
    if (!pevmstatedb->ReadStorage(addr, slot, value)) {
        return ToEthData(zero);
    }
    return ToEthData(value);
}

// clang-format off
const CRPCCommand commands[] =
{ //  category   name                       actor (function)            argNames
    //  ---------  -------------------------  --------------------------  ----------
    { "evm",      "evm_executeReadOnly",     &evm_executeReadOnly,       {"bytecode_hex", "calldata_hex", "gas_limit"} },

    // Phase 3 — Ethereum JSON-RPC compatibility (read-only subset).
    { "ethereum", "eth_chainId",             &eth_chainId,               {} },
    { "ethereum", "eth_blockNumber",         &eth_blockNumber,           {} },
    { "ethereum", "eth_gasPrice",            &eth_gasPrice,              {} },
    { "ethereum", "eth_getBalance",          &eth_getBalance,            {"address", "block"} },
    { "ethereum", "eth_getTransactionCount", &eth_getTransactionCount,   {"address", "block"} },
    { "ethereum", "eth_getCode",             &eth_getCode,               {"address", "block"} },
    { "ethereum", "eth_getStorageAt",        &eth_getStorageAt,          {"address", "slot", "block"} },
};
// clang-format on

} // namespace

void RegisterEthereumRPCCommands(CRPCTable& t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}
