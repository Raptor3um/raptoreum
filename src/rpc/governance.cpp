// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <chainparams.h>
#include <smartnode/activesmartnode.h>
#include <core_io.h>
#include <governance/governance.h>
#include <governance/governance-vote.h>
#include <governance/governance-classes.h>
#include <governance/governance-validators.h>
#include <evo/deterministicmns.h>
#include <index/txindex.h>
#include <txmempool.h>
#include <validation.h>
#include <smartnode/smartnode-sync.h>
#include <messagesigner.h>
#include <net.h>
#include <node/context.h>
#include <rpc/blockchain.h>
#include <rpc/util.h>
#include <rpc/server.h>
#include <util/system.h>
#include <wallet/rpcwallet.h>

#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif // ENABLE_WALLET

void gobject_count_help(const JSONRPCRequest &request) {
    RPCHelpMan{"gobject count",
               "Count governance objects and votes\n",
               {
                       {"mode", RPCArg::Type::STR, /* default */ "json",
                        "Output format: json (\"json\") or string in free form (\"all\")"},
               },
               RPCResults{},
               RPCExamples{""}
    }.Check(request);
}

static UniValue gobject_count(const JSONRPCRequest &request) {
    gobject_count_help(request);

    std::string strMode{"json"};

    if (!request.params[0].isNull()) {
        strMode = request.params[0].get_str();
    }

    if (strMode != "json" && strMode != "all")
        gobject_count_help(request);

    return strMode == "json" ? governance.ToJson() : governance.ToString();
}

static void gobject_deserialize_help(const JSONRPCRequest &request) {
    RPCHelpMan{"gobject deserialize",
               "Deserialize governance object from hex string to JSON\n",
               {
                       {"hex_data", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "data in hex string form"},
               },
               RPCResults{},
               RPCExamples{""},
    }.Check(request);
}

static UniValue gobject_deserialize(const JSONRPCRequest &request) {
    gobject_deserialize_help(request);

    std::string strHex = request.params[0].get_str();

    std::vector<unsigned char> v = ParseHex(strHex);
    std::string s(v.begin(), v.end());

    UniValue u(UniValue::VOBJ);
    u.read(s);

    return u.write().c_str();
}

static void gobject_check_help(const JSONRPCRequest &request) {
    RPCHelpMan{"gobject check",
               "Validate governance object data (proposal only)\n",
               {
                       {"hex_data", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "data in hex string format"},
               },
               RPCResults{},
               RPCExamples{""},
    }.Check(request);
}

static UniValue gobject_check(const JSONRPCRequest &request) {
    gobject_check_help(request);

    // ASSEMBLE NEW GOVERNANCE OBJECT FROM USER PARAMETERS

    uint256 hashParent;

    int nRevision = 1;

    int64_t nTime = GetAdjustedTime();
    std::string strDataHex = request.params[0].get_str();

    CGovernanceObject govobj(hashParent, nRevision, nTime, uint256(), strDataHex);

    if (govobj.GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL) {
        CProposalValidator validator(strDataHex, false);
        if (!validator.Validate()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Invalid proposal data, error messages:" + validator.GetErrorMessages());
        }
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid object type, only proposals can be validated");
    }

    UniValue objResult(UniValue::VOBJ);

    objResult.pushKV("Object status", "OK");

    return objResult;
}

#ifdef ENABLE_WALLET
static void gobject_prepare_help(const JSONRPCRequest& request)
{
    RPCHelpMan{"gobject prepare",
        "Prepare governance object by signing and creating tx\n"
        + HELP_REQUIRING_PASSPHRASE,
        {
            {"parent-hash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "hash of the parent object, \"0\" is root"},
            {"revision", RPCArg::Type::NUM, RPCArg::Optional::NO, "object revision in the system"},
            {"time", RPCArg::Type::NUM, RPCArg::Optional::NO, "time this object was created"},
            {"data-hex", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "data in hex string form"},
            {"use-IS", RPCArg::Type::BOOL, /* default */ "false", "Deprecated and ignored"},
            {"outputHash", RPCArg::Type::STR_HEX, /* default */ "", "the single output to submit the proposal fee from"},
            {"outputIndex", RPCArg::Type::NUM, /* default */ "", "The output index."},
        },
        RPCResults{},
        RPCExamples{""},
    }.Check(request);
}

static UniValue gobject_prepare(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    if (request.fHelp || (request.params.size() != 5 && request.params.size() != 6 && request.params.size() != 8))
        gobject_prepare_help(request);

    EnsureWalletIsUnlocked(pwallet);

    // ASSEMBLE NEW GOVERNANCE OBJECT FROM USER PARAMETERS

    uint256 hashParent;

    // -- attach to root node (root node doesn't really exist, but has a hash of zero)
    if (request.params[0].get_str() == "0") {
        hashParent = uint256();
    } else {
        hashParent = ParseHashV(request.params[0], "parent-hash");
    }

    int nRevision = ParseInt32V(request.params[1], "revision");
    int64_t nTime = ParseInt64V(request.params[2], "time");
    std::string strDataHex = request.params[3].get_str();

    // CREATE A NEW COLLATERAL TRANSACTION FOR THIS SPECIFIC OBJECT

    CGovernanceObject govobj(hashParent, nRevision, nTime, uint256(), strDataHex);

    // This command is dangerous because it consumes 5 RAPTOREUM irreversibly.
    // If params are lost, it's very hard to bruteforce them and yet
    // users ignore all instructions on raptoreumcentral etc. and do not save them...
    // Let's log them here and hope users do not mess with debug.log
    LogPrintf("gobject_prepare -- params: %s %s %s %s, data: %s, hash: %s\n",
                request.params[0].getValStr(), request.params[1].getValStr(),
                request.params[2].getValStr(), request.params[3].getValStr(),
                govobj.GetDataAsPlainString(), govobj.GetHash().ToString());

    if (govobj.GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL) {
        CProposalValidator validator(strDataHex, false);
        if (!validator.Validate()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid proposal data, error messages:" + validator.GetErrorMessages());
        }
    }

    if (govobj.GetObjectType() == GOVERNANCE_OBJECT_TRIGGER) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Trigger objects need not be prepared (however only smartnodes can create them)");
    }

    LOCK(pwallet->cs_wallet);

    if (g_txindex) {
        g_txindex->BlockUntilSyncedToCurrentChain();
    }

    {
      LOCK(cs_main);
      std::string strError = "";
      if (!govobj.IsValidLocally(strError, false))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Governance object is not valid - " + govobj.GetHash().ToString() + " - " + strError);
    }

    // If specified, spend this outpoint as the proposal fee
    COutPoint outpoint;
    outpoint.SetNull();
    if (!request.params[5].isNull() && !request.params[6].isNull()) {
        uint256 collateralHash = ParseHashV(request.params[5], "outputHash");
        int32_t collateralIndex = ParseInt32V(request.params[6], "outputIndex");
        if (collateralHash.IsNull() || collateralIndex < 0) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("invalid hash or index: %s-%d", collateralHash.ToString(), collateralIndex));
        }
        outpoint = COutPoint(collateralHash, (uint32_t)collateralIndex);
    }

    CTransactionRef tx;

    if (!pwallet->GetBudgetSystemCollateralTX(tx, govobj.GetHash(), govobj.GetMinCollateralFee(), outpoint)) {
        std::string err = "Error making collateral transaction for governance object. Please check your wallet balance and make sure your wallet is unlocked.";
        if (!request.params[5].isNull() && !request.params[6].isNull()) {
            err += "Please verify your specified output is valid and is enough for the combined proposal fee and transaction fee.";
        }
        throw JSONRPCError(RPC_INTERNAL_ERROR, err);
    }

    if (!pwallet->WriteGovernanceObject({hashParent, nRevision, nTime, tx->GetHash(), strDataHex})) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "WriteGovernanceObject failed");
    }

    // -- send the tx to the network
    {
        LOCK(cs_main);
        pwallet->CommitTransaction(tx, {}, {});
    }

    LogPrint(BCLog::GOBJECT, "gobject_prepare -- GetDataAsPlainString = %s, hash = %s, txid = %s\n",
                govobj.GetDataAsPlainString(), govobj.GetHash().ToString(), tx->GetHash().ToString());

    return tx->GetHash().ToString();
}

