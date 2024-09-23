// Copyright (c) 2017-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <index/txindex.h>
#include <node/context.h>
#include <rpc/blockchain.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <validation.h>

#include <smartnode/activesmartnode.h>
#include <evo/deterministicmns.h>

#include <llmq/quorums.h>
#include <llmq/quorums_commitment.h>
#include <llmq/quorums_blockprocessor.h>
#include <llmq/quorums_debug.h>
#include <llmq/quorums_dkgsession.h>
#include <llmq/quorums_signing.h>
#include <llmq/quorums_signing_shares.h>

namespace llmq {
    extern const std::string CLSIG_REQUESTID_PREFIX;
}

void quorum_list_help(const JSONRPCRequest &request) {
    RPCHelpMan{"quorum list",
               "List of on-chain quorums\n",
               {
                       {"count", RPCArg::Type::NUM, /* default */ "",
                        "Number of quorums to list. Will list active quorums if \"count\" is not specified."},
               },
               RPCResult{
                       RPCResult::Type::OBJ, "", "",
                       {
                               {RPCResult::Type::ARR, "quorumName", "List of quorum hashes per some quorum type",
                                {
                                        {RPCResult::Type::STR_HEX, "quorumHash",
                                         "Quorum hash. Note: most recent quorums come first."},
                                }},
                       }},
               RPCExamples{
                       HelpExampleCli("quorum", "list")
                       + HelpExampleCli("quorum", "list 10")
                       + HelpExampleRpc("quorum", "list, 10")
               },
    }.Check(request);
}

UniValue quorum_list(const JSONRPCRequest &request) {
    quorum_list_help(request);

    int count = -1;
    if (!request.params[0].isNull()) {
        count = ParseInt32V(request.params[0], "count");
        if (count < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "count can't be negative");
        }
    }

    UniValue ret(UniValue::VOBJ);

    CBlockIndex * pindexTip = WITH_LOCK(cs_main,
    return ::ChainActive().Tip());

    for (auto &type: llmq::CLLMQUtils::GetEnabledQuorumTypes(pindexTip)) {
        const auto &llmq_params = llmq::GetLLMQParams(type);
        UniValue v(UniValue::VARR);

        auto quorums = llmq::quorumManager->ScanQuorums(type, pindexTip,
                                                        count > -1 ? count : llmq_params.signingActiveQuorumCount);
        for (auto &q: quorums) {
            v.push_back(q->qc->quorumHash.ToString());
        }

        ret.pushKV(std::string(llmq_params.name), v);
    }


    return ret;
}

void quorum_info_help(const JSONRPCRequest &request) {
    RPCHelpMan{"quorum info",
               "Return information about a quorum\n",
               {
                       {"llmqType", RPCArg::Type::NUM, RPCArg::Optional::NO, "LLMQ type."},
                       {"quorumHash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Block hash of quorum."},
                       {"includeSkShare", RPCArg::Type::BOOL, /* default */ "", "Include secret key share in output."},
               },
               RPCResults{},
               RPCExamples{""},
    }.Check(request);
}

UniValue BuildQuorumInfo(const llmq::CQuorumCPtr &quorum, bool includeMembers, bool includeSkShare) {
    UniValue ret(UniValue::VOBJ);

    ret.pushKV("height", quorum->m_quorum_base_block_index->nHeight);
    ret.pushKV("type", std::string(quorum->params.name));
    ret.pushKV("quorumHash", quorum->qc->quorumHash.ToString());
    ret.pushKV("minedBlock", quorum->minedBlockHash.ToString());

    if (includeMembers) {
        UniValue membersArr(UniValue::VARR);
        for (size_t i = 0; i < quorum->members.size(); i++) {
            auto &dmn = quorum->members[i];
            UniValue mo(UniValue::VOBJ);
            mo.pushKV("proTxHash", dmn->proTxHash.ToString());
            mo.pushKV("pubKeyOperator", dmn->pdmnState->pubKeyOperator.Get().ToString());
            mo.pushKV("valid", quorum->qc->validMembers[i]);
            if (quorum->qc->validMembers[i]) {
                CBLSPublicKey pubKey = quorum->GetPubKeyShare(i);
                if (pubKey.IsValid()) {
                    mo.pushKV("pubKeyShare", pubKey.ToString());
                }
            }
            membersArr.push_back(mo);
        }

        ret.pushKV("members", membersArr);
    }
    ret.pushKV("quorumPublicKey", quorum->qc->quorumPublicKey.ToString());
    const CBLSSecretKey &skShare = quorum->GetSkShare();
    if (includeSkShare && skShare.IsValid()) {
        ret.pushKV("secretKeyShare", skShare.ToString());
    }
    return ret;
}

