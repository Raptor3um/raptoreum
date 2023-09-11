// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2020 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <clientversion.h>
#include <consensus/consensus.h>
#include <core_io.h>
#include <evo/mnauth.h>
#include <httpserver.h>
#include <init.h>
#include <interfaces/chain.h>
#include <key_io.h>
#include <net.h>
#include <netbase.h>
#include <node/context.h>
#include <rpc/blockchain.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <scheduler.h>
#include <script/descriptor.h>
#include <timedata.h>
#include <txmempool.h>
#include <util/check.h>
#include <util/ref.h>
#include <util/system.h>
#include <util/strencodings.h>
#include <util/validation.h>
#include <validation.h>
#include <warnings.h>

#include <smartnode/smartnode-sync.h>
#include <spork.h>

#include <stdint.h>
#include <tuple>

#ifdef HAVE_MALLOC_INFO
#include <malloc.h>
#endif

#include <boost/algorithm/string.hpp>

#include <univalue.h>
#include "assets/assets.h"
#include <assets/assetstype.h>

static UniValue mnsync(const JSONRPCRequest &request) {
    RPCHelpMan{"mnsync",
               "Returns the sync status, updates to the next step or resets it entirely.\n",
               {
                       {"mode", RPCArg::Type::STR, RPCArg::Optional::NO, "[status|next|reset]"},
               },
               RPCResults{},
               RPCExamples{""}
    }.Check(request);

    std::string strMode = request.params[0].get_str();

    if (strMode == "status") {
        UniValue objStatus(UniValue::VOBJ);
        objStatus.pushKV("AssetID", smartnodeSync.GetAssetID());
        objStatus.pushKV("AssetName", smartnodeSync.GetAssetName());
        objStatus.pushKV("AssetStartTime", smartnodeSync.GetAssetStartTime());
        objStatus.pushKV("Attempt", smartnodeSync.GetAttempt());
        objStatus.pushKV("IsBlockchainSynced", smartnodeSync.IsBlockchainSynced());
        objStatus.pushKV("IsSynced", smartnodeSync.IsSynced());
        return objStatus;
    }

    if (strMode == "next") {
        NodeContext &node = EnsureNodeContext(request.context);
        smartnodeSync.SwitchToNextAsset(*node.connman);
        return "sync updated to " + smartnodeSync.GetAssetName();
    }

    if (strMode == "reset") {
        smartnodeSync.Reset(true);
        return "success";
    }
    return "failure";
}

/*
    Used for updating/reading spork settings on the network
*/
static UniValue spork(const JSONRPCRequest &request) {
    if (request.params.size() == 1) {
        // basic mode, show info
        std::string strCommand = request.params[0].get_str();
        if (strCommand == "show") {
            UniValue ret(UniValue::VOBJ);
            for (const auto &sporkDef: sporkDefs) {
                ret.pushKV(std::string(sporkDef.name), sporkManager.GetSporkValue(sporkDef.sporkId));
            }
            return ret;
        } else if (strCommand == "active") {
            UniValue ret(UniValue::VOBJ);
            for (const auto &sporkDef: sporkDefs) {
                ret.pushKV(std::string(sporkDef.name), sporkManager.IsSporkActive(sporkDef.sporkId));
            }
            return ret;
        }
    }

    if (request.fHelp || request.params.size() != 2) {
        // default help, for basic mode
        RPCHelpMan{"spork",
                   "\nShows information about current state of sporks\n",
                   {
                           {"command", RPCArg::Type::STR, RPCArg::Optional::NO,
                            "'show' to show all current spork values, 'active' to show which sporks are active"},
                   },
                   {
                           RPCResult{"For 'show'",
                                     RPCResult::Type::OBJ_DYN, "",
                                     "keys are the sporks, and values indicates its value",
                                     {
                                             {RPCResult::Type::NUM, "SPORK_NAME",
                                              "The value of the specific spork with the name SPORK_NAME"},
                                     }},
                           RPCResult{"For 'active'",
                                     RPCResult::Type::OBJ_DYN, "",
                                     "keys are the sporks, and values indicates its status",
                                     {
                                             {RPCResult::Type::BOOL, "SPORK_NAME",
                                              "'true' for time-based sporks if spork is active and 'false' otherwise"},
                                     }},
                   },
                   RPCExamples{
                           HelpExampleCli("spork", "show")
                           + HelpExampleRpc("spork", "\"show\"")
                   }}.Check(request);
    } else {
        // advanced mode, update spork values
        SporkId nSporkID = CSporkManager::GetSporkIDByName(request.params[0].get_str());
        if (nSporkID == SPORK_INVALID)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid spork name");

        NodeContext &node = EnsureNodeContext(request.context);
        if (!node.connman)
            throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

        // SPORK VALUE
        int64_t nValue = request.params[1].get_int64();

        //broadcast new spork
        if (sporkManager.UpdateSpork(nSporkID, nValue, *node.connman)) {
            return "success";
        } else {
            RPCHelpMan{"spork",
                       "\nUpdate the value of the specific spork. Requires \"-sporkkey\" to be set to sign the message.\n",
                       {
                               {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "The name of the spork to update"},
                               {"value", RPCArg::Type::NUM, RPCArg::Optional::NO, "The new desired value of the spork"},
                       },
                       RPCResult{
                               RPCResult::Type::STR, "result",
                               "\"success\" if spork value was updated or this help otherwise"
                       },
                       RPCExamples{
                               HelpExampleCli("spork", "SPORK_2_INSTANTSEND_ENABLED 4070908800")
                               + HelpExampleRpc("spork", "\"SPORK_2_INSTANTSEND_ENABLED\", 4070908800")
                       },
            }.Check(request);
        }
    }
    return NullUniValue;
}

