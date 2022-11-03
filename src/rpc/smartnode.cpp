// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <smartnode/activesmartnode.h>
#include <base58.h>
#include <clientversion.h>
#include <init.h>
#include <netbase.h>
#include <validation.h>
#include <util.h>
#include <utilmoneystr.h>
#include <txmempool.h>

#include <evo/specialtx.h>
#include <evo/deterministicmns.h>

#include <governance/governance-classes.h>

#include <smartnode/smartnode-payments.h>
#include <smartnode/smartnode-sync.h>

#include <rpc/server.h>

#include <wallet/coincontrol.h>
#include <wallet/rpcwallet.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif // ENABLE_WALLET

#include <fstream>
#include <iomanip>
#include <univalue.h>

UniValue smartnodelist(const JSONRPCRequest& request);

void smartnode_list_help()
{
    throw std::runtime_error(
            "smartnodelist ( \"mode\" \"filter\" )\n"
            "Get a list of smartnodes in different modes. This call is identical to 'smartnode list' call.\n"
            "\nArguments:\n"
            "1. \"mode\"      (string, optional/required to use filter, defaults = json) The mode to run list in\n"
            "2. \"filter\"    (string, optional) Filter results. Partial match by outpoint by default in all modes,\n"
            "                                    additional matches in some modes are also available\n"
            "\nAvailable modes:\n"
            "  addr           - Print ip address associated with a smartnode (can be additionally filtered, partial match)\n"
            "  full           - Print info in format 'status payee lastpaidtime lastpaidblock IP'\n"
            "                   (can be additionally filtered, partial match)\n"
            "  info           - Print info in format 'status payee IP'\n"
            "                   (can be additionally filtered, partial match)\n"
            "  json           - Print info in JSON format (can be additionally filtered, partial match)\n"
            "  lastpaidblock  - Print the last block height a node was paid on the network\n"
            "  lastpaidtime   - Print the last time a node was paid on the network\n"
            "  owneraddress   - Print the smartnode owner Raptoreum address\n"
            "  payee          - Print the smartnode payout Raptoreum address (can be additionally filtered,\n"
            "                   partial match)\n"
            "  pubKeyOperator - Print the smartnode operator public key\n"
            "  status         - Print smartnode status: ENABLED / POSE_BANNED\n"
            "                   (can be additionally filtered, partial match)\n"
            "  votingaddress  - Print the smartnode voting Raptoreum address\n"
        );
}

UniValue smartnode_list(const JSONRPCRequest& request)
{
    if (request.fHelp)
        smartnode_list_help();
    JSONRPCRequest newRequest = request;
    newRequest.params.setArray();
    // forward params but skip "list"
    for (unsigned int i = 1; i < request.params.size(); i++) {
        newRequest.params.push_back(request.params[i]);
    }
    return smartnodelist(newRequest);
}

void smartnode_connect_help()
{
    throw std::runtime_error(
            "smartnode connect \"address\"\n"
            "Connect to given smartnode\n"
            "\nArguments:\n"
            "1. \"address\"      (string, required) The address of the smartnode to connect\n"
        );
}

UniValue smartnode_connect(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2)
        smartnode_connect_help();

    std::string strAddress = request.params[1].get_str();

    CService addr;
    if (!Lookup(strAddress.c_str(), addr, 0, false))
        throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Incorrect smartnode address %s", strAddress));

    // TODO: Pass CConnman instance somehow and don't use global variable.
    g_connman->OpenSmartnodeConnection(CAddress(addr, NODE_NETWORK));
    if (!g_connman->IsConnected(CAddress(addr, NODE_NETWORK), CConnman::AllNodes))
        throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Couldn't connect to smartnode %s", strAddress));

    return "successfully connected";
}

void smartnode_count_help()
{
    throw std::runtime_error(
            "smartnode count\n"
            "Get information about number of smartnodes.\n"
        );
}

UniValue smartnode_count(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        smartnode_count_help();

    auto mnList = deterministicMNManager->GetListAtChainTip();
    int total = mnList.GetAllMNsCount();
    int enabled = mnList.GetValidMNsCount();

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("total", total);
    obj.pushKV("enabled", enabled);
    return obj;
}

