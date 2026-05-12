// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <clientversion.h>
#include <net.h>
#include <node/context.h>
#include <evm/account.h>
#include <evm/apply.h>
#include <evm/balance.h>
#include <evm/host.h>
#include <evm/rawtx.h>
#include <evm/receipt.h>
#include <evm/smoke.h>
#include <evm/state_cache.h>
#include <evm/state_db.h>
#include <evo/specialtx.h>
#include <node/transaction.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <streams.h>
#include <util/ref.h>
#include <util/strencodings.h>
#include <validation.h>
#include <version.h>

#include <evmc/evmc.hpp>
#include <evmone/evmone.h>

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

// ----------------------------------------------------------------------
// Phase 3.2 — eth_call / eth_estimateGas
// ----------------------------------------------------------------------
//
// eth_call runs a read-only EVM execution against a snapshot of the
// current chain state. The call frame is dispatched through evmone
// exactly as a Phase 2 EVM transaction would be, but the resulting
// state changes are discarded: the CEvmStateCache is never flushed,
// so the EVM state DB is untouched.
//
// Parameter shape mirrors Ethereum's JSON-RPC spec:
//
//   {
//     "from":     "0x...",           // optional sender; defaults to 0x0
//     "to":       "0x...",           // required contract address
//     "gas":      "0x...",           // optional gas limit
//     "gasPrice": "0x...",           // optional; ignored on read-only
//     "value":    "0x...",           // optional value forwarded
//     "data":     "0x..."            // optional calldata
//   }
//
// eth_estimateGas runs the same execution and returns the gas the
// frame consumed. A future refinement is a binary search to find the
// minimum gas the call succeeds at; for now we report the actual
// consumption of a single attempt with a generous limit, which is
// accurate for non-pathological contracts.

namespace {

// Parse a 0x-prefixed quantity hex into uint64. Returns 0 on missing.
uint64_t ParseEthQuantity(const UniValue& v, const std::string& name)
{
    if (v.isNull()) return 0;
    if (!v.isStr()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, name + " must be a hex quantity string");
    }
    const std::string stripped = StripHexPrefix(v.get_str());
    if (stripped.empty()) return 0;
    // Ethereum quantity hex allows odd lengths ("0x0", "0xa"); pad
    // for the IsHex validity check only.
    const std::string padded = (stripped.size() % 2 == 0)
        ? stripped : ("0" + stripped);
    if (!IsHex(padded)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, name + " is not valid hex");
    }
    // Reject values > uint64 max (16 hex nibbles).
    if (stripped.size() > 16) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, name + " quantity exceeds uint64");
    }
    uint64_t out = 0;
    for (char c : stripped) {
        out <<= 4;
        if (c >= '0' && c <= '9') out |= static_cast<uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f') out |= static_cast<uint64_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') out |= static_cast<uint64_t>(c - 'A' + 10);
        else throw JSONRPCError(RPC_INVALID_PARAMETER, name + " is not valid hex");
    }
    return out;
}

// Parse the "data" field — arbitrary-length 0x-prefixed hex.
std::vector<uint8_t> ParseEthDataField(const UniValue& v, const std::string& name)
{
    if (v.isNull()) return {};
    if (!v.isStr()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, name + " must be a hex data string");
    }
    const std::string stripped = StripHexPrefix(v.get_str());
    if (stripped.empty()) return {};
    if (stripped.size() % 2 != 0 || !IsHex(stripped)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, name + " is not valid hex data");
    }
    return ParseHex(stripped);
}

// Bundle of fields parsed out of the eth_call object.
struct EthCallObject
{
    uint160 from;              // defaults to all-zero
    uint160 to;                // required
    uint64_t gas{30'000'000};  // generous default matching our block gas cap
    uint64_t value{0};         // in weis
    std::vector<uint8_t> data;
};

EthCallObject ParseEthCallObject(const UniValue& obj)
{
    if (!obj.isObject()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                          "call object must be a JSON object");
    }
    EthCallObject out;
    const UniValue& toV = obj["to"];
    if (toV.isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "'to' is required for eth_call");
    }
    out.to = ParseEthAddress(toV, "to");

    const UniValue& fromV = obj["from"];
    if (!fromV.isNull()) {
        out.from = ParseEthAddress(fromV, "from");
    }
    out.gas = obj["gas"].isNull()
        ? out.gas
        : ParseEthQuantity(obj["gas"], "gas");
    if (out.gas == 0) out.gas = 30'000'000; // sentinel: 0 means "use default"
    out.value = ParseEthQuantity(obj["value"], "value");
    out.data = ParseEthDataField(obj["data"], "data");
    return out;
}

// Run a single read-only EVM call and return both the apply-layer
// result and the raw evmone status/output. We avoid going through
// the Phase 2.4 process layer (which mutates sender nonce / balance);
// eth_call is a "what would this return" probe and must not bump
// nonces or charge gas.
struct EthCallExecResult
{
    evmc_status_code statusCode;
    int64_t gasUsed;
    std::vector<uint8_t> output;
};

EthCallExecResult ExecuteEthCall(const EthCallObject& call)
{
    EthCallExecResult out{EVMC_FAILURE, 0, {}};
    if (!pevmstatedb) {
        // No EVM state DB: treat as empty world.
        return out;
    }

    evm::CEvmStateCache cache(*pevmstatedb);
    evm::ExecutionContext ctx;
    ctx.chainId = ActiveEvmChainId();
    {
        LOCK(cs_main);
        const int height = ::ChainActive().Height();
        ctx.blockHeight = static_cast<uint64_t>(std::max(0, height));
        if (::ChainActive().Tip() != nullptr) {
            ctx.blockTimestamp = ::ChainActive().Tip()->GetBlockTime();
        }
    }
    ctx.blockGasLimit = 30'000'000;
    // baseFee defaults to zero; matches Phase 2.4e ConnectBlock state
    // until FUP-1/FUP-2 land the header-field dynamics.

    // Load the recipient's deployed code (if any).
    std::vector<uint8_t> code;
    {
        evm::CEvmAccount recipientAccount;
        if (cache.GetAccount(call.to, recipientAccount) &&
            recipientAccount.codeHash != evm::CEvmAccount::EmptyCodeHash())
        {
            cache.GetCode(recipientAccount.codeHash, code);
        }
    }

    evm::CEvmHost host(cache, ctx);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.flags = 0;
    msg.depth = 0;
    msg.gas = static_cast<int64_t>(call.gas);
    std::memcpy(msg.recipient.bytes, call.to.begin(), 20);
    std::memcpy(msg.sender.bytes, call.from.begin(), 20);
    msg.code_address = msg.recipient;
    // Encode value into the low 8 bytes of the BE 256-bit field.
    for (int i = 0; i < 8; ++i) {
        msg.value.bytes[24 + i] = static_cast<uint8_t>(call.value >> (56 - 8 * i));
    }
    msg.input_data = call.data.empty() ? nullptr : call.data.data();
    msg.input_size = call.data.size();

    evmc::VM vm{evmc_create_evmone()};
    evmc::Result r = vm.execute(host, EVMC_CANCUN, msg,
                               code.empty() ? nullptr : code.data(),
                               code.size());

    out.statusCode = r.status_code;
    out.gasUsed = static_cast<int64_t>(call.gas) - r.gas_left;
    if (r.output_size > 0 && r.output_data != nullptr) {
        out.output.assign(r.output_data, r.output_data + r.output_size);
    }
    return out;
}

} // anonymous namespace (Phase 3.2 helpers)