static UniValue validateaddress(const JSONRPCRequest &request) {
    RPCHelpMan{"validateaddress",
               "\nReturn information about the given raptoreum address.\n",
               {
                       {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The Raptoreum address to validate"},
               },
               RPCResult{
                       RPCResult::Type::OBJ, "", "",
                       {
                               {RPCResult::Type::BOOL, "isvalid",
                                "If the address is valid or not. If not, this is the only property returned."},
                               {RPCResult::Type::STR, "address", "The raptoreum address validated"},
                               {RPCResult::Type::STR_HEX, "scriptPubKey",
                                "The hex-encoded scriptPubKey generated by the address"},
                               {RPCResult::Type::BOOL, "isscript", "If the key is a script"},
                       }
               },
               RPCExamples{
                       HelpExampleCli("validateaddress", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\"")
                       + HelpExampleRpc("validateaddress", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\"")
               },
    }.Check(request);

    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    bool isValid = IsValidDestination(dest);

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("isvalid", isValid);
    if (isValid) {
        std::string currentAddress = EncodeDestination(dest);
        ret.pushKV("address", currentAddress);

        CScript scriptPubKey = GetScriptForDestination(dest);
        ret.pushKV("scriptPubKey", HexStr(scriptPubKey));;

        UniValue detail = DescribeAddress(dest);
        ret.pushKVs(detail);
    }
    return ret;
}

static UniValue createmultisig(const JSONRPCRequest &request) {
    RPCHelpMan{"createmultisig",
               "\nCreates a multi-signature address with n signature of m keys required.\n"
               "It returns a json object with the address and redeemScript.\n",
               {
                       {"nrequired", RPCArg::Type::NUM, RPCArg::Optional::NO,
                        "The number of required signatures out of the n keys."},
                       {"keys", RPCArg::Type::ARR, RPCArg::Optional::NO, "A json array of hex-encoded public keys.",
                        {
                                {"key", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "The hex-encoded public key"},
                        }},
               },
               RPCResult{
                       RPCResult::Type::OBJ, "", "",
                       {
                               {RPCResult::Type::STR, "address", "The value of the new multisig address."},
                               {RPCResult::Type::STR_HEX, "redeemScript",
                                "The string value of the hex-encoded redemption script."},
                       }
               },
               RPCExamples{
                       "\nCreate a multisig address from 2 public keys\n"
                       + HelpExampleCli("createmultisig",
                                        "2 \"[\\\"03789ed0bb717d88f7d321a368d905e7430207ebbd82bd342cf11ae157a7ace5fd\\\",\\\"03dbc6764b8884a92e871274b87583e6d5c2a58819473e17e107ef3f6aa5a61626\\\"]\"") +
                       "\nAs a json rpc call\n"
                       + HelpExampleRpc("createmultisig",
                                        "2, \"[\\\"03789ed0bb717d88f7d321a368d905e7430207ebbd82bd342cf11ae157a7ace5fd\\\",\\\"03dbc6764b8884a92e871274b87583e6d5c2a58819473e17e107ef3f6aa5a61626\\\"]\"")
               },
    }.Check(request);

    int required = request.params[0].get_int();

    // Get the public keys
    const UniValue &keys = request.params[1].get_array();
    std::vector <CPubKey> pubkeys;
    for (unsigned int i = 0; i < keys.size(); ++i) {
        if (IsHex(keys[i].get_str()) && (keys[i].get_str().length() == 66 || keys[i].get_str().length() == 130)) {
            pubkeys.push_back(HexToPubKey(keys[i].get_str()));
        } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Invalid public key: %s\n.", keys[i].get_str()));
        }
    }

    // Construct using pay-to-script-hash:
    CScript inner = CreateMultisigRedeemscript(required, pubkeys);
    CScriptID innerID(inner);

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", EncodeDestination(innerID));
    result.pushKV("redeemScript", HexStr(inner));

    return result;
}

static UniValue getdescriptorinfo(const JSONRPCRequest &request) {
    RPCHelpMan{"getdescriptorinfo",
               {"\nAnalyses a descriptor.\n"},
               {
                       {"descriptor", RPCArg::Type::STR, RPCArg::Optional::NO, "The descriptor"},
               },
               RPCResult{
                       RPCResult::Type::OBJ, "", "",
                       {
                               {RPCResult::Type::STR, "descriptor",
                                "The descriptor in canonical form, without private keys"},
                               {RPCResult::Type::STR, "checksum", "The checksum for the input descriptor"},
                               {RPCResult::Type::BOOL, "isrange", "Whether the descriptor is ranged"},
                               {RPCResult::Type::BOOL, "issolvable", "Whether the descriptor is solvable"},
                               {RPCResult::Type::BOOL, "hasprivatekeys",
                                "Whether the input descriptor contained at least one private key"},
                       }
               },
               RPCExamples{
                       "\nAnalyse a descriptor\n"
                       + HelpExampleCli("getdescriptorinfo",
                                        "\"pkh([d34db33f/84h/0h/0h]0279be667ef9dcbbac55a06295Ce870b07029Bfcdb2dce28d959f2815b16f81798)\"")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR});

    FlatSigningProvider provider;
    std::string error;
    auto desc = Parse(request.params[0].get_str(), provider, error);
    if (!desc) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, error);
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("descriptor", desc->ToString());
    result.pushKV("checksum", GetDescriptorChecksum(request.params[0].get_str()));
    result.pushKV("isrange", desc->IsRange());
    result.pushKV("issolvable", desc->IsSolvable());
    result.pushKV("hasprivatekeys", provider.keys.size() > 0);
    return result;
}

static UniValue deriveaddresses(const JSONRPCRequest &request) {
    RPCHelpMan{"deriveaddresses",
               "\nDerives one or more addresses corresponding to an output descriptor.\n"
               "Examples of output descriptors are:\n"
               "    pkh(<pubkey>)                        P2PKH outputs for the given pubkey\n"
               "    sh(multi(<n>,<pubkey>,<pubkey>,...)) P2SH-multisig outputs for the given threshold and pubkeys\n"
               "    raw(<hex script>)                    Outputs whose scriptPubKey equals the specified hex scripts\n"
               "\nIn the above, <pubkey> either refers to a fixed public key in hexadecimal notation, or to an xpub/xprv optionally followed by one\n"
               "or more path elements separated by \"/\", where \"h\" represents a hardened child key.\n"
               "For more information on output descriptors, see the documentation in the doc/descriptors.md file.\n",
               {
                       {"descriptor", RPCArg::Type::STR, RPCArg::Optional::NO, "The descriptor"},
                       {"range", RPCArg::Type::RANGE, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "If a ranged descriptor is used, this specifies the beginning of the range (in [begin,end] notation) to derive."},
               },
               RPCResult{
                       RPCResult::Type::ARR, "", "",
                       {
                               {RPCResult::Type::STR, "address", "the derived addresses"},
                       }
               },
               RPCExamples{
                       "\nFirst three receive addresses\n"
                       + HelpExampleCli("deriveaddresses",
                                        "\"pkh([d34db33f/84h/0h/0h]xpub6DJ2dNUysrn5Vt36jH2KLBT2i1auw1tTSSomg8PhqNiUtx8QX2SvC9nrHu81fT41fvDUnhMjEzQgXnQjKEu3oaqMSzhSrHMxyyoEAmUHQbY/0/*)#cjjspncu\" \"[0,2]\"")
               }
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VNUM, UniValue::VNUM});
    const std::string desc_str = request.params[0].get_str();

    int64_t range_begin = 0;
    int64_t range_end = 0;

    if (request.params.size() >= 2 && !request.params[1].isNull()) {
        std::tie(range_begin, range_end) = ParseDescriptorRange(request.params[1]);
    }

    FlatSigningProvider key_provider;
    std::string error;
    auto desc = Parse(desc_str, key_provider, error, /* require_checksum = */ true);
    if (!desc) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, error);
    }

    if (!desc->IsRange() && request.params.size() > 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Range should not be specified for an un-ranged descriptor");
    }

    if (desc->IsRange() && request.params.size() == 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Range must be specified for a ranged descriptor");
    }

    UniValue addresses(UniValue::VARR);

    for (int i = range_begin; i <= range_end; ++i) {
        FlatSigningProvider provider;
        std::vector <CScript> scripts;
        if (!desc->Expand(i, key_provider, scripts, provider)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Cannot derive script without private keys"));
        }

        for (const CScript &script: scripts) {
            CTxDestination dest;
            if (!ExtractDestination(script, dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                                   strprintf("Descriptor does not have a corresponding address"));
            }

            addresses.push_back(EncodeDestination(dest));
        }
    }

    // This should not be possible, but an assert seems overkill:
    if (addresses.empty()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Unexpected empty result");
    }

    return addresses;
}