static void gobject_list_prepared_help(const JSONRPCRequest& request)
{
    RPCHelpMan{"gobject list-prepared",
        "Returns a list of governance objects prepared by this wallet with \"gobject prepare\" sorted by their creation time.\n"
        + HELP_REQUIRING_PASSPHRASE,
        {
            {"count", RPCArg::Type::NUM, /* default */ "10", "Maximum number of objects to return."},
        },
        RPCResults{},
        RPCExamples{""},
    }.Check(request);
}

UniValue gobject_list_prepared(const JSONRPCRequest& request)
{
    gobject_list_prepared_help(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    EnsureWalletIsUnlocked(pwallet);

    int64_t nCount = request.params.empty() ? 10 : ParseInt64V(request.params[1], "count");
    if (nCount < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative count");
    }
    // Get a list of all prepared governance objects stored in the wallet
    LOCK(pwallet->cs_wallet);
    std::vector<const CGovernanceObject*> vecObjects = pwallet->GetGovernanceObjects();
    // Sort the vector by the object creation time/hex data
    std::sort(vecObjects.begin(), vecObjects.end(), [](const CGovernanceObject* a, const CGovernanceObject* b) {
        bool fGreater = a->GetCreationTime() > b->GetCreationTime();
        bool fEqual = a->GetCreationTime() == b->GetCreationTime();
        bool fHexGreater = a->GetDataAsHexString() > b->GetDataAsHexString();
        return fGreater || (fEqual && fHexGreater);
    });

    UniValue jsonArray(UniValue::VARR);
    auto it = vecObjects.rbegin() + std::max<int>(0, vecObjects.size() - nCount);
    while (it != vecObjects.rend()) {
        jsonArray.push_back((*it++)->ToJson());
    }

    return jsonArray;
}
#endif // ENABLE_WALLET

static void gobject_submit_help(const JSONRPCRequest &request) {
    RPCHelpMan{"gobject submit",
               "Submit governance object to network\n",
               {
                       {"parent-hash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                        "hash of the parent object, \"0\" is root"},
                       {"revision", RPCArg::Type::NUM, RPCArg::Optional::NO, "object revision in the system"},
                       {"time", RPCArg::Type::NUM, RPCArg::Optional::NO, "time this object was created"},
                       {"data-hex", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "data in hex string form"},
                       {"fee-txid", RPCArg::Type::STR_HEX, /* default */ "",
                        "fee-tx id, required for all objects except triggers"},
               },
               RPCResults{},
               RPCExamples{""},
    }.Check(request);
}

static UniValue gobject_submit(const JSONRPCRequest &request) {
    gobject_submit_help(request);

    if (!smartnodeSync.IsBlockchainSynced()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Must wait for client to sync with smartnode network. Try again in a minute or so.");
    }

    auto mnList = deterministicMNManager->GetListAtChainTip();
    bool fMnFound = WITH_LOCK(activeSmartnodeInfoCs,
    return mnList.HasValidMNByCollateral(activeSmartnodeInfo.outpoint));

    LogPrint(BCLog::GOBJECT,
             "gobject_submit -- pubKeyOperator = %s, outpoint = %s, params.size() = %lld, fMnFound = %d\n",
             (WITH_LOCK(activeSmartnodeInfoCs,
    return activeSmartnodeInfo.blsPubKeyOperator ? activeSmartnodeInfo.blsPubKeyOperator->ToString() : "N/A")),
    WITH_LOCK(activeSmartnodeInfoCs,
    return activeSmartnodeInfo.outpoint.ToStringShort()), request.params.size(), fMnFound);

    // ASSEMBLE NEW GOVERNANCE OBJECT FROM USER PARAMETERS

    uint256 txidFee;

    if (!request.params[0].isNull()) {
        txidFee = ParseHashV(request.params[0], "fee-txid");
    }
    uint256 hashParent;
    if (request.params[1].get_str() ==
        "0") { // attach to root node (root node doesn't really exist, but has a hash of zero)
        hashParent = uint256();
    } else {
        hashParent = ParseHashV(request.params[1], "parent-hash");
    }

    // GET THE PARAMETERS FROM USER

    int nRevision = ParseInt32V(request.params[2], "revision");
    int64_t nTime = ParseInt64V(request.params[3], "time");
    std::string strDataHex = request.params[4].get_str();

    CGovernanceObject govobj(hashParent, nRevision, nTime, txidFee, strDataHex);

    LogPrint(BCLog::GOBJECT, "gobject_submit -- GetDataAsPlainString = %s, hash = %s, txid = %s\n",
             govobj.GetDataAsPlainString(), govobj.GetHash().ToString(), txidFee.ToString());

    if (govobj.GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL) {
        CProposalValidator validator(strDataHex, false);
        if (!validator.Validate()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Invalid proposal data, error messages:" + validator.GetErrorMessages());
        }
    }

    // Attempt to sign triggers if we are a MN
    if (govobj.GetObjectType() == GOVERNANCE_OBJECT_TRIGGER) {
        if (fMnFound) {
            // govobj.SetSmartnodeOutpoint(activeSmartnodeInfo.outpoint);
            // govobj.Sign(*activeSmartnodeInfo.blsKeyOperator);
            LOCK(activeSmartnodeInfoCs);
            govobj.SetSmartnodeOutpoint(activeSmartnodeInfo.outpoint);
            govobj.Sign(*activeSmartnodeInfo.blsKeyOperator);
        } else {
            LogPrintf("gobject(submit) -- Object submission rejected because node is not a smartnode\n");
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Only valid smartnodes can submit this type of object");
        }
    } else if (request.params.size() != 6) {
        LogPrintf("gobject(submit) -- Object submission rejected because fee tx not provided\n");
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "The fee-txid parameter must be included to submit this type of object");
    }

    std::string strHash = govobj.GetHash().ToString();

    std::string strError = "";
    bool fMissingConfirmations;
    {
        if (g_txindex) {
            g_txindex->BlockUntilSyncedToCurrentChain();
        }

        LOCK2(cs_main, ::mempool.cs);
        if (!govobj.IsValidLocally(strError, fMissingConfirmations, true) && !fMissingConfirmations) {
            LogPrintf(
                    "gobject(submit) -- Object submission rejected because object is not valid - hash = %s, strError = %s\n",
                    strHash, strError);
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Governance object is not valid - " + strHash + " - " + strError);
        }
    }

    // RELAY THIS OBJECT
    // Reject if rate check fails but don't update buffer
    if (!governance.SmartnodeRateCheck(govobj)) {
        LogPrintf("gobject(submit) -- Object submission rejected because of rate check failure - hash = %s\n", strHash);
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Object creation rate limit exceeded");
    }

    LogPrintf("gobject(submit) -- Adding locally created governance object - %s\n", strHash);

    const NodeContext &node = EnsureNodeContext(request.context);
    if (fMissingConfirmations) {
        governance.AddPostponedObject(govobj);
        govobj.Relay(*node.connman);
    } else {
        governance.AddGovernanceObject(govobj, *node.connman);
    }

    return govobj.GetHash().ToString();
}

