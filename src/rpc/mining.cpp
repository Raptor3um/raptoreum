// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2020 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/params.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <key_io.h>
#include <miner.h>
#include <net.h>
#include <node/context.h>
#include <policy/fees.h>
#include <pow.h>
#include <rpc/blockchain.h>
#include <rpc/mining.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <script/descriptor.h>
#include <script/sign.h>
#include <shutdown.h>
#include <txmempool.h>
#include <util/system.h>
#include <util/fees.h>
#include <util/strencodings.h>
#include <util/validation.h>
#include <validation.h>
#include <validationinterface.h>
#include <versionbitsinfo.h>
#include <warnings.h>

#include <governance/governance-classes.h>
#include <smartnode/smartnode-payments.h>
#include <smartnode/smartnode-sync.h>

#include <evo/deterministicmns.h>
#include <evo/specialtx.h>
#include <evo/cbtx.h>

#include <memory>
#include <stdint.h>

extern double nHashesPerSec;
extern std::string alsoHashString;

// Wallet `generate` RPC method deprecated
// ---------------------------------------

// The wallet's `generate` RPC method has been deprecated and will be fully
// removed in v0.17.

// `generate` is only used for testing. The RPC call reaches across multiple
// subsystems (wallet and mining), so is deprecated to simplify the wallet-node
// interface. Projects that are using `generate` for testing purposes should
// transition to using the `generatetoaddress` call, which does not require or use
// the wallet component. Calling `generatetoaddress` with an address returned by
// `getnewaddress` gives the same functionality as the old `generate` method.

// To continue using `generate` in v0.17, restart raptoreumd with the
// `-deprecatedrpc=generate` configuration.


/**
 * Return average network hashes per second based on the last 'lookup' blocks,
 * or from the last difficulty change if 'lookup' is nonpositive.
 * If 'height' is nonnegative, compute the estimate at the time when a given block was found.
 */
static UniValue GetNetworkHashPS(int lookup, int height) {
    CBlockIndex *pb = ::ChainActive().Tip();

    if (height >= 0 && height < ::ChainActive().Height())
        pb = ::ChainActive()[height];

    if (pb == nullptr || !pb->nHeight)
        return 0;

    // If lookup is -1, then use blocks since last difficulty change.
    if (lookup <= 0)
        lookup = pb->nHeight % Params().GetConsensus().DifficultyAdjustmentInterval() + 1;

    // If lookup is larger than chain, then set it to chain length.
    if (lookup > pb->nHeight)
        lookup = pb->nHeight;

    CBlockIndex *pb0 = pb;
    int64_t minTime = pb0->GetBlockTime();
    int64_t maxTime = minTime;
    for (int i = 0; i < lookup; i++) {
        pb0 = pb0->pprev;
        int64_t time = pb0->GetBlockTime();
        minTime = std::min(time, minTime);
        maxTime = std::max(time, maxTime);
    }

    // In case there's a situation where minTime == maxTime, we don't want a divide by zero exception.
    if (minTime == maxTime)
        return 0;

    arith_uint256 workDiff = pb->nChainWork - pb0->nChainWork;
    int64_t timeDiff = maxTime - minTime;

    return workDiff.getdouble() / timeDiff;
}

static UniValue getnetworkhashps(const JSONRPCRequest &request) {
    RPCHelpMan{"getnetworkhashps",
               "\nReturns the estimated network hashes per second based on the last n blocks.\n"
               "Pass in [blocks] to override # of blocks, -1 specifies since last difficulty change.\n"
               "Pass in [height] to estimate the network speed at the time when a certain block was found.\n",
               {
                       {"nblocks", RPCArg::Type::NUM, /* default */ "120",
                        "The number of blocks, or -1 for blocks since last difficulty change."},
                       {"height", RPCArg::Type::NUM, /* default */ "-1",
                        "To estimate at the time of the given height."},
               },
               RPCResult{
                       RPCResult::Type::NUM, "", "Hashes per second estimated"},
               RPCExamples{
                       HelpExampleCli("getnetworkhashps", "")
                       + HelpExampleRpc("getnetworkhashps", "")
               },
    }.Check(request);

    LOCK(cs_main);
    return GetNetworkHashPS(!request.params[0].isNull() ? request.params[0].get_int() : 120,
                            !request.params[1].isNull() ? request.params[1].get_int() : -1);
}

#if ENABLE_MINER
UniValue generateBlocks(ChainstateManager& chainman, const CTxMemPool& mempool, std::shared_ptr<CReserveScript> coinbaseScript, int nGenerate, uint64_t nMaxTries, bool keepScript)
{
    int nHeightEnd = 0;
    int nHeight = 0;

    {   // Don't keep cs_main locked
        LOCK(cs_main);
        nHeight = ::ChainActive().Height();
        nHeightEnd = nHeight+nGenerate;
    }
    unsigned int nExtraNonce = 0;
    UniValue blockHashes(UniValue::VARR);
    while (nHeight < nHeightEnd && !ShutdownRequested())
    {
        std::unique_ptr<CBlockTemplate> pblocktemplate(BlockAssembler(mempool, Params()).CreateNewBlock(coinbaseScript->reserveScript));
        if (!pblocktemplate.get())
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't create new block");
        CBlock *pblock = &pblocktemplate->block;
        {
            LOCK(cs_main);
            IncrementExtraNonce(pblock, ::ChainActive().Tip(), nExtraNonce);
        }
        while (nMaxTries > 0 && pblock->nNonce < std::numeric_limits<uint32_t>::max() && !CheckProofOfWork(pblock->GetPOWHash(), pblock->nBits, Params().GetConsensus()) && !ShutdownRequested()) {
            ++pblock->nNonce;
            --nMaxTries;
        }
        if (nMaxTries == 0 || ShutdownRequested()) {
            break;
        }
        if (pblock->nNonce == std::numeric_limits<uint32_t>::max()) {
            continue;
        }
        std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
        if (!chainman.ProcessNewBlock(Params(), shared_pblock, true, nullptr))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "ProcessNewBlock, block not accepted");
        ++nHeight;
        blockHashes.push_back(pblock->GetHash().GetHex());

        // mark script as important because it was used at least for one coinbase output if the script came from the wallet
        if (keepScript) {
            LOCK(cs_main);
            coinbaseScript->KeepScript();
        }
    }
    return blockHashes;
}

static bool getScriptFromDescriptor(const std::string& descriptor, CScript& script, std::string& error)
{
    FlatSigningProvider key_provider;
    const auto desc = Parse(descriptor, key_provider, error, /* require_checksum = */ false);
    if (desc) {
        if (desc->IsRange()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Ranged descriptor not accepted. Maybe pass through deriveaddresses first?");
        }

        FlatSigningProvider provider;
        std::vector<CScript> scripts;
        if (!desc->Expand(0, key_provider, scripts, provider)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Cannot derive script without private keys"));
        }

        // Combo desriptors can have 2 or 4 scripts, so we can't just check scripts.size() == 1
        assert(scripts.size() > 0 && scripts.size() <= 4);

        if (scripts.size() == 1) {
            script = scripts.at(0);
        } else if (scripts.size() == 4) {
            // For uncompressed keys, take the 3rd script, since it is p2wpkh
            script = scripts.at(2);
        } else {
            // Else take the 2nd script, since it is p2pkh
            script = scripts.at(1);
        }

        return true;
    } else {
        return false;
    }
}