UniValue quorum_info(const JSONRPCRequest &request) {
    quorum_info_help(request);

    Consensus::LLMQType llmqType = (Consensus::LLMQType) ParseInt32V(request.params[0], "llmqType");
    if (!Params().GetConsensus().llmqs.count(llmqType)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid LLMQ type");
    }

    uint256 quorumHash = ParseHashV(request.params[1], "quorumHash");
    bool includeSkShare = false;
    if (!request.params[2].isNull()) {
        includeSkShare = ParseBoolV(request.params[2], "includeSkShare");
    }

    auto quorum = llmq::quorumManager->GetQuorum(llmqType, quorumHash);
    if (!quorum) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "quorum not found");
    }

    return BuildQuorumInfo(quorum, true, includeSkShare);
}

void quorum_dkgstatus_help(const JSONRPCRequest &request) {
    RPCHelpMan{"quorum dkgstatus",
               "Return the status of the current DKG process.\n"
               "Works only when SPORK_17_QUORUM_DKG_ENABLED spork is ON.\n",
               {
                       {"detail_level", RPCArg::Type::NUM, /* default */ "0",
                        "Detail level of output.\n"
                        "0=Only show counts. 1=Show member indexes. 2=Show member's ProTxHashes."},
               },
               RPCResults{},
               RPCExamples{""},
    }.Check(request);
}

UniValue quorum_dkgstatus(const JSONRPCRequest &request) {
    quorum_dkgstatus_help(request);

    int detailLevel = 0;
    if (!request.params[0].isNull()) {
        detailLevel = ParseInt32V(request.params[0], "detail_level");
        if (detailLevel < 0 || detailLevel > 2) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid detail_level");
        }
    }

    llmq::CDKGDebugStatus status;
    llmq::quorumDKGDebugManager->GetLocalDebugStatus(status);

    auto ret = status.ToJson(detailLevel);

    CBlockIndex * pindexTip = WITH_LOCK(cs_main,
    return ::ChainActive().Tip());
    int tipHeight = pindexTip->nHeight;

    auto proTxHash = WITH_LOCK(activeSmartnodeInfoCs,
    return activeSmartnodeInfo.proTxHash);
    UniValue mineableCommitments(UniValue::VOBJ);
    UniValue quorumConnections(UniValue::VOBJ);
    for (const auto &type: llmq::CLLMQUtils::GetEnabledQuorumTypes(pindexTip)) {
        const auto &llmq_params = llmq::GetLLMQParams(type);

        if (fSmartnodeMode) {
            const CBlockIndex *pQuorumBaseBlockIndex = WITH_LOCK(cs_main,
            return ::ChainActive()[tipHeight - (tipHeight % llmq_params.dkgInterval)]);
            auto allConnections = llmq::CLLMQUtils::GetQuorumConnections(llmq_params, pQuorumBaseBlockIndex, proTxHash,
                                                                         false);
            auto outboundConnections = llmq::CLLMQUtils::GetQuorumConnections(llmq_params, pQuorumBaseBlockIndex,
                                                                              proTxHash, true);
            std::map <uint256, CAddress> foundConnections;
            NodeContext &node = EnsureNodeContext(request.context);
            node.connman->ForEachNode([&](const CNode *pnode) {
                auto verifiedProRegTxHash = pnode->GetVerifiedProRegTxHash();
                if (!verifiedProRegTxHash.IsNull() && allConnections.count(verifiedProRegTxHash)) {
                    foundConnections.emplace(verifiedProRegTxHash, pnode->addr);
                }
            });
            UniValue arr(UniValue::VARR);
            for (auto &ec: allConnections) {
                UniValue obj(UniValue::VOBJ);
                obj.pushKV("proTxHash", ec.ToString());
                if (foundConnections.count(ec)) {
                    obj.pushKV("connected", true);
                    obj.pushKV("address", foundConnections[ec].ToString(false));
                } else {
                    obj.pushKV("connected", false);
                }
                obj.pushKV("outbound", outboundConnections.count(ec) != 0);
                arr.push_back(obj);
            }
            quorumConnections.pushKV(std::string(llmq_params.name), arr);
        }

        LOCK(cs_main);
        llmq::CFinalCommitment fqc;
        if (llmq::quorumBlockProcessor->GetMineableCommitment(llmq_params, tipHeight, fqc)) {
            UniValue obj(UniValue::VOBJ);
            fqc.ToJson(obj);
            mineableCommitments.pushKV(std::string(llmq_params.name), obj);
        }
    }

    ret.pushKV("mineableCommitments", mineableCommitments);
    ret.pushKV("quorumConnections", quorumConnections);

    return ret;
}