static UniValue verifymessage(const JSONRPCRequest &request) {
    RPCHelpMan{"verifymessage",
               "\nVerify a signed message\n",
               {
                       {"address", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "The Raptoreum address to use for the signature."},
                       {"signature", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "The signature provided by the signer in base 64 encoding (see signmessage)."},
                       {"message", RPCArg::Type::STR, RPCArg::Optional::NO, "The message that was signed."},
               },
               RPCResult{
                       RPCResult::Type::BOOL, "", "If the signature is verified or not."
               },
               RPCExamples{
                       "\nUnlock the wallet for 30 seconds\n"
                       + HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
                       "\nCreate the signature\n"
                       + HelpExampleCli("signmessage", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\" \"my message\"") +
                       "\nVerify the signature\n"
                       + HelpExampleCli("verifymessage",
                                        "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\" \"signature\" \"my message\"") +
                       "\nAs json rpc\n"
                       + HelpExampleRpc("verifymessage",
                                        "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\", \"signature\", \"my message\"")
               },
    }.Check(request);

    LOCK(cs_main);

    std::string strAddress = request.params[0].get_str();
    std::string strSign = request.params[1].get_str();
    std::string strMessage = request.params[2].get_str();

    CTxDestination destination = DecodeDestination(strAddress);
    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");
    }

    const CKeyID *keyID = boost::get<CKeyID>(&destination);
    if (!keyID) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");
    }

    bool fInvalid = false;
    std::vector<unsigned char> vchSig = DecodeBase64(strSign.c_str(), &fInvalid);

    if (fInvalid)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Malformed base64 encoding");

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig))
        return false;

    return (pubkey.GetID() == *keyID);
}

UniValue signmessagewithprivkey(const JSONRPCRequest &request) {
    RPCHelpMan{"signmessagewithprivkey",
               "\nSign a message with the private key of an address\n",
               {
                       {"privkey", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "The private key to sign the message with."},
                       {"message", RPCArg::Type::STR, RPCArg::Optional::NO, "The message to create a signature of."},
               },
               RPCResult{
                       RPCResult::Type::STR, "", "The signature of the message encoded in base 64"
               },
               RPCExamples{
                       "\nCreate the signature\n"
                       + HelpExampleCli("signmessagewithprivkey", "\"privkey\" \"my message\"") +
                       "\nVerify the signature\n"
                       + HelpExampleCli("verifymessage",
                                        "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwG\" \"signature\" \"my message\"") +
                       "\nAs json rpc\n"
                       + HelpExampleRpc("signmessagewithprivkey", "\"privkey\", \"my message\"")
               },
    }.Check(request);

    std::string strPrivkey = request.params[0].get_str();
    std::string strMessage = request.params[1].get_str();

    CKey key = DecodeSecret(strPrivkey);
    if (!key.IsValid()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
    }

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    std::vector<unsigned char> vchSig;
    if (!key.SignCompact(ss.GetHash(), vchSig))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sign failed");

    return EncodeBase64(vchSig);
}

static UniValue setmocktime(const JSONRPCRequest &request) {
    RPCHelpMan{"setmocktime",
               "\nSet the local time to given timestamp (-regtest only)\n",
               {
                       {"timestamp", RPCArg::Type::NUM, RPCArg::Optional::NO, UNIX_EPOCH_TIME + "\n"
                                                                                                "Pass 0 to go back to using the system time."},
               },
               RPCResult{RPCResult::Type::NONE, "", ""},
               RPCExamples{""},
    }.Check(request);

    if (!Params().MineBlocksOnDemand())
        throw std::runtime_error("setmocktime for regression testing (-regtest mode) only");

    // For now, don't change mocktime if we're in the middle of validation, as
    // this could have an effect on mempool time-based eviction, as well as
    // IsCurrentForFeeEstimation() and IsInitialBlockDownload().
    // TODO: figure out the right way to synchronize around mocktime, and
    // ensure all call sites of GetTime() are accessing this safely.
    LOCK(cs_main);

    RPCTypeCheck(request.params, {UniValue::VNUM});
    int64_t time = request.params[0].get_int64();
    SetMockTime(time);
    if (request.context.Has<NodeContext>()) {
        for (const auto &chain_client: request.context.Get<NodeContext>().chain_clients) {
            chain_client->setMockTime(time);
        }
    }

    return NullUniValue;
}

static UniValue mnauth(const JSONRPCRequest &request) {
    RPCHelpMan{"mnauth",
               "\nOverride MNAUTH processing results for the specified node with a user provided data (-regtest only).\n",
               {
                       {"nodeId", RPCArg::Type::NUM, RPCArg::Optional::NO,
                        "Internal peer id of the node the mock data gets added to."},
                       {"proTxHash", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "The authenticated proTxHash as hex string."},
                       {"publicKey", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "The authenticated public key as hex string."},
               },
               RPCResult{
                       RPCResult::Type::BOOL, "result", "true, if the node was updated"},
               RPCExamples{""},
    }.Check(request);

    if (!Params().MineBlocksOnDemand())
        throw std::runtime_error("mnauth for regression testing (-regtest mode) only");

    int nodeId = ParseInt64V(request.params[0], "nodeId");
    uint256 proTxHash = ParseHashV(request.params[1], "proTxHash");
    if (proTxHash.IsNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "proTxHash invalid");
    }
    CBLSPublicKey publicKey;
    publicKey.SetHexStr(request.params[2].get_str());
    if (!publicKey.IsValid()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "publicKey invalid");
    }

    NodeContext &node = EnsureNodeContext(request.context);
    bool fSuccess = node.connman->ForNode(nodeId, CConnman::AllNodes, [&](CNode *pNode) {
        pNode->SetVerifiedProRegTxHash(proTxHash);
        pNode->SetVerifiedPubKeyHash(publicKey.GetHash());
        return true;
    });

    return fSuccess;
}

bool getAddressFromIndex(const int &type, const uint160 &hash, std::string &address) {
    if (type == 2) {
        address = EncodeDestination(CScriptID(hash));
    } else if (type == 1) {
        address = EncodeDestination(CKeyID(hash));
    } else {
        return false;
    }
    return true;
}

bool getIndexKey(const std::string &str, uint160 &hashBytes, int &type) {
    CTxDestination dest = DecodeDestination(str);
    if (!IsValidDestination(dest)) {
        type = 0;
        return false;
    }
    const CKeyID *keyID = boost::get<CKeyID>(&dest);
    const CScriptID *scriptID = boost::get<CScriptID>(&dest);
    type = keyID ? 1 : 2;
    hashBytes = keyID ? *keyID : *scriptID;
    return true;
}

