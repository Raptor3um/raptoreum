// Copyright (c) 2019-2020 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/context.h>
#include <validation.h>
#ifdef ENABLE_WALLET
#include <coinjoin/coinjoin-client.h>
#include <coinjoin/coinjoin-client-options.h>
#include <wallet/rpcwallet.h>
#endif // ENABLE_WALLET
#include <coinjoin/coinjoin-server.h>
#include <rpc/blockchain.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <util/strencodings.h>

#include <univalue.h>

#ifdef ENABLE_WALLET
static UniValue coinjoin(const JSONRPCRequest& request)
{
    RPCHelpMan{"coinjoin",
        "\nAvailable commands:\n"
        "  start       - Start mixing\n"
        "  stop        - Stop mixing\n"
        "  reset       - Reset mixing",
        {
            {"command", RPCArg::Type::STR, RPCArg::Optional::NO, "The command to execute"},
        },
        RPCResult{},
        RPCExamples{""},
    }.Check(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    if (fSmartnodeMode)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Client-side mixing is not supported on smartnodes");

    if (!CCoinJoinClientOptions::IsEnabled()) {
        if (!gArgs.GetBoolArg("-enablecoinjoin", true)) {
            // otherwise it's on by default, unless cmd line option says otherwise
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Mixing is disabled via -enablecoinjoin=0 command line option, remove it to enable mixing again");
        } else {
            // not enablecoinjoin=false case,
            // most likely something bad happened and we disabled it while running the wallet
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Mixing is disabled due to some internal error");
        }
    }

    auto it = coinJoinClientManagers.find(pwallet->GetName());

    if (request.params[0].get_str() == "start") {
        {
            LOCK(pwallet->cs_wallet);
            if (pwallet->IsLocked(true))
                throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please unlock wallet for mixing with walletpassphrase first.");
        }

        if (!it->second->StartMixing()) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Mixing has been started already.");
        }

        bool result = it->second->DoAutomaticDenominating(*g_rpc_node->connman);
        return "Mixing " + (result ? "started successfully" : ("start failed: " + it->second->GetStatuses() + ", will retry"));
    }

    if (request.params[0].get_str() == "stop") {
        it->second->StopMixing();
        return "Mixing was stopped";
    }

    if (request.params[0].get_str() == "reset") {
        it->second->ResetPool();
        return "Mixing was reset";
    }

    return "Unknown command, please see \"help coinjoin\"";
}
#endif // ENABLE_WALLET

UniValue getpoolinfo(const JSONRPCRequest& request)
{
    throw std::runtime_error(
            RPCHelpMan{"getpoolinfo",
                "DEPRECATED. Please use getcoinjoininfo instead.\n",
            {},
            RPCResult{},
            RPCExamples{""},
            .ToString()
    );
}

UniValue getcoinjoininfo(const JSONRPCRequest& request)
{
    RPCHelpMan{"getcoinjoininfo",
        "Returns an object containing an information about CoinJoin settings and state.\n",
        {},
        {
            RPCResult{"for regular nodes",
                RPCResult::Type::OBJ, "", "",
                {
                    {RPCResult::Type::BOOL, "enabled", "Whether mixing functionality is enabled"},
                    {RPCResult::Type::BOOL, "multisession", "Whether CoinJoin Multisession option is enabled"},
                    {RPCResult::Type::NUM, "max_sessions", "How many parallel mixing sessions can there be at once"},
                    {RPCResult::Type::NUM, "max_rounds", "How many rounds to mix"},
                    {RPCResult::Type::NUM, "max_amount", "Target CoinJoin balance in " + CURRENCY_UNIT + ""},
                    {RPCResult::Type::NUM, "denoms_goal", "How many inputs of each denominated amount to target"},
                    {RPCResult::Type::NUM, "denoms_hardcap", "Maximum limit of how many inputs of each denominated amount to create"},
                    {RPCResult::Type::NUM, "queue_size", "How many queues there are currently on the network"},
                    {RPCResult::Type::BOOL, "running", "Whether mixing is currently running"},
                    {RPCResult::Type::ARR, "sessions", "",
                    {
                        {RPCResult::Type::OBJ, "", "",
                        {
                            {RPCResult::Type::STR_HEX, "protxhash", "The ProTxHash of the smartnode"},
                            {RPCResult::Type::STR_HEX, "outpoint", "The outpoint of the smartnode"},
                            {RPCResult::Type::STR, "service", "The IP address and port of the smartnode"},
                            {RPCResult::Type::NUM, "denomination", "The denomination of the mixing session in " + CURRENCY_UNIT + ""},
                            {RPCResult::Type::STR_HEX, "state", "Current state of the mixing session"},
                            {RPCResult::Type::NUM, "entries_count", "The number of entries in the mixing session"},
                        }},
                    }},
                    {RPCResult::Type::NUM, "keys_left", "How many new keys are left since last automatic backup"},
                    {RPCResult::Type::STR, "warnings", "Warnings if any"},
                }},
            RPCResult{"for smartnodes",
                RPCResult::Type::OBJ, "", "",
                {
                    {RPCResult::Type::NUM, "queue_size", "How many queues there are currently on the network"},
                    {RPCResult::Type::NUM, "denomination", "The denomination of the mixing session in " + CURRENCY_UNIT + ""},
                    {RPCResult::Type::STR_HEX, "state", "Current state of the mixing session"},
                    {RPCResult::Type::NUM, "entries_count", "The number of entries in the mixing session"},
                }},
        },
        RPCExamples{
            HelpExampleCli("getcoinjoininfo", "")
            + HelpExampleRpc("getcoinjoininfo", "")
        },
    }.Check(request);

    UniValue obj(UniValue::VOBJ);

    if (fSmartnodeMode) {
        coinJoinServer.GetJsonInfo(obj);
        return obj;
    }


#ifdef ENABLE_WALLET

    CCoinJoinClientOptions::GetJsonInfo(obj);

    obj.pushKV("queue_size", coinJoinClientQueueManager.GetQueueSize());

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
    if (!pwallet) {
        return obj;
    }

    coinJoinClientManagers.at(pwallet->GetName())->GetJsonInfo(obj);

    obj.pushKV("keys_left",     pwallet->nKeysLeftSinceAutoBackup);
    obj.pushKV("warnings",      pwallet->nKeysLeftSinceAutoBackup < COINJOIN_KEYS_THRESHOLD_WARNING
                                        ? "WARNING: keypool is almost depleted!" : "");
#endif // ENABLE_WALLET

    return obj;
}

static const CRPCCommand commands[] =
    { //  category              name                      actor (function)         argNames
        //  --------------------- ------------------------  ---------------------------------
        { "raptoreum",               "getpoolinfo",            &getpoolinfo,            {} },
        { "raptoreum",               "getcoinjoininfo",        &getcoinjoininfo,        {} },
#ifdef ENABLE_WALLET
        { "raptoreum",               "coinjoin",               &coinjoin,               {} },
#endif // ENABLE_WALLET
};

void RegisterCoinJoinRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