UniValue GetNextSmartnodeForPayment(int heightShift)
{
    auto mnList = deterministicMNManager->GetListAtChainTip();
    auto payees = mnList.GetProjectedMNPayees(heightShift);
    if (payees.empty())
        return "unknown";
    auto payee = payees.back();
    CScript payeeScript = payee->pdmnState->scriptPayout;

    CTxDestination payeeDest;
    ExtractDestination(payeeScript, payeeDest);

    UniValue obj(UniValue::VOBJ);

    obj.pushKV("height",        mnList.GetHeight() + heightShift);
    obj.pushKV("IP:port",       payee->pdmnState->addr.ToString());
    obj.pushKV("proTxHash",     payee->proTxHash.ToString());
    obj.pushKV("outpoint",      payee->collateralOutpoint.ToStringShort());
    obj.pushKV("payee",         IsValidDestination(payeeDest) ? EncodeDestination(payeeDest) : "UNKNOWN");
    return obj;
}

void smartnode_winner_help()
{
    if (!IsDeprecatedRPCEnabled("smartnode_winner")) {
        throw std::runtime_error("DEPRECATED: set -deprecatedrpc=smartnode_winner to enable it");
    }

    throw std::runtime_error(
            "smartnode winner\n"
            "Print info on next smartnode winner to vote for\n"
        );
}

UniValue smartnode_winner(const JSONRPCRequest& request)
{
    if (request.fHelp || !IsDeprecatedRPCEnabled("smartnode_winner"))
        smartnode_winner_help();

    return GetNextSmartnodeForPayment(10);
}

void smartnode_current_help()
{
    if (!IsDeprecatedRPCEnabled("smartnode_current")) {
        throw std::runtime_error("DEPRECATED: set -deprecatedrpc=smartnode_current to enable it");
    }

    throw std::runtime_error(
            "smartnode current\n"
            "Print info on current smartnode winner to be paid the next block (calculated locally)\n"
        );
}

UniValue smartnode_current(const JSONRPCRequest& request)
{
    if (request.fHelp || !IsDeprecatedRPCEnabled("smartnode_current"))
        smartnode_current_help();

    return GetNextSmartnodeForPayment(1);
}

#ifdef ENABLE_WALLET
void smartnode_outputs_help()
{
    throw std::runtime_error(
            "smartnode outputs\n"
            "Print smartnode compatible outputs\n"
        );
}

UniValue smartnode_outputs(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp))
        return NullUniValue;

    if (request.fHelp)
        smartnode_outputs_help();

    LOCK2(cs_main, pwallet->cs_wallet);

    // Find possible candidates
    std::vector<COutput> vPossibleCoins;
    CCoinControl coin_control;
    coin_control.nCoinType = CoinType::ONLY_SMARTNODE_COLLATERAL;
    pwallet->AvailableCoins(vPossibleCoins, true, &coin_control);

    UniValue obj(UniValue::VOBJ);
    for (const auto& out : vPossibleCoins) {
        obj.pushKV(out.tx->GetHash().ToString(), strprintf("%d", out.i));
    }

    return obj;
}

#endif // ENABLE_WALLET

void smartnode_status_help()
{
    throw std::runtime_error(
            "smartnode status\n"
            "Print smartnode status information\n"
        );
}

UniValue smartnode_status(const JSONRPCRequest& request)
{
    if (request.fHelp)
        smartnode_status_help();

    if (!fSmartnodeMode)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a smartnode");

    UniValue mnObj(UniValue::VOBJ);

    CDeterministicMNCPtr dmn;
    {
        LOCK(activeSmartnodeInfoCs);

        // keep compatibility with legacy status for now (might get deprecated/removed later)
        mnObj.pushKV("outpoint", activeSmartnodeInfo.outpoint.ToStringShort());
        mnObj.pushKV("service", activeSmartnodeInfo.service.ToString());
        dmn = deterministicMNManager->GetListAtChainTip().GetMN(activeSmartnodeInfo.proTxHash);
    }
    if (dmn) {
        mnObj.pushKV("proTxHash", dmn->proTxHash.ToString());
        mnObj.pushKV("collateralHash", dmn->collateralOutpoint.hash.ToString());
        mnObj.pushKV("collateralIndex", (int)dmn->collateralOutpoint.n);
        UniValue stateObj;
        dmn->pdmnState->ToJson(stateObj);
        mnObj.pushKV("dmnState", stateObj);
    }
    mnObj.pushKV("state", activeSmartnodeManager->GetStateString());
    mnObj.pushKV("status", activeSmartnodeManager->GetStatus());

    return mnObj;
}