static UniValue generatetodescriptor(const JSONRPCRequest& request)
{
    RPCHelpMan{
        "generatetodescriptor",
        "\nMine blocks immediately to a specified descriptor (before the RPC call returns)\n",
        {
            {"num_blocks", RPCArg::Type::NUM, RPCArg::Optional::NO, "How many blocks are generated immediately."},
            {"descriptor", RPCArg::Type::STR, RPCArg::Optional::NO, "The descriptor to send the newly generated bitcoin to."},
            {"maxtries", RPCArg::Type::NUM, /* default */ "1000000", "How many iterations to try."},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "hashes of blocks generated",
            {
                {RPCResult::Type::STR_HEX, "", "blockhash"},
            }},
        RPCExamples{
            "\nGenerate 11 blocks to mydesc\n"
            + HelpExampleCli("generatetodescriptor", "11 \"mydesc\"")},
    }.Check(request);

    const int num_blocks{request.params[0].get_int()};
    const int64_t max_tries{request.params[2].isNull() ? 1000000 : request.params[2].get_int()};

    FlatSigningProvider key_provider;
    std::string error;
    const auto desc = Parse(request.params[1].get_str(), key_provider, error, /* require_checksum = */ false);
    if (!desc) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, error);
    }
    if (desc->IsRange()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Ranged descriptor not accepted. Maybe pass through deriveaddresses first?");
    }

    FlatSigningProvider provider;
    std::vector<CScript> coinbase_script;
    if (!desc->Expand(0, key_provider, coinbase_script, provider)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Cannot derive script without private keys"));
    }

    const CTxMemPool& mempool = EnsureMemPool(request.context);
    ChainstateManager& chainman = EnsureChainman(request.context);

    assert(coinbase_script.size() == 1);

    std::shared_ptr<CReserveScript> coinbaseScript = std::make_shared<CReserveScript>();
    coinbaseScript->reserveScript = coinbase_script.at(0);

    return generateBlocks(chainman, mempool, coinbaseScript, num_blocks, max_tries, false);
}

static UniValue generatetoaddress(const JSONRPCRequest& request)
{
    RPCHelpMan{"generatetoaddress",
        "\nMine blocks immediately to a specified address (before the RPC call returns)\n",
        {
            {"nblocks", RPCArg::Type::NUM, RPCArg::Optional::NO, "How many blocks are generated immediately."},
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The address to send the newly generated RTM to."},
            {"maxtries", RPCArg::Type::NUM, /* default */ "1000000", "How many iterations to try."},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "hashes of blocks generated",
            {
                {RPCResult::Type::STR_HEX, "", "blockhash"},
            }},
        RPCExamples{
            "\nGenerate 11 blocks to myaddress\n"
            + HelpExampleCli("generatetoaddress", "11 \"myaddress\"")
            + "If you are running the Raptoreum Core wallet, you can get a new address to send the newly generated coins to with:\n"
            + HelpExampleCli("getnewaddress", "")},
    }.Check(request);

    int nGenerate = request.params[0].get_int();
    uint64_t nMaxTries = 1000000;
    if (!request.params[2].isNull()) {
        nMaxTries = request.params[2].get_int();
    }

    CTxDestination destination = DecodeDestination(request.params[1].get_str());
    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error: Invalid address");
    }

    const CTxMemPool& mempool = EnsureMemPool(request.context);
    ChainstateManager& chainman = EnsureChainman(request.context);

    std::shared_ptr<CReserveScript> coinbaseScript = std::make_shared<CReserveScript>();
    coinbaseScript->reserveScript = GetScriptForDestination(destination);

    return generateBlocks(chainman, mempool, coinbaseScript, nGenerate, nMaxTries, false);
}
#else

static UniValue generatetoaddress(const JSONRPCRequest &request) {
    throw JSONRPCError(RPC_METHOD_NOT_FOUND, "This call is not available because RPC miner isn't compiled");
}

static UniValue generatetodescriptor(const JSONRPCRequest &request) {
    throw JSONRPCError(RPC_METHOD_NOT_FOUND, "This call is not available because RPC miner isn't compiled");
}

static UniValue generateblock(const JSONRPCRequest &request) {
    throw JSONRPCError(RPC_METHOD_NOT_FOUND, "This call is not available because RPC miner isn't compiled");
}

#endif // ENABLE_MINER

static UniValue getmininginfo(const JSONRPCRequest &request) {
    RPCHelpMan{"getmininginfo",
               "\nReturns a json object containing mining-related information.",
               {},
               RPCResult{
                       RPCResult::Type::OBJ, "", "",
                       {
                               {RPCResult::Type::NUM, "blocks", "The current block"},
                               {RPCResult::Type::NUM, "currentblocksize", /* optional */ true, "The last block size"},
                               {RPCResult::Type::NUM, "currentblocktx", /* optional */ true,
                                "The number of block transactions of the last block"},
                               {RPCResult::Type::NUM, "difficulty", "The current difficulty"},
                               {RPCResult::Type::NUM, "networkhashps", "The network hashes per second"},
                               {RPCResult::Type::NUM, "hashespersec", "Your current hashes per second"},
                               {RPCResult::Type::STR, "algos", "Current solving block algos orders"},
                               {RPCResult::Type::NUM, "pooledtx", "The size of the mempool"},
                               {RPCResult::Type::STR, "chain", "current network name (main, test, regtest)"},
                               {RPCResult::Type::STR, "warnings", "any network and blockchain warnings"},
                       }},
               RPCExamples{
                       HelpExampleCli("getmininginfo", "")
                       + HelpExampleRpc("getmininginfo", "")
               },
    }.Check(request);

    LOCK(cs_main);
    const CTxMemPool &mempool = EnsureMemPool(request.context);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("blocks", (int) ::ChainActive().Height());
    obj.pushKV("currentblocksize", (uint64_t) nLastBlockSize);
    obj.pushKV("currentblocktx", (uint64_t) nLastBlockTx);
    obj.pushKV("difficulty", (double) GetDifficulty(::ChainActive().Tip()));
    obj.pushKV("networkhashps", getnetworkhashps(request));
    obj.pushKV("hashespersec", (double) nHashesPerSec);
    obj.pushKV("algos", (std::string) alsoHashString);
    obj.pushKV("pooledtx", (uint64_t) mempool.size());
    obj.pushKV("chain", Params().NetworkIDString());
    obj.pushKV("warnings", GetWarnings(false));
    return obj;
}