bool getAddressesFromParams(const UniValue &params, std::vector <std::pair<uint160, int>> &addresses) {
    if (params[0].isStr()) {
        uint160 hashBytes;
        int type = 0;
        if (!getIndexKey(params[0].get_str(), hashBytes, type)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
        }
        addresses.push_back(std::make_pair(hashBytes, type));
    } else if (params[0].isObject()) {

        UniValue addressValues = find_value(params[0].get_obj(), "addresses");
        if (!addressValues.isArray()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Addresses is expected to be an array");
        }

        std::vector <UniValue> values = addressValues.getValues();

        for (std::vector<UniValue>::iterator it = values.begin(); it != values.end(); ++it) {

            uint160 hashBytes;
            int type = 0;
            if (!getIndexKey(it->get_str(), hashBytes, type)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
            }
            addresses.push_back(std::make_pair(hashBytes, type));
        }
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    return true;
}

bool heightSort(std::pair <CAddressUnspentKey, CAddressUnspentValue> a,
                std::pair <CAddressUnspentKey, CAddressUnspentValue> b) {
    return a.second.blockHeight < b.second.blockHeight;
}

bool timestampSort(std::pair <CMempoolAddressDeltaKey, CMempoolAddressDelta> a,
                   std::pair <CMempoolAddressDeltaKey, CMempoolAddressDelta> b) {
    return a.second.time < b.second.time;
}

UniValue getaddressmempool(const JSONRPCRequest &request) {
    RPCHelpMan{"getaddressmempool",
               "\nReturns all mempool deltas for an address (requires addressindex to be enabled).\n",
               {
                       {"addresses", RPCArg::Type::ARR, /* default */ "", "",
                        {
                                {"address", RPCArg::Type::STR, /* default */ "", "The base58check encoded address"},
                        },
                       },
               },
               RPCResult{
                       RPCResult::Type::ARR, "", "",
                       {
                               {RPCResult::Type::OBJ, "", "",
                                {
                                        {RPCResult::Type::STR, "address", "The base58check encoded address"},
                                        {RPCResult::Type::STR, "asset", "The asset name of the associated asset"},
                                        {RPCResult::Type::STR, "assetId", "(only on assets) The asset Id of the associated asset"},
                                        {RPCResult::Type::STR_HEX, "txid", "The related txid"},
                                        {RPCResult::Type::NUM, "index", "The related input or output index"},
                                        {RPCResult::Type::NUM, "satoshis", "The difference of duffs"},
                                        {RPCResult::Type::NUM_TIME, "timestamp",
                                         "The time the transaction entered the mempool (seconds)"},
                                        {RPCResult::Type::STR_HEX, "prevtxid", "The previous txid (if spending)"},
                                        {RPCResult::Type::NUM, "prevout",
                                         "The previous transaction output index (if spending)"},
                                }},
                       }},
               RPCExamples{
                       HelpExampleCli("getaddressmempool",
                                      "'{\"addresses\": [\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\"]}'")
                       +
                       HelpExampleRpc("getaddressmempool", "{\"addresses\": [\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\"]}")
               },
    }.Check(request);

    std::vector <std::pair<uint160, int>> addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    std::vector <std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>> indexes;

    if (!mempool.getAddressIndex(addresses, indexes)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
    }

    std::sort(indexes.begin(), indexes.end(), timestampSort);

    UniValue result(UniValue::VARR);

    for (std::vector < std::pair < CMempoolAddressDeltaKey, CMempoolAddressDelta > > ::iterator it = indexes.begin();
            it != indexes.end();
    it++) {

        std::string address;
        if (!getAddressFromIndex(it->first.type, it->first.addressBytes, address)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
        }

        UniValue delta(UniValue::VOBJ);
        delta.pushKV("address", address);
        if (it->first.asset != "RTM"){
            CAssetMetaData tmpAsset;
            if (!passetsCache->GetAssetMetaData(it->first.asset, tmpAsset)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: Asset asset metadata not found");
            }
            delta.pushKV("asset", tmpAsset.name);
            delta.pushKV("assetId", it->first.asset);
        } else {
            delta.pushKV("asset", it->first.asset);
        }
        delta.pushKV("txid", it->first.txhash.GetHex());
        delta.pushKV("index", (int) it->first.index);
        delta.pushKV("satoshis", it->second.amount);
        delta.pushKV("timestamp", it->second.time);
        if (it->second.amount < 0) {
            delta.pushKV("prevtxid", it->second.prevhash.GetHex());
            delta.pushKV("prevout", (int) it->second.prevout);
        }
        result.push_back(delta);
    }

    return result;
}

UniValue getaddressutxos(const JSONRPCRequest &request) {
    RPCHelpMan{
            "getaddressutxos",
            "\nReturns all unspent outputs for an address (requires addressindex to be enabled).\n",
            {
                {"addresses", RPCArg::Type::ARR, /* default */ "", "",
                    {
                            {"address", RPCArg::Type::STR, /* default */ "", "The base58check encoded address"},
                    },
                },
                {"asset", RPCArg::Type::STR, /* default */ "RTM", "Get UTXOs for a particular asset instead of RTM ('*' for all assets).",}
            },
            {
                RPCResult{"For RTM",
                    RPCResult::Type::ARR, "", "",
                    {
                        {RPCResult::Type::OBJ, "", "",
                            {
                                {RPCResult::Type::STR, "address", "The address base58check encoded"},
                                {RPCResult::Type::STR_HEX, "txid", "The output txid"},
                                {RPCResult::Type::NUM, "index", "The output index"},
                                {RPCResult::Type::STR_HEX, "script", "The script hex-encoded"},
                                {RPCResult::Type::NUM, "satoshis", "The number of duffs of the output"},
                                {RPCResult::Type::NUM, "height", "The block height"},
                            },
                        },
                    },
                },
                RPCResult{"For Assets",
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::ARR, "Asset name", "",
                            {{RPCResult::Type::OBJ, "", "",
                                {
                                    {RPCResult::Type::STR, "address", "The address base58check encoded"},
                                    {RPCResult::Type::STR, "assetId", "The asset id associated with the UTXOs"},
                                    {RPCResult::Type::STR, "uniqueId", "(if unique) The unique id associated with the UTXOs"},
                                    {RPCResult::Type::STR_HEX, "txid", "The output txid"},
                                    {RPCResult::Type::NUM, "index", "The output index"},
                                    {RPCResult::Type::STR_HEX, "script", "The script hex-encoded"},
                                    {RPCResult::Type::NUM, "satoshis", "The number of duffs of the output"},
                                    {RPCResult::Type::NUM, "height", "The block height"},
                                },
                            }},
                        },
                    },
                },
            },
            RPCExamples{
                    HelpExampleCli("getaddressutxos", "'{\"addresses\": [\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\"]}'")
                    + HelpExampleRpc("getaddressutxos", "{\"addresses\": [\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\"]}")
            },
    }.Check(request);

    std::vector <std::pair<uint160, int>> addresses;
    std::string assetId = "RTM";

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    bool excludeUnspendable = false;
    if (request.params[0].isObject()) {
        UniValue excludeUnspendableValue = find_value(request.params[0].get_obj(), "excludeUnspendable");
        if (!excludeUnspendableValue.isNull()) {
            if (excludeUnspendableValue.isStr()) {
                excludeUnspendable = excludeUnspendableValue.get_str().compare("true") == 0;
            } else if (excludeUnspendableValue.isBool()) {
                excludeUnspendable = excludeUnspendableValue.get_bool();
            }
        }

        UniValue assetParam = find_value(request.params[0].get_obj(), "asset");
        if (assetParam.isStr()) {
            if (!Params().IsAssetsActive(::ChainActive().Tip()))
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Assets aren't active. assetId can't be specified.");
            assetId = assetParam.get_str();
            if (assetId != "*" && !passetsCache->GetAssetId(assetId, assetId))
                throw JSONRPCError(RPC_INVALID_PARAMETER, "ERROR: Asset not found");
        }
    }

    std::vector <std::pair<CAddressUnspentKey, CAddressUnspentValue>> unspentOutputs;

    for (std::vector < std::pair < uint160, int > > ::iterator it = addresses.begin(); it != addresses.end();
    it++) {
        if (!GetAddressUnspent((*it).first, (*it).second, unspentOutputs)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
        }
    }

    std::sort(unspentOutputs.begin(), unspentOutputs.end(), heightSort);

    
    if (assetId == "RTM"){
        UniValue result(UniValue::VARR);
        for (std::vector < std::pair < CAddressUnspentKey, CAddressUnspentValue > >
                                                        ::const_iterator it = unspentOutputs.begin(); it !=
                                                                                                        unspentOutputs.end();
        it++) {
            if (assetId != it->first.asset)
                continue;

            UniValue output(UniValue::VOBJ);
            std::string address;
            if (!getAddressFromIndex(it->first.type, it->first.hashBytes, address)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
            }
            if (excludeUnspendable) {
                int currentHeight = ::ChainActive().Tip() == nullptr ? 0 : ::ChainActive().Tip()->nHeight;
                bool isFSpendable = (it->second.fSpendableHeight >= 0 && it->second.fSpendableHeight <= currentHeight) ||
                                    (it->second.fSpendableTime >= 0 && it->second.fSpendableTime <= GetAdjustedTime());
                if (!isFSpendable) {
                    continue;
                }
            }
            output.pushKV("address", address);
            output.pushKV("txid", it->first.txhash.GetHex());
            output.pushKV("outputIndex", (int) it->first.index);
            output.pushKV("script", HexStr(it->second.script));
            output.pushKV("satoshis", it->second.satoshis);
            output.pushKV("height", it->second.blockHeight);
            output.pushKV("spendableHeight", it->second.fSpendableHeight);
            output.pushKV("spendableTime", it->second.fSpendableTime);
            result.push_back(output);
        }
        return result;
    } else {
        UniValue result(UniValue::VOBJ);
        std::map<std::string, std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>> mapassetUTXO;
        for (std::vector < std::pair < CAddressUnspentKey, CAddressUnspentValue > >
                                                        ::const_iterator it = unspentOutputs.begin(); it !=
                                                                                                        unspentOutputs.end();
        it++) {
            if (it->first.asset == "RTM")
                continue;

            if (assetId != it->first.asset && assetId != "*")
                continue;

            if (excludeUnspendable) {
                int currentHeight = ::ChainActive().Tip() == nullptr ? 0 : ::ChainActive().Tip()->nHeight;
                bool isFSpendable = (it->second.fSpendableHeight >= 0 && it->second.fSpendableHeight <= currentHeight) ||
                                    (it->second.fSpendableTime >= 0 && it->second.fSpendableTime <= GetAdjustedTime());
                if (!isFSpendable) {
                    continue;
                }
            }
            if (!mapassetUTXO.count(it->first.asset)){
                std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> tmp;
                mapassetUTXO.insert(std::make_pair(it->first.asset, tmp));
            }
            mapassetUTXO[it->first.asset].push_back(*it);
        }
        for (auto asset : mapassetUTXO){
            UniValue assetutxo(UniValue::VARR);
            for (auto it : asset.second){
                UniValue output(UniValue::VOBJ);
                std::string address;
                if (!getAddressFromIndex(it.first.type, it.first.hashBytes, address)) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
                }
                output.pushKV("address", address);
                output.pushKV("assetId", it.first.asset);
                CAssetTransfer assetTransfer;
                if (it.second.isUnique)
                    output.pushKV("uniqueId", it.second.uniqueId);
                output.pushKV("txid", it.first.txhash.GetHex());
                output.pushKV("outputIndex", (int) it.first.index);
                output.pushKV("script", HexStr(it.second.script));
                output.pushKV("satoshis", it.second.satoshis);
                output.pushKV("height", it.second.blockHeight);
                output.pushKV("spendableHeight", it.second.fSpendableHeight);
                output.pushKV("spendableTime", it.second.fSpendableTime);
                assetutxo.push_back(output);
            }

            CAssetMetaData tmpAsset;
            if (!passetsCache->GetAssetMetaData(asset.first, tmpAsset)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: Asset asset metadata not found");
            }
            result.pushKV(tmpAsset.name, assetutxo);      
        }
        return result;
    }
}