std::string GetRequiredPaymentsString(int nBlockHeight, const CDeterministicMNCPtr &payee)
{
    std::string strPayments = "Unknown";
    if (payee) {
        CTxDestination dest;
        if (!ExtractDestination(payee->pdmnState->scriptPayout, dest)) {
            assert(false);
        }
        strPayments = EncodeDestination(dest);
        if (payee->nOperatorReward != 0 && payee->pdmnState->scriptOperatorPayout != CScript()) {
            if (!ExtractDestination(payee->pdmnState->scriptOperatorPayout, dest)) {
                assert(false);
            }
            strPayments += ", " + EncodeDestination(dest);
        }
    }
    if (CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
        std::vector<CTxOut> voutSuperblock;
        if (!CSuperblockManager::GetSuperblockPayments(nBlockHeight, voutSuperblock)) {
            return strPayments + ", error";
        }
        std::string strSBPayees = "Unknown";
        for (const auto& txout : voutSuperblock) {
            CTxDestination dest;
            ExtractDestination(txout.scriptPubKey, dest);
            if (strSBPayees != "Unknown") {
                strSBPayees += ", " + EncodeDestination(dest);
            } else {
                strSBPayees = EncodeDestination(dest);
            }
        }
        strPayments += ", " + strSBPayees;
    }
    return strPayments;
}

void smartnode_winners_help()
{
    throw std::runtime_error(
            "smartnode winners ( count \"filter\" )\n"
            "Print list of smartnode winners\n"
            "\nArguments:\n"
            "1. count        (numeric, optional) number of last winners to return\n"
            "2. filter       (string, optional) filter for returned winners\n"
        );
}

UniValue smartnode_winners(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 3)
        smartnode_winners_help();

    const CBlockIndex* pindexTip{nullptr};
    {
        LOCK(cs_main);
        pindexTip = chainActive.Tip();
        if (!pindexTip) return NullUniValue;
    }

    int nCount = 10;
    std::string strFilter = "";

    if (!request.params[1].isNull()) {
        nCount = atoi(request.params[1].get_str());
    }

    if (!request.params[2].isNull()) {
        strFilter = request.params[2].get_str();
    }

    UniValue obj(UniValue::VOBJ);

    int nChainTipHeight = pindexTip->nHeight;
    int nStartHeight = std::max(nChainTipHeight - nCount, 1);

    for (int h = nStartHeight; h <= nChainTipHeight; h++) {
        auto payee = deterministicMNManager->GetListForBlock(pindexTip->GetAncestor(h - 1)).GetMNPayee();
        std::string strPayments = GetRequiredPaymentsString(h, payee);
        if (strFilter != "" && strPayments.find(strFilter) == std::string::npos) continue;
        obj.pushKV(strprintf("%d", h), strPayments);
    }

    auto projection = deterministicMNManager->GetListForBlock(pindexTip).GetProjectedMNPayees(20);
    for (size_t i = 0; i < projection.size(); i++) {
        int h = nChainTipHeight + 1 + i;
        std::string strPayments = GetRequiredPaymentsString(h, projection[i]);
        if (strFilter != "" && strPayments.find(strFilter) == std::string::npos) continue;
        obj.pushKV(strprintf("%d", h), strPayments);
    }

    return obj;
}
void smartnode_payments_help()
{
    throw std::runtime_error(
            "smartnode payments ( \"blockhash\" count )\n"
            "\nReturns an array of deterministic smartnodes and their payments for the specified block\n"
            "\nArguments:\n"
            "1. \"blockhash\"                       (string, optional, default=tip) The hash of the starting block\n"
            "2. count                             (numeric, optional, default=1) The number of blocks to return.\n"
            "                                     Will return <count> previous blocks if <count> is negative.\n"
            "                                     Both 1 and -1 correspond to the chain tip.\n"
            "\nResult:\n"
            "  [                                  (array) Blocks\n"
            "    {\n"
            "       \"height\" : n,                 (numeric) The height of the block\n"
            "       \"blockhash\" : \"hash\",         (string) The hash of the block\n"
            "       \"amount\": n                   (numeric) Amount received in this block by all smartnodes\n"
            "       \"smartnodes\": [              (array) Smartnodes that received payments in this block\n"
            "          {\n"
            "             \"proTxHash\": \"xxxx\",    (string) The hash of the corresponding ProRegTx\n"
            "             \"amount\": n             (numeric) Amount received by this smartnode\n"
            "             \"payees\": [             (array) Payees who received a share of this payment\n"
            "                {\n"
            "                  \"address\" : \"xxx\", (string) Payee address\n"
            "                  \"script\" : \"xxx\",  (string) Payee scriptPubKey\n"
            "                  \"amount\": n        (numeric) Amount received by this payee\n"
            "                },...\n"
            "             ]\n"
            "          },...\n"
            "       ]\n"
            "    },...\n"
            "  ]\n"
        );
}