// NOTE: Unlike wallet RPC (which use BTC values), mining RPCs follow GBT (BIP 22) in using satoshi amounts
static UniValue prioritisetransaction(const JSONRPCRequest &request) {
    RPCHelpMan{"prioritisetransaction",
               "Accepts the transaction into mined blocks at a higher (or lower) priority\n",
               {
                       {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id."},
                       {"fee_delta", RPCArg::Type::NUM, RPCArg::Optional::NO,
                        "The fee value (in duffs) to add (or subtract, if negative).\n"
                        "Note, that this value is not a fee rate. It is a value to modify absolute fee of the TX.\n"
                        "The fee is not actually paid, only the algorithm for selecting transactions into a block\n"
                        "considers the transaction as it would have paid a higher (or lower) fee."},
               },
               RPCResult{
                       RPCResult::Type::BOOL, "", "Returns true"},
               RPCExamples{
                       HelpExampleCli("prioritisetransaction", "\"txid\" 10000")
                       + HelpExampleRpc("prioritisetransaction", "\"txid\", 10000")
               },
    }.Check(request);

    LOCK(cs_main);

    uint256 hash = ParseHashV(request.params[0].get_str(), "txid");
    CAmount nAmount = request.params[1].get_int64();

    EnsureMemPool(request.context).PrioritiseTransaction(hash, nAmount);
    return true;
}


// NOTE: Assumes a conclusive result; if result is inconclusive, it must be handled by caller
static UniValue BIP22ValidationResult(const CValidationState &state) {
    if (state.IsValid())
        return NullUniValue;

    if (state.IsError())
        throw JSONRPCError(RPC_VERIFY_ERROR, FormatStateMessage(state));
    if (state.IsInvalid()) {
        std::string strRejectReason = state.GetRejectReason();
        if (strRejectReason.empty())
            return "rejected";
        return strRejectReason;
    }
    // Should be impossible
    return "valid?";
}

std::string gbt_vb_name(const Consensus::DeploymentPos pos) {
    const struct VBDeploymentInfo &vbinfo = VersionBitsDeploymentInfo[pos];
    std::string s = vbinfo.name;
    if (!vbinfo.gbt_force) {
        s.insert(s.begin(), '!');
    }
    return s;
}

static UniValue getblocktemplate(const JSONRPCRequest &request) {
    RPCHelpMan{"getblocktemplate",
               "\nIf the request parameters include a 'mode' key, that is used to explicitly select between the default 'template' request or a 'proposal'.\n"
               "It returns data needed to construct a block to work on.\n"
               "For full specification, see BIPs 22, 23, and 9:\n"
               "    https://github.com/bitcoin/bips/blob/master/bip-0022.mediawiki\n"
               "    https://github.com/bitcoin/bips/blob/master/bip-0023.mediawiki\n"
               "    https://github.com/bitcoin/bips/blob/master/bip-0009.mediawiki#getblocktemplate_changes\n",
               {
                       {"template_request", RPCArg::Type::OBJ, /* default */ "", "A json object in the following spec",
                        {
                                {"mode", RPCArg::Type::STR, /* treat as named arg */
                                 RPCArg::Optional::OMITTED_NAMED_ARG,
                                 "This must be set to \"template\", \"proposal\" (see BIP 23), or omitted"},
                                {"capabilities", RPCArg::Type::ARR, /* treat as named arg */
                                 RPCArg::Optional::OMITTED_NAMED_ARG, "A list of strings",
                                 {
                                         {"support", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                          "client side supported feature, 'longpoll', 'coinbasetxn', 'coinbasevalue', 'proposal', 'serverlist', 'workid'"},
                                 },
                                },
                                {"rules", RPCArg::Type::ARR, /* default_val */ "", "A list of strings",
                                 {
                                         {"support", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                          "client side supported softfork deployment"},
                                 },
                                },
                        },
                        "\"template_request\""},
               },
               RPCResult{
                       RPCResult::Type::OBJ, "", "",
                       {
                               {RPCResult::Type::ARR, "capabilities", "specific client side supported features",
                                {
                                        {RPCResult::Type::STR, "", "capability"},
                                }},
                               {RPCResult::Type::NUM, "version", "The preferred block version"},
                               {RPCResult::Type::ARR, "rules", "specific block rules that are to be enforced",
                                {
                                        {RPCResult::Type::STR, "", "rulename"},
                                }},
                               {RPCResult::Type::OBJ_DYN, "vbavailable",
                                "set of pending, supported versionbit (BIP 9) softfork deployments",
                                {
                                        {RPCResult::Type::NUM, "rulename",
                                         "identifies the bit number as indicating acceptance and readiness for the named softfork rule"},
                                }},
                               {RPCResult::Type::NUM, "vbrequired",
                                "bit mask of versionbits the server requires set in submissions"},
                               {RPCResult::Type::STR, "previousblockhash", "The hash of current highest block"},
                               // {RPCResult::Type::ARR, transactions", "contents of non-coinbase transactions that should be included in the next block",
                               {RPCResult::Type::ARR, "?????",
                                "contents of non-coinbase transactions that should be included in the next block",
                                {
                                        {RPCResult::Type::OBJ, "", "",
                                         {
                                                 {RPCResult::Type::STR_HEX, "data",
                                                  "transaction data encoded in hexadecimal (byte-for-byte)"},
                                                 {RPCResult::Type::STR_HEX, "txid",
                                                  "transaction id encoded in little-endian hexadecimal"},
                                                 {RPCResult::Type::STR_HEX, "hash",
                                                  "hash encoded in little-endian hexadecimal (including witness data)"},
                                                 {RPCResult::Type::ARR, "depends", "array of numbers",
                                                  {
                                                          {RPCResult::Type::NUM, "",
                                                           "transactions before this one (by 1-based index in 'transactions' list) that must be present in the final block if this one is"},
                                                  }},
                                                 {RPCResult::Type::NUM, "fee",
                                                  "difference in value between transaction inputs and outputs (in satoshis); for coinbase transactions, this is a negative Number of the total collected block fees (ie, not including the block subsidy); if key is not present, fee is unknown and clients MUST NOT assume there isn't one"},
                                                 {RPCResult::Type::NUM, "sigops",
                                                  "total number of SigOps, as counted for purposes of block limits; if key is not present, sigop count is unknown and clients MUST NOT assume there aren't any"},
                                         }},
                                }},
                               {RPCResult::Type::OBJ, "coinbaseaux",
                                "data that should be included in the coinbase's scriptSig content",
                                {
                                        {RPCResult::Type::ELISION, "", ""},
                                }},
                               {RPCResult::Type::NUM, "coinbasevalue",
                                "maximum allowable input to coinbase transaction, including the generation award and transaction fees (in satoshis)"},
                               {RPCResult::Type::OBJ, "coinbasetxn", "information for coinbase transaction",
                                {
                                        {RPCResult::Type::ELISION, "", ""},
                                }},
                               {RPCResult::Type::STR, "target", "The hash target"},
                               {RPCResult::Type::NUM_TIME, "mintime",
                                "The minimum timestamp appropriate for the next block time, expressed in " +
                                UNIX_EPOCH_TIME},
                               {RPCResult::Type::ARR, "mutable", "list of ways the block template may be changed",
                                {
                                        {RPCResult::Type::STR, "value",
                                         "A way the block template may be changed, e.g. 'time', 'transactions', 'prevblock'"},
                                }},
                               {RPCResult::Type::STR_HEX, "noncerange", "A range of valid nonces"},
                               {RPCResult::Type::NUM, "sigoplimit", "limit of sigops in blocks"},
                               {RPCResult::Type::NUM, "sizelimit", "limit of block size"},
                               {RPCResult::Type::NUM_TIME, "curtime", "current timestamp in " + UNIX_EPOCH_TIME},
                               {RPCResult::Type::STR, "bits", "compressed target of next block"},
                               {RPCResult::Type::STR, "previousbits", "compressed target of current highest block"},
                               {RPCResult::Type::NUM, "height", "The height of the next block"},
                               {RPCResult::Type::ARR, "masternode",
                                "required masternode payments that must be included in the next block",
                                {
                                        {RPCResult::Type::OBJ, "", "",
                                         {
                                                 {RPCResult::Type::STR_HEX, "payee", "payee address"},
                                                 {RPCResult::Type::STR_HEX, "script", "payee scriptPubKey"},
                                                 {RPCResult::Type::NUM, "amount", "required amount to pay"},
                                         }},
                                }},
                               {RPCResult::Type::BOOL, "smartnode_payments_started",
                                "true, if smartnode payments started"},
                               {RPCResult::Type::BOOL, "smartnode_payments_enforced",
                                "true, if smartnode payments are enforced"},
                               {RPCResult::Type::ARR, "superblock",
                                "required superblock payees that must be included in the next block",
                                {
                                        {RPCResult::Type::OBJ, "", "",
                                         {
                                                 {RPCResult::Type::STR_HEX, "payee", "payee address"},
                                                 {RPCResult::Type::STR_HEX, "script", "payee scriptPubKey"},
                                                 {RPCResult::Type::NUM, "amount", "required amount to pay"},
                                         }},
                                }},
                               {RPCResult::Type::BOOL, "superblocks_started", "true, if superblock payments started"},
                               {RPCResult::Type::BOOL, "superblocks_enabled",
                                "true, if superblock payments are enabled"},
                               {RPCResult::Type::STR_HEX, "coinbase_payload",
                                "coinbase transaction payload data encoded in hexadecimal"},
                       }},
               RPCExamples{
                       HelpExampleCli("getblocktemplate", "")
                       + HelpExampleRpc("getblocktemplate", "")
               },
    }.Check(request);

    LOCK(cs_main);

    std::string strMode = "template";
    UniValue lpval = NullUniValue;
    std::set <std::string> setClientRules;
    int64_t nMaxVersionPreVB = -1;
    if (!request.params[0].isNull()) {
        const UniValue &oparam = request.params[0].get_obj();
        const UniValue &modeval = find_value(oparam, "mode");
        if (modeval.isStr())
            strMode = modeval.get_str();
        else if (modeval.isNull()) {
            /* Do nothing */
        } else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");
        lpval = find_value(oparam, "longpollid");

        if (strMode == "proposal") {
            const UniValue &dataval = find_value(oparam, "data");
            if (!dataval.isStr())
                throw JSONRPCError(RPC_TYPE_ERROR, "Missing data String key for proposal");

            CBlock block;
            if (!DecodeHexBlk(block, dataval.get_str()))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

            uint256 hash = block.GetHash();
            const CBlockIndex *pindex = LookupBlockIndex(hash);
            if (pindex) {
                if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                    return "duplicate";
                if (pindex->nStatus & BLOCK_FAILED_MASK)
                    return "duplicate-invalid";
                return "duplicate-inconclusive";
            }

            CBlockIndex *const pindexPrev = ::ChainActive().Tip();
            // TestBlockValidity only supports blocks built on the current Tip
            if (block.hashPrevBlock != pindexPrev->GetBlockHash())
                return "inconclusive-not-best-prevblk";
            CValidationState state;
            TestBlockValidity(state, Params(), block, pindexPrev, false, true);
            return BIP22ValidationResult(state);
        }

        const UniValue &aClientRules = find_value(oparam, "rules");
        if (aClientRules.isArray()) {
            for (unsigned int i = 0; i < aClientRules.size(); ++i) {
                const UniValue &v = aClientRules[i];
                setClientRules.insert(v.get_str());
            }
        } else {
            // NOTE: It is important that this NOT be read if versionbits is supported
            const UniValue &uvMaxVersion = find_value(oparam, "maxversion");
            if (uvMaxVersion.isNum()) {
                nMaxVersionPreVB = uvMaxVersion.get_int64();
            }
        }
    }

    if (strMode != "template")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");

    NodeContext &node = EnsureNodeContext(request.context);
    if (!node.connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    if (node.connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0)
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, PACKAGE_NAME
    " is not connected!");

    if (::ChainstateActive().IsInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Raptoreum Core is downloading blocks...");

    // next bock is a superblock and we need governance info to correctly construct it
    if (AreSuperblocksEnabled()
        && !smartnodeSync.IsSynced()
        && CSuperblock::IsValidBlockHeight(::ChainActive().Height() + 1))
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Raptoreum Core is syncing with network...");

    static unsigned int nTransactionsUpdatedLast;
    const CTxMemPool &mempool = EnsureMemPool(request.context);

    if (!lpval.isNull()) {
        // Wait to respond until either the best block changes, OR a minute has passed and there are more transactions
        uint256 hashWatchedChain;
        std::chrono::steady_clock::time_point checktxtime;
        unsigned int nTransactionsUpdatedLastLP;

        if (lpval.isStr()) {
            // Format: <hashBestChain><nTransactionsUpdatedLast>
            std::string lpstr = lpval.get_str();

            hashWatchedChain.SetHex(lpstr.substr(0, 64));
            nTransactionsUpdatedLastLP = atoi64(lpstr.substr(64));
        } else {
            // NOTE: Spec does not specify behaviour for non-string longpollid, but this makes testing easier
            hashWatchedChain = ::ChainActive().Tip()->GetBlockHash();
            nTransactionsUpdatedLastLP = nTransactionsUpdatedLast;
        }

        // Release the wallet and main lock while waiting
        LEAVE_CRITICAL_SECTION(cs_main);
        {
            checktxtime = std::chrono::steady_clock::now() + std::chrono::minutes(1);

            WAIT_LOCK(g_best_block_mutex, lock);
            while (g_best_block == hashWatchedChain && IsRPCRunning()) {
                if (g_best_block_cv.wait_until(lock, checktxtime) == std::cv_status::timeout) {
                    // Timeout: Check transactions for update
                    if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLastLP)
                        break;
                    checktxtime += std::chrono::seconds(10);
                }
            }
        }
        ENTER_CRITICAL_SECTION(cs_main);

        if (!IsRPCRunning())
            throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Shutting down");
        // TODO: Maybe recheck connections/IBD and (if something wrong) send an expires-immediately template to stop miners?
    }

    // Update block
    static CBlockIndex *pindexPrev;
    static int64_t nStart;
    static std::unique_ptr <CBlockTemplate> pblocktemplate;
    if (pindexPrev != ::ChainActive().Tip() ||
        (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 5)) {
        // Clear pindexPrev so future calls make a new block, despite any failures from here on
        pindexPrev = nullptr;

        // Store the ::ChainActive().Tip() used before CreateNewBlock, to avoid races
        nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
        CBlockIndex *pindexPrevNew = ::ChainActive().Tip();
        nStart = GetTime();

        // Create new block
        CScript scriptDummy = CScript() << OP_TRUE;
        pblocktemplate = BlockAssembler(mempool, Params()).CreateNewBlock(scriptDummy);
        if (!pblocktemplate)
            throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory");

        // Need to update only after we know CreateNewBlock succeeded
        pindexPrev = pindexPrevNew;
    }
    CBlock *pblock = &pblocktemplate->block; // pointer for convenience
    const Consensus::Params &consensusParams = Params().GetConsensus();

    // Update nTime
    UpdateTime(pblock, consensusParams, pindexPrev);
    pblock->nNonce = 0;

    UniValue aCaps(UniValue::VARR);
    aCaps.push_back("proposal");

    UniValue transactions(UniValue::VARR);
    std::map <uint256, int64_t> setTxIndex;
    int i = 0;
    for (const auto &it: pblock->vtx) {
        const CTransaction &tx = *it;
        uint256 txHash = tx.GetHash();
        setTxIndex[txHash] = i++;

        if (tx.IsCoinBase())
            continue;

        UniValue entry(UniValue::VOBJ);

        entry.pushKV("data", EncodeHexTx(tx));

        entry.pushKV("hash", txHash.GetHex());

        UniValue deps(UniValue::VARR);
        for (const CTxIn &in: tx.vin) {
            if (setTxIndex.count(in.prevout.hash))
                deps.push_back(setTxIndex[in.prevout.hash]);
        }
        entry.pushKV("depends", deps);

        int index_in_template = i - 1;
        entry.pushKV("fee", pblocktemplate->vTxFees[index_in_template]);
        entry.pushKV("specialTxfee", pblocktemplate->vSpecialTxFees[index_in_template]);
        entry.pushKV("sigops", pblocktemplate->vTxSigOps[index_in_template]);

        transactions.push_back(entry);
    }

    UniValue aux(UniValue::VOBJ);
    aux.pushKV("flags", HexStr(COINBASE_FLAGS));

    arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);

    UniValue aMutable(UniValue::VARR);
    aMutable.push_back("time");
    aMutable.push_back("transactions");
    aMutable.push_back("prevblock");

    UniValue result(UniValue::VOBJ);
    result.pushKV("capabilities", aCaps);

    UniValue aRules(UniValue::VARR);
    UniValue vbavailable(UniValue::VOBJ);
    for (int j = 0; j < (int) Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j) {
        Consensus::DeploymentPos pos = Consensus::DeploymentPos(j);
        ThresholdState state = VersionBitsState(pindexPrev, consensusParams, pos, versionbitscache);
        switch (state) {
            case ThresholdState::DEFINED:
            case ThresholdState::FAILED:
                // Not exposed to GBT at all
                break;
            case ThresholdState::LOCKED_IN:
                // Ensure bit is set in block version
                pblock->nVersion |= VersionBitsMask(consensusParams, pos);
                [[fallthrough]];
            case ThresholdState::STARTED: {
                const struct VBDeploymentInfo &vbinfo = VersionBitsDeploymentInfo[pos];
                vbavailable.pushKV(gbt_vb_name(pos), consensusParams.vDeployments[pos].bit);
                if (setClientRules.find(vbinfo.name) == setClientRules.end()) {
                    if (!vbinfo.gbt_force) {
                        // If the client doesn't support this, don't indicate it in the [default] version
                        pblock->nVersion &= ~VersionBitsMask(consensusParams, pos);
                    }
                }
                break;
            }
            case ThresholdState::ACTIVE: {
                // Add to rules only
                const struct VBDeploymentInfo &vbinfo = VersionBitsDeploymentInfo[pos];
                aRules.push_back(gbt_vb_name(pos));
                if (setClientRules.find(vbinfo.name) == setClientRules.end()) {
                    // Not supported by the client; make sure it's safe to proceed
                    if (!vbinfo.gbt_force) {
                        // If we do anything other than throw an exception here, be sure version/force isn't sent to old clients
                        throw JSONRPCError(RPC_INVALID_PARAMETER,
                                           strprintf("Support for '%s' rule requires explicit client support",
                                                     vbinfo.name));
                    }
                }
                break;
            }
        }
    }
    result.pushKV("version", pblock->nVersion);
    result.pushKV("rules", aRules);
    result.pushKV("vbavailable", vbavailable);
    result.pushKV("vbrequired", int(0));

    if (nMaxVersionPreVB >= 2) {
        // If VB is supported by the client, nMaxVersionPreVB is -1, so we won't get here
        // Because BIP 34 changed how the generation transaction is serialized, we can only use version/force back to v2 blocks
        // This is safe to do [otherwise-]unconditionally only because we are throwing an exception above if a non-force deployment gets activated
        // Note that this can probably also be removed entirely after the first BIP9 non-force deployment (ie, probably segwit) gets activated
        aMutable.push_back("version/force");
    }

    result.pushKV("previousblockhash", pblock->hashPrevBlock.GetHex());
    result.pushKV("transactions", transactions);
    result.pushKV("coinbaseaux", aux);
    result.pushKV("coinbasevalue", (int64_t) pblock->vtx[0]->GetValueOut());
    result.pushKV("longpollid", ::ChainActive().Tip()->GetBlockHash().GetHex() + i64tostr(nTransactionsUpdatedLast));
    result.pushKV("target", hashTarget.GetHex());
    result.pushKV("mintime", (int64_t) pindexPrev->GetMedianTimePast() + 1);
    result.pushKV("mutable", aMutable);
    result.pushKV("noncerange", "00000000ffffffff");
    result.pushKV("sigoplimit", (int64_t) MaxBlockSigOps(fDIP0001ActiveAtTip));
    result.pushKV("sizelimit", (int64_t) MaxBlockSize(fDIP0001ActiveAtTip));
    result.pushKV("curtime", pblock->GetBlockTime());
    result.pushKV("bits", strprintf("%08x", pblock->nBits));
    result.pushKV("previousbits", strprintf("%08x", pblocktemplate->nPrevBits));
    result.pushKV("height", (int64_t)(pindexPrev->nHeight + 1));

    UniValue smartnodeObj(UniValue::VARR);
    for (const auto &txout: pblocktemplate->voutSmartnodePayments) {
        CTxDestination dest;
        ExtractDestination(txout.scriptPubKey, dest);

        UniValue obj(UniValue::VOBJ);
        obj.pushKV("payee", EncodeDestination(dest).c_str());
        obj.pushKV("script", HexStr(txout.scriptPubKey));
        obj.pushKV("amount", txout.nValue);
        smartnodeObj.push_back(obj);
    }

    result.pushKV("smartnode", smartnodeObj);
    result.pushKV("smartnode_payments_started", pindexPrev->nHeight + 1 > consensusParams.nSmartnodePaymentsStartBlock);
    result.pushKV("smartnode_payments_enforced", true);

    UniValue superblockObjArray(UniValue::VARR);
    if (pblocktemplate->voutSuperblockPayments.size()) {
        for (const auto &txout: pblocktemplate->voutSuperblockPayments) {
            UniValue entry(UniValue::VOBJ);
            CTxDestination dest;
            ExtractDestination(txout.scriptPubKey, dest);
            entry.pushKV("payee", EncodeDestination(dest).c_str());
            entry.pushKV("script", HexStr(txout.scriptPubKey));
            entry.pushKV("amount", txout.nValue);
            superblockObjArray.push_back(entry);
        }
    }
    result.pushKV("superblock", superblockObjArray);
    result.pushKV("superblocks_started", pindexPrev->nHeight + 1 > consensusParams.nSuperblockStartBlock);
    result.pushKV("superblocks_enabled", AreSuperblocksEnabled());

    UniValue founderObj(UniValue::VOBJ);
    FounderPayment founderPayment = Params().GetConsensus().nFounderPayment;
    if (pblock->txoutFounder != CTxOut()) {
        CTxDestination founder_addr;
        ExtractDestination(pblock->txoutFounder.scriptPubKey, founder_addr);
        founderObj.pushKV("payee", EncodeDestination(founder_addr).c_str());
        founderObj.pushKV("script", HexStr(pblock->txoutFounder.scriptPubKey));
        founderObj.pushKV("amount", pblock->txoutFounder.nValue);
    }
    result.pushKV("founder", founderObj);
    result.pushKV("founder_payments_started", pindexPrev->nHeight + 1 > founderPayment.getStartBlock());

    result.pushKV("coinbase_payload", HexStr(pblock->vtx[0]->vExtraPayload));


    return result;
}

