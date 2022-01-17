// Copyright (c) 2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/specialtx_utilities.h>
#include <future/fee.h>

#ifdef ENABLE_WALLET
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>
#include <wallet/rpcwallet.h>
#endif //ENABLE_WALLET

#include <evo/specialtx.h>
#include <evo/providertx.h>

#ifdef ENABLE_WALLET
extern UniValue signrawtransaction(const JSONRPCRequest& request);
extern UniValue sendrawtransaction(const JSONRPCRequest& request);
#endif //ENABLE_WALLET

std::string GetFtxHelp(int nFtxParamNum, std::string strFtxParamName)
{
  static const std::map<std::string, std::string> mapFtxParamHelp = {
    {"amount", "%d. \"amount\"           (numeric, required) RTM Amount to be sent\n"},
    {"fromAddress", "%d. \"fromAddress\" (string, required) Raptoreum Source Address FutureTx was made from. Must hold enough RTM to cover amount + FutureTx Fee + Network Fee.\n"},
    {"toAddress", "%d. \"toAddress\"     (string, required) Raptoreum Destination Address.\n"},
    {"maturity", "%d. \"maturity\"       (numeric, required) Number of confirmations (blocks) needed to make this FutureTx spendable.\n"},
    {"lockTime", "%d. \"lockTime\"       (numeric, required) Number of seconds needed counted from 1st confirmation to make this FutureTx spendable.\n"},
  };

  auto it = mapFtxParamHelp.find(strFtxParamName);
  if(it == mapFtxParamHelp.end())
    throw std::runtime_error(strprintf("FIXME: WRONG PARAM NAME %s!", strFtxParamName));

  return strprintf(it->second, nFtxParamNum);
}

[[ noreturn ]] void futuretx_send_help(CWallet* const pwallet)
{
    throw std::runtime_error(
      "futuretx send \"amount\" \"fromAddress\" \"toAddress\" \"maturity\" \"lockTime\"\n"
      "Send amount of RTM fromAddress to toAddress and locked by maturity or lockTime. Transaction fees are variable. \n"
      "If maturity is negative, this transaction is locked by lockTime."
      "If lockTime is negative, this transaction is locked by maturity."
      "If both are negative, this transaction is locked until an external condition is met."
      "Otherwise, it is unlocked and spendable by either maturity or lockTime whichever comes first.\n"
      + HelpRequiringPassphrase(pwallet) + "\n"
      "\nArguments:\n"
      + GetFtxHelp(1, "amount")
      + GetFtxHelp(2, "fromAddress")
      + GetFtxHelp(3, "toAddress")
      + GetFtxHelp(4, "maturity")
      + GetFtxHelp(5, "lockTime") +
      "\nResult:\n"
      "\"txid:\"       (string) FutureTx transaction id.\n"
      "\nExamples:\n"
      + HelpExampleCli("futuretx", "send \"amount\" \"fromAddress\" \"toAddress\" \"maturity\" \"lockTime\"")
    );
}

[[ noreturn ]] void futuretx_fee_help()
{
    throw std::runtime_error(
            "futuretx fee ...\n"
            "Get current future transaction fee amount \n"

    );
}
#ifdef ENABLE_WALLET
UniValue futuretx_send(const JSONRPCRequest& request)
{
  std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
  CWallet* const pwallet = wallet.get();
	if(request.fHelp || request.params.size() != 6)
	  futuretx_send_help(pwallet);

	if(!EnsureWalletIsAvailable(pwallet, request.fHelp))
	  return NullUniValue;

	EnsureWalletIsUnlocked(pwallet);

  CMutableTransaction tx;
  tx.nVersion = 3;
  tx.nType = TRANSACTION_FUTURE;

	CFutureTx ftx;
	ftx.nVersion = CFutureTx::CURRENT_VERSION;
	ftx.lockTime = ParseInt32V(request.params[5], "lockTime");
	ftx.maturity = ParseInt32V(request.params[4], "maturity");
	ftx.lockOutputIndex = 0;
	ftx.updatableByDestination = false;

	CAmount amount = AmountFromValue(request.params[1]);
	CAmount futureFee = getFutureFees();

  CTxDestination toAddress = DecodeDestination(request.params[3].get_str());
  if(!IsValidDestination(toAddress))
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Invalid Receipent Raptoreum address: %s", request.params[3].get_str()));

  CScript toAddressScript = GetScriptForDestination(toAddress);
  CTxOut toTxOut(amount, toAddressScript);
  tx.vout.emplace_back(toTxOut);

  CTxDestination fromAddress = DecodeDestination(request.params[2].get_str());
  if(!IsValidDestination(fromAddress))
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Invalid Sender Raptoreum address: %s", request.params[2].get_str()));

  FundSpecialTx(pwallet, tx, ftx, fromAddress, futureFee);
  //double check to be sure lockOutputIndex is to update to new index if it change because of FundSpecialTx
  for (const auto& txOut : tx.vout)
  {
    if(txOut.scriptPubKey == toAddressScript)
      break;

		ftx.lockOutputIndex++;
	}

  UpdateSpecialTxInputsHash(tx, ftx);
  SetTxPayload(tx, ftx);
  //UniValue result(UniValue::VOBJ);
	//TxToUniv(CTransaction(std::move(tx)), uint256(), result);
	//return result;
	return SignAndSendSpecialTx(tx);

}
#endif //ENABLE_WALLET

UniValue futuretx_fee(const JSONRPCRequest& request)
{
	if(request.params.size() > 1)
	{
		futuretx_fee_help();
  }

  return getFutureFees() / COIN;
}

UniValue futuretx_info(const JSONRPCRequest& request) {
	throw std::runtime_error("not yet implemented");
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
    { "future",                "futuretx",                    &futuretx,                   {}  },
};

void RegisterFutureRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}