void quorum_memberof_help(const JSONRPCRequest &request) {
    RPCHelpMan{"quorum memberof",
               "Checks which quorums the given smartnode is a member of.\n",
               {
                       {"proTxHash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "ProTxHash of the smartnode."},
                       {"scanQuorumsCount", RPCArg::Type::NUM, /* default */ "",
                        "Number of quorums to scan for. If not specified,\n"
                        "the active quorum count for each specific quorum type is used."},
               },
               RPCResults{},
               RPCExamples{""},
    }.Check(request);
}

UniValue quorum_memberof(const JSONRPCRequest &request) {
    quorum_memberof_help(request);

    uint256 protxHash = ParseHashV(request.params[0], "proTxHash");
    int scanQuorumsCount = -1;
    if (!request.params[1].isNull()) {
        scanQuorumsCount = ParseInt32V(request.params[1], "scanQuorumsCount");
        if (scanQuorumsCount <= 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid scanQuorumsCount parameter");
        }
    }

    const CBlockIndex *pindexTip = WITH_LOCK(cs_main,
    return ::ChainActive().Tip());

    auto mnList = deterministicMNManager->GetListForBlock(pindexTip);
    auto dmn = mnList.GetMN(protxHash);
    if (!dmn) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "smartnode not found");
    }

    UniValue result(UniValue::VARR);

    for (const auto &type: llmq::CLLMQUtils::GetEnabledQuorumTypes(pindexTip)) {
        const auto &llmq_params = llmq::GetLLMQParams(type);
        size_t count = llmq_params.signingActiveQuorumCount;
        if (scanQuorumsCount != -1) {
            count = (size_t) scanQuorumsCount;
        }
        auto quorums = llmq::quorumManager->ScanQuorums(llmq_params.type, count);
        for (auto &quorum: quorums) {
            if (quorum->IsMember(dmn->proTxHash)) {
                auto json = BuildQuorumInfo(quorum, false, false);
                json.pushKV("isValidMember", quorum->IsValidMember(dmn->proTxHash));
                json.pushKV("memberIndex", quorum->GetMemberIndex(dmn->proTxHash));
                result.push_back(json);
            }
        }
    }

    return result;
}