void gobject_vote_conf_help(const JSONRPCRequest &request) {
    RPCHelpMan{"gobject vote-conf",
               "Vote on a governance object by masternode configured in dash.conf\n",
               {
                       {"governance-hash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                        "hash of the governance object"},
                       {"vote", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "vote, possible values: [funding|valid|delete|endorsed]"},
                       {"vote-outcome", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "vote outcome, possible values: [yes|no|abstain]"},
               },
               RPCResults{},
               RPCExamples{""}
    }.Check(request);
}

static UniValue gobject_vote_conf(const JSONRPCRequest &request) {
    gobject_vote_conf_help(request);

    uint256 hash;

    hash = ParseHashV(request.params[0], "Object hash");
    std::string strVoteSignal = request.params[1].get_str();
    std::string strVoteOutcome = request.params[2].get_str();

    vote_signal_enum_t eVoteSignal = CGovernanceVoting::ConvertVoteSignal(strVoteSignal);
    if (eVoteSignal == VOTE_SIGNAL_NONE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid vote signal. Please using one of the following: "
                           "(funding|valid|delete|endorsed)");
    }

    vote_outcome_enum_t eVoteOutcome = CGovernanceVoting::ConvertVoteOutcome(strVoteOutcome);
    if (eVoteOutcome == VOTE_OUTCOME_NONE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid vote outcome. Please use one of the following: 'yes', 'no' or 'abstain'");
    }

    int govObjType;
    {
        LOCK(governance.cs);
        CGovernanceObject *pGovObj = governance.FindGovernanceObject(hash);
        if (!pGovObj) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Governance object not found");
        }
        govObjType = pGovObj->GetObjectType();
    }

    int nSuccessful = 0;
    int nFailed = 0;

    UniValue resultsObj(UniValue::VOBJ);

    UniValue statusObj(UniValue::VOBJ);
    UniValue returnObj(UniValue::VOBJ);

    auto dmn = WITH_LOCK(activeSmartnodeInfoCs,
    return deterministicMNManager->GetListAtChainTip().GetValidMNByCollateral(activeSmartnodeInfo.outpoint));

    if (!dmn) {
        nFailed++;
        statusObj.pushKV("result", "failed");
        statusObj.pushKV("errorMessage", "Can't find smartnode by collateral output");
        resultsObj.pushKV("raptoreum.conf", statusObj);
        returnObj.pushKV("overall",
                         strprintf("Voted successfully %d time(s) and failed %d time(s).", nSuccessful, nFailed));
        returnObj.pushKV("detail", resultsObj);
        return returnObj;
    }

    CGovernanceVote vote(dmn->collateralOutpoint, hash, eVoteSignal, eVoteOutcome);

    bool signSuccess = false;
    if (govObjType == GOVERNANCE_OBJECT_PROPOSAL && eVoteSignal == VOTE_SIGNAL_FUNDING) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Can't use vote-conf for proposals");
    }

    {
        LOCK(activeSmartnodeInfoCs);
        if (activeSmartnodeInfo.blsKeyOperator) {
            signSuccess = vote.Sign(*activeSmartnodeInfo.blsKeyOperator);
        }
    }

    if (!signSuccess) {
        nFailed++;
        statusObj.pushKV("result", "failed");
        statusObj.pushKV("errorMessage", "Failure to sign.");
        resultsObj.pushKV("raptoreum.conf", statusObj);
        returnObj.pushKV("overall",
                         strprintf("Voted successfully %d time(s) and failed %d time(s).", nSuccessful, nFailed));
        returnObj.pushKV("detail", resultsObj);
        return returnObj;
    }

    CGovernanceException exception;
    const NodeContext &node = EnsureNodeContext(request.context);
    if (governance.ProcessVoteAndRelay(vote, exception, *node.connman)) {
        nSuccessful++;
        statusObj.pushKV("result", "success");
    } else {
        nFailed++;
        statusObj.pushKV("result", "failed");
        statusObj.pushKV("errorMessage", exception.GetMessage());
    }

    resultsObj.pushKV("raptoreum.conf", statusObj);

    returnObj.pushKV("overall",
                     strprintf("Voted successfully %d time(s) and failed %d time(s).", nSuccessful, nFailed));
    returnObj.pushKV("detail", resultsObj);

    return returnObj;
}