class submitblock_StateCatcher : public CValidationInterface {
public:
    uint256 hash;
    bool found;
    CValidationState state;

    explicit submitblock_StateCatcher(const uint256 &hashIn) : hash(hashIn), found(false), state() {}

protected:
    void BlockChecked(const CBlock &block, const CValidationState &stateIn) override {
        if (block.GetHash() != hash)
            return;
        found = true;
        state = stateIn;
    }
};

static UniValue submitblock(const JSONRPCRequest &request) {
    // We allow 2 arguments for compliance with BIP22. Argument 2 is ignored.
    RPCHelpMan{"submitblock",
               "\nAttempts to submit new block to network.\n"
               "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.\n",
               {
                       {"hexdata", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "the hex-encoded block data to submit"},
                       {"dummy", RPCArg::Type::STR, /* default */ "ignored",
                        "dummy value, for compatibility with BIP22. This value is ignored."},
               },
               RPCResult{RPCResult::Type::NONE, "",
                         "Returns JSON Null when valid, a string according to BIP22 otherwise"},
               RPCExamples{
                       HelpExampleCli("submitblock", "\"mydata\"")
                       + HelpExampleRpc("submitblock", "\"mydata\"")
               },
    }.Check(request);

    std::shared_ptr <CBlock> blockptr = std::make_shared<CBlock>();
    CBlock &block = *blockptr;
    if (!DecodeHexBlk(block, request.params[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");
    }

    if (block.vtx.empty() || !block.vtx[0]->IsCoinBase()) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block does not start with a coinbase");
    }

    uint256 hash = block.GetHash();
    {
        LOCK(cs_main);
        const CBlockIndex *pindex = LookupBlockIndex(hash);
        if (pindex) {
            if (pindex->IsValid(BLOCK_VALID_SCRIPTS)) {
                return "duplicate";
            }
            if (pindex->nStatus & BLOCK_FAILED_MASK) {
                return "duplicate-invalid";
            }
        }
    }

    bool new_block;
    submitblock_StateCatcher sc(block.GetHash());
    RegisterValidationInterface(&sc);
    bool accepted = EnsureChainman(request.context).ProcessNewBlock(Params(), blockptr, /* fForceProcessing */
                                                                    true, /* fNewBlock */ &new_block);
    UnregisterValidationInterface(&sc);
    if (!new_block) {
        if (!accepted) {
            // TODO Maybe pass down fNewBlock to AcceptBlockHeader, so it is properly set to true in this case?
            return "invalid";
        }
        return "duplicate";
    }
    if (!sc.found) {
        return "inconclusive";
    }
    return BIP22ValidationResult(sc.state);
}

static UniValue submitheader(const JSONRPCRequest &request) {
    RPCHelpMan{"submitheader",
               "\nDecode the given hexdata as a header and submit it as a candidate chain tip if valid."
               "\nThrows when the header is invalid.\n",
               {
                       {"hexdata", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "the hex-encoded block header data"},
               },
               RPCResult{RPCResult::Type::NONE, "", "None"},
               RPCExamples{
                       HelpExampleCli("submitheader", "\"aabbcc\"") +
                       HelpExampleRpc("submitheader", "\"aabbcc\"")
               },
    }.Check(request);

    CBlockHeader h;
    if (!DecodeHexBlockHeader(h, request.params[0].get_str())) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block header decode failed");
    }
    {
        LOCK(cs_main);
        if (!LookupBlockIndex(h.hashPrevBlock)) {
            throw JSONRPCError(RPC_VERIFY_ERROR,
                               "Must submit previous header (" + h.hashPrevBlock.GetHex() + ") first");
        }
    }

    CValidationState state;
    EnsureChainman(request.context).ProcessNewBlockHeaders({h}, state, Params(), /* ppindex */
                                                           nullptr, /* first_invalid */ nullptr);
    if (state.IsValid()) return NullUniValue;
    if (state.IsError()) {
        throw JSONRPCError(RPC_VERIFY_ERROR, FormatStateMessage(state));
    }
    throw JSONRPCError(RPC_VERIFY_ERROR, state.GetRejectReason());
}