UniValue eth_call(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_call",
        "\nExecute a read-only EVM call against the current chain state and\n"
        "return the contract's RETURN bytes. State changes are discarded.\n",
        {
            {"callObject", RPCArg::Type::OBJ, RPCArg::Optional::NO,
             "The call to execute (Ethereum JSON-RPC call object).",
             {
                 {"from", RPCArg::Type::STR, /* default */ "\"0x0...0\"",
                  "Sender address (0x-prefixed 20 bytes). Defaults to zero."},
                 {"to", RPCArg::Type::STR, RPCArg::Optional::NO,
                  "Recipient contract address (required)."},
                 {"gas", RPCArg::Type::STR, /* default */ "\"0x1c9c380\"",
                  "Gas limit as a 0x-prefixed hex quantity. Defaults to 30M."},
                 {"gasPrice", RPCArg::Type::STR, /* default */ "\"0x0\"",
                  "Ignored for read-only calls; accepted for client compatibility."},
                 {"value", RPCArg::Type::STR, /* default */ "\"0x0\"",
                  "Value forwarded (in weis) as a 0x-prefixed quantity."},
                 {"data", RPCArg::Type::STR, /* default */ "\"0x\"",
                  "Calldata as a 0x-prefixed hex byte string."},
             }},
            {"block", RPCArg::Type::STR, /* default */ "\"latest\"",
             "Block tag. Only 'latest'/'pending' are supported in this build."},
        },
        RPCResult{RPCResult::Type::STR_HEX, "output",
                  "RETURN bytes from the call as 0x-prefixed hex. '0x' on empty."},
        RPCExamples{
            HelpExampleCli("eth_call",
                "'{\"to\":\"0x000000000000000000000000000000000000000a\",\"data\":\"0x\"}' \"latest\"")
            + HelpExampleRpc("eth_call",
                "{\"to\":\"0x000000000000000000000000000000000000000a\",\"data\":\"0x\"}, \"latest\"")
        },
    }.Check(request);

    const EthCallObject call = ParseEthCallObject(request.params[0]);
    RequireLatestBlockTag(request.params[1], "block");

    const EthCallExecResult r = ExecuteEthCall(call);
    if (r.statusCode == EVMC_REVERT) {
        // Ethereum surfaces REVERT via a JSON-RPC error with the
        // revert bytes attached. Wallets parse those bytes into
        // "Error(string)" messages. For now we expose the revert
        // bytes in a structured error to keep the surface small.
        throw JSONRPCError(RPC_TRANSACTION_REJECTED,
                          "execution reverted; revert data: " + ToEthData(r.output));
    }
    if (r.statusCode != EVMC_SUCCESS) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED,
                          strprintf("evm execution failed: status %d", static_cast<int>(r.statusCode)));
    }
    return ToEthData(r.output);
}

UniValue eth_estimateGas(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_estimateGas",
        "\nReturn the gas a single read-only execution of the given call would\n"
        "consume. Useful for clients sizing the gasLimit of a real transaction.\n"
        "\nA single-attempt estimate today; binary-search refinement to find the\n"
        "minimum-gas-that-succeeds is a follow-up (FUP-6 functional-test trail).\n",
        {
            {"callObject", RPCArg::Type::OBJ, RPCArg::Optional::NO,
             "Same shape as eth_call's callObject.",
             {
                 {"from", RPCArg::Type::STR, /* default */ "\"0x0...0\"", "Sender address."},
                 {"to", RPCArg::Type::STR, RPCArg::Optional::NO, "Recipient contract address."},
                 {"gas", RPCArg::Type::STR, /* default */ "\"0x1c9c380\"", "Gas limit hex."},
                 {"gasPrice", RPCArg::Type::STR, /* default */ "\"0x0\"", "Accepted for client compatibility."},
                 {"value", RPCArg::Type::STR, /* default */ "\"0x0\"", "Value forwarded (weis hex)."},
                 {"data", RPCArg::Type::STR, /* default */ "\"0x\"", "Calldata hex."},
             }},
            {"block", RPCArg::Type::STR, /* default */ "\"latest\"",
             "Block tag. Only 'latest'/'pending' are supported in this build."},
        },
        RPCResult{RPCResult::Type::STR, "gas",
                  "Gas consumed by the call (0x-prefixed quantity)."},
        RPCExamples{
            HelpExampleCli("eth_estimateGas",
                "'{\"to\":\"0x000000000000000000000000000000000000000a\",\"data\":\"0x\"}' \"latest\"")
            + HelpExampleRpc("eth_estimateGas",
                "{\"to\":\"0x000000000000000000000000000000000000000a\",\"data\":\"0x\"}, \"latest\"")
        },
    }.Check(request);

    const EthCallObject call = ParseEthCallObject(request.params[0]);
    RequireLatestBlockTag(request.params[1], "block");

    const EthCallExecResult r = ExecuteEthCall(call);
    if (r.statusCode != EVMC_SUCCESS) {
        // The call failed under the supplied gas (which defaults to
        // 30M — generous enough that real OOG is the actual cause).
        // Surface that as a JSON-RPC error so clients don't silently
        // pick a too-low value.
        throw JSONRPCError(RPC_TRANSACTION_REJECTED,
                          strprintf("gas estimation failed: evm status %d",
                                    static_cast<int>(r.statusCode)));
    }
    return ToEthQuantity(static_cast<uint64_t>(r.gasUsed));
}