void quorum_sign_help(const JSONRPCRequest &request) {
    RPCHelpMan{"quorum sign",
               "Threshold-sign a message\n",
               {
                       {"llmqType", RPCArg::Type::NUM, RPCArg::Optional::NO, "LLMQ type."},
                       {"id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Request id."},
                       {"msgHash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Message hash."},
                       {"quorumHash", RPCArg::Type::STR_HEX, /* default */ "", "The quorum identifier."},
                       {"submit", RPCArg::Type::BOOL, /* default */ "true",
                        "Submits the signature share to the network if this is true. "
                        "Returns an object containing the signature share if this is false."},
               },
               RPCResults{},
               RPCExamples{""},
    }.Check(request);
}

void quorum_verify_help(const JSONRPCRequest &request) {
    RPCHelpMan{"quorum verify",
               "Test if a quorum signature is valid for a request id and a message hash\n",
               {
                       {"llmqType", RPCArg::Type::NUM, RPCArg::Optional::NO, "LLMQ type."},
                       {"id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Request id."},
                       {"msgHash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Message hash."},
                       {"signature", RPCArg::Type::STR, RPCArg::Optional::NO, "Quorum signature to verify."},
                       {"quorumHash", RPCArg::Type::STR_HEX, /* default */ "",
                        "The quorum identifier.\n"
                        "Set to \"\" if you want to specify signHeight instead."},
                       {"signHeight", RPCArg::Type::NUM, /* default */ "",
                        "The height at which the message was signed.\n"
                        "Only works when quorumHash is \"\"."},
               },
               RPCResults{},
               RPCExamples{""},
    }.Check(request);
}

void quorum_hasrecsig_help(const JSONRPCRequest &request) {
    RPCHelpMan{"quorum hasrecsig",
               "Test if a valid recovered signature is present\n",
               {
                       {"llmqType", RPCArg::Type::NUM, RPCArg::Optional::NO, "LLMQ type."},
                       {"id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Request id."},
                       {"msgHash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Message hash."},
               },
               RPCResults{},
               RPCExamples{""},
    }.Check(request);
}

void quorum_getrecsig_help(const JSONRPCRequest &request) {
    RPCHelpMan{"quorum getrecsig",
               "Get a recovered signature\n",
               {
                       {"llmqType", RPCArg::Type::NUM, RPCArg::Optional::NO, "LLMQ type."},
                       {"id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Request id."},
                       {"msgHash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Message hash."},
               },
               RPCResults{},
               RPCExamples{""},
    }.Check(request);
}

void quorum_isconflicting_help(const JSONRPCRequest &request) {
    RPCHelpMan{"quorum isconflicting",
               "Test if a conflict exists\n",
               {
                       {"llmqType", RPCArg::Type::NUM, RPCArg::Optional::NO, "LLMQ type."},
                       {"id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Request id."},
                       {"msgHash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Message hash."},
               },
               RPCResults{},
               RPCExamples{""},
    }.Check(request);
}

