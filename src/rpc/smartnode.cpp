// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <evo/deterministicmns.h>
#include <governance/governance-classes.h>
#include <index/txindex.h>
#include <net.h>
#include <netbase.h>
#include <node/context.h>
#include <rpc/blockchain.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <smartnode/activesmartnode.h>
#include <smartnode/smartnode-payments.h>
#include <univalue.h>
#include <validation.h>
#include <wallet/coincontrol.h>
#include <wallet/rpcwallet.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif // ENABLE_WALLET

#include <fstream>
#include <iomanip>

static UniValue smartnodelist(const JSONRPCRequest& request);

static void smartnode_list_help(const JSONRPCRequest& request)
{
    RPCHelpMan{"smartnodelist",
        "Get a list of smartnodes in different modes. This call is identical to 'smartnode list' call.\n"
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
        "  votingaddress  - Print the smartnode voting Raptoreum address\n",
        {
            {"mode", RPCArg::Type::STR, /* default */ "json", "The mode to run list in"},
            {"filter", RPCArg::Type::STR, /* default */ "", "Filter results. Partial match by outpoint by default in all modes, additional matches in some modes are also available"},
        },
        RPCResults{},
        RPCExamples{""},
    }.Check(request);
}

static void smartnode_connect_help(const JSONRPCRequest& request)
{
    RPCHelpMan{"smartnode connect",
        "Connect to given smartnode\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The address of the smartnode to connect"},
        },
        RPCResults{},
        RPCExamples{""}
    }.Check(request);
}

static UniValue smartnode_connect(const JSONRPCRequest& request)
{
    smartnode_connect_help(request);

    std::string strAddress = request.params[0].get_str();

    CService addr;
    if (!Lookup(strAddress.c_str(), addr, 0, false))
        throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Incorrect smartnode address %s", strAddress));

    // TODO: Pass CConnman instance somehow and don't use global variable.
    NodeContext& node = EnsureNodeContext(request.context);
    node.connman->OpenSmartnodeConnection(CAddress(addr, NODE_NETWORK));
    if (!node.connman->IsConnected(CAddress(addr, NODE_NETWORK), CConnman::AllNodes))
        throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Couldn't connect to smartnode %s", strAddress));

    return "successfully connected";
}

static void smartnode_count_help(const JSONRPCRequest& request)
{
    RPCHelpMan{"smartnode count",
        "Get information about number of smartnodes.\n",
        {},
        RPCResults{},
        RPCExamples{""}
    }.Check(request);
}

static UniValue smartnode_count(const JSONRPCRequest& request)
{
    smartnode_count_help(request);

    auto mnList = deterministicMNManager->GetListAtChainTip();
    int total = mnList.GetAllMNsCount();
    int enabled = mnList.GetValidMNsCount();

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("total", total);
    obj.pushKV("enabled", enabled);
    return obj;
}

static UniValue GetNextSmartnodeForPayment(int heightShift)
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

static void smartnode_winner_help(const JSONRPCRequest& request)
{
    if (!IsDeprecatedRPCEnabled("smartnode_winner")) {
        throw std::runtime_error("DEPRECATED: set -deprecatedrpc=smartnode_winner to enable it");
    }

    RPCHelpMan{"smartnode winner",
        "Print info on next smartnode winner to vote for\n",
        {},
        RPCResults{},
        RPCExamples{""}
    }.Check(request);
}

static UniValue smartnode_winner(const JSONRPCRequest& request)
{
    smartnode_winner_help(request);

    return GetNextSmartnodeForPayment(10);
}

static void smartnode_current_help(const JSONRPCRequest& request)
{
    if (!IsDeprecatedRPCEnabled("smartnode_current")) {
        throw std::runtime_error("DEPRECATED: set -deprecatedrpc=smartnode_current to enable it");
    }

    RPCHelpMan{"smartnode current",
        "Print info on current smartnode winner to be paid the next block (calculated locally)\n",
        {},
        RPCResults{},
        RPCExamples{""}
    }.Check(request);
}

static UniValue smartnode_current(const JSONRPCRequest& request)
{
    smartnode_current_help(request);

    return GetNextSmartnodeForPayment(1);
}