UniValue VoteWithSmartnodes(const JSONRPCRequest &request, const std::map <uint256, CKey> &keys,
                            const uint256 &hash, vote_signal_enum_t eVoteSignal,
                            vote_outcome_enum_t eVoteOutcome) {
    const NodeContext &node = EnsureNodeContext(request.context);
    {
        LOCK(governance.cs);
        CGovernanceObject *pGovObj = governance.FindGovernanceObject(hash);
        if (!pGovObj) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Governance object not found");
        }
    }

    int nSuccessful = 0;
    int nFailed = 0;

    auto mnList = deterministicMNManager->GetListAtChainTip();

    UniValue resultsObj(UniValue::VOBJ);

    for (const auto &p: keys) {
        const auto &proTxHash = p.first;
        const auto &key = p.second;

        UniValue statusObj(UniValue::VOBJ);

        auto dmn = mnList.GetValidMN(proTxHash);
        if (!dmn) {
            nFailed++;
            statusObj.pushKV("result", "failed");
            statusObj.pushKV("errorMessage", "Can't find smartnode by proTxHash");
            resultsObj.pushKV(proTxHash.ToString(), statusObj);
            continue;
        }

        CGovernanceVote vote(dmn->collateralOutpoint, hash, eVoteSignal, eVoteOutcome);
        if (!vote.Sign(key, key.GetPubKey().GetID())) {
            nFailed++;
            statusObj.pushKV("result", "failed");
            statusObj.pushKV("errorMessage", "Failure to sign.");
            resultsObj.pushKV(proTxHash.ToString(), statusObj);
            continue;
        }

        CGovernanceException exception;
        if (governance.ProcessVoteAndRelay(vote, exception, *node.connman)) {
            nSuccessful++;
            statusObj.pushKV("result", "success");
        } else {
            nFailed++;
            statusObj.pushKV("result", "failed");
            statusObj.pushKV("errorMessage", exception.GetMessage());
        }

        resultsObj.pushKV(proTxHash.ToString(), statusObj);
    }

    UniValue returnObj(UniValue::VOBJ);
    returnObj.pushKV("overall",
                     strprintf("Voted successfully %d time(s) and failed %d time(s).", nSuccessful, nFailed));
    returnObj.pushKV("detail", resultsObj);

    return returnObj;
}