// ----------------------------------------------------------------------
// Phase 3.3 — Block exploration: eth_getBlockBy{Number,Hash} +
// eth_getBlockTransactionCountBy{Number,Hash}.
// ----------------------------------------------------------------------
//
// MetaMask, Etherscan-style explorers, and most RPC clients poll
// these on every refresh. Returns an Ethereum-shaped block object:
//
//   { number, hash, parentHash, sha3Uncles, logsBloom,
//     transactionsRoot, stateRoot, receiptsRoot, miner, difficulty,
//     totalDifficulty, extraData, size, gasLimit, gasUsed,
//     timestamp, transactions[], uncles[] }
//
// Fields backed by Raptoreum chain data: number, hash, parentHash,
// timestamp, size, transactions[]. Fields with no Raptoreum analog
// today (stateRoot / receiptsRoot per D2; logsBloom; sha3Uncles)
// return canonical zeros — clients tolerate this for chains where
// the consensus surface deviates from Ethereum L1.
//
// The fullTx parameter:
//   false → transactions[] is an array of 0x-prefixed tx hashes
//   true  → transactions[] is an array of full Ethereum-shaped tx
//           objects (subset: hash, blockHash, blockNumber,
//           transactionIndex, from, to, value, input, gas, gasPrice,
//           type, nonce). EVM-typed Raptoreum txs are mapped from
//           their CEvmCallTx / CEvmDeployTx payload; non-EVM txs are
//           emitted with a synthetic from=0x0, to=0x0, input=0x.

namespace {

// 32-byte zero word, formatted once for reuse in fields with no
// Raptoreum analog today.
const std::string kZeroWord =
    "0x0000000000000000000000000000000000000000000000000000000000000000";
const std::string kZeroAddress = "0x0000000000000000000000000000000000000000";
// 256-byte (2048-bit) zero bloom filter.
std::string ZeroLogsBloom()
{
    return "0x" + std::string(512, '0');
}

// uint256 (Bitcoin Core layout — bitcoinish little-endian internally
// but exposed big-endian via begin/end byte iteration) to Ethereum
// 0x-prefixed 32-byte hex.
std::string Uint256ToEthHex(const uint256& v)
{
    // Bitcoin Core stores uint256 in little-endian byte order; the
    // Ethereum convention is big-endian. Reverse on the way out.
    std::vector<uint8_t> bytes(32);
    for (int i = 0; i < 32; ++i) bytes[i] = *(v.begin() + (31 - i));
    return "0x" + HexStr(bytes);
}

// Resolve a block tag ("latest"/"pending" or a 0x-quantity height)
// into a CBlockIndex*. Throws RPC_INVALID_PARAMETER on miss.
CBlockIndex* ResolveBlockTagToIndex(const UniValue& tag,
                                   const std::string& name)
{
    if (tag.isNull()) {
        return ::ChainActive().Tip();
    }
    if (!tag.isStr()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                          name + " must be a string block tag");
    }
    const std::string s = tag.get_str();
    if (s == "latest" || s == "pending") return ::ChainActive().Tip();
    if (s == "earliest") return ::ChainActive().Genesis();
    // Otherwise treat as a 0x-prefixed hex quantity height.
    // Ethereum quantity hex is minimally encoded — "0x0", "0xa",
    // "0x10" — so odd-length payloads are legal. IsHex requires
    // even length; pad the payload before the hex-validity check.
    const std::string stripped = StripHexPrefix(s);
    const std::string padded = (stripped.size() % 2 == 0)
        ? stripped : ("0" + stripped);
    if (stripped.empty() || stripped.size() > 16 || !IsHex(padded)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                          name + " is not a recognised block tag");
    }
    uint64_t height = 0;
    for (char c : stripped) {
        height <<= 4;
        if (c >= '0' && c <= '9') height |= static_cast<uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f') height |= static_cast<uint64_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') height |= static_cast<uint64_t>(c - 'A' + 10);
        else throw JSONRPCError(RPC_INVALID_PARAMETER, name + " has invalid hex");
    }
    if (height > static_cast<uint64_t>(::ChainActive().Height())) {
        return nullptr; // out of range
    }
    return ::ChainActive()[static_cast<int>(height)];
}

// Parse a 0x-prefixed 32-byte block hash string into uint256.
uint256 ParseEthBlockHash(const UniValue& v, const std::string& name)
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
    // Eth-style hex is big-endian; Bitcoin uint256 stores
    // little-endian. Reverse the bytes for the round trip.
    std::vector<unsigned char> beBytes = ParseHex(stripped);
    std::reverse(beBytes.begin(), beBytes.end());
    return uint256(beBytes);
}

// Compute the on-disk serialised size of a block (matches what
// Ethereum reports under `size`).
uint64_t BlockSerializedSize(const CBlock& block)
{
    return ::GetSerializeSize(block, PROTOCOL_VERSION);
}

// Build the Ethereum-shaped tx object for `eth_getBlockByX` with
// fullTx=true. Subset of fields — sufficient for MetaMask + most
// explorers; missing fields default to zero/empty.
UniValue FormatTransactionForBlock(const CTransaction& tx,
                                  const uint256& blockHash,
                                  uint64_t blockHeight,
                                  uint64_t txIndex)
{
    UniValue out(UniValue::VOBJ);
    out.pushKV("hash", Uint256ToEthHex(tx.GetHash()));
    out.pushKV("blockHash", Uint256ToEthHex(blockHash));
    out.pushKV("blockNumber", ToEthQuantity(blockHeight));
    out.pushKV("transactionIndex", ToEthQuantity(txIndex));
    out.pushKV("chainId", ToEthQuantity(static_cast<uint64_t>(ActiveEvmChainId())));
    // Type 2 = EIP-1559 tx; matches the gas-fee model we run.
    out.pushKV("type", "0x2");

    // EVM-typed txs project their payload fields onto Ethereum's tx
    // shape. Non-EVM txs (TRANSACTION_NORMAL, asset txs, etc.) get
    // synthetic zeros — they exist in the UTXO subsystem but have
    // no native Ethereum analog.
    if (tx.nType == TRANSACTION_EVM_CALL) {
        evm::CEvmCallTx payload;
        if (GetTxPayload(tx, payload)) {
            uint160 to;
            std::memcpy(to.begin(), payload.toAddress.begin() + 12, 20);
            uint160 from;
            std::memcpy(from.begin(), payload.senderHash.begin() + 12, 20);
            out.pushKV("from", ToEthData(from));
            out.pushKV("to", ToEthData(to));
            out.pushKV("value", ToEthQuantity(payload.value));
            out.pushKV("input", ToEthData(payload.data));
            out.pushKV("gas", ToEthQuantity(payload.gasLimit));
            out.pushKV("gasPrice", ToEthQuantity(payload.maxFeePerGas));
            out.pushKV("maxFeePerGas", ToEthQuantity(payload.maxFeePerGas));
            out.pushKV("maxPriorityFeePerGas", ToEthQuantity(payload.maxPriorityFeePerGas));
            out.pushKV("nonce", ToEthQuantity(payload.nonce));
            return out;
        }
    } else if (tx.nType == TRANSACTION_EVM_DEPLOY) {
        evm::CEvmDeployTx payload;
        if (GetTxPayload(tx, payload)) {
            uint160 from;
            std::memcpy(from.begin(), payload.senderHash.begin() + 12, 20);
            out.pushKV("from", ToEthData(from));
            // Deploy txs have to=null in Ethereum's spec.
            out.pushKV("to", UniValue());
            out.pushKV("value", "0x0");
            out.pushKV("input", ToEthData(payload.code));
            out.pushKV("gas", ToEthQuantity(payload.gasLimit));
            out.pushKV("gasPrice", ToEthQuantity(payload.maxFeePerGas));
            out.pushKV("maxFeePerGas", ToEthQuantity(payload.maxFeePerGas));
            out.pushKV("maxPriorityFeePerGas", ToEthQuantity(payload.maxPriorityFeePerGas));
            out.pushKV("nonce", ToEthQuantity(payload.nonce));
            return out;
        }
    }
    // Non-EVM or malformed-payload fallback.
    out.pushKV("from", kZeroAddress);
    out.pushKV("to", kZeroAddress);
    out.pushKV("value", "0x0");
    out.pushKV("input", "0x");
    out.pushKV("gas", "0x0");
    out.pushKV("gasPrice", "0x0");
    out.pushKV("nonce", "0x0");
    return out;
}

