// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <buspool.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <rpc/register.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <streams.h>
#include <txdecoupling.h>
#include <util/validation.h>
#include <validation.h>

namespace {
CBusPoolManager& Manager()
{
    if (!IsBusPoolEnabled() || !busPoolManager) {
        throw JSONRPCError(RPC_MISC_ERROR, "Buspool RPC requires -txdecoupling=1 on regtest");
    }
    return *busPoolManager;
}

CTransactionRef DecodeTransaction(const UniValue& value)
{
    RPCTypeCheckArgument(value, UniValue::VSTR);
    if (value.get_str().size() > 200000) throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Transaction exceeds 100000 bytes");
    CMutableTransaction tx;
    if (!DecodeHexTx(tx, value.get_str())) throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    return MakeTransactionRef(std::move(tx));
}

UniValue StatementJSON(const CTxValidationCertificate& certificate)
{
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << certificate;
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", certificate.txid.GetHex());
    result.pushKV("parent", certificate.parentHash.GetHex());
    result.pushKV("positive", certificate.result == CTxValidationCertificate::POSITIVE);
    result.pushKV("quorumhash", certificate.quorumHash.GetHex());
    result.pushKV("requestid", GetTxValidationRequestId(certificate, Params().GetConsensus().hashGenesisBlock).GetHex());
    result.pushKV("messagehash", GetTxValidationMessageHash(certificate).GetHex());
    result.pushKV("hex", HexStr(stream));
    return result;
}

UniValue getbuspoolinfo(const JSONRPCRequest& request)
{
    RPCHelpMan{"getbuspoolinfo", "Return bounded experimental body-cache usage.\n", {},
        RPCResult{RPCResult::Type::OBJ, "", "Retention and candidate counts", {
            {RPCResult::Type::NUM, "size", "Retained records"},
            {RPCResult::Type::NUM, "bytes", "Accounted body and index memory"},
            {RPCResult::Type::NUM, "maxcount", "Record limit"},
            {RPCResult::Type::NUM, "maxbytes", "Byte limit"},
            {RPCResult::Type::NUM, "pending", "Tracked signing requests"},
            {RPCResult::Type::NUM, "eligible", "Currently eligible candidates"},
        }}, RPCExamples{HelpExampleCli("getbuspoolinfo", "")}}.Check(request);
    return Manager().GetInfo();
}

UniValue getbuspoolentry(const JSONRPCRequest& request)
{
    RPCHelpMan{"getbuspoolentry", "Describe a retained transaction and its current candidate provenance.\n",
        {{"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Transaction ID"}},
        RPCResult{RPCResult::Type::OBJ, "", "Retained entry", {{RPCResult::Type::ELISION, "", "Body, provenance and eligibility fields"}}},
        RPCExamples{HelpExampleCli("getbuspoolentry", "\"txid\"")}}.Check(request);
    auto result = Manager().GetEntry(ParseHashV(request.params[0], "txid"));
    if (result.isNull()) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not retained in buspool");
    return result;
}

UniValue requesttxvalidation(const JSONRPCRequest& request)
{
    RPCHelpMan{"requesttxvalidation", "Validate locally and request an anchored quorum script certificate.\n"
        "A negative result is diagnostic; unavailable context causes abstention. This does not admit the transaction.\n",
        {{"hexstring", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Complete transaction"}},
        RPCResult{RPCResult::Type::OBJ, "", "Request or abstention", {{RPCResult::Type::ELISION, "", "Status, statement fields and submission result"}}},
        RPCExamples{HelpExampleCli("requesttxvalidation", "\"hex\"")}}.Check(request);
    auto& manager = Manager();
    const auto tx = DecodeTransaction(request.params[0]);
    CTxValidationCertificate statement;
    CValidationState state;
    bool submitted;
    if (!manager.RequestValidation(tx, statement, submitted, state)) {
        UniValue result(UniValue::VOBJ);
        result.pushKV("status", "abstain");
        result.pushKV("reason", state.GetRejectReason());
        return result;
    }
    auto result = StatementJSON(statement);
    result.pushKV("status", "requested");
    result.pushKV("submitted", submitted);
    return result;
}

UniValue gettxcertificate(const JSONRPCRequest& request)
{
    RPCHelpMan{"gettxcertificate", "Return a retained signed certificate or pending local statement.\n",
        {{"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Transaction ID"}},
        RPCResult{RPCResult::Type::OBJ, "", "Statement and recovery state", {{RPCResult::Type::ELISION, "", "Statement fields, encoded certificate and recovered flag"}}},
        RPCExamples{HelpExampleCli("gettxcertificate", "\"txid\"")}}.Check(request);
    CTxValidationCertificate statement;
    bool recovered;
    if (!Manager().GetStatement(ParseHashV(request.params[0], "txid"), statement, recovered)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No retained validation statement");
    }
    auto result = StatementJSON(statement);
    result.pushKV("recovered", recovered);
    return result;
}

UniValue submitbuspooltransaction(const JSONRPCRequest& request)
{
    RPCHelpMan{"submitbuspooltransaction", "Admit a complete transaction with an eligible positive certificate.\n"
        "Every non-script admission rule still applies. Requires activated regtest delegated-script consensus.\n",
        {{"hexstring", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Complete transaction"},
         {"certificate", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Serialized validation certificate"}},
        RPCResult{RPCResult::Type::STR_HEX, "", "Admitted transaction ID"},
        RPCExamples{HelpExampleCli("submitbuspooltransaction", "\"hex\" \"certificate\"")}}.Check(request);
    auto& manager = Manager();
    const auto tx = DecodeTransaction(request.params[0]);
    RPCTypeCheckArgument(request.params[1], UniValue::VSTR);
    CTxValidationCertificate certificate;
    if (request.params[1].get_str().size() != 2 * GetSerializeSize(certificate, SER_NETWORK, PROTOCOL_VERSION)) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Invalid certificate size");
    }
    try {
        CDataStream stream(ParseHexV(request.params[1], "certificate"), SER_NETWORK, PROTOCOL_VERSION);
        stream >> certificate;
        if (!stream.empty()) throw std::ios_base::failure("trailing certificate data");
    } catch (const std::exception&) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Certificate decode failed");
    }
    CValidationState state;
    if (!manager.SubmitTransaction(tx, certificate, state)) {
        throw JSONRPCError(state.IsInvalid() ? RPC_TRANSACTION_REJECTED : RPC_TRANSACTION_ERROR, FormatStateMessage(state));
    }
    return tx->GetHash().GetHex();
}
} // namespace

void RegisterBusPoolRPCCommands(CRPCTable& table)
{
    static const CRPCCommand commands[] = {
        {"blockchain", "getbuspoolinfo", &getbuspoolinfo, {}},
        {"blockchain", "getbuspoolentry", &getbuspoolentry, {"txid"}},
        {"rawtransactions", "requesttxvalidation", &requesttxvalidation, {"hexstring"}},
        {"rawtransactions", "gettxcertificate", &gettxcertificate, {"txid"}},
        {"rawtransactions", "submitbuspooltransaction", &submitbuspooltransaction, {"hexstring", "certificate"}},
    };
    for (const auto& command : commands) table.appendCommand(command.name, &command);
}