UniValue getaddressdeltas(const JSONRPCRequest &request) {
    RPCHelpMan{"getaddressdeltas",
               "\nReturns all changes for an address (requires addressindex to be enabled).\n",
               {
                    {"addresses", RPCArg::Type::ARR, /* default */ "", "",
                        {
                                {"address", RPCArg::Type::STR, /* default */ "", "The base58check encoded address"},
                        },
                    },
                    {"asset", RPCArg::Type::STR, /* default */ "RTM", "Get all changes for a particular asset instead of RTM.",}

               },
               RPCResult{
                       RPCResult::Type::ARR, "", "",
                       {
                               {RPCResult::Type::OBJ, "", "",
                                {
                                        {RPCResult::Type::NUM, "satoshis", "The difference of duffs"},
                                        {RPCResult::Type::STR, "assetId", "The asset id"},
                                        {RPCResult::Type::STR_HEX, "txid", "The related txid"},
                                        {RPCResult::Type::NUM, "index", "The related input or output index"},
                                        {RPCResult::Type::NUM, "blockindex", "The related block index"},
                                        {RPCResult::Type::NUM, "height", "The block height"},
                                        {RPCResult::Type::STR, "address", "The base58check encoded address"},
                                }},
                       }},
               RPCExamples{
                       HelpExampleCli("getaddressdeltas", "'{\"addresses\": [\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\"]}'")
                       + HelpExampleRpc("getaddressdeltas", "{\"addresses\": [\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\"]}")
               },
    }.Check(request);

    UniValue startValue = find_value(request.params[0].get_obj(), "start");
    UniValue endValue = find_value(request.params[0].get_obj(), "end");

    std::string assetId = "RTM";

    if (request.params[0].isObject()) {
        UniValue assetParam = find_value(request.params[0].get_obj(), "asset");
        if (assetParam.isStr()) {
            if (!Params().IsAssetsActive(::ChainActive().Tip()))
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Assets aren't active. assetId can't be specified.");
            assetId = assetParam.get_str();
            if (assetId != "*" && !passetsCache->GetAssetId(assetId, assetId))
                throw JSONRPCError(RPC_INVALID_PARAMETER, "ERROR: Asset not found");
        }
    }

    int start = 0;
    int end = 0;

    if (startValue.isNum() && endValue.isNum()) {
        start = startValue.get_int();
        end = endValue.get_int();
        if (end < start) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "End value is expected to be greater than start");
        }
    }

    std::vector <std::pair<uint160, int>> addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    std::vector <std::pair<CAddressIndexKey, CAmount>> addressIndex;

    for (std::vector < std::pair < uint160, int > > ::iterator it = addresses.begin(); it != addresses.end();
    it++) {
        if (start > 0 && end > 0) {
            if (!GetAddressIndex((*it).first, (*it).second, addressIndex, start, end)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
            }
        } else {
            if (!GetAddressIndex((*it).first, (*it).second, addressIndex)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
            }
        }
    }

    UniValue result(UniValue::VARR);

    for (std::vector < std::pair < CAddressIndexKey, CAmount > > ::const_iterator it = addressIndex.begin(); it !=
                                                                                                             addressIndex.end();
    it++) {
        if (it->first.asset != assetId && assetId != "*")
            continue;

        std::string address;
        if (!getAddressFromIndex(it->first.type, it->first.hashBytes, address)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
        }

        UniValue delta(UniValue::VOBJ);
        delta.pushKV("satoshis", it->second);
        if (it->first.asset != "RTM"){
            CAssetMetaData tmpAsset;
            if (!passetsCache->GetAssetMetaData(it->first.asset, tmpAsset)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: Asset asset metadata not found");
            }
            delta.pushKV("asset", tmpAsset.name);
            delta.pushKV("assetId", it->first.asset);
        }
        delta.pushKV("txid", it->first.txhash.GetHex());
        delta.pushKV("index", (int) it->first.index);
        delta.pushKV("blockindex", (int) it->first.txindex);
        delta.pushKV("height", it->first.blockHeight);
        delta.pushKV("address", address);
        result.push_back(delta);
    }

    return result;
}