static UniValue estimatesmartfee(const JSONRPCRequest &request) {
    RPCHelpMan{"estimatesmartfee",
               "\nEstimates the approximate fee per kilobyte needed for a transaction to begin\n"
               "confirmation within conf_target blocks if possible and return the number of blocks\n"
               "for which the estimate is valid.\n",
               {
                       {"conf_target", RPCArg::Type::NUM, RPCArg::Optional::NO,
                        "Confirmation target in blocks (1 - 1008)"},
                       {"estimate_mode", RPCArg::Type::STR, /* default */ "CONSERVATIVE", "The fee estimate mode.\n"
                                                                                          "                   Whether to return a more conservative estimate which also satisfies\n"
                                                                                          "                   a longer history. A conservative estimate potentially returns a\n"
                                                                                          "                   higher feerate and is more likely to be sufficient for the desired\n"
                                                                                          "                   target, but is not as responsive to short term drops in the\n"
                                                                                          "                   prevailing fee market.  Must be one of:\n"
                                                                                          "       \"UNSET\" (defaults to CONSERVATIVE)\n"
                                                                                          "       \"ECONOMICAL\"\n"
                                                                                          "       \"CONSERVATIVE\""},
               },
               RPCResult{
                       RPCResult::Type::OBJ, "", "",
                       {
                               {RPCResult::Type::NUM, "feerate", /* optional */ true,
                                "estimate fee rate in " + CURRENCY_UNIT + "/kB"},
                               {RPCResult::Type::ARR, "errors", "Errors encountered during processing",
                                {
                                        {RPCResult::Type::STR, "", "error"},
                                }},
                               {RPCResult::Type::NUM, "blocks", "block number where estimate was found\n"
                                                                "The request target will be clamped between 2 and the highest target\n"
                                                                "fee estimation is able to return based on how long it has been running.\n"
                                                                "An error is returned if not enough transactions and blocks\n"
                                                                "have been observed to make an estimate for any number of blocks."},
                       }},
               RPCExamples{
                       HelpExampleCli("estimatesmartfee", "6")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VNUM, UniValue::VSTR});
    RPCTypeCheckArgument(request.params[0], UniValue::VNUM);
    unsigned int max_target = ::feeEstimator.HighestTargetTracked(FeeEstimateHorizon::LONG_HALFLIFE);
    unsigned int conf_target = ParseConfirmTarget(request.params[0], max_target);
    bool conservative = true;
    if (!request.params[1].isNull()) {
        FeeEstimateMode fee_mode;
        if (!FeeModeFromString(request.params[1].get_str(), fee_mode)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid estimate_mode parameter");
        }
        if (fee_mode == FeeEstimateMode::ECONOMICAL) conservative = false;
    }

    UniValue result(UniValue::VOBJ);
    UniValue errors(UniValue::VARR);
    FeeCalculation feeCalc;
    CFeeRate feeRate = ::feeEstimator.estimateSmartFee(conf_target, &feeCalc, conservative);
    if (feeRate != CFeeRate(0)) {
        result.pushKV("feerate", ValueFromAmount(feeRate.GetFeePerK()));
    } else {
        errors.push_back("Insufficient data or no feerate found");
        result.pushKV("errors", errors);
    }
    result.pushKV("blocks", feeCalc.returnedTarget);
    return result;
}