#ifdef ENABLE_WALLET
void gobject_vote_many_help(const JSONRPCRequest& request)
{
    RPCHelpMan{"gobject vote-many",
        "Vote on a governance object by all smartnodes for which the voting key is present in the local wallet\n"
        + HELP_REQUIRING_PASSPHRASE,
        {
            {"governance-hash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "hash of the governance object"},
            {"vote", RPCArg::Type::STR, RPCArg::Optional::NO, "vote, possible values: [funding|valid|delete|endorsed]"},
            {"vote-outcome", RPCArg::Type::STR, RPCArg::Optional::NO, "vote outcome, possible values: [yes|no|abstain]"},
        },
        RPCResults{},
        RPCExamples{""}
    }.Check(request);
}

static UniValue gobject_vote_many(const JSONRPCRequest& request)
{
    gobject_vote_many_help(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    uint256 hash = ParseHashV(request.params[0], "Object hash");
    std::string strVoteSignal = request.params[1].get_str();
    std::string strVoteOutcome = request.params[2].get_str();

    vote_signal_enum_t eVoteSignal = CGovernanceVoting::ConvertVoteSignal(strVoteSignal);
    if (eVoteSignal == VOTE_SIGNAL_NONE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid vote signal. Please using one of the following: "
                           "(funding|valid|delete|endorsed)");
    }

    vote_outcome_enum_t eVoteOutcome = CGovernanceVoting::ConvertVoteOutcome(strVoteOutcome);
    if (eVoteOutcome == VOTE_OUTCOME_NONE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid vote outcome. Please use one of the following: 'yes', 'no' or 'abstain'");
    }

    EnsureWalletIsUnlocked(pwallet);

    std::map<uint256, CKey> votingKeys;

    auto mnList = deterministicMNManager->GetListAtChainTip();
    mnList.ForEachMN(true, [&](const CDeterministicMNCPtr& dmn) {
        CKey votingKey;
        if (pwallet->GetKey(dmn->pdmnState->keyIDVoting, votingKey)) {
            votingKeys.emplace(dmn->proTxHash, votingKey);
        }
    });

    return VoteWithSmartnodes(request, votingKeys, hash, eVoteSignal, eVoteOutcome);
}

void gobject_vote_alias_help(const JSONRPCRequest& request)
{
    RPCHelpMan{"gobject vote-alias",
        "Vote on a governance object by smartnode's voting key (if present in local wallet)\n"
        + HELP_REQUIRING_PASSPHRASE,
        {
            {"governance-hash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "hash of the governance object"},
            {"vote", RPCArg::Type::STR, RPCArg::Optional::NO, "vote, possible values: [funding|valid|delete|endorsed]"},
            {"vote-outcome", RPCArg::Type::STR, RPCArg::Optional::NO, "vote outcome, possible values: [yes|no|abstain]"},
            {"protx-hash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "smartnode's proTxHash"},
        },
        RPCResults{},
        RPCExamples{""}
    }.Check(request);
}

static UniValue gobject_vote_alias(const JSONRPCRequest& request)
{
    gobject_vote_alias_help(request);

    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet* const pwallet = wallet.get();

    uint256 hash = ParseHashV(request.params[0], "Object hash");
    std::string strVoteSignal = request.params[1].get_str();
    std::string strVoteOutcome = request.params[2].get_str();

    vote_signal_enum_t eVoteSignal = CGovernanceVoting::ConvertVoteSignal(strVoteSignal);
    if (eVoteSignal == VOTE_SIGNAL_NONE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid vote signal. Please using one of the following: "
                           "(funding|valid|delete|endorsed)");
    }

    vote_outcome_enum_t eVoteOutcome = CGovernanceVoting::ConvertVoteOutcome(strVoteOutcome);
    if (eVoteOutcome == VOTE_OUTCOME_NONE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid vote outcome. Please use one of the following: 'yes', 'no' or 'abstain'");
    }

    EnsureWalletIsUnlocked(pwallet);

    uint256 proTxHash = ParseHashV(request.params[4], "protx-hash");
    auto dmn = deterministicMNManager->GetListAtChainTip().GetValidMN(proTxHash);
    if (!dmn) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid or unknown proTxHash");
    }

    CKey votingKey;
    if (!pwallet->GetKey(dmn->pdmnState->keyIDVoting, votingKey)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Private key for voting address %s not known by wallet", EncodeDestination(dmn->pdmnState->keyIDVoting)));
    }

    std::map<uint256, CKey> votingKeys;
    votingKeys.emplace(proTxHash, votingKey);

    return VoteWithSmartnodes(request, votingKeys, hash, eVoteSignal, eVoteOutcome);
}
#endif

UniValue ListObjects(const std::string &strCachedSignal, const std::string &strType, int nStartTime) {
    UniValue objResult(UniValue::VOBJ);

    // GET MATCHING GOVERNANCE OBJECTS

    if (g_txindex) {
        g_txindex->BlockUntilSyncedToCurrentChain();
    }

    LOCK2(cs_main, governance.cs);

    std::vector <CGovernanceObject> objs = governance.GetAllNewerThan(nStartTime);
    governance.UpdateLastDiffTime(GetTime());

    // CREATE RESULTS FOR USER

    for (const auto &govObj: objs) {
        if (strCachedSignal == "valid" && !govObj.IsSetCachedValid()) continue;
        if (strCachedSignal == "funding" && !govObj.IsSetCachedFunding()) continue;
        if (strCachedSignal == "delete" && !govObj.IsSetCachedDelete()) continue;
        if (strCachedSignal == "endorsed" && !govObj.IsSetCachedEndorsed()) continue;

        if (strType == "proposals" && govObj.GetObjectType() != GOVERNANCE_OBJECT_PROPOSAL) continue;
        if (strType == "triggers" && govObj.GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) continue;

        UniValue bObj(UniValue::VOBJ);
        bObj.pushKV("DataHex", govObj.GetDataAsHexString());
        bObj.pushKV("DataString", govObj.GetDataAsPlainString());
        bObj.pushKV("Hash", govObj.GetHash().ToString());
        bObj.pushKV("CollateralHash", govObj.GetCollateralHash().ToString());
        bObj.pushKV("ObjectType", govObj.GetObjectType());
        bObj.pushKV("CreationTime", govObj.GetCreationTime());
        const COutPoint &smartnodeOutpoint = govObj.GetSmartnodeOutpoint();
        if (smartnodeOutpoint != COutPoint()) {
            bObj.pushKV("SigningSmartnode", smartnodeOutpoint.ToStringShort());
        }

        // REPORT STATUS FOR FUNDING VOTES SPECIFICALLY
        bObj.pushKV("AbsoluteYesCount", govObj.GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING));
        bObj.pushKV("YesCount", govObj.GetYesCount(VOTE_SIGNAL_FUNDING));
        bObj.pushKV("NoCount", govObj.GetNoCount(VOTE_SIGNAL_FUNDING));
        bObj.pushKV("AbstainCount", govObj.GetAbstainCount(VOTE_SIGNAL_FUNDING));

        // REPORT VALIDITY AND CACHING FLAGS FOR VARIOUS SETTINGS
        std::string strError = "";
        bObj.pushKV("fBlockchainValidity", govObj.IsValidLocally(strError, false));
        bObj.pushKV("IsValidReason", strError.c_str());
        bObj.pushKV("fCachedValid", govObj.IsSetCachedValid());
        bObj.pushKV("fCachedFunding", govObj.IsSetCachedFunding());
        bObj.pushKV("fCachedDelete", govObj.IsSetCachedDelete());
        bObj.pushKV("fCachedEndorsed", govObj.IsSetCachedEndorsed());

        objResult.pushKV(govObj.GetHash().ToString(), bObj);
    }

    return objResult;
}

void gobject_list_help(const JSONRPCRequest &request) {
    RPCHelpMan{"gobject list",
               "List governance objects (can be filtered by signal and/or object type)\n",
               {
                       {"signal", RPCArg::Type::STR, /* default */ "valid",
                        "cached signal, possible values: [valid|funding|delete|endorsed|all]"},
                       {"type", RPCArg::Type::STR, /* default */ "all",
                        "object type, possible values: [proposals|triggers|all]"},
               },
               RPCResults{},
               RPCExamples{""}
    }.Check(request);
}

static UniValue gobject_list(const JSONRPCRequest &request) {
    gobject_list_help(request);

    std::string strCachedSignal = "valid";
    if (!request.params[0].isNull()) {
        strCachedSignal = request.params[0].get_str();
    }
    if (strCachedSignal != "valid" && strCachedSignal != "funding" && strCachedSignal != "delete" &&
        strCachedSignal != "endorsed" && strCachedSignal != "all")
        return "Invalid signal, should be 'valid', 'funding', 'delete', 'endorsed' or 'all'";

    std::string strType = "all";
    if (!request.params[1].isNull()) {
        strType = request.params[1].get_str();
    }
    if (strType != "proposals" && strType != "triggers" && strType != "all")
        return "Invalid type, should be 'proposals', 'triggers' or 'all'";

    return ListObjects(strCachedSignal, strType, 0);
}

void gobject_diff_help(const JSONRPCRequest &request) {
    RPCHelpMan{"gobject diff",
               "List differences since last diff or list\n",
               {
                       {"signal", RPCArg::Type::STR, /* default */ "valid",
                        "cached signal, possible values: [valid|funding|delete|endorsed|all]"},
                       {"type", RPCArg::Type::STR, /* default */ "all",
                        "object type, possible values: [proposals|triggers|all]"},
               },
               RPCResults{},
               RPCExamples{""}
    }.Check(request);
}