UniValue FormatBlock(CBlockIndex* pindex, const CBlock& block, bool fullTx)
{
    UniValue out(UniValue::VOBJ);
    const uint256 blockHash = block.GetHash();
    out.pushKV("number", ToEthQuantity(static_cast<uint64_t>(pindex->nHeight)));
    out.pushKV("hash", Uint256ToEthHex(blockHash));
    out.pushKV("parentHash", Uint256ToEthHex(block.hashPrevBlock));
    // PoW nonce is 4 bytes in Bitcoin/Raptoreum; Ethereum's "nonce"
    // field is an 8-byte data hex. Pad the high 4 bytes with zeros.
    out.pushKV("nonce", strprintf("0x00000000%08x", block.nNonce));
    out.pushKV("sha3Uncles", kZeroWord);     // no uncles in Raptoreum
    out.pushKV("logsBloom", ZeroLogsBloom()); // FUP: aggregate from receipts
    out.pushKV("transactionsRoot", Uint256ToEthHex(block.hashMerkleRoot));
    out.pushKV("stateRoot", kZeroWord);   // FUP-1 / D2 header field
    out.pushKV("receiptsRoot", kZeroWord); // FUP-1 / D2 header field
    // Miner / coinbase: the first output of the coinbase tx carries
    // the script we'd map to an Ethereum-style address. Until the
    // proper UTXO→EVM-address mapping lands, return zero.
    out.pushKV("miner", kZeroAddress);
    out.pushKV("difficulty", ToEthQuantity(pindex->nBits));
    out.pushKV("totalDifficulty", ToEthQuantity(pindex->nBits));
    out.pushKV("extraData", "0x");
    out.pushKV("size", ToEthQuantity(BlockSerializedSize(block)));
    out.pushKV("gasLimit", ToEthQuantity(30'000'000)); // FUP-1 / D2
    out.pushKV("gasUsed", ToEthQuantity(0));           // FUP: sum from receipts
    out.pushKV("timestamp", ToEthQuantity(static_cast<uint64_t>(block.GetBlockTime())));
    UniValue txs(UniValue::VARR);
    for (size_t i = 0; i < block.vtx.size(); ++i) {
        if (fullTx) {
            txs.push_back(FormatTransactionForBlock(
                *block.vtx[i], blockHash,
                static_cast<uint64_t>(pindex->nHeight), i));
        } else {
            txs.push_back(Uint256ToEthHex(block.vtx[i]->GetHash()));
        }
    }
    out.pushKV("transactions", txs);
    out.pushKV("uncles", UniValue(UniValue::VARR));
    return out;
}

} // anonymous namespace (Phase 3.3 helpers)

UniValue eth_getBlockByNumber(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_getBlockByNumber",
        "\nReturns the block at the given height as an Ethereum-shaped object.\n"
        "\nFields with no Raptoreum analog today (stateRoot, receiptsRoot,\n"
        "logsBloom, sha3Uncles, miner) return canonical zeros — D2 header\n"
        "field landing (tracked as FUP-1) populates them properly.\n",
        {
            {"block", RPCArg::Type::STR, RPCArg::Optional::NO,
             "Block tag ('latest', 'earliest', 'pending') or 0x-prefixed hex height."},
            {"fullTx", RPCArg::Type::BOOL, /* default */ "false",
             "If true, transactions[] is an array of full tx objects; "
             "otherwise just tx hashes."},
        },
        RPCResult{RPCResult::Type::OBJ, "", "Ethereum-shaped block object",
                  {RPCResult{RPCResult::Type::ELISION, "", "Standard Ethereum eth_getBlock fields"}}},
        RPCExamples{
            HelpExampleCli("eth_getBlockByNumber", "\"latest\" false")
            + HelpExampleRpc("eth_getBlockByNumber", "\"latest\", false")
        },
    }.Check(request);

    LOCK(cs_main);
    CBlockIndex* pindex = ResolveBlockTagToIndex(request.params[0], "block");
    if (pindex == nullptr) return UniValue(UniValue::VNULL);

    const bool fullTx = !request.params[1].isNull() && request.params[1].get_bool();

    CBlock block;
    if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus())) {
        throw JSONRPCError(RPC_MISC_ERROR, "Failed to read block from disk");
    }
    return FormatBlock(pindex, block, fullTx);
}

UniValue eth_getBlockByHash(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_getBlockByHash",
        "\nReturns the block with the given hash as an Ethereum-shaped object.\n",
        {
            {"blockHash", RPCArg::Type::STR, RPCArg::Optional::NO,
             "0x-prefixed 32-byte block hash."},
            {"fullTx", RPCArg::Type::BOOL, /* default */ "false",
             "If true, transactions[] is an array of full tx objects; "
             "otherwise just tx hashes."},
        },
        RPCResult{RPCResult::Type::OBJ, "", "Ethereum-shaped block object",
                  {RPCResult{RPCResult::Type::ELISION, "", "Standard Ethereum eth_getBlock fields"}}},
        RPCExamples{
            HelpExampleCli("eth_getBlockByHash", "\"0x...\" false")
            + HelpExampleRpc("eth_getBlockByHash", "\"0x...\", false")
        },
    }.Check(request);

    const uint256 hash = ParseEthBlockHash(request.params[0], "blockHash");
    const bool fullTx = !request.params[1].isNull() && request.params[1].get_bool();

    LOCK(cs_main);
    auto it = ::BlockIndex().find(hash);
    if (it == ::BlockIndex().end()) {
        return UniValue(UniValue::VNULL);
    }
    CBlockIndex* pindex = it->second;
    if (pindex == nullptr) return UniValue(UniValue::VNULL);

    CBlock block;
    if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus())) {
        throw JSONRPCError(RPC_MISC_ERROR, "Failed to read block from disk");
    }
    return FormatBlock(pindex, block, fullTx);
}

