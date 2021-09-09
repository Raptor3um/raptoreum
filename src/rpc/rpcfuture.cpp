// Copyright (c) 2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/specialtx_utilities.h"
#include "future/fee.h"

#ifdef ENABLE_WALLET
extern UniValue signrawtransaction(const JSONRPCRequest& request);
extern UniValue sendrawtransaction(const JSONRPCRequest& request);
#endif//ENABLE_WALLET


[[ noreturn ]] void futuretx_send_help()
{
    throw std::runtime_error(
            "futuretx send amount fromAddress toAddress maturity lockTime ...\n"
            "Send amount of RTM fromAddress to toAddress and locked by maturity or lockTime. Transaction fees are variable. \n"
    		"If maturity is negative, this transaction is locked by lockTime."
    		"If lockTime is negative, this transaction is locked by maturity."
    		"If both are negative, this transaction is locked until an external condition is met."
    		"Otherwise, it is unlocked and spendable by either maturity or lockTime whichever comes first.\n"
            "\nArguments:\n"
            "1. \"amount\"        (Number, required) Amount of RTM to be sent\n"
            "2. \"fromAddress\"   (String, required) Source address where unspent is from. It needs to have enough for amount + future fee + mining fee. \n"
            "3. \"toAddress\"     (String, required) Destination address\n"
            "4. \"maturity\"      (Number, required) Amount of confirmations needed for this transaction to be spendable.\n"
            "5. \"lockTime\"      (Number, required) Number of seconds from first confirmations for this transaction to be spendable.\n"

    );
}

[[ noreturn ]] void futuretx_fee_help()
{
    throw std::runtime_error(
            "futuretx fee ...\n"
            "Get current future transaction fee amount \n"

    );
}
//#ifdef ENABLE_WALLET
UniValue futuretx_send(const JSONRPCRequest& request) {
	if(request.params.size() < 6) {
		futuretx_send_help();
	}
	CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
	CAmount amount = AmountFromValue(request.params[1]);
    CBitcoinAddress fromAddress(request.params[2].get_str());
    CBitcoinAddress toAddress(request.params[3].get_str());
    int32_t maturity = ParseInt32V(request.params[4], "maturity");
    int32_t lockTime = ParseInt32V(request.params[5], "lockTime");
    CScript toAddressScript = GetScriptForDestination(toAddress.Get());
    CTxOut toTxOut(amount, toAddressScript);

	CMutableTransaction tx;
	tx.nVersion = 3;
	tx.nType = TRANSACTION_FUTURE;
    tx.vout.emplace_back(toTxOut);

    CFutureTx ftx;
	ftx.nVersion = CFutureTx::CURRENT_VERSION;
	ftx.lockTime = lockTime;
	ftx.maturity = maturity;
	ftx.lockOutputIndex = 0;
	ftx.updatableByDestination = false;

	CAmount futureFee = getFutureFees();
    FundSpecialTx(pwallet, tx, ftx, fromAddress.Get(), futureFee);
    UpdateSpecialTxInputsHash(tx, ftx);
    SetTxPayload(tx, ftx);
    //UniValue result(UniValue::VOBJ);
	//TxToUniv(CTransaction(std::move(tx)), uint256(), result);
	//return result;
	return SignAndSendSpecialTx(tx);
;
}
//#endif

UniValue futuretx_fee(const JSONRPCRequest& request) {
	if(request.params.size() > 1) {
		futuretx_fee_help();
	}
	return getFutureFees() / COIN;
}

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
    		"  fee               - Return current fee amount for Future transaction\n"
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
    } else if (command == "fee") {
        return futuretx_fee(request);
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