UniValue smartnode_payments(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 3) {
        smartnode_payments_help();
    }

    CBlockIndex* pindex{nullptr};

    if (request.params[1].isNull()) {
        LOCK(cs_main);
        pindex = chainActive.Tip();
    } else {
        LOCK(cs_main);
        uint256 blockHash = ParseHashV(request.params[1], "blockhash");
        pindex = LookupBlockIndex(blockHash);
        if (pindex == nullptr) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }
    }

    int64_t nCount = request.params.size() > 2 ? ParseInt64V(request.params[2], "count") : 1;

    // A temporary vector which is used to sort results properly (there is no "reverse" in/for UniValue)
    std::vector<UniValue> vecPayments;

    while (vecPayments.size() < std::abs(nCount) != 0 && pindex != nullptr) {

        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus())) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");
        }

        // Note: we have to actually calculate block reward from scratch instead of simply querying coinbase vout
        // because miners might collect less coins than they potentially could and this would break our calculations.
        CAmount nBlockFees{0};
        for (const auto& tx : block.vtx) {
            if (tx->IsCoinBase()) {
                continue;
            }
            CAmount nValueIn{0};
            for (const auto txin : tx->vin) {
                CTransactionRef txPrev;
                uint256 blockHashTmp;
                GetTransaction(txin.prevout.hash, txPrev, Params().GetConsensus(), blockHashTmp);
                nValueIn += txPrev->vout[txin.prevout.n].nValue;
            }
            nBlockFees += nValueIn - tx->GetValueOut();
        }

        std::vector<CTxOut> voutSmartnodePayments, voutDummy;
        CMutableTransaction dummyTx;
        CAmount specialTxFees;
        CAmount blockReward = nBlockFees + GetBlockSubsidy(pindex->pprev->nBits, pindex->pprev->nHeight, Params().GetConsensus());
        FillBlockPayments(dummyTx, pindex->nHeight, blockReward, voutSmartnodePayments, voutDummy, specialTxFees);

        UniValue blockObj(UniValue::VOBJ);
        CAmount payedPerBlock{0};

        UniValue smartnodeArr(UniValue::VARR);
        UniValue protxObj(UniValue::VOBJ);
        UniValue payeesArr(UniValue::VARR);
        CAmount payedPerSmartnode{0};

        for (const auto& txout : voutSmartnodePayments) {
            UniValue obj(UniValue::VOBJ);
            CTxDestination dest;
            ExtractDestination(txout.scriptPubKey, dest);
            obj.pushKV("address", EncodeDestination(dest));
            obj.pushKV("script", HexStr(txout.scriptPubKey));
            obj.pushKV("amount", txout.nValue);
            payedPerSmartnode += txout.nValue;
            payeesArr.push_back(obj);
        }

        const auto dmnPayee = deterministicMNManager->GetListForBlock(pindex).GetMNPayee();
        protxObj.pushKV("proTxHash", dmnPayee == nullptr ? "" : dmnPayee->proTxHash.ToString());
        protxObj.pushKV("amount", payedPerSmartnode);
        protxObj.pushKV("payees", payeesArr);
        payedPerBlock += payedPerSmartnode;
        smartnodeArr.push_back(protxObj);

        blockObj.pushKV("height", pindex->nHeight);
        blockObj.pushKV("blockhash", pindex->GetBlockHash().ToString());
        blockObj.pushKV("amount", payedPerBlock);
        blockObj.pushKV("smartnodes", smartnodeArr);
        vecPayments.push_back(blockObj);

        if (nCount > 0) {
            LOCK(cs_main);
            pindex = chainActive.Next(pindex);
        } else {
            pindex = pindex->pprev;
        }
    }

    if (nCount < 0) {
        std::reverse(vecPayments.begin(), vecPayments.end());
    }

    UniValue paymentsArr(UniValue::VARR);
    for (const auto& payment : vecPayments) {
        paymentsArr.push_back(payment);
    }

    return paymentsArr;
}