UniValue quorum_sigs_cmd(const JSONRPCRequest &request) {
    auto cmd = request.strMethod;
    if (request.fHelp || (request.params.size() != 3)) {
        if (cmd == "quorumsign") {
            quorum_sign_help(request);
        } else if (cmd == "quorumverify") {
            quorum_verify_help(request);
        } else if (cmd == "quorumhasrecsig") {
            quorum_hasrecsig_help(request);
        } else if (cmd == "quorumgetrecsig") {
            quorum_getrecsig_help(request);
        } else if (cmd == "quorumisconflicting") {
            quorum_isconflicting_help(request);
        } else {
            // shouldn't happen as it's already handled by the caller
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid cmd");
        }
    }

    Consensus::LLMQType llmqType = (Consensus::LLMQType) ParseInt32V(request.params[0], "llmqType");
    if (!Params().GetConsensus().llmqs.count(llmqType)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid LLMQ type");
    }

    uint256 id = ParseHashV(request.params[1], "id");
    uint256 msgHash = ParseHashV(request.params[2], "msgHash");

    if (cmd == "quorumsign") {
        uint256 quorumHash;
        if (!request.params[3].isNull() && !request.params[3].get_str().empty()) {
            quorumHash = ParseHashV(request.params[3], "quorumHash");
        }
        bool fSubmit{true};
        if (!request.params[4].isNull()) {
            fSubmit = ParseBoolV(request.params[4], "submit");
        }
        if (fSubmit) {
            return llmq::quorumSigningManager->AsyncSignIfMember(llmqType, id, msgHash, quorumHash);
        } else {

            llmq::CQuorumCPtr pQuorum;

            if (quorumHash.IsNull()) {
                pQuorum = llmq::quorumSigningManager->SelectQuorumForSigning(llmqType, id);
            } else {
                pQuorum = llmq::quorumManager->GetQuorum(llmqType, quorumHash);
            }

            if (pQuorum == nullptr) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "quorum not found");
            }

            auto sigShare = llmq::quorumSigSharesManager->CreateSigShare(pQuorum, id, msgHash);

            if (!sigShare.has_value() || !sigShare->sigShare.Get().IsValid()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "failed to create sigShare");
            }

            UniValue obj(UniValue::VOBJ);
            obj.pushKV("llmqType", static_cast<uint8_t>(llmqType));
            obj.pushKV("quorumHash", sigShare->getQuorumHash().ToString());
            obj.pushKV("quorumMember", sigShare->getQuorumMember());
            obj.pushKV("id", id.ToString());
            obj.pushKV("msgHash", msgHash.ToString());
            obj.pushKV("signHash", sigShare->GetSignHash().ToString());
            obj.pushKV("signature", sigShare->sigShare.Get().ToString());

            return obj;
        }
    } else if (cmd == "quorumverify") {
        CBLSSignature sig;
        if (!sig.SetHexStr(request.params[3].get_str())) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid signature format");
        }

        if (request.params[4].isNull() || (request.params[4].get_str().empty() && !request.params[5].isNull())) {
            int signHeight{-1};
            if (!request.params[5].isNull()) {
                signHeight = ParseInt32V(request.params[5], "signHeight");
            }
            // First check against the current active set, if it fails check against the last active set
            int signOffset{llmq::GetLLMQParams(llmqType).dkgInterval};
            return llmq::quorumSigningManager->VerifyRecoveredSig(llmqType, signHeight, id, msgHash, sig, 0) ||
                   llmq::quorumSigningManager->VerifyRecoveredSig(llmqType, signHeight, id, msgHash, sig, signOffset);
        } else {
            uint256 quorumHash = ParseHashV(request.params[4], "quorumHash");
            llmq::CQuorumCPtr quorum = llmq::quorumManager->GetQuorum(llmqType, quorumHash);

            if (!quorum) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "quorum not found");
            }

            uint256 signHash = llmq::CLLMQUtils::BuildSignHash(llmqType, quorum->qc->quorumHash, id, msgHash);
            return sig.VerifyInsecure(quorum->qc->quorumPublicKey, signHash);
        }
    } else if (cmd == "quorumhasrecsig") {
        return llmq::quorumSigningManager->HasRecoveredSig(llmqType, id, msgHash);
    } else if (cmd == "quorumgetrecsig") {
        llmq::CRecoveredSig recSig;
        if (!llmq::quorumSigningManager->GetRecoveredSigForId(llmqType, id, recSig)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "recovered signature not found");
        }
        if (recSig.getMsgHash() != msgHash) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "recovered signature not found");
        }
        return recSig.ToJson();
    } else if (cmd == "quorumisconflicting") {
        return llmq::quorumSigningManager->IsConflicting(llmqType, id, msgHash);
    } else {
        // shouldn't happen as it's already handled by the caller
        throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid cmd");
    }
}

void quorum_selectquorum_help(const JSONRPCRequest &request) {
    RPCHelpMan{"quorum selectquorum",
               "Returns the quorum that would/should sign a request\n",
               {
                       {"llmqType", RPCArg::Type::NUM, RPCArg::Optional::NO, "LLMQ type."},
                       {"id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Request id."},
               },
               RPCResults{},
               RPCExamples{""},
    }.Check(request);
}

