// Copyright (c) 2019-2020 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validation.h"
#ifdef ENABLE_WALLET
#include "privatesend/privatesend-client.h"
#endif // ENABLE_WALLET
#include "privatesend/privatesend-server.h"
#include "rpc/server.h"

#include <univalue.h>

#ifdef ENABLE_WALLET
UniValue privatesend(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "privatesend \"command\"\n"
            "\nArguments:\n"
            "1. \"command\"        (string or set of strings, required) The command to execute\n"
            "\nAvailable commands:\n"
            "  start       - Start mixing\n"
            "  stop        - Stop mixing\n"
            "  reset       - Reset mixing\n"
        );

    if (fSmartnodeMode)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Client-side mixing is not supported on smartnodes");

    if (!privateSendClient.fEnablePrivateSend) {
        if (fLiteMode) {
            // mixing is disabled by default in lite mode
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Mixing is disabled in lite mode, use -enableprivatesend command line option to enable mixing again");
        } else if (!gArgs.GetBoolArg("-enableprivatesend", true)) {
            // otherwise it's on by default, unless cmd line option says otherwise
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Mixing is disabled via -enableprivatesend=0 command line option, remove it to enable mixing again");
        } else {
            // neither litemode nor enableprivatesend=false casee,
            // most likely smth bad happened and we disabled it while running the wallet
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Mixing is disabled due to some internal error");
        }
    }

    if (request.params[0].get_str() == "start") {
        {
            LOCK(pwallet->cs_wallet);
            if (pwallet->IsLocked(true))
                throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please unlock wallet for mixing with walletpassphrase first.");
        }

        privateSendClient.fPrivateSendRunning = true;
        bool result = privateSendClient.DoAutomaticDenominating(*g_connman);
        return "Mixing " + (result ? "started successfully" : ("start failed: " + privateSendClient.GetStatuses() + ", will retry"));
    }

    if (request.params[0].get_str() == "stop") {
        privateSendClient.fPrivateSendRunning = false;
        return "Mixing was stopped";
    }

    if (request.params[0].get_str() == "reset") {
        privateSendClient.ResetPool();
        return "Mixing was reset";
    }

    return "Unknown command, please see \"help privatesend\"";
}
#endif // ENABLE_WALLET

UniValue getpoolinfo(const JSONRPCRequest& request)
{
    throw std::runtime_error(
            "getpoolinfo\n"
            "DEPRECATED. Please use getprivatesendinfo instead.\n"
    );
}

UniValue getprivatesendinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
                "getprivatesendinfo\n"
                "Returns an object containing an information about PrivateSend settings and state.\n"
                "\nResult (for regular nodes):\n"
                "{\n"
                "  \"enabled\": true|false,             (bool) Whether mixing functionality is enabled\n"
                "  \"running\": true|false,             (bool) Whether mixing is currently running\n"
                "  \"multisession\": true|false,        (bool) Whether PrivateSend Multisession option is enabled\n"
                "  \"max_sessions\": xxx,               (numeric) How many parallel mixing sessions can there be at once\n"
                "  \"max_rounds\": xxx,                 (numeric) How many rounds to mix\n"
                "  \"max_amount\": xxx,                 (numeric) Target PrivateSend balance in " + CURRENCY_UNIT + "\n"
                "  \"max_denoms\": xxx,                 (numeric) How many inputs of each denominated amount to create\n"
                "  \"queue_size\": xxx,                 (numeric) How many queues there are currently on the network\n"
                "  \"sessions\":                        (array of json objects)\n"
                "    [\n"
                "      {\n"
                "      \"protxhash\": \"...\",            (string) The ProTxHash of the smartnode\n"
                "      \"outpoint\": \"txid-index\",      (string) The outpoint of the smartnode\n"
                "      \"service\": \"host:port\",        (string) The IP address and port of the smartnode\n"
                "      \"denomination\": xxx,           (numeric) The denomination of the mixing session in " + CURRENCY_UNIT + "\n"
                "      \"state\": \"...\",                (string) Current state of the mixing session\n"
                "      \"entries_count\": xxx,          (numeric) The number of entries in the mixing session\n"
                "      }\n"
                "      ,...\n"
                "    ],\n"
                "  \"keys_left\": xxx,                  (numeric) How many new keys are left since last automatic backup\n"
                "  \"warnings\": \"...\"                  (string) Warnings if any\n"
                "}\n"
                "\nResult (for smartnodes):\n"
                "{\n"
                "  \"queue_size\": xxx,                 (numeric) How many queues there are currently on the network\n"
                "  \"denomination\": xxx,               (numeric) The denomination of the mixing session in " + CURRENCY_UNIT + "\n"
                "  \"state\": \"...\",                    (string) Current state of the mixing session\n"
                "  \"entries_count\": xxx,              (numeric) The number of entries in the mixing session\n"
                "}\n"
                "\nExamples:\n"
                + HelpExampleCli("getprivatesendinfo", "")
                + HelpExampleRpc("getprivatesendinfo", "")
        );
    }

    UniValue obj(UniValue::VOBJ);

    if (fSmartnodeMode) {
        privateSendServer.GetJsonInfo(obj);
        return obj;
    }


#ifdef ENABLE_WALLET
    privateSendClient.GetJsonInfo(obj);

    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        return obj;
    }

    obj.push_back(Pair("keys_left",     pwallet->nKeysLeftSinceAutoBackup));
    obj.push_back(Pair("warnings",      pwallet->nKeysLeftSinceAutoBackup < PRIVATESEND_KEYS_THRESHOLD_WARNING
                                        ? "WARNING: keypool is almost depleted!" : ""));
#endif // ENABLE_WALLET

    return obj;
}

static const CRPCCommand commands[] =
    { //  category              name                      actor (function)         okSafe argNames
        //  --------------------- ------------------------  -----------------------  ------ ----------
        { "raptoreum",               "getpoolinfo",            &getpoolinfo,            true,  {} },
        { "raptoreum",               "getprivatesendinfo",     &getprivatesendinfo,     true,  {} },
#ifdef ENABLE_WALLET
        { "raptoreum",               "privatesend",            &privatesend,            false, {} },
#endif // ENABLE_WALLET
};

void RegisterPrivateSendRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