static UniValue estimaterawfee(const JSONRPCRequest &request) {
    RPCHelpMan{"estimaterawfee",
               "\nWARNING: This interface is unstable and may disappear or change!\n"
               "\nWARNING: This is an advanced API call that is tightly coupled to the specific\n"
               "         implementation of fee estimation. The parameters it can be called with\n"
               "         and the results it returns will change if the internal implementation changes.\n"
               "\nEstimates the approximate fee per kilobyte needed for a transaction to begin\n"
               "confirmation within conf_target blocks if possible.\n",
               {
                       {"conf_target", RPCArg::Type::NUM, RPCArg::Optional::NO,
                        "Confirmation target in blocks (1 - 1008)"},
                       {"threshold", RPCArg::Type::NUM, /* default */ "0.95",
                        "The proportion of transactions in a given feerate range that must have been\n"
                        "               confirmed within conf_target in order to consider those feerates as high enough and proceed to check\n"
                        "               lower buckets."},
               },
               RPCResult{
                       RPCResult::Type::OBJ, "",
                       "Results are returned for any horizon which tracks blocks up to the confirmation target",
                       {
                               {RPCResult::Type::OBJ, "short", /* optional */ true, "estimate for short time horizon",
                                {
                                        {RPCResult::Type::NUM, "feerate", /* optional */ true,
                                         "estimate fee rate in " + CURRENCY_UNIT + "/kB"},
                                        {RPCResult::Type::NUM, "decay",
                                         "exponential decay (per block) for historical moving average of confirmation data"},
                                        {RPCResult::Type::NUM, "scale",
                                         "The resolution of confirmation targets at this time horizon"},
                                        {RPCResult::Type::OBJ, "pass", /* optional */ true,
                                         "information about the lowest range of feerates to succeed in meeting the threshold",
                                         {
                                                 {RPCResult::Type::NUM, "startrange", "start of feerate range"},
                                                 {RPCResult::Type::NUM, "endrange", "end of feerate range"},
                                                 {RPCResult::Type::NUM, "withintarget",
                                                  "number of txs over history horizon in the feerate range that were confirmed within target"},
                                                 {RPCResult::Type::NUM, "totalconfirmed",
                                                  "number of txs over history horizon in the feerate range that were confirmed at any point"},
                                                 {RPCResult::Type::NUM, "inmempool",
                                                  "current number of txs in mempool in the feerate range unconfirmed for at least target blocks"},
                                                 {RPCResult::Type::NUM, "leftmempool",
                                                  "number of txs over history horizon in the feerate range that left mempool unconfirmed after target"},
                                         }},
                                        {RPCResult::Type::OBJ, "fail", /* optional */ true,
                                         "information about the highest range of feerates to fail to meet the threshold",
                                         {
                                                 {RPCResult::Type::ELISION, "", ""},
                                         }},
                                        {RPCResult::Type::ARR, "errors", /* optional */ true,
                                         "Errors encountered during processing",
                                         {
                                                 {RPCResult::Type::STR, "error", ""},
                                         }},
                                }},
                               {RPCResult::Type::OBJ, "medium", /* optional */ true, "estimate for medium time horizon",
                                {
                                        {RPCResult::Type::ELISION, "", ""},
                                }},
                               {RPCResult::Type::OBJ, "long", /* optional */ true, "estimate for long time horizon",
                                {
                                        {RPCResult::Type::ELISION, "", ""},
                                }},
                       }},
               RPCExamples{
                       HelpExampleCli("estimaterawfee", "6 0.9")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VNUM, UniValue::VNUM}, true);
    RPCTypeCheckArgument(request.params[0], UniValue::VNUM);
    unsigned int max_target = ::feeEstimator.HighestTargetTracked(FeeEstimateHorizon::LONG_HALFLIFE);
    unsigned int conf_target = ParseConfirmTarget(request.params[0], max_target);
    double threshold = 0.95;
    if (!request.params[1].isNull()) {
        threshold = request.params[1].get_real();
    }
    if (threshold < 0 || threshold > 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid threshold");
    }

    UniValue result(UniValue::VOBJ);

    for (const FeeEstimateHorizon horizon: {FeeEstimateHorizon::SHORT_HALFLIFE, FeeEstimateHorizon::MED_HALFLIFE,
                                            FeeEstimateHorizon::LONG_HALFLIFE}) {
        CFeeRate feeRate;
        EstimationResult buckets;

        // Only output results for horizons which track the target
        if (conf_target > ::feeEstimator.HighestTargetTracked(horizon)) continue;

        feeRate = ::feeEstimator.estimateRawFee(conf_target, threshold, horizon, &buckets);
        UniValue horizon_result(UniValue::VOBJ);
        UniValue errors(UniValue::VARR);
        UniValue passbucket(UniValue::VOBJ);
        passbucket.pushKV("startrange", round(buckets.pass.start));
        passbucket.pushKV("endrange", round(buckets.pass.end));
        passbucket.pushKV("withintarget", round(buckets.pass.withinTarget * 100.0) / 100.0);
        passbucket.pushKV("totalconfirmed", round(buckets.pass.totalConfirmed * 100.0) / 100.0);
        passbucket.pushKV("inmempool", round(buckets.pass.inMempool * 100.0) / 100.0);
        passbucket.pushKV("leftmempool", round(buckets.pass.leftMempool * 100.0) / 100.0);
        UniValue failbucket(UniValue::VOBJ);
        failbucket.pushKV("startrange", round(buckets.fail.start));
        failbucket.pushKV("endrange", round(buckets.fail.end));
        failbucket.pushKV("withintarget", round(buckets.fail.withinTarget * 100.0) / 100.0);
        failbucket.pushKV("totalconfirmed", round(buckets.fail.totalConfirmed * 100.0) / 100.0);
        failbucket.pushKV("inmempool", round(buckets.fail.inMempool * 100.0) / 100.0);
        failbucket.pushKV("leftmempool", round(buckets.fail.leftMempool * 100.0) / 100.0);

        // CFeeRate(0) is used to indicate error as a return value from estimateRawFee
        if (feeRate != CFeeRate(0)) {
            horizon_result.pushKV("feerate", ValueFromAmount(feeRate.GetFeePerK()));
            horizon_result.pushKV("decay", buckets.decay);
            horizon_result.pushKV("scale", (int) buckets.scale);
            horizon_result.pushKV("pass", passbucket);
            // buckets.fail.start == -1 indicates that all buckets passed, there is no fail bucket to output
            if (buckets.fail.start != -1) horizon_result.pushKV("fail", failbucket);
        } else {
            // Output only information that is still meaningful in the event of error
            horizon_result.pushKV("decay", buckets.decay);
            horizon_result.pushKV("scale", (int) buckets.scale);
            horizon_result.pushKV("fail", failbucket);
            errors.push_back("Insufficient data or no feerate found which meets threshold");
            horizon_result.pushKV("errors", errors);
        }
        result.pushKV(StringForFeeEstimateHorizon(horizon), horizon_result);
    }
    return result;
}