static UniValue getaddressbalance(const JSONRPCRequest &request) {
    RPCHelpMan{"getaddressbalance",
               "\nReturns the balance for an address(es) (requires addressindex to be enabled).\n",
        {
            {"addresses", RPCArg::Type::ARR, /* default */ "", "",
                {
                    {"address", RPCArg::Type::STR, /* default */ "", "The base58check encoded address"},
                },
            },
            {"asset", RPCArg::Type::STR, /* default */ "RTM", "Get balance for a particular asset instead of RTM. (\"*\" for all assets)"},
        },
        {
            RPCResult{"For RTM",
                RPCResult::Type::OBJ, "", "",
                {
                    {RPCResult::Type::NUM, "balance", "The current total balance in duffs"},
                    {RPCResult::Type::NUM, "balance_immature", "The current immature balance in duffs"},
                    {RPCResult::Type::NUM, "balance_spendable", "The current spendable balance in duffs"},
                    {RPCResult::Type::NUM, "received", "The total number of duffs received (including change)"},
                }
            },
            RPCResult{"For assets",
                RPCResult::Type::OBJ, " ", "",
                {
                    {RPCResult::Type::OBJ, "asset name", "",
                    {
                        {RPCResult::Type::NUM, "balance", "The current total balance in duffs"},
                        {RPCResult::Type::NUM, "balance_immature", "The current immature balance in duffs"},
                        {RPCResult::Type::NUM, "balance_spendable", "The current spendable balance in duffs"},
                        {RPCResult::Type::NUM, "received", "The total number of duffs received (including change)"},
                    }},
                }
            },
        },
        RPCExamples{
                HelpExampleCli("getaddressbalance",
                                "'{\"addresses\": [\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\"]}'")
                +
                HelpExampleRpc("getaddressbalance", "{\"addresses\": [\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\"]}")
        },
    }.Check(request);

    std::vector <std::pair<uint160, int>> addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    std::string assetId = "RTM";

    if (request.params[0].isObject()) {
        UniValue assetParam = find_value(request.params[0].get_obj(), "asset");
        if (assetParam.isStr()) {
            if (!Params().IsAssetsActive(::ChainActive().Tip()))
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Assets aren't active. assetId can't be specified.");
            assetId = assetParam.get_str();
            if (assetId != "*" && !passetsCache->GetAssetId(assetId, assetId))
                throw JSONRPCError(RPC_INVALID_PARAMETER, "ERROR: Asset not found");
        }
    }

    std::vector <std::pair<CAddressIndexKey, CAmount>> addressIndex;

    for (std::vector < std::pair < uint160, int > > ::iterator it = addresses.begin(); it != addresses.end();
    it++) {
        if (!GetAddressIndex((*it).first, (*it).second, addressIndex)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
        }
    }

    int nHeight = WITH_LOCK(cs_main, return ::ChainActive().Height());
    if (assetId == "RTM") {
        CAmount balance = 0;
        CAmount balance_spendable = 0;
        CAmount balance_immature = 0;
        CAmount received = 0;

        for (std::vector < std::pair < CAddressIndexKey, CAmount > > ::const_iterator it = addressIndex.begin(); it !=
                                                                                                                addressIndex.end();
        it++) {
            if (it->first.asset != "RTM")
                continue;
            if (it->second > 0) {
                received += it->second;
            }
            if (it->first.txindex == 0 && nHeight - it->first.blockHeight < COINBASE_MATURITY) {
                balance_immature += it->second;
            } else {
                balance_spendable += it->second;
            }
            balance += it->second;
        }

        UniValue result(UniValue::VOBJ);
        result.pushKV("balance", balance);
        result.pushKV("balance_immature", balance_immature);
        result.pushKV("balance_spendable", balance_spendable);
        result.pushKV("received", received);

        return result;
    } else {
        struct balance {
            CAmount balance;
            CAmount balance_spendable;
            CAmount balance_immature;
            CAmount received;
        };

        std::map<std::string, balance> mapbalance;
        for (std::vector < std::pair < CAddressIndexKey, CAmount > > ::const_iterator it = addressIndex.begin(); 
                                                                            it != addressIndex.end(); it++) {
            if (it->first.asset != assetId && assetId != "*")
                continue;

            if (mapbalance.find(it->first.asset) == mapbalance.end()) {
                balance tmp;
                mapbalance.insert(std::make_pair(it->first.asset, tmp));
            }
            if (it->second > 0) {
                mapbalance[it->first.asset].received += it->second;
            }
            if (it->first.txindex == 0 && nHeight - it->first.blockHeight < COINBASE_MATURITY) {
                mapbalance[it->first.asset].balance_immature += it->second;
            } else {
                mapbalance[it->first.asset].balance_spendable += it->second;
            }
            mapbalance[it->first.asset].balance += it->second;
        }

        UniValue result(UniValue::VOBJ);
        for (auto it : mapbalance){
            UniValue asset(UniValue::VOBJ);
            asset.pushKV("balance", it.second.balance);
            asset.pushKV("balance_immature", it.second.balance_immature);
            asset.pushKV("balance_spendable", it.second.balance_spendable);
            asset.pushKV("received", it.second.received);
            if (it.first == "RTM") {
                result.pushKV(it.first, asset);
            } else {
                CAssetMetaData tmpAsset;
                if (!passetsCache->GetAssetMetaData(it.first, tmpAsset)) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Error: Asset asset metadata not found");
                }
                result.pushKV(tmpAsset.name, asset);
            }      
        }

        return result;
    }

}