[[ noreturn ]] void smartnode_help()
{
    throw std::runtime_error(
        "smartnode \"command\" ...\n"
        "Set of commands to execute smartnode related actions\n"
        "\nArguments:\n"
        "1. \"command\"        (string or set of strings, required) The command to execute\n"
        "\nAvailable commands:\n"
        "  count        - Get information about number of smartnodes\n"
        "  current      - DEPRECATED Print info on current smartnode winner to be paid the next block (calculated locally)\n"
#ifdef ENABLE_WALLET
        "  outputs      - Print smartnode compatible outputs\n"
#endif // ENABLE_WALLET
        "  status       - Print smartnode status information\n"
        "  list         - Print list of all known smartnodes (see smartnodelist for more info)\n"
        "  payments     - Return information about smartnode payments in a mined block\n"
        "  winner       - DEPRECATED Print info on next smartnode winner to vote for\n"
        "  winners      - Print list of smartnode winners\n"
        );
}

UniValue smartnode(const JSONRPCRequest& request)
{
    std::string strCommand;
    if (!request.params[0].isNull()) {
        strCommand = request.params[0].get_str();
    }

    if (request.fHelp && strCommand.empty()) {
        smartnode_help();
    }

    if (strCommand == "list") {
        return smartnode_list(request);
    } else if (strCommand == "connect") {
        return smartnode_connect(request);
    } else if (strCommand == "count") {
        return smartnode_count(request);
    } else if (strCommand == "current") {
        return smartnode_current(request);
    } else if (strCommand == "winner") {
        return smartnode_winner(request);
#ifdef ENABLE_WALLET
    } else if (strCommand == "outputs") {
        return smartnode_outputs(request);
#endif // ENABLE_WALLET
    } else if (strCommand == "status") {
        return smartnode_status(request);
    } else if (strCommand == "payments") {
        return smartnode_payments(request);
    } else if (strCommand == "winners") {
        return smartnode_winners(request);
    } else {
        smartnode_help();
    }
}