#ifdef ENABLE_WALLET
static void smartnode_outputs_help(const JSONRPCRequest& request)
{
    RPCHelpMan{"smartnode outputs",
        "Print smartnode compatible outputs\n",
        {},
        RPCResults{},
        RPCExamples{""}
    }.Check(request);
}

static UniValue smartnode_outputs(const JSONRPCRequest& request)
{
    smartnode_outputs_help(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    // Find possible candidates
    std::vector<COutput> vPossibleCoins;
    CCoinControl coin_control;
    coin_control.nCoinType = CoinType::ONLY_SMARTNODE_COLLATERAL;
    {
      LOCK2(cs_main, pwallet->cs_wallet);
      pwallet->AvailableCoins(vPossibleCoins, true, &coin_control);
    }
    UniValue obj(UniValue::VOBJ);
    for (const auto& out : vPossibleCoins) {
        obj.pushKV(out.tx->GetHash().ToString(), strprintf("%d", out.i));
    }

    return obj;
}

#endif // ENABLE_WALLET

static void smartnode_status_help(const JSONRPCRequest& request)
{
    RPCHelpMan{"smartnode status",
        "Print smartnode status information\n",
        {},
        RPCResults{},
        RPCExamples{""}
    }.Check(request);
}

static UniValue smartnode_status(const JSONRPCRequest& request)
{
    if (request.fHelp)
        smartnode_status_help(request);

    if (!fSmartnodeMode)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a smartnode");

    UniValue mnObj(UniValue::VOBJ);

    CDeterministicMNCPtr dmn;
    {
        LOCK(activeSmartnodeInfoCs);

        // Keep compatibility with legacy status for now.
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

static void smartnode_winners_help(const JSONRPCRequest& request)
{
    RPCHelpMan{"smartnode winners",
        "Print list of smartnode winners\n",
        {
            {"count", RPCArg::Type::NUM, /* default */ "", "number of last winners to return"},
            {"filter", RPCArg::Type::STR, /* default */ "", "filter for returned winners"},
        },
        RPCResults{},
        RPCExamples{""}
    }.Check(request);
}

static UniValue smartnode_winners(const JSONRPCRequest& request)
{
    smartnode_winners_help(request);

    const CBlockIndex* pindexTip{nullptr};
    {
        LOCK(cs_main);
        pindexTip = ::ChainActive().Tip();
        if (!pindexTip) return NullUniValue;
    }

    int nCount = 10;
    std::string strFilter = "";

    if (!request.params[0].isNull()) {
        nCount = atoi(request.params[0].get_str());
    }

    if (!request.params[1].isNull()) {
        strFilter = request.params[1].get_str();
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
void smartnode_payments_help(const JSONRPCRequest& request)
{
    RPCHelpMan{"smartnode payments",
        "\nReturns an array of deterministic smartnodes and their payments for the specified block\n",
        {
            {"blockhash", RPCArg::Type::STR_HEX, /* default */ "tip", "The hash of the starting block"},
            {"count", RPCArg::Type::NUM, /* default */ "1", "The number of blocks to return. Will return <count> previous blocks if <count> is negative. Both 1 and -1 correspond to the chain tip."},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "Blocks",
            {
                {RPCResult::Type::OBJ, "", "",
                {
                    {RPCResult::Type::NUM, "height", "The height of the block"},
                    {RPCResult::Type::STR_HEX, "blockhash", "The hash of the block"},
                    {RPCResult::Type::NUM, "amount", "Amount received in this block by all smartnodes"},
                    {RPCResult::Type::ARR, "smartnodes", "Smartnodes that received payments in this block",
                    {
                        {RPCResult::Type::STR_HEX, "proTxHash", "The hash of the corresponding ProRegTx"},
                        {RPCResult::Type::NUM, "amount", "Amount received by this smartnode"},
                        {RPCResult::Type::ARR, "payees", "Payees who received a share of this payment",
                        {
                            {RPCResult::Type::STR, "address", "Payee address"},
                            {RPCResult::Type::STR_HEX, "script", "Payee scriptPubKey"},
                            {RPCResult::Type::NUM, "amount", "Amount received by this payee"},
                        }},
                    }},
                }},
            },
        },
        RPCExamples{""}
    }.Check(request);
}

UniValue smartnode_payments(const JSONRPCRequest& request)
{
    smartnode_payments_help(request);

    CBlockIndex* pindex{nullptr};

    if (g_txindex) {
        g_txindex->BlockUntilSyncedToCurrentChain();
    }

    if (request.params[0].isNull()) {
        LOCK(cs_main);
        pindex = ::ChainActive().Tip();
    } else {
        LOCK(cs_main);
        uint256 blockHash = ParseHashV(request.params[0], "blockhash");
        pindex = LookupBlockIndex(blockHash);
        if (pindex == nullptr) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }
    }

    int64_t nCount = request.params.size() > 2 ? ParseInt64V(request.params[1], "count") : 1;

    // A temporary vector which is used to sort results properly (there is no "reverse" in/for UniValue)
    std::vector<UniValue> vecPayments;

    while (vecPayments.size() < uint64_t(std::abs(nCount)) && pindex != nullptr) {

        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus())) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");
        }

        // Note: we have to actually calculate block reward from scratch instead of simply querying coinbase vout
        // because miners might collect less coins than they potentially could and this would break our calculations.
        CAmount nBlockFees{0};
        NodeContext& node = EnsureNodeContext(request.context);
        for (const auto& tx : block.vtx) {
            if (tx->IsCoinBase()) {
                continue;
            }
            CAmount nValueIn{0};
            for (const auto& txin : tx->vin) {
                uint256 blockHashTmp;
                CTransactionRef txPrev = GetTransaction(/* block_index */ nullptr, node.mempool, txin.prevout.hash, Params().GetConsensus(), blockHashTmp);
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
            pindex = ::ChainActive().Next(pindex);
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
    RPCHelpMan{"smartnode",
        "Set of commands to execute smartnode related actions\n"
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
        "  winners      - Print list of smartnode winners\n",
        {
            {"command", RPCArg::Type::STR, RPCArg::Optional::NO, "The command to execute"},
        },
        RPCResults{},
        RPCExamples{""},
    }.Throw();
}

static UniValue smartnode(const JSONRPCRequest& request)
{
    const JSONRPCRequest new_request{request.strMethod == "smartnode" ? request.squashed() : request};
    const std::string command{new_request.strMethod};

    if (command == "smartnodeconnect") {
        return smartnode_connect(new_request);
    } else if (command == "smartnodecount") {
        return smartnode_count(new_request);
    } else if (command == "smartnodecurrent") {
        return smartnode_current(new_request);
    } else if (command == "smartnodewinner") {
        return smartnode_winner(new_request);
#ifdef ENABLE_WALLET
    } else if (command == "smartnodeoutputs") {
        return smartnode_outputs(new_request);
#endif // ENABLE_WALLET
    } else if (command == "smartnodestatus") {
        return smartnode_status(new_request);
    } else if (command == "smartnodepayments") {
        return smartnode_payments(new_request);
    } else if (command == "smartnodewinners") {
        return smartnode_winners(new_request);
    } else if (command == "smartnodelist") {
        return smartnodelist(new_request);
    } else {
        smartnode_help();
    }
}

static UniValue smartnodelist(const JSONRPCRequest& request)
{
    std::string strMode = "json";
    std::string strFilter = "";

    if (!request.params[0].isNull()) strMode = request.params[0].get_str();
    if (!request.params[1].isNull()) strFilter = request.params[1].get_str();

    strMode = ToLower(strMode);

    if (request.fHelp || (
                strMode != "addr" && strMode != "full" && strMode != "info" && strMode != "json" &&
                strMode != "owneraddress" && strMode != "votingaddress" &&
                strMode != "lastpaidtime" && strMode != "lastpaidblock" &&
                strMode != "payee" && strMode != "pubkeyoperator" &&
                strMode != "status"))
    {
        smartnode_list_help(request);
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
        const CBlockIndex* pindex = ::ChainActive()[dmn->pdmnState->nLastPaidHeight];
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
    { "raptoreum",               "smartnodelist",         &smartnode,             {} },
};

void RegisterSmartnodeRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
