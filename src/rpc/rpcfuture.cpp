// Copyright (c) 2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "init.h"
#include "messagesigner.h"
#include "rpc/server.h"
#include "utilmoneystr.h"
#include "validation.h"

#ifdef ENABLE_WALLET
#include "wallet/coincontrol.h"
#include "wallet/wallet.h"
#include "wallet/rpcwallet.h"
#endif//ENABLE_WALLET

#include "netbase.h"
#include "evo/specialtx.h"
#include "evo/providertx.h"

#include <iostream>
#include <unistd.h>

using namespace std;

#ifdef ENABLE_WALLET
extern UniValue signrawtransaction(const JSONRPCRequest& request);
extern UniValue sendrawtransaction(const JSONRPCRequest& request);
#endif//ENABLE_WALLET

#ifdef ENABLE_WALLET
UniValue futuretx_send(const JSONRPCRequest& request) {
	throw std::runtime_error(
				"not yet implemented"
				);
}
#endif

UniValue futuretx_info(const JSONRPCRequest& request) {
	throw std::runtime_error(
			"not yet implemented"
			);
}

[[ noreturn ]] void futuretx_help()
{
    throw std::runtime_error(
            "futuretx \"command\" ...\n"
            "Set of commands to execute FutureTx related actions.\n"
            "To get help on individual commands, use \"help futuretx command\".\n"
            "\nArguments:\n"
            "1. \"command\"        (string, required) The command to execute\n"
            "\nAvailable commands:\n"
#ifdef ENABLE_WALLET
            "  send              - Create and send FutureTx to network\n"
#endif
            "  info              - Return information about a FutureTx\n"
    );
}


UniValue futuretx(const JSONRPCRequest& request)
{
    if (request.fHelp && request.params.empty()) {
       futuretx_help();
    }

    std::string command;
    if (request.params.size() >= 1) {
        command = request.params[0].get_str();
    }

#ifdef ENABLE_WALLET
    if (command == "send") {
        return futuretx_send(request);
    }
#endif
    if (command == "info") {
        return futuretx_info(request);
    }  else {
    	futuretx_help();
    }
}


static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "future",                "futuretx",                    &futuretx,                   false, {}  },
};

void RegisterFutureRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}