static UniValue gobject_diff(const JSONRPCRequest &request) {
    gobject_diff_help(request);

    std::string strCachedSignal = "valid";
    if (!request.params[0].isNull()) {
        strCachedSignal = request.params[0].get_str();
    }
    if (strCachedSignal != "valid" && strCachedSignal != "funding" && strCachedSignal != "delete" &&
        strCachedSignal != "endorsed" && strCachedSignal != "all")
        return "Invalid signal, should be 'valid', 'funding', 'delete', 'endorsed' or 'all'";

    std::string strType = "all";
    if (!request.params[1].isNull()) {
        strType = request.params[1].get_str();
    }
    if (strType != "proposals" && strType != "triggers" && strType != "all")
        return "Invalid type, should be 'proposals', 'triggers' or 'all'";

    return ListObjects(strCachedSignal, strType, governance.GetLastDiffTime());
}

void gobject_get_help(const JSONRPCRequest &request) {
    RPCHelpMan{"gobject get",
               "Get governance object by hash\n",
               {
                       {"governance-hash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "object id"},
               },
               RPCResults{},
               RPCExamples{""}
    }.Check(request);
}

static UniValue gobject_get(const JSONRPCRequest &request) {
    gobject_get_help(request);

    // COLLECT VARIABLES FROM OUR USER
    uint256 hash = ParseHashV(request.params[0], "GovObj hash");

    if (g_txindex) {
        g_txindex->BlockUntilSyncedToCurrentChain();
    }

    // FIND THE GOVERNANCE OBJECT THE USER IS LOOKING FOR
    LOCK2(cs_main, governance.cs);
    CGovernanceObject *pGovObj = governance.FindGovernanceObject(hash);

    if (pGovObj == nullptr) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown governance object");
    }

    // REPORT BASIC OBJECT STATS

    UniValue objResult(UniValue::VOBJ);
    objResult.pushKV("DataHex", pGovObj->GetDataAsHexString());
    objResult.pushKV("DataString", pGovObj->GetDataAsPlainString());
    objResult.pushKV("Hash", pGovObj->GetHash().ToString());
    objResult.pushKV("CollateralHash", pGovObj->GetCollateralHash().ToString());
    objResult.pushKV("ObjectType", pGovObj->GetObjectType());
    objResult.pushKV("CreationTime", pGovObj->GetCreationTime());
    const COutPoint &smartnodeOutpoint = pGovObj->GetSmartnodeOutpoint();
    if (smartnodeOutpoint != COutPoint()) {
        objResult.pushKV("SigningSmartnode", smartnodeOutpoint.ToStringShort());
    }

    // SHOW (MUCH MORE) INFORMATION ABOUT VOTES FOR GOVERNANCE OBJECT (THAN LIST/DIFF ABOVE)
    // -- FUNDING VOTING RESULTS

    UniValue objFundingResult(UniValue::VOBJ);
    objFundingResult.pushKV("AbsoluteYesCount", pGovObj->GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING));
    objFundingResult.pushKV("YesCount", pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING));
    objFundingResult.pushKV("NoCount", pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING));
    objFundingResult.pushKV("AbstainCount", pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));
    objResult.pushKV("FundingResult", objFundingResult);

    // -- VALIDITY VOTING RESULTS
    UniValue objValid(UniValue::VOBJ);
    objValid.pushKV("AbsoluteYesCount", pGovObj->GetAbsoluteYesCount(VOTE_SIGNAL_VALID));
    objValid.pushKV("YesCount", pGovObj->GetYesCount(VOTE_SIGNAL_VALID));
    objValid.pushKV("NoCount", pGovObj->GetNoCount(VOTE_SIGNAL_VALID));
    objValid.pushKV("AbstainCount", pGovObj->GetAbstainCount(VOTE_SIGNAL_VALID));
    objResult.pushKV("ValidResult", objValid);

    // -- DELETION CRITERION VOTING RESULTS
    UniValue objDelete(UniValue::VOBJ);
    objDelete.pushKV("AbsoluteYesCount", pGovObj->GetAbsoluteYesCount(VOTE_SIGNAL_DELETE));
    objDelete.pushKV("YesCount", pGovObj->GetYesCount(VOTE_SIGNAL_DELETE));
    objDelete.pushKV("NoCount", pGovObj->GetNoCount(VOTE_SIGNAL_DELETE));
    objDelete.pushKV("AbstainCount", pGovObj->GetAbstainCount(VOTE_SIGNAL_DELETE));
    objResult.pushKV("DeleteResult", objDelete);

    // -- ENDORSED VIA SMARTNODE-ELECTED BOARD
    UniValue objEndorsed(UniValue::VOBJ);
    objEndorsed.pushKV("AbsoluteYesCount", pGovObj->GetAbsoluteYesCount(VOTE_SIGNAL_ENDORSED));
    objEndorsed.pushKV("YesCount", pGovObj->GetYesCount(VOTE_SIGNAL_ENDORSED));
    objEndorsed.pushKV("NoCount", pGovObj->GetNoCount(VOTE_SIGNAL_ENDORSED));
    objEndorsed.pushKV("AbstainCount", pGovObj->GetAbstainCount(VOTE_SIGNAL_ENDORSED));
    objResult.pushKV("EndorsedResult", objEndorsed);

    // --
    std::string strError = "";
    objResult.pushKV("fLocalValidity", pGovObj->IsValidLocally(strError, false));
    objResult.pushKV("IsValidReason", strError.c_str());
    objResult.pushKV("fCachedValid", pGovObj->IsSetCachedValid());
    objResult.pushKV("fCachedFunding", pGovObj->IsSetCachedFunding());
    objResult.pushKV("fCachedDelete", pGovObj->IsSetCachedDelete());
    objResult.pushKV("fCachedEndorsed", pGovObj->IsSetCachedEndorsed());
    return objResult;
}