UniValue setgenerate(const JSONRPCRequest &request) {
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
                "setgenerate generate ( genproclimit )\n"
                "\nSet 'generate' true or false to turn generation on or off.\n"
                "Generation is limited to 'genproclimit' processors, -1 is unlimited.\n"
                "See the getgenerate call for the current setting.\n"
                "\nArguments:\n"
                "1. generate         (boolean, required) Set to true to turn on generation, false to turn off.\n"
                "3. genproclimit     (numeric, optional) Set the processor limit for when generation is on. Can be -1 for unlimited.\n"
                "\nExamples:\n"
                "\nSet the generation on with a limit of one processor\n"
                + HelpExampleCli("setgenerate", "true 1") +
                "\nCheck the setting\n"
                + HelpExampleCli("getgenerate", "") +
                "\nTurn off generation\n"
                + HelpExampleCli("setgenerate", "false") +
                "\nUsing json rpc\n"
                + HelpExampleRpc("setgenerate", "true, 1")
        );

    if (Params().MineBlocksOnDemand())
        throw JSONRPCError(RPC_METHOD_NOT_FOUND,
                           "Use the generate method instead of setgenerate on this network");

    bool fGenerate = true;
    if (request.params.size() > 0)
        fGenerate = request.params[0].get_bool();

    int nGenProcLimit = gArgs.GetArg("-genproclimit", DEFAULT_GENERATE_THREADS);
    if (request.params.size() > 1) {
        nGenProcLimit = request.params[1].get_int();
        if (nGenProcLimit == 0)
            fGenerate = false;
    }


    gArgs.SoftSetArg("-gen", (fGenerate ? "1" : "0"));
    gArgs.SoftSetArg("-genproclimit", itostr(nGenProcLimit));

    NodeContext &node = EnsureNodeContext(request.context);
    if (!node.connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    int numCores = GenerateRaptoreums(fGenerate, nGenProcLimit, Params(), node);

    nGenProcLimit = nGenProcLimit >= 0 ? nGenProcLimit : numCores;
    std::string msg = std::to_string(nGenProcLimit) + " of " + std::to_string(numCores);

    return msg;
}