UniValue quorum_selectquorum(const JSONRPCRequest &request) {
    quorum_selectquorum_help(request);

    Consensus::LLMQType llmqType = (Consensus::LLMQType) ParseInt32V(request.params[0], "llmqType");
    if (!Params().GetConsensus().llmqs.count(llmqType)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid LLMQ type");
    }

    uint256 id = ParseHashV(request.params[1], "id");

    UniValue ret(UniValue::VOBJ);

    auto quorum = llmq::quorumSigningManager->SelectQuorumForSigning(llmqType, id);
    if (!quorum) {
        throw JSONRPCError(RPC_MISC_ERROR, "no quorums active");
    }
    ret.pushKV("quorumHash", quorum->qc->quorumHash.ToString());

    UniValue recoveryMembers(UniValue::VARR);
    for (size_t i = 0; i < size_t(quorum->params.recoveryMembers); i++) {
        auto dmn = llmq::quorumSigSharesManager->SelectMemberForRecovery(quorum, id, i);
        recoveryMembers.push_back(dmn->proTxHash.ToString());
    }
    ret.pushKV("recoveryMembers", recoveryMembers);

    return ret;
}

void quorum_dkgsimerror_help(const JSONRPCRequest &request) {
    RPCHelpMan{"quorum dkgsimerror",
               "This enables simulation of errors and malicious behaviour in the DKG. Do NOT use this on mainnet\n"
               "as you will get yourself very likely PoSe banned for this.\n",
               {
                       {"type", RPCArg::Type::STR, RPCArg::Optional::NO, "Error type."},
                       {"rate", RPCArg::Type::NUM, RPCArg::Optional::NO, "Rate at which to simulate this error type."},
               },
               RPCResults{},
               RPCExamples{""},
    }.Check(request);
}

UniValue quorum_dkgsimerror(const JSONRPCRequest &request) {
    quorum_dkgsimerror_help(request);

    std::string type = request.params[0].get_str();
    double rate = ParseDoubleV(request.params[1], "rate");

    if (rate < 0 || rate > 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid rate. Must be between 0 and 1");
    }

    llmq::SetSimulatedDKGErrorRate(type, rate);

    return UniValue();
}

void quorum_getdata_help(const JSONRPCRequest &request) {
    RPCHelpMan{"quorum getdata",
               "Send a QGETDATA message to the specified peer.\n",
               {
                       {"nodeId", RPCArg::Type::NUM, RPCArg::Optional::NO,
                        "The internal nodeId of the peer to request quorum data from."},
                       {"llmqType", RPCArg::Type::NUM, RPCArg::Optional::NO,
                        "The quorum type related to the quorum data being requested."},
                       {"quorumHash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                        "The quorum hash related to the quorum data being requested."},
                       {"dataMask", RPCArg::Type::NUM, RPCArg::Optional::NO,
                        "Specify what data to request.\n"
                        "Possible values: 1 - Request quorum verification vector\n"
                        "2 - Request encrypted contributions for member defined by \"proTxHash\". \"proTxHash\" must be specified if this option is used.\n"
                        "3 - Request both, 1 and 2"},
                       {"proTxHash", RPCArg::Type::STR_HEX, /* default */ "",
                        "The proTxHash the contributions will be requested for. Must be member of the specified LLMQ."},
               },
               RPCResults{},
               RPCExamples{""},
    }.Check(request);
}

UniValue quorum_getdata(const JSONRPCRequest &request) {
    quorum_getdata_help(request);

    NodeId nodeId = ParseInt64V(request.params[0], "nodeId");
    Consensus::LLMQType llmqType = static_cast<Consensus::LLMQType>(ParseInt32V(request.params[1], "llmqType"));
    uint256 quorumHash = ParseHashV(request.params[2], "quorumHash");
    uint16_t nDataMask = static_cast<uint16_t>(ParseInt32V(request.params[3], "dataMask"));
    uint256 proTxHash;

    // Check if request wants ENCRYPTED_CONTRIBUTIONS data
    if (nDataMask & llmq::CQuorumDataRequest::ENCRYPTED_CONTRIBUTIONS) {
        if (!request.params[4].isNull()) {
            proTxHash = ParseHashV(request.params[4], "proTxHash");
            if (proTxHash.IsNull()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "proTxHash invalid");
            }
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "proTxHash missing");
        }
    }

    const CBlockIndex *pQuorumBaseBlockIndex = WITH_LOCK(cs_main,
    return LookupBlockIndex(quorumHash));

    NodeContext &node = EnsureNodeContext(request.context);
    return node.connman->ForNode(nodeId, [&](CNode *pNode) {
        return llmq::quorumManager->RequestQuorumData(pNode, llmqType, pQuorumBaseBlockIndex, nDataMask, proTxHash);
    });
}