void gobject_getcurrentvotes_help(const JSONRPCRequest &request) {
    RPCHelpMan{"gobject getcurrentvotes",
               "Get only current (tallying) votes for a governance object hash (does not include old votes)\n",
               {
                       {"governance-hash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "object id"},
                       {"txid", RPCArg::Type::STR_HEX, /* default */ "", "smartnode collateral txid"},
                       {"vout", RPCArg::Type::STR, /* default */ "",
                        "smartnode collateral output index, required if <txid> presents"},
               },
               RPCResults{},
               RPCExamples{""}
    }.Check(request);
}

static UniValue gobject_getcurrentvotes(const JSONRPCRequest &request) {
    gobject_getcurrentvotes_help(request);

    // COLLECT PARAMETERS FROM USER

    uint256 hash = ParseHashV(request.params[0], "Governance hash");

    COutPoint mnCollateralOutpoint;
    if (!request.params[1].isNull() && !request.params[2].isNull()) {
        uint256 txid = ParseHashV(request.params[1], "Smartnode Collateral hash");
        std::string strVout = request.params[2].get_str();
        mnCollateralOutpoint = COutPoint(txid, (uint32_t) atoi(strVout));
    }

    // FIND OBJECT USER IS LOOKING FOR

    LOCK(governance.cs);

    CGovernanceObject *pGovObj = governance.FindGovernanceObject(hash);

    if (pGovObj == nullptr) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown governance-hash");
    }

    // REPORT RESULTS TO USER

    UniValue bResult(UniValue::VOBJ);

    // GET MATCHING VOTES BY HASH, THEN SHOW USERS VOTE INFORMATION

    std::vector <CGovernanceVote> vecVotes = governance.GetCurrentVotes(hash, mnCollateralOutpoint);
    for (const auto &vote: vecVotes) {
        bResult.pushKV(vote.GetHash().ToString(), vote.ToString());
    }

    return bResult;
}

[[noreturn]] void gobject_help() {
    RPCHelpMan{"gobject",
               "Set of commands to manage governance objects.\n"
               "\nAvailable commands:\n"
               "  check              - Validate governance object data (proposal only)\n"
               #ifdef ENABLE_WALLET
               "  prepare            - Prepare governance object by signing and creating tx\n"
        "  list-prepared      - Returns a list of governance objects prepared by this wallet with \"gobject prepare\"\n"
               #endif // ENABLE_WALLET
               "  submit             - Submit governance object to network\n"
               "  deserialize        - Deserialize governance object from hex string to JSON\n"
               "  count              - Count governance objects and votes (additional param: 'json' or 'all', default: 'json')\n"
               "  get                - Get governance object by hash\n"
               "  getcurrentvotes    - Get only current (tallying) votes for a governance object hash (does not include old votes)\n"
               "  list               - List governance objects (can be filtered by signal and/or object type)\n"
               "  diff               - List differences since last diff\n"
               #ifdef ENABLE_WALLET
               "  vote-alias         - Vote on a governance object by smartnode proTxHash\n"
               #endif // ENABLE_WALLET
               "  vote-conf          - Vote on a governance object by smartnode configured in raptoreum.conf\n"
#ifdef ENABLE_WALLET
            "  vote-many          - Vote on a governance object by all smartnodes for which the voting key is in the wallet\n"
#endif // ENABLE_WALLET
            ,
               {},
               RPCResults{},
               RPCExamples{""}
    }.Throw();
}

static UniValue gobject(const JSONRPCRequest &request) {
    const JSONRPCRequest new_request{request.strMethod == "gobject" ? request.squashed() : request};
    const std::string command{new_request.strMethod};

    if (command == "getobjectcount") {
        return gobject_count(new_request);
    } else if (command == "gobjectdeserialize") {
        // DEBUG : TEST DESERIALIZATION OF GOVERNANCE META DATA
        return gobject_deserialize(new_request);
    } else if (command == "gobjectcheck") {
        // VALIDATE A GOVERNANCE OBJECT PRIOR TO SUBMISSION
        return gobject_check(new_request);
#ifdef ENABLE_WALLET
        } else if (command == "gobjectprepare") {
            // PREPARE THE GOVERNANCE OBJECT BY CREATING A COLLATERAL TRANSACTION
            return gobject_prepare(new_request);
        } else if (command == "gobjectlist-prepared") {
            return gobject_list_prepared(new_request);
#endif // ENABLE_WALLET
    } else if (command == "goibjectsubmit") {
        // AFTER COLLATERAL TRANSACTION HAS MATURED USER CAN SUBMIT GOVERNANCE OBJECT TO PROPAGATE NETWORK
        /*
            ------ Example Governance Item ------

            gobject submit 6e622bb41bad1fb18e7f23ae96770aeb33129e18bd9efe790522488e580a0a03 0 1 1464292854 "beer-reimbursement" 5b5b22636f6e7472616374222c207b2270726f6a6563745f6e616d65223a20225c22626565722d7265696d62757273656d656e745c22222c20227061796d656e745f61646472657373223a20225c225879324c4b4a4a64655178657948726e34744744514238626a6876464564615576375c22222c2022656e645f64617465223a202231343936333030343030222c20226465736372697074696f6e5f75726c223a20225c227777772e646173687768616c652e6f72672f702f626565722d7265696d62757273656d656e745c22222c2022636f6e74726163745f75726c223a20225c22626565722d7265696d62757273656d656e742e636f6d2f3030312e7064665c22222c20227061796d656e745f616d6f756e74223a20223233342e323334323232222c2022676f7665726e616e63655f6f626a6563745f6964223a2037342c202273746172745f64617465223a202231343833323534303030227d5d5d1
        */
        return gobject_submit(new_request);
    } else if (command == "gobjectvote-conf") {
        return gobject_vote_conf(new_request);
#ifdef ENABLE_WALLET
        } else if (command == "gobjectvote-many") {
            return gobject_vote_many(new_request);
        } else if (command == "gobjectvote-alias") {
            return gobject_vote_alias(new_request);
#endif
    } else if (command == "gobjectlist") {
        // USERS CAN QUERY THE SYSTEM FOR A LIST OF VARIOUS GOVERNANCE ITEMS
        return gobject_list(new_request);
    } else if (command == "gobjectdiff") {
        return gobject_diff(new_request);
    } else if (command == "gobjectget") {
        // GET SPECIFIC GOVERNANCE ENTRY
        return gobject_get(new_request);
    } else if (command == "gobjectgetcurrentvotes") {
        // GET VOTES FOR SPECIFIC GOVERNANCE OBJECT
        return gobject_getcurrentvotes(new_request);
    } else {
        gobject_help();
    }
}