static const CRPCCommand commands[] =
        { //  category              name                      actor (function)         argNames
                //  --------------------- ------------------------  -----------------------  ----------
                {"mining", "getnetworkhashps", &getnetworkhashps, {"nblocks", "height"}},
                {"mining", "getmininginfo", &getmininginfo, {}},
                {"mining", "prioritisetransaction", &prioritisetransaction, {"txid", "fee_delta"}},
                {"mining", "getblocktemplate", &getblocktemplate, {"template_request"}},
                {"mining", "submitblock", &submitblock, {"hexdata", "dummy"}},
                {"mining", "submitheader", &submitheader, {"hexdata"}},

#if ENABLE_MINER
                { "generating",         "generatetoaddress",      &generatetoaddress,      {"nblocks", "address", "maxtries"} },
                   { "generating",         "generatetodescriptor",   &generatetodescriptor,   {"num_blocks", "descriptor", "maxtries"} },
                { "generating",         "setgenerate",   &setgenerate,   {"generate", "genproclimit"}  },

#else
                {"hidden", "generatetoaddress", &generatetoaddress, {"nblocks", "address",
                                                                     "maxtries"}}, // Hidden as it isn't functional, just an error to let people know if miner isn't compiled
                {"hidden", "generatetodescriptor", &generatetodescriptor, {"num_blocks", "descriptor", "maxtries"}},
#endif // ENABLE_MINER

                {"util", "estimatesmartfee", &estimatesmartfee, {"conf_target", "estimate_mode"}},
                {"hidden", "estimaterawfee", &estimaterawfee, {"conf_target", "threshold"}},
        };

void RegisterMiningRPCCommands(CRPCTable &t) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