UniValue eth_getBlockTransactionCountByNumber(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_getBlockTransactionCountByNumber",
        "\nReturns the number of transactions in the block at the given height.\n",
        {{"block", RPCArg::Type::STR, RPCArg::Optional::NO,
          "Block tag or 0x-prefixed hex height."}},
        RPCResult{RPCResult::Type::STR, "count", "Transaction count (0x-prefixed)"},
        RPCExamples{
            HelpExampleCli("eth_getBlockTransactionCountByNumber", "\"latest\"")
            + HelpExampleRpc("eth_getBlockTransactionCountByNumber", "\"latest\"")
        },
    }.Check(request);

    LOCK(cs_main);
    CBlockIndex* pindex = ResolveBlockTagToIndex(request.params[0], "block");
    if (pindex == nullptr) return ToEthQuantity(0);

    CBlock block;
    if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus())) {
        throw JSONRPCError(RPC_MISC_ERROR, "Failed to read block from disk");
    }
    return ToEthQuantity(static_cast<uint64_t>(block.vtx.size()));
}

UniValue eth_getBlockTransactionCountByHash(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_getBlockTransactionCountByHash",
        "\nReturns the number of transactions in the block with the given hash.\n",
        {{"blockHash", RPCArg::Type::STR, RPCArg::Optional::NO,
          "0x-prefixed 32-byte block hash."}},
        RPCResult{RPCResult::Type::STR, "count", "Transaction count (0x-prefixed)"},
        RPCExamples{
            HelpExampleCli("eth_getBlockTransactionCountByHash", "\"0x...\"")
            + HelpExampleRpc("eth_getBlockTransactionCountByHash", "\"0x...\"")
        },
    }.Check(request);

    const uint256 hash = ParseEthBlockHash(request.params[0], "blockHash");

    LOCK(cs_main);
    auto it = ::BlockIndex().find(hash);
    if (it == ::BlockIndex().end()) return ToEthQuantity(0);
    CBlockIndex* pindex = it->second;
    if (pindex == nullptr) return ToEthQuantity(0);

    CBlock block;
    if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus())) {
        throw JSONRPCError(RPC_MISC_ERROR, "Failed to read block from disk");
    }
    return ToEthQuantity(static_cast<uint64_t>(block.vtx.size()));
}

// ----------------------------------------------------------------------
// Phase 3.4 — Ethereum / Web3 metadata methods
// ----------------------------------------------------------------------
//
// MetaMask, ethers.js, viem, web3.js and most dApps poll these on
// connect to probe the node. Returning canonical answers (chain id,
// peer count, sync status, client version) lets the wallet show
// "Connected" instead of an error.

UniValue eth_protocolVersion(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_protocolVersion",
        "\nReturns the Ethereum protocol version this node speaks.\n"
        "\nReports 0x41 (65) — the value modern clients return; the JSON-RPC\n"
        "surface is more meaningful than the wire version on a non-devp2p chain.\n",
        {},
        RPCResult{RPCResult::Type::STR, "version", "Protocol version as a 0x quantity"},
        RPCExamples{
            HelpExampleCli("eth_protocolVersion", "")
            + HelpExampleRpc("eth_protocolVersion", "")
        },
    }.Check(request);
    return ToEthQuantity(0x41);
}

UniValue eth_syncing(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_syncing",
        "\nReturns false when the node is at the chain tip; otherwise a sync\n"
        "object with startingBlock / currentBlock / highestBlock.\n",
        {},
        RPCResult{RPCResult::Type::STR, "result", "false or sync status object"},
        RPCExamples{
            HelpExampleCli("eth_syncing", "")
            + HelpExampleRpc("eth_syncing", "")
        },
    }.Check(request);

    LOCK(cs_main);
    if (!::ChainstateActive().IsInitialBlockDownload()) {
        return UniValue(false);
    }
    UniValue out(UniValue::VOBJ);
    out.pushKV("startingBlock", ToEthQuantity(0));
    const int height = ::ChainActive().Height();
    out.pushKV("currentBlock", ToEthQuantity(static_cast<uint64_t>(std::max(0, height))));
    out.pushKV("highestBlock", ToEthQuantity(static_cast<uint64_t>(std::max(0, height))));
    return out;
}

UniValue eth_accounts(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_accounts",
        "\nReturns the EVM accounts the node manages. Until wallet integration\n"
        "lands (Phase 5), this returns an empty array — clients drive their own\n"
        "signing via local keys / hardware wallets and use eth_sendRawTransaction.\n",
        {},
        RPCResult{RPCResult::Type::ARR, "", "EVM addresses",
                  {RPCResult{RPCResult::Type::STR, "address", "0x-prefixed 20-byte address"}}},
        RPCExamples{
            HelpExampleCli("eth_accounts", "")
            + HelpExampleRpc("eth_accounts", "")
        },
    }.Check(request);
    return UniValue(UniValue::VARR);
}

UniValue eth_coinbase(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_coinbase",
        "\nReturns the address mining rewards are sent to. Returns the zero\n"
        "address until Phase 5 wallet integration lands.\n",
        {},
        RPCResult{RPCResult::Type::STR, "address", "0x-prefixed 20-byte coinbase address"},
        RPCExamples{
            HelpExampleCli("eth_coinbase", "")
            + HelpExampleRpc("eth_coinbase", "")
        },
    }.Check(request);
    return kZeroAddress;
}

UniValue eth_mining(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_mining",
        "\nReturns whether the node is mining. Raptoreum uses PoW + masternode\n"
        "consensus; the EVM-side answer is always false (this node does not\n"
        "claim to be the network's miner from MetaMask's perspective).\n",
        {},
        RPCResult{RPCResult::Type::BOOL, "mining", "false"},
        RPCExamples{HelpExampleCli("eth_mining", "")},
    }.Check(request);
    return UniValue(false);
}

UniValue eth_hashrate(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_hashrate",
        "\nReturns the EVM-side hashrate. Always 0x0 — Raptoreum's PoW is\n"
        "GhostRider on the UTXO side and not reported through the EVM RPC.\n",
        {},
        RPCResult{RPCResult::Type::STR, "hashrate", "Always 0x0"},
        RPCExamples{HelpExampleCli("eth_hashrate", "")},
    }.Check(request);
    return ToEthQuantity(0);
}