[[noreturn]] void quorum_help() {
    throw std::runtime_error(
            RPCHelpMan{"quorum",
                       "Set of commands for quorums/LLMQs.\n"
                       "To get help on individual commands, use \"help quorum command\".\n"
                       "\nAvailable commands:\n"
                       "  list              - List of on-chain quorums\n"
                       "  info              - Return information about a quorum\n"
                       "  dkgsimerror       - Simulates DKG errors and malicious behavior\n"
                       "  dkgstatus         - Return the status of the current DKG process\n"
                       "  memberof          - Checks which quorums the given smartnode is a member of\n"
                       "  sign              - Threshold-sign a message\n"
                       "  verify            - Test if a quorum signature is valid for a request id and a message hash\n"
                       "  hasrecsig         - Test if a valid recovered signature is present\n"
                       "  getrecsig         - Get a recovered signature\n"
                       "  isconflicting     - Test if a conflict exists\n"
                       "  selectquorum      - Return the quorum that would/should sign a request\n"
                       "  getdata           - Request quorum data from other smartnodes in the quorum\n",
                       {
                               {"command", RPCArg::Type::STR, RPCArg::Optional::NO, "command to execute"},
                       },
                       RPCResults{},
                       RPCExamples{""},
            }.ToString());
}

UniValue _quorum(const JSONRPCRequest &request) {
    const JSONRPCRequest new_request{request.strMethod == "quorum" ? request.squashed() : request};
    const std::string command{new_request.strMethod};

    if (command == "quorumlist") {
        return quorum_list(new_request);
    } else if (command == "quoruminfo") {
        return quorum_info(new_request);
    } else if (command == "quorumdkgstatus") {
        return quorum_dkgstatus(new_request);
    } else if (command == "quorummemberof") {
        return quorum_memberof(new_request);
    } else if (command == "quorumsign" || command == "quorumverify" || command == "quorumhasrecsig" ||
               command == "quorumgetrecsig" || command == "quorumisconflicting") {
        return quorum_sigs_cmd(new_request);
    } else if (command == "quorumselectquorum") {
        return quorum_selectquorum(new_request);
    } else if (command == "quorumdkgsimerror") {
        return quorum_dkgsimerror(new_request);
    } else if (command == "quorumgetdata") {
        return quorum_getdata(new_request);
    } else {
        quorum_help();
    }
}

void verifychainlock_help(const JSONRPCRequest &request) {
    RPCHelpMan{"verifychainlock",
               "Test if a quorum signature is valid for a ChainLock.\n",
               {
                       {"blockHash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The block hash of the ChainLock."},
                       {"signature", RPCArg::Type::STR, RPCArg::Optional::NO, "The signature of the ChainLock."},
                       {"blockHeight", RPCArg::Type::NUM, /* default */ "",
                        "The height of the ChainLock. There will be an internal lookup of \"blockHash\" if this is not provided."},
               },
               RPCResults{},
               RPCExamples{""},
    }.Check(request);
}