static UniValue getaddresstxids(const JSONRPCRequest &request) {
    RPCHelpMan{"getaddresstxids",
               "\nReturns the txids for an address(es) (requires addressindex to be enabled).\n",
               {
                       {"addresses", RPCArg::Type::ARR, /* default */ "", "",
                        {
                                {"address", RPCArg::Type::STR, /* default */ "", "The base58check encoded address"},
                        },
                       },
               },
               RPCResult{
                       RPCResult::Type::ARR, "", "",
                       {{RPCResult::Type::STR_HEX, "transactionid", "The transaction id"}}
               },
               RPCExamples{
                       HelpExampleCli("getaddresstxids", "'{\"addresses\": [\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\"]}'")
                       + HelpExampleRpc("getaddresstxids", "{\"addresses\": [\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\"]}")
               },
    }.Check(request);

    std::vector <std::pair<uint160, int>> addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    int start = 0;
    int end = 0;
    if (request.params[0].isObject()) {
        UniValue startValue = find_value(request.params[0].get_obj(), "start");
        UniValue endValue = find_value(request.params[0].get_obj(), "end");
        if (startValue.isNum() && endValue.isNum()) {
            start = startValue.get_int();
            end = endValue.get_int();
        }
    }

    std::vector <std::pair<CAddressIndexKey, CAmount>> addressIndex;

    for (std::vector < std::pair < uint160, int > > ::iterator it = addresses.begin(); it != addresses.end();
    it++) {
        if (start > 0 && end > 0) {
            if (!GetAddressIndex((*it).first, (*it).second, addressIndex, start, end)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
            }
        } else {
            if (!GetAddressIndex((*it).first, (*it).second, addressIndex)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
            }
        }
    }

    std::set <std::pair<int, std::string>> txids;
    UniValue result(UniValue::VARR);

    for (std::vector < std::pair < CAddressIndexKey, CAmount > > ::const_iterator it = addressIndex.begin(); it !=
                                                                                                             addressIndex.end();
    it++) {
        int height = it->first.blockHeight;
        std::string txid = it->first.txhash.GetHex();

        if (addresses.size() > 1) {
            txids.insert(std::make_pair(height, txid));
        } else {
            if (txids.insert(std::make_pair(height, txid)).second) {
                result.push_back(txid);
            }
        }
    }

    if (addresses.size() > 1) {
        for (std::set < std::pair < int, std::string > > ::const_iterator it = txids.begin(); it != txids.end();
        it++) {
            result.push_back(it->second);
        }
    }

    return result;

}

static UniValue getspentinfo(const JSONRPCRequest &request) {
    RPCHelpMan{"getspentinfo",
               "\nReturns the txid and index where an output is spent.\n",
               {
                       {"request", RPCArg::Type::OBJ, /* default */ "", "",
                        {
                                {"txid", RPCArg::Type::STR_HEX, /* default */ "", "The hex string of the txid"},
                                {"index", RPCArg::Type::NUM, /* default */ "", "The start block height"},
                        },
                       },
               },
               RPCResult{
                       RPCResult::Type::OBJ, "", "",
                       {
                               {RPCResult::Type::STR_HEX, "txid", "The transaction id"},
                               {RPCResult::Type::NUM, "index", "The spending input index"},
                       }},
               RPCExamples{
                       HelpExampleCli("getspentinfo",
                                      "'{\"txid\": \"0437cd7f8525ceed2324359c2d0ba26006d92d856a9c20fa0241106ee5a597c9\", \"index\": 0}'")
                       + HelpExampleRpc("getspentinfo",
                                        "{\"txid\": \"0437cd7f8525ceed2324359c2d0ba26006d92d856a9c20fa0241106ee5a597c9\", \"index\": 0}")
               },
    }.Check(request);

    UniValue txidValue = find_value(request.params[0].get_obj(), "txid");
    UniValue indexValue = find_value(request.params[0].get_obj(), "index");

    if (!txidValue.isStr() || !indexValue.isNum()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid txid or index");
    }

    uint256 txid = ParseHashV(txidValue, "txid");
    int outputIndex = indexValue.get_int();

    CSpentIndexKey key(txid, outputIndex);
    CSpentIndexValue value;

    if (!GetSpentIndex(key, value)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unable to get spent info");
    }

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("txid", value.txid.GetHex());
    obj.pushKV("index", (int) value.inputIndex);
    obj.pushKV("height", value.blockHeight);

    return obj;
}

static UniValue mockscheduler(const JSONRPCRequest &request) {
    RPCHelpMan{"mockscheduler",
               "\nBump the scheduler into the future (-regtest only)\n",
               {
                       {"delta_time", RPCArg::Type::NUM, RPCArg::Optional::NO,
                        "Number of seconds to forward the scheduler into the future."},
               },
               RPCResults{},
               RPCExamples{""},
    }.Check(request);

    if (!Params().IsMockableChain()) {
        throw std::runtime_error("mockscheduler is for regression testing (-regtest mode) only");
    }

    // check params are valid values
    RPCTypeCheck(request.params, {UniValue::VNUM});
    int64_t delta_seconds = request.params[0].get_int64();
    if ((delta_seconds <= 0) || (delta_seconds > 3600)) {
        throw std::runtime_error("delta_time must be between 1 and 3600 seconds (1 hr)");
    }

    // protect against null pointer dereference
    CHECK_NONFATAL(request.context.Has<NodeContext>());
    NodeContext &node = request.context.Get<NodeContext>();
    CHECK_NONFATAL(node.scheduler);
    node.scheduler->MockForward(std::chrono::seconds(delta_seconds));

    return NullUniValue;
}

static UniValue RPCLockedMemoryInfo() {
    LockedPool::Stats stats = LockedPoolManager::Instance().stats();
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("used", uint64_t(stats.used));
    obj.pushKV("free", uint64_t(stats.free));
    obj.pushKV("total", uint64_t(stats.total));
    obj.pushKV("locked", uint64_t(stats.locked));
    obj.pushKV("chunks_used", uint64_t(stats.chunks_used));
    obj.pushKV("chunks_free", uint64_t(stats.chunks_free));
    return obj;
}

#ifdef HAVE_MALLOC_INFO
static std::string RPCMallocInfo()
{
    char *ptr = nullptr;
    size_t size = 0;
    FILE *f = open_memstream(&ptr, &size);
    if (f) {
        malloc_info(0, f);
        fclose(f);
        if (ptr) {
            std::string rv(ptr, size);
            free(ptr);
            return rv;
        }
    }
    return "";
}
#endif

UniValue getmemoryinfo(const JSONRPCRequest &request) {
    /* Please, avoid using the word "pool" here in the RPC interface or help,
     * as users will undoubtedly confuse it with the other "memory pool"
     */
    RPCHelpMan{"getmemoryinfo",
               "Returns an object containing information about memory usage.\n",
               {
                       {"mode", RPCArg::Type::STR, /* default */ "\"stats\"",
                        "determines what kind of information is returned.\n"
                        "  - \"stats\" returns general statistics about memory usage in the daemon.\n"
                        "  - \"mallocinfo\" returns an XML string describing low-level heap state (only available if compiled with glibc 2.10+)."},
               },
               {
                       RPCResult{"mode \"stats\"",
                                 RPCResult::Type::OBJ, "", "",
                                 {
                                         {RPCResult::Type::OBJ, "locked", "Information about locked memory manager",
                                          {
                                                  {RPCResult::Type::NUM, "used", "Number of bytes used"},
                                                  {RPCResult::Type::NUM, "free",
                                                   "Number of bytes available in current arenas"},
                                                  {RPCResult::Type::NUM, "total", "Total number of bytes managed"},
                                                  {RPCResult::Type::NUM, "locked",
                                                   "Amount of bytes that succeeded locking. If this number is smaller than total, locking pages failed at some point and key data could be swapped to disk."},
                                                  {RPCResult::Type::NUM, "chunks_used", "Number allocated chunks"},
                                                  {RPCResult::Type::NUM, "chunks_free", "Number unused chunks"},
                                          }},
                                 }
                       },
                       RPCResult{"mode \"mallocinfo\"",
                                 RPCResult::Type::STR, "", "\"<malloc version=\"1\">...\""
                       },
               },
               RPCExamples{
                       HelpExampleCli("getmemoryinfo", "")
                       + HelpExampleRpc("getmemoryinfo", "")
               },
    }.Check(request);

    std::string mode = request.params[0].isNull() ? "stats" : request.params[0].get_str();
    if (mode == "stats") {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("locked", RPCLockedMemoryInfo());
        return obj;
    } else if (mode == "mallocinfo") {
#ifdef HAVE_MALLOC_INFO
        return RPCMallocInfo();
#else
        throw JSONRPCError(RPC_INVALID_PARAMETER, "mallocinfo is only available when compiled with glibc 2.10+");
#endif
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "unknown mode " + mode);
    }
}

