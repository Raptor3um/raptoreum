// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/smoke.h>

#include <rpc/protocol.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <util/strencodings.h>

#include <univalue.h>

#include <cstdint>
#include <string>
#include <vector>

/**
 * Phase 0 RPC namespace for EVM smoke testing.
 *
 * The single command exposed here, evm_executeReadOnly, is a developer tool
 * to validate that evmone is properly linked into raptoreumd. It executes
 * arbitrary bytecode against an empty world state with no consensus impact.
 *
 * This is NOT the eth_* JSON-RPC namespace — that arrives in Phase 3.
 * This namespace is "evm_" with a single read-only execution probe.
 */

namespace {

// Strip an optional "0x" / "0X" prefix from a hex string.
std::string StripHexPrefix(const std::string& hex)
{
    if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        return hex.substr(2);
    }
    return hex;
}

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

// clang-format off
const CRPCCommand commands[] =
{ //  category    name                       actor (function)            argNames
    //  ----------  -------------------------  --------------------------  ----------
    { "evm",      "evm_executeReadOnly",     &evm_executeReadOnly,       {"bytecode_hex", "calldata_hex", "gas_limit"} },
};
// clang-format on

} // namespace

void RegisterEthereumRPCCommands(CRPCTable& t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}