UniValue verifychainlock(const JSONRPCRequest &request) {
    verifychainlock_help(request);

    const uint256 nBlockHash = ParseHashV(request.params[0], "blockHash");

    CBLSSignature chainLockSig;
    if (!chainLockSig.SetHexStr(request.params[1].get_str())) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid signature format");
    }

    int nBlockHeight;
    if (request.params[2].isNull()) {
        const CBlockIndex *pIndex = WITH_LOCK(cs_main,
        return LookupBlockIndex(nBlockHash));
        if (pIndex == nullptr) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "blockHash not found");
        }
        nBlockHeight = pIndex->nHeight;
    } else {
        nBlockHeight = ParseInt32V(request.params[2], "blockHeight");
    }

    const auto llmqType = Params().GetConsensus().llmqTypeChainLocks;
    const uint256 nRequestId = ::SerializeHash(std::make_pair(llmq::CLSIG_REQUESTID_PREFIX, nBlockHeight));
    return llmq::CSigningManager::VerifyRecoveredSig(llmqType, nBlockHeight, nRequestId, nBlockHash, chainLockSig);
}

void verifyislock_help(const JSONRPCRequest &request) {
    RPCHelpMan{"verifyislock",
               "Test if a quorum signature is valid for an InstantSend Lock\n",
               {
                       {"id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Request id."},
                       {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id."},
                       {"signature", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "The InstantSend Lock signature to verify."},
                       {"maxHeight", RPCArg::Type::NUM, "", "The maximum height to search quorums from."},
               },
               RPCResults{},
               RPCExamples{""},
    }.Check(request);
}

UniValue verifyislock(const JSONRPCRequest &request) {
    verifyislock_help(request);

    uint256 id = ParseHashV(request.params[0], "id");
    uint256 txid = ParseHashV(request.params[1], "txid");

    CBLSSignature sig;
    if (!sig.SetHexStr(request.params[2].get_str())) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "invalid signature format");
    }

    if (g_txindex) {
        g_txindex->BlockUntilSyncedToCurrentChain();
    }

    CBlockIndex *pindexMined{nullptr};
    {
        LOCK(cs_main);
        uint256 hash_block;
        CTransactionRef tx = GetTransaction(/* block_index */ nullptr, /* mempool */ nullptr, txid,
                                                              Params().GetConsensus(), hash_block);
        if (tx && !hash_block.IsNull()) {
            pindexMined = LookupBlockIndex(hash_block);
        }
    }

    int maxHeight{-1};
    if (!request.params[3].isNull()) {
        maxHeight = ParseInt32V(request.params[3], "maxHeight");
    }

    int signHeight;
    if (pindexMined == nullptr || pindexMined->nHeight > maxHeight) {
        signHeight = maxHeight;
    } else { // pindexMined->nHeight <= maxHeight
        signHeight = pindexMined->nHeight;
    }

    auto llmqType = Params().GetConsensus().llmqTypeInstantSend;

    // First check against the current active set, if it fails check against the last active set
    int signOffset{llmq::GetLLMQParams(llmqType).dkgInterval};
    return llmq::quorumSigningManager->VerifyRecoveredSig(llmqType, signHeight, id, txid, sig, 0) ||
           llmq::quorumSigningManager->VerifyRecoveredSig(llmqType, signHeight, id, txid, sig, signOffset);
}

static const CRPCCommand commands[] =
        { //  category              name                      actor (function)
                //  --------------------- ------------------------  -----------------------
                {"evo", "quorum",          &_quorum,         {}},
                {"evo", "verifychainlock", &verifychainlock, {"blockHash", "signature", "blockHeight"}},
                {"evo", "verifyislock",    &verifyislock,    {"id",        "txid",      "signature", "maxHeight"}},
        };

void RegisterQuorumsRPCCommands(CRPCTable &tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