UniValue smartnodelist(const JSONRPCRequest& request)
{
    std::string strMode = "json";
    std::string strFilter = "";

    if (!request.params[0].isNull()) strMode = request.params[0].get_str();
    if (!request.params[1].isNull()) strFilter = request.params[1].get_str();

    std::transform(strMode.begin(), strMode.end(), strMode.begin(), ::tolower);

    if (request.fHelp || (
                strMode != "addr" && strMode != "full" && strMode != "info" && strMode != "json" &&
                strMode != "owneraddress" && strMode != "votingaddress" &&
                strMode != "lastpaidtime" && strMode != "lastpaidblock" &&
                strMode != "payee" && strMode != "pubkeyoperator" &&
                strMode != "status"))
    {
        smartnode_list_help();
    }

    UniValue obj(UniValue::VOBJ);

    auto mnList = deterministicMNManager->GetListAtChainTip();
    auto dmnToStatus = [&](const CDeterministicMNCPtr& dmn) {
        if (mnList.IsMNValid(dmn)) {
            return "ENABLED";
        }
        if (mnList.IsMNPoSeBanned(dmn)) {
            return "POSE_BANNED";
        }
        return "UNKNOWN";
    };
    auto dmnToLastPaidTime = [&](const CDeterministicMNCPtr& dmn) {
        if (dmn->pdmnState->nLastPaidHeight == 0) {
            return (int)0;
        }

        LOCK(cs_main);
        const CBlockIndex* pindex = chainActive[dmn->pdmnState->nLastPaidHeight];
        return (int)pindex->nTime;
    };

    mnList.ForEachMN(false, [&](const CDeterministicMNCPtr& dmn) {
        std::string strOutpoint = dmn->collateralOutpoint.ToStringShort();
        Coin coin;
        std::string collateralAddressStr = "UNKNOWN";
        if (GetUTXOCoin(dmn->collateralOutpoint, coin)) {
            CTxDestination collateralDest;
            if (ExtractDestination(coin.out.scriptPubKey, collateralDest)) {
                collateralAddressStr = EncodeDestination(collateralDest);
            }
        }

        CScript payeeScript = dmn->pdmnState->scriptPayout;
        CTxDestination payeeDest;
        std::string payeeStr = "UNKNOWN";
        if (ExtractDestination(payeeScript, payeeDest)) {
            payeeStr = EncodeDestination(payeeDest);
        }

        if (strMode == "addr") {
            std::string strAddress = dmn->pdmnState->addr.ToString(false);
            if (strFilter !="" && strAddress.find(strFilter) == std::string::npos &&
                strOutpoint.find(strFilter) == std::string::npos) return;
            obj.pushKV(strOutpoint, strAddress);
        } else if (strMode == "full") {
            std::ostringstream streamFull;
            streamFull << std::setw(18) <<
                           dmnToStatus(dmn) << " " <<
                           payeeStr << " " << std::setw(10) <<
                           dmnToLastPaidTime(dmn) << " "  << std::setw(6) <<
                           dmn->pdmnState->nLastPaidHeight << " " <<
                           dmn->pdmnState->addr.ToString();
            std::string strFull = streamFull.str();
            if (strFilter !="" && strFull.find(strFilter) == std::string::npos &&
                strOutpoint.find(strFilter) == std::string::npos) return;
            obj.pushKV(strOutpoint, strFull);
        } else if (strMode == "info") {
            std::ostringstream streamInfo;
            streamInfo << std::setw(18) <<
                           dmnToStatus(dmn) << " " <<
                           payeeStr << " " <<
                           dmn->pdmnState->addr.ToString();
            std::string strInfo = streamInfo.str();
            if (strFilter !="" && strInfo.find(strFilter) == std::string::npos &&
                strOutpoint.find(strFilter) == std::string::npos) return;
            obj.pushKV(strOutpoint, strInfo);
        } else if (strMode == "json") {
            std::ostringstream streamInfo;
            streamInfo <<  dmn->proTxHash.ToString() << " " <<
                           dmn->pdmnState->addr.ToString() << " " <<
                           payeeStr << " " <<
                           dmnToStatus(dmn) << " " <<
                           dmnToLastPaidTime(dmn) << " " <<
                           dmn->pdmnState->nLastPaidHeight << " " <<
                           EncodeDestination(dmn->pdmnState->keyIDOwner) << " " <<
                           EncodeDestination(dmn->pdmnState->keyIDVoting) << " " <<
                           collateralAddressStr << " " <<
                           dmn->pdmnState->pubKeyOperator.Get().ToString();
            std::string strInfo = streamInfo.str();
            if (strFilter !="" && strInfo.find(strFilter) == std::string::npos &&
                strOutpoint.find(strFilter) == std::string::npos) return;
            UniValue objMN(UniValue::VOBJ);
            objMN.pushKV("proTxHash", dmn->proTxHash.ToString());
            objMN.pushKV("address", dmn->pdmnState->addr.ToString());
            objMN.pushKV("payee", payeeStr);
            objMN.pushKV("status", dmnToStatus(dmn));
            objMN.pushKV("lastpaidtime", dmnToLastPaidTime(dmn));
            objMN.pushKV("lastpaidblock", dmn->pdmnState->nLastPaidHeight);
            objMN.pushKV("owneraddress", EncodeDestination(dmn->pdmnState->keyIDOwner));
            objMN.pushKV("votingaddress", EncodeDestination(dmn->pdmnState->keyIDVoting));
            objMN.pushKV("collateraladdress", collateralAddressStr);
            objMN.pushKV("pubkeyoperator", dmn->pdmnState->pubKeyOperator.Get().ToString());
            obj.pushKV(strOutpoint, objMN);
        } else if (strMode == "lastpaidblock") {
            if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) return;
            obj.pushKV(strOutpoint, dmn->pdmnState->nLastPaidHeight);
        } else if (strMode == "lastpaidtime") {
            if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) return;
            obj.pushKV(strOutpoint, dmnToLastPaidTime(dmn));
        } else if (strMode == "payee") {
            if (strFilter !="" && payeeStr.find(strFilter) == std::string::npos &&
                strOutpoint.find(strFilter) == std::string::npos) return;
            obj.pushKV(strOutpoint, payeeStr);
        } else if (strMode == "owneraddress") {
            if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) return;
            obj.pushKV(strOutpoint, EncodeDestination(dmn->pdmnState->keyIDOwner));
        } else if (strMode == "pubkeyoperator") {
            if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) return;
            obj.pushKV(strOutpoint, dmn->pdmnState->pubKeyOperator.Get().ToString());
        } else if (strMode == "status") {
            std::string strStatus = dmnToStatus(dmn);
            if (strFilter !="" && strStatus.find(strFilter) == std::string::npos &&
                strOutpoint.find(strFilter) == std::string::npos) return;
            obj.pushKV(strOutpoint, strStatus);
        } else if (strMode == "votingaddress") {
            if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) return;
            obj.pushKV(strOutpoint, EncodeDestination(dmn->pdmnState->keyIDVoting));
        }
    });

    return obj;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         argNames
  //  --------------------- ------------------------  -----------------------  ----------
    { "raptoreum",               "smartnode",             &smartnode,             {} },
    { "raptoreum",               "smartnodelist",         &smartnodelist,         {} },
};

void RegisterSmartnodeRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