void EnableOrDisableLogCategories(UniValue cats, bool enable) {
    cats = cats.get_array();
    for (unsigned int i = 0; i < cats.size(); ++i) {
        std::string cat = cats[i].get_str();

        bool success;
        if (enable) {
            success = LogInstance().EnableCategory(cat);
        } else {
            success = LogInstance().DisableCategory(cat);
        }

        if (!success) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown logging category " + cat);
        }
    }
}

UniValue logging(const JSONRPCRequest &request) {
    RPCHelpMan{"logging",
               "Gets and sets the logging configuration.\n"
               "When called without an argument, returns the list of categories with status that are currently being debug logged or not.\n"
               "When called with arguments, adds or removes categories from debug logging and return the lists above.\n"
               "The arguments are evaluated in order \"include\", \"exclude\".\n"
               "If an item is both included and excluded, it will thus end up being excluded.\n"
               "The valid logging categories are: " + LogInstance().LogCategoriesString() + "\n"
                                                                                            "In addition, the following are available as category names with special meanings:\n"
                                                                                            "  - \"all\",  \"1\" : represent all logging categories.\n"
                                                                                            "  - \"raptoreum\" activates all Raptoreum-specific categories at once.\n"
                                                                                            "To deactivate all categories at once you can specify \"all\" in <exclude>.\n"
                                                                                            "  - \"none\", \"0\" : even if other logging categories are specified, ignore all of them.\n",
               {
                       {"include", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of categories to add debug logging",
                        {
                                {"include_category", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                 "the valid logging category"},
                        }},
                       {"exclude", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of categories to remove debug logging",
                        {
                                {"exclude_category", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                 "the valid logging category"},
                        }},
               },
               RPCResult{
                       RPCResult::Type::OBJ_DYN, "",
                       "where keys are the logging categories, and values indicates its status",
                       {
                               {RPCResult::Type::BOOL, "category",
                                "if being debug logged or not. false:inactive, true:active"},
                       }
               },
               RPCExamples{
                       HelpExampleCli("logging", "\"[\\\"all\\\"]\" \"[\\\"http\\\"]\"")
                       + HelpExampleRpc("logging", "[\"all\"], \"[libevent]\"")
               },
    }.Check(request);

    uint32_t original_log_categories = LogInstance().GetCategoryMask();
    if (request.params[0].isArray()) {
        EnableOrDisableLogCategories(request.params[0], true);
    }

    if (request.params[1].isArray()) {
        EnableOrDisableLogCategories(request.params[1], false);
    }
    uint32_t updated_log_categories = LogInstance().GetCategoryMask();
    uint32_t changed_log_categories = original_log_categories ^ updated_log_categories;

    // Update libevent logging if BCLog::LIBEVENT has changed.
    // If the library version doesn't allow it, UpdateHTTPServerLogging() returns false,
    // in which case we should clear the BCLog::LIBEVENT flag.
    // Throw an error if the user has explicitly asked to change only the libevent
    // flag and it failed.
    if (changed_log_categories & BCLog::LIBEVENT) {
        if (!UpdateHTTPServerLogging(LogInstance().WillLogCategory(BCLog::LIBEVENT))) {
            LogInstance().DisableCategory(BCLog::LIBEVENT);
            if (changed_log_categories == BCLog::LIBEVENT) {
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                                   "libevent logging cannot be updated when using libevent before v2.1.1.");
            }
        }
    }

    UniValue result(UniValue::VOBJ);
    for (const auto &logCatActive: LogInstance().LogCategoriesList()) {
        result.pushKV(logCatActive.category, logCatActive.active);
    }

    return result;
}

UniValue echo(const JSONRPCRequest &request) {
    RPCHelpMan{"echo|echojson ...",
               "\nSimply echo back the input arguments. This command is for testing.\n"
               "\nIt will return an internal bug report when exactly 100 arguments are passed.\n"
               "\nThe difference between echo and echojson is that echojson has argument conversion enabled in the client-side table in "
               "raptoreum-cli and the GUI. There is no server-side difference.",
               {},
               RPCResult{RPCResult::Type::NONE, "", "Returns whatever was passed in"},
               RPCExamples{""},
    }.Check(request);

    return request.params;
}

static const CRPCCommand commands[] =
        { //  category              name                      actor (function)         argNames
                //  --------------------- ------------------------  -----------------------  ----------
                {"control",      "getmemoryinfo",          &getmemoryinfo,          {"mode"}},
                {"control",      "logging",                &logging,                {"include",    "exclude"}},
                {"util",         "validateaddress",        &validateaddress,        {"address"}},
                {"util",         "createmultisig",         &createmultisig,         {"nrequired",  "keys"}},
                {"util",         "deriveaddresses",        &deriveaddresses,        {"descriptor", "begin",     "end"}},
                {"util",         "getdescriptorinfo",      &getdescriptorinfo,      {"descriptor"}},
                {"util",         "verifymessage",          &verifymessage,          {"address",    "signature", "message"}},
                {"util",         "signmessagewithprivkey", &signmessagewithprivkey, {"privkey",    "message"}},
                {"blockchain",   "getspentinfo",           &getspentinfo,           {"json"}},

                /* Address index */
                {"addressindex", "getaddressmempool",      &getaddressmempool,      {"addresses"}},
                {"addressindex", "getaddressutxos",        &getaddressutxos,        {"addresses"}},
                {"addressindex", "getaddressdeltas",       &getaddressdeltas,       {"addresses"}},
                {"addressindex", "getaddresstxids",        &getaddresstxids,        {"addresses"}},
                {"addressindex", "getaddressbalance",      &getaddressbalance,      {"addresses"}},

                /* Raptoreum features */
                {"raptoreum",    "mnsync",                 &mnsync,                 {}},
                {"raptoreum",    "spork",                  &spork,                  {"arg0",       "value"}},

                /* Not shown in help */
                {"hidden",       "setmocktime",            &setmocktime,            {"timestamp"}},
                {"hidden",       "mockscheduler",          &mockscheduler,          {"delta_time"}},
                {"hidden",       "echo",                   &echo,                   {"arg0",       "arg1",      "arg2", "arg3", "arg4", "arg5", "arg6", "arg7", "arg8", "arg9"}},
                {"hidden",       "echojson",               &echo,                   {"arg0",       "arg1",      "arg2", "arg3", "arg4", "arg5", "arg6", "arg7", "arg8", "arg9"}},
                {"hidden",       "mnauth",                 &mnauth,                 {"nodeId",     "proTxHash", "publicKey"}},
        };

void RegisterMiscRPCCommands(CRPCTable &t) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