UniValue eth_maxPriorityFeePerGas(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_maxPriorityFeePerGas",
        "\nReturns the suggested EIP-1559 max-priority-fee-per-gas.\n"
        "\nReports 0x0 until base-fee dynamics activate (FUP-1/FUP-2).\n",
        {},
        RPCResult{RPCResult::Type::STR, "fee", "0x-prefixed quantity"},
        RPCExamples{HelpExampleCli("eth_maxPriorityFeePerGas", "")},
    }.Check(request);
    return ToEthQuantity(0);
}

UniValue net_version(const JSONRPCRequest& request)
{
    RPCHelpMan{"net_version",
        "\nReturns the active network id as a decimal string (per the net_ spec).\n",
        {},
        RPCResult{RPCResult::Type::STR, "version", "Decimal-string network id"},
        RPCExamples{HelpExampleCli("net_version", "")},
    }.Check(request);
    return strprintf("%d", ActiveEvmChainId());
}

UniValue net_listening(const JSONRPCRequest& request)
{
    RPCHelpMan{"net_listening",
        "\nReturns true when the node is actively listening for P2P connections.\n",
        {},
        RPCResult{RPCResult::Type::BOOL, "listening", "True if listening"},
        RPCExamples{HelpExampleCli("net_listening", "")},
    }.Check(request);
    if (!request.context.Has<NodeContext>()) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Node context not found");
    }
    const NodeContext& node = request.context.Get<NodeContext>();
    return UniValue(static_cast<bool>(node.connman));
}

UniValue net_peerCount(const JSONRPCRequest& request)
{
    RPCHelpMan{"net_peerCount",
        "\nReturns the number of connected P2P peers as a 0x quantity.\n",
        {},
        RPCResult{RPCResult::Type::STR, "count", "0x-prefixed peer count"},
        RPCExamples{HelpExampleCli("net_peerCount", "")},
    }.Check(request);
    if (!request.context.Has<NodeContext>()) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Node context not found");
    }
    const NodeContext& node = request.context.Get<NodeContext>();
    const uint64_t n = node.connman
        ? static_cast<uint64_t>(node.connman->GetNodeCount(CConnman::CONNECTIONS_ALL))
        : 0;
    return ToEthQuantity(n);
}

UniValue web3_clientVersion(const JSONRPCRequest& request)
{
    RPCHelpMan{"web3_clientVersion",
        "\nReturns the underlying client's full version string.\n",
        {},
        RPCResult{RPCResult::Type::STR, "client", "Client name / version"},
        RPCExamples{HelpExampleCli("web3_clientVersion", "")},
    }.Check(request);
    return std::string("Raptoreum/") + FormatFullVersion() + "/EVM";
}

// ----------------------------------------------------------------------
// Phase 3.5 — eth_sendRawTransaction
// ----------------------------------------------------------------------
//
// Accepts a wallet-signed EIP-1559 (type 0x02) or legacy EIP-155
// transaction blob, recovers the sender, wraps it as a Raptoreum
// special transaction (TRANSACTION_EVM_CALL or TRANSACTION_EVM_DEPLOY)
// with the matching CEvmCallTx / CEvmDeployTx payload, and broadcasts
// through the normal mempool path. The returned hash is the
// keccak256 of the original wire bytes — what dApps key receipts /
// logs by, and what eth_getTransactionByHash will look up once that
// lands in Phase 3.6.
//
// Notes:
//   - chainId is verified against the active network's EVM chain
//     id (EIP-155 replay protection).
//   - access lists are accepted only when empty; populating evmone's
//     warm-slot tracking from a non-empty access list is a follow-up.
//   - The wallet that signed the tx took responsibility for sender
//     nonce and gas. The pre-flight check in ProcessEvmTx (Phase 2.4)
//     will reject the tx in-block if the recovered sender's nonce
//     doesn't match — that's the consensus-side check, separate from
//     this RPC entry.

UniValue eth_sendRawTransaction(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_sendRawTransaction",
        "\nAccept a wallet-signed Ethereum transaction (EIP-1559 type-0x02 or\n"
        "legacy EIP-155), verify its signature, wrap it as the matching\n"
        "Raptoreum special tx (TRANSACTION_EVM_CALL or TRANSACTION_EVM_DEPLOY),\n"
        "and broadcast through the standard mempool pipeline.\n",
        {
            {"signedTx", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
             "Wallet-signed transaction wire bytes, 0x-prefixed hex."},
        },
        RPCResult{RPCResult::Type::STR_HEX, "txHash",
                  "Keccak-256 of the signed wire bytes (the tx hash dApps "
                  "see in eth_getTransactionByHash / eth_getTransactionReceipt)."},
        RPCExamples{
            HelpExampleCli("eth_sendRawTransaction", "\"0x02f8...\"")
            + HelpExampleRpc("eth_sendRawTransaction", "\"0x02f8...\"")
        },
    }.Check(request);

    if (!request.params[0].isStr()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                          "signedTx must be 0x-prefixed hex");
    }
    const std::string stripped = StripHexPrefix(request.params[0].get_str());
    if (stripped.empty() || stripped.size() % 2 != 0 || !IsHex(stripped)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                          "signedTx must be 0x-prefixed hex");
    }
    const std::vector<uint8_t> wire = ParseHex(stripped);

    evm::DecodedRawTx decoded;
    const uint64_t expectedChainId =
        static_cast<uint64_t>(ActiveEvmChainId());
    if (!evm::DecodeRawEthTx(wire, expectedChainId, decoded)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                          "failed to decode signed tx; check envelope, chain id, "
                          "and signature");
    }

    // -- Map to Raptoreum special tx ---------------------------------
    CMutableTransaction mtx;
    mtx.nVersion = 3;
    if (decoded.emptyTo) {
        // Contract creation.
        mtx.nType = TRANSACTION_EVM_DEPLOY;
        evm::CEvmDeployTx payload;
        payload.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
        payload.code = decoded.data;
        payload.gasLimit = decoded.gasLimit;
        payload.maxFeePerGas = decoded.maxFeePerGas;
        payload.maxPriorityFeePerGas = decoded.maxPriorityFeePerGas;
        // senderHash is the 20-byte EVM address widened into a
        // uint256 (low 20 bytes carry the address).
        payload.senderHash.SetNull();
        std::memcpy(payload.senderHash.begin() + 12, decoded.sender.begin(), 20);
        payload.nonce = decoded.nonce;
        CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
        s << payload;
        mtx.vExtraPayload.assign(s.begin(), s.end());
    } else {
        mtx.nType = TRANSACTION_EVM_CALL;
        evm::CEvmCallTx payload;
        payload.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
        payload.toAddress.SetNull();
        std::memcpy(payload.toAddress.begin() + 12, decoded.to.begin(), 20);
        payload.value = decoded.value;
        payload.data = decoded.data;
        payload.gasLimit = decoded.gasLimit;
        payload.maxFeePerGas = decoded.maxFeePerGas;
        payload.maxPriorityFeePerGas = decoded.maxPriorityFeePerGas;
        payload.senderHash.SetNull();
        std::memcpy(payload.senderHash.begin() + 12, decoded.sender.begin(), 20);
        payload.nonce = decoded.nonce;
        CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
        s << payload;
        mtx.vExtraPayload.assign(s.begin(), s.end());
    }

    // -- Broadcast via the standard mempool path. --------------------
    CTransactionRef tx = MakeTransactionRef(std::move(mtx));
    if (!request.context.Has<NodeContext>()) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Node context not found");
    }
    NodeContext& node = request.context.Get<NodeContext>();

    // Phase 3.6 — register the eth_hash <-> rtm_hash cross-indices
    // BEFORE broadcast. ConnectTip's receipt-generation pass reads
    // the reverse direction so the persisted CEvmReceipt knows its
    // Ethereum-side hash. Writing both directions here is cheap and
    // lets eth_getTransactionReceipt(eth_hash) work end-to-end as
    // soon as the tx lands in a block.
    const uint256 ethHash = evm::EthTxHash(wire);
    if (pevmstatedb) {
        const uint256 rtmHash = tx->GetHash();
        const bool fwd = pevmstatedb->WriteEthToRtmHash(ethHash, rtmHash);
        const bool rev = pevmstatedb->WriteRtmToEthHash(rtmHash, ethHash);
        if (!fwd || !rev) {
            throw JSONRPCError(RPC_INTERNAL_ERROR,
                              "failed to register eth/rtm tx-hash cross-index");
        }
    }

    AssertLockNotHeld(cs_main);
    std::string errString;
    const TransactionError err = BroadcastTransaction(
        node, tx, errString, /*highfee=*/ 0,
        /*relay=*/ true, /*wait_callback=*/ true, /*bypass_limits=*/ false);
    if (err != TransactionError::OK) {
        throw JSONRPCTransactionError(err, errString);
    }

    // Return the Ethereum-style tx hash (keccak256 of wire bytes) —
    // NOT the Raptoreum sha256d wrapper hash. dApps look this up via
    // eth_getTransactionByHash / eth_getTransactionReceipt.
    return ToEthData(ethHash);
}