UniValue voteraw(const JSONRPCRequest &request) {
    RPCHelpMan{"voteraw",
               "Compile and relay a governance vote with provided external signature instead of signing vote internally\n",
               {
                       {"mn-collateral-tx-hash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, ""},
                       {"mn-collateral-tx-index", RPCArg::Type::NUM, RPCArg::Optional::NO, ""},
                       {"governance-hash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, ""},
                       {"vote-signal", RPCArg::Type::STR, RPCArg::Optional::NO, ""},
                       {"vote-outcome", RPCArg::Type::STR, RPCArg::Optional::NO, "yes|no|abstain"},
                       {"time", RPCArg::Type::NUM, RPCArg::Optional::NO, ""},
                       {"vote-sig", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, ""},
               },
               RPCResults{},
               RPCExamples{""}
    }.Check(request);

    uint256 hashMnCollateralTx = ParseHashV(request.params[0], "mn collateral tx hash");
    int nMnCollateralTxIndex = request.params[1].get_int();
    COutPoint outpoint = COutPoint(hashMnCollateralTx, nMnCollateralTxIndex);

    uint256 hashGovObj = ParseHashV(request.params[2], "Governance hash");
    std::string strVoteSignal = request.params[3].get_str();
    std::string strVoteOutcome = request.params[4].get_str();

    vote_signal_enum_t eVoteSignal = CGovernanceVoting::ConvertVoteSignal(strVoteSignal);
    if (eVoteSignal == VOTE_SIGNAL_NONE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid vote signal. Please using one of the following: "
                           "(funding|valid|delete|endorsed)");
    }

    vote_outcome_enum_t eVoteOutcome = CGovernanceVoting::ConvertVoteOutcome(strVoteOutcome);
    if (eVoteOutcome == VOTE_OUTCOME_NONE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid vote outcome. Please use one of the following: 'yes', 'no' or 'abstain'");
    }

    int govObjType;
    {
        LOCK(governance.cs);
        CGovernanceObject *pGovObj = governance.FindGovernanceObject(hashGovObj);
        if (!pGovObj) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Governance object not found");
        }
        govObjType = pGovObj->GetObjectType();
    }

    int64_t nTime = request.params[5].get_int64();
    std::string strSig = request.params[6].get_str();
    bool fInvalid = false;
    std::vector<unsigned char> vchSig = DecodeBase64(strSig.c_str(), &fInvalid);

    if (fInvalid) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Malformed base64 encoding");
    }

    auto dmn = deterministicMNManager->GetListAtChainTip().GetValidMNByCollateral(outpoint);

    if (!dmn) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failure to find smartnode in list : " + outpoint.ToStringShort());
    }

    CGovernanceVote vote(outpoint, hashGovObj, eVoteSignal, eVoteOutcome);
    vote.SetTime(nTime);
    vote.SetSignature(vchSig);

    bool onlyVotingKeyAllowed = govObjType == GOVERNANCE_OBJECT_PROPOSAL && vote.GetSignal() == VOTE_SIGNAL_FUNDING;

    if (!vote.IsValid(onlyVotingKeyAllowed)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failure to verify vote.");
    }

    CGovernanceException exception;
    const NodeContext &node = EnsureNodeContext(request.context);
    if (governance.ProcessVoteAndRelay(vote, exception, *node.connman)) {
        return "Voted successfully";
    } else {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Error voting : " + exception.GetMessage());
    }
}

static UniValue getgovernanceinfo(const JSONRPCRequest &request) {
    RPCHelpMan{"getgovernanceinfo",
               "Returns an object containing governance parameters.\n",
               {},
               RPCResult{
                       RPCResult::Type::OBJ, "", "",
                       {
                               {RPCResult::Type::NUM, "governanceminquorum",
                                "the absolute minimum number of votes needed to trigger a governance action"},
                               {RPCResult::Type::NUM, "proposalfee",
                                "the collateral transaction fee which must be paid to create a proposal in " +
                                CURRENCY_UNIT + ""},
                               {RPCResult::Type::NUM, "superblockcycle", "the number of blocks between superblocks"},
                               {RPCResult::Type::NUM, "lastsuperblock", "the block number of the last superblock"},
                               {RPCResult::Type::NUM, "nextsuperblock", "the block number of the next superblock"},
                       }},
               RPCExamples{
                       HelpExampleCli("getgovernanceinfo", "")
                       + HelpExampleRpc("getgovernanceinfo", "")
               },
    }.Check(request);

    int nLastSuperblock = 0, nNextSuperblock = 0;
    int nBlockHeight = WITH_LOCK(cs_main,
    return ::ChainActive().Height());

    CSuperblock::GetNearestSuperblocksHeights(nBlockHeight, nLastSuperblock, nNextSuperblock);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("governanceminquorum", Params().GetConsensus().nGovernanceMinQuorum);
    obj.pushKV("proposalfee", ValueFromAmount(GOVERNANCE_PROPOSAL_FEE_TX));
    obj.pushKV("superblockcycle", Params().GetConsensus().nSuperblockCycle);
    obj.pushKV("lastsuperblock", nLastSuperblock);
    obj.pushKV("nextsuperblock", nNextSuperblock);

    return obj;
}

static UniValue getsuperblockbudget(const JSONRPCRequest &request) {
    RPCHelpMan{"getsuperblockbudget",
               "\nReturns the absolute maximum sum of superblock payments allowed.\n",
               {
                       {"index", RPCArg::Type::NUM, RPCArg::Optional::NO, "The block index"},
               },
               RPCResult{
                       RPCResult::Type::NUM, "n",
                       "The absolute maximum sum of superblock payments allowed, in " + CURRENCY_UNIT},
               RPCExamples{
                       HelpExampleCli("getsuperblockbudget", "1000")
                       + HelpExampleRpc("getsuperblockbudget", "1000")
               },
    }.Check(request);

    int nBlockHeight = request.params[0].get_int();
    if (nBlockHeight < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");
    }

    return ValueFromAmount(CSuperblock::GetPaymentsLimit(nBlockHeight));
}

static const CRPCCommand commands[] =
        { //  category              name                      actor (function)         argNames
                //  --------------------- ------------------------  -----------------------  ----------
                /* Raptoreum features */
                {"raptoreum", "getgovernanceinfo",   &getgovernanceinfo,   {}},
                {"raptoreum", "getsuperblockbudget", &getsuperblockbudget, {"index"}},
                {"raptoreum", "gobject",             &gobject,             {}},
                {"raptoreum", "voteraw",             &voteraw,             {"tx_hash", "tx_index", "gov_hash", "signal", "outcome", "time", "sig"}},

        };

void RegisterGovernanceRPCCommands(CRPCTable &t) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