// ----------------------------------------------------------------------
// Phase 3.6 — eth_getTransactionReceipt + eth_getLogs
// ----------------------------------------------------------------------
//
// Receipts are written by ConnectTip during block validation and
// keyed by the wrapper's sha256d (rtmTxHash). The eth_hash <-> rtm_hash
// cross-index (populated by eth_sendRawTransaction at submit time)
// lets dApp clients look up receipts via the Ethereum hash they
// received from sendRawTransaction.

namespace {

// Format a CEvmReceipt as the Ethereum-shaped JSON object that
// eth_getTransactionReceipt clients expect.
UniValue FormatReceiptForRpc(const evm::CEvmReceipt& r)
{
    UniValue out(UniValue::VOBJ);
    out.pushKV("transactionHash",
              r.ethTxHash.IsNull() ? ToEthData(r.rtmTxHash)
                                   : ToEthData(r.ethTxHash));
    out.pushKV("transactionIndex", ToEthQuantity(r.txIndex));
    out.pushKV("blockHash", Uint256ToEthHex(r.blockHash));
    out.pushKV("blockNumber", ToEthQuantity(r.blockHeight));
    out.pushKV("from", ToEthData(r.sender));
    if (r.isContractCreation) {
        out.pushKV("to", UniValue());
        out.pushKV("contractAddress", ToEthData(r.contractAddress));
    } else {
        out.pushKV("to", ToEthData(r.to));
        out.pushKV("contractAddress", UniValue());
    }
    out.pushKV("cumulativeGasUsed", ToEthQuantity(r.cumulativeGasUsed));
    out.pushKV("gasUsed", ToEthQuantity(r.gasUsed));
    out.pushKV("effectiveGasPrice", ToEthQuantity(r.effectiveGasPrice));
    out.pushKV("status", ToEthQuantity(r.status));
    // Type 2 = EIP-1559; that's what we generate from
    // eth_sendRawTransaction. Legacy txs would surface as type 0
    // but we don't yet track this through to receipts; FUP.
    out.pushKV("type", "0x2");
    out.pushKV("logsBloom", ZeroLogsBloom());

    UniValue logsArr(UniValue::VARR);
    for (size_t i = 0; i < r.logs.size(); ++i) {
        const auto& log = r.logs[i];
        UniValue lo(UniValue::VOBJ);
        lo.pushKV("removed", false);
        lo.pushKV("logIndex", ToEthQuantity(static_cast<uint64_t>(i)));
        lo.pushKV("transactionIndex", ToEthQuantity(r.txIndex));
        lo.pushKV("transactionHash",
                 r.ethTxHash.IsNull() ? ToEthData(r.rtmTxHash)
                                      : ToEthData(r.ethTxHash));
        lo.pushKV("blockHash", Uint256ToEthHex(r.blockHash));
        lo.pushKV("blockNumber", ToEthQuantity(r.blockHeight));
        lo.pushKV("address", ToEthData(log.address));
        UniValue topicsArr(UniValue::VARR);
        for (const auto& t : log.topics) topicsArr.push_back(ToEthData(t));
        lo.pushKV("topics", topicsArr);
        lo.pushKV("data", ToEthData(log.data));
        logsArr.push_back(lo);
    }
    out.pushKV("logs", logsArr);
    return out;
}

// Read + deserialize a receipt by Raptoreum tx hash. Returns false
// on miss.
bool LoadReceiptByRtmHash(const uint256& rtmTxHash, evm::CEvmReceipt& out)
{
    if (!pevmstatedb) return false;
    std::vector<uint8_t> bytes;
    if (!pevmstatedb->ReadReceiptBytes(rtmTxHash, bytes)) return false;
    CDataStream s(bytes, SER_DISK, CLIENT_VERSION);
    try {
        s >> out;
    } catch (...) {
        return false;
    }
    return true;
}

} // anonymous namespace (Phase 3.6 helpers)

UniValue eth_getTransactionReceipt(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_getTransactionReceipt",
        "\nReturns the receipt for the given Ethereum transaction hash, or null\n"
        "if the tx has not been mined yet (or the hash is unknown).\n",
        {
            {"txHash", RPCArg::Type::STR, RPCArg::Optional::NO,
             "0x-prefixed 32-byte Ethereum tx hash."},
        },
        RPCResult{RPCResult::Type::OBJ, "", "Ethereum-shaped receipt or null",
                  {RPCResult{RPCResult::Type::ELISION, "",
                             "Standard eth_getTransactionReceipt fields"}}},
        RPCExamples{
            HelpExampleCli("eth_getTransactionReceipt", "\"0x07b1...\"")
            + HelpExampleRpc("eth_getTransactionReceipt", "\"0x07b1...\"")
        },
    }.Check(request);

    const uint256 ethHash = ParseEthBlockHash(request.params[0], "txHash");
    if (!pevmstatedb) return UniValue(UniValue::VNULL);

    // Resolve the eth_hash to the Raptoreum wrapper hash.
    uint256 rtmHash;
    if (!pevmstatedb->ReadEthToRtmHash(ethHash, rtmHash)) {
        // Fallback: caller may have passed the rtm hash directly
        // (txs not submitted via eth_sendRawTransaction don't have
        // a cross-index entry; we accept the rtm hash transparently
        // for compatibility with internal tooling).
        rtmHash = ethHash;
    }
    evm::CEvmReceipt receipt;
    if (!LoadReceiptByRtmHash(rtmHash, receipt)) {
        return UniValue(UniValue::VNULL);
    }
    return FormatReceiptForRpc(receipt);
}

UniValue eth_getTransactionByHash(const JSONRPCRequest& request)
{
    RPCHelpMan{"eth_getTransactionByHash",
        "\nReturns the Ethereum-shaped transaction object for the given hash,\n"
        "or null if unknown.\n"
        "\nMinimum-viable today: surfaces the fields that survive serialization\n"
        "into the wrapper's vExtraPayload. Full mempool/chain dual-lookup with\n"
        "block coordinates is a follow-up (requires txindex integration).\n",
        {
            {"txHash", RPCArg::Type::STR, RPCArg::Optional::NO,
             "0x-prefixed 32-byte Ethereum tx hash."},
        },
        RPCResult{RPCResult::Type::OBJ, "", "Ethereum-shaped tx or null",
                  {RPCResult{RPCResult::Type::ELISION, "",
                             "Standard eth_getTransactionByHash fields"}}},
        RPCExamples{
            HelpExampleCli("eth_getTransactionByHash", "\"0x07b1...\"")
        },
    }.Check(request);

    const uint256 ethHash = ParseEthBlockHash(request.params[0], "txHash");
    if (!pevmstatedb) return UniValue(UniValue::VNULL);

    uint256 rtmHash;
    if (!pevmstatedb->ReadEthToRtmHash(ethHash, rtmHash)) {
        rtmHash = ethHash;
    }
    // Reuse receipt to extract sender/to/coords. For richer fields
    // (value/input/gas) the wrapper tx itself would need to be
    // loaded — a txindex follow-up.
    evm::CEvmReceipt receipt;
    if (!LoadReceiptByRtmHash(rtmHash, receipt)) {
        return UniValue(UniValue::VNULL);
    }
    UniValue out(UniValue::VOBJ);
    out.pushKV("hash", ToEthData(receipt.ethTxHash.IsNull()
                                 ? receipt.rtmTxHash : receipt.ethTxHash));
    out.pushKV("blockHash", Uint256ToEthHex(receipt.blockHash));
    out.pushKV("blockNumber", ToEthQuantity(receipt.blockHeight));
    out.pushKV("transactionIndex", ToEthQuantity(receipt.txIndex));
    out.pushKV("from", ToEthData(receipt.sender));
    if (receipt.isContractCreation) {
        out.pushKV("to", UniValue());
    } else {
        out.pushKV("to", ToEthData(receipt.to));
    }
    out.pushKV("chainId", ToEthQuantity(static_cast<uint64_t>(ActiveEvmChainId())));
    out.pushKV("type", "0x2");
    return out;
}

// clang-format off
const CRPCCommand commands[] =
{ //  category   name                       actor (function)            argNames
    //  ---------  -------------------------  --------------------------  ----------
    { "evm",      "evm_executeReadOnly",     &evm_executeReadOnly,       {"bytecode_hex", "calldata_hex", "gas_limit"} },

    // Phase 3 — Ethereum JSON-RPC compatibility (read-only subset).
    { "ethereum", "eth_chainId",                            &eth_chainId,                            {} },
    { "ethereum", "eth_blockNumber",                        &eth_blockNumber,                        {} },
    { "ethereum", "eth_gasPrice",                           &eth_gasPrice,                           {} },
    { "ethereum", "eth_getBalance",                         &eth_getBalance,                         {"address", "block"} },
    { "ethereum", "eth_getTransactionCount",                &eth_getTransactionCount,                {"address", "block"} },
    { "ethereum", "eth_getCode",                            &eth_getCode,                            {"address", "block"} },
    { "ethereum", "eth_getStorageAt",                       &eth_getStorageAt,                       {"address", "slot", "block"} },
    { "ethereum", "eth_call",                               &eth_call,                               {"callObject", "block"} },
    { "ethereum", "eth_estimateGas",                        &eth_estimateGas,                        {"callObject", "block"} },
    { "ethereum", "eth_getBlockByNumber",                   &eth_getBlockByNumber,                   {"block", "fullTx"} },
    { "ethereum", "eth_getBlockByHash",                     &eth_getBlockByHash,                     {"blockHash", "fullTx"} },
    { "ethereum", "eth_getBlockTransactionCountByNumber",   &eth_getBlockTransactionCountByNumber,   {"block"} },
    { "ethereum", "eth_getBlockTransactionCountByHash",     &eth_getBlockTransactionCountByHash,     {"blockHash"} },
    { "ethereum", "eth_protocolVersion",                    &eth_protocolVersion,                    {} },
    { "ethereum", "eth_syncing",                            &eth_syncing,                            {} },
    { "ethereum", "eth_accounts",                           &eth_accounts,                           {} },
    { "ethereum", "eth_coinbase",                           &eth_coinbase,                           {} },
    { "ethereum", "eth_mining",                             &eth_mining,                             {} },
    { "ethereum", "eth_hashrate",                           &eth_hashrate,                           {} },
    { "ethereum", "eth_maxPriorityFeePerGas",               &eth_maxPriorityFeePerGas,               {} },
    { "ethereum", "net_version",                            &net_version,                            {} },
    { "ethereum", "net_listening",                          &net_listening,                          {} },
    { "ethereum", "net_peerCount",                          &net_peerCount,                          {} },
    { "ethereum", "web3_clientVersion",                     &web3_clientVersion,                     {} },
    { "ethereum", "eth_sendRawTransaction",                 &eth_sendRawTransaction,                 {"signedTx"} },
    { "ethereum", "eth_getTransactionReceipt",              &eth_getTransactionReceipt,              {"txHash"} },
    { "ethereum", "eth_getTransactionByHash",               &eth_getTransactionByHash,               {"txHash"} },
};
// clang-format on

} // namespace

void RegisterEthereumRPCCommands(CRPCTable& t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}
