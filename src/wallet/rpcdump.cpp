// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <core_io.h>
#include <interfaces/chain.h>
#include <key_io.h>
#include <merkleblock.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <script/descriptor.h>
#include <script/script.h>
#include <script/standard.h>
#include <sync.h>
#include <util/bip32.h>
#include <util/system.h>
#include <util/time.h>
#include <validation.h>
#include <wallet/wallet.h>

#include <wallet/rpcwallet.h>

#include <stdint.h>
#include <tuple>

#include <boost/algorithm/string.hpp>

#include <univalue.h>

std::string static EncodeDumpString(const std::string &str) {
    std::stringstream ret;
    for (const unsigned char c: str) {
        if (c <= 32 || c >= 128 || c == '%') {
            ret << '%' << HexStr(Span<const unsigned char>(&c, 1));
        } else {
            ret << c;
        }
    }
    return ret.str();
}

std::string DecodeDumpString(const std::string &str) {
    std::stringstream ret;
    for (unsigned int pos = 0; pos < str.length(); pos++) {
        unsigned char c = str[pos];
        if (c == '%' && pos + 2 < str.length()) {
            c = (((str[pos + 1] >> 6) * 9 + ((str[pos + 1] - '0') & 15)) << 4) |
                ((str[pos + 2] >> 6) * 9 + ((str[pos + 2] - '0') & 15));
            pos += 2;
        }
        ret << c;
    }
    return ret.str();
}

static const int64_t TIMESTAMP_MIN = 0;

static void RescanWallet(CWallet &wallet, const WalletRescanReserver &reserver, int64_t time_begin = TIMESTAMP_MIN,
                         bool update = true) {
    int64_t scanned_time = wallet.RescanFromTime(time_begin, reserver, update);
    if (wallet.IsAbortingRescan()) {
        throw JSONRPCError(RPC_MISC_ERROR, "Rescan aborted by user.");
    } else if (scanned_time > time_begin) {
        throw JSONRPCError(RPC_WALLET_ERROR,
                           "Rescan was unable to fully rescan the blockchain. Some transactions may be missing.");
    }
}

UniValue importprivkey(const JSONRPCRequest &request) {
    RPCHelpMan{"importprivkey",
               "\nAdds a private key (as returned by dumpprivkey) to your wallet. Requires a new wallet backup.\n"
               "Hint: use importmulti to import more than one private key.\n"
               "\nNote: This call can take over an hour to complete if rescan is true, during that time, other rpc calls\n"
               "may report that the imported key exists but related transactions are still missing, leading to temporarily incorrect/bogus balances and unspent outputs until rescan completes.\n",
               {
                       {"privkey", RPCArg::Type::STR, RPCArg::Optional::NO, "The private key (see dumpprivkey)"},
                       {"label", RPCArg::Type::STR, /* default */ "current label if address exists, otherwise \"\"",
                        "An optional label"},
                       {"rescan", RPCArg::Type::BOOL, /* default */ "true", "Rescan the wallet for transactions"},
               },
               RPCResult{RPCResult::Type::NONE, "", ""},
               RPCExamples{
                       "\nDump a private key\n"
                       + HelpExampleCli("dumpprivkey", "\"myaddress\"") +
                       "\nImport the private key with rescan\n"
                       + HelpExampleCli("importprivkey", "\"mykey\"") +
                       "\nImport using a label and without rescan\n"
                       + HelpExampleCli("importprivkey", "\"mykey\" \"testing\" false") +
                       "\nImport using default blank label and without rescan\n"
                       + HelpExampleCli("importprivkey", "\"mykey\" \"\" false") +
                       "\nAs a JSON-RPC call\n"
                       + HelpExampleRpc("importprivkey", "\"mykey\", \"testing\", false")
               },
    }.Check(request);

    std::shared_ptr <CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet *const pwallet = wallet.get();

    if (pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Cannot import private keys to a wallet with private keys disabled");
    }

    WalletBatch batch(pwallet->GetDBHandle());
    WalletRescanReserver reserver(pwallet);
    bool fRescan = true;
    {
        LOCK(pwallet->cs_wallet);

        EnsureWalletIsUnlocked(pwallet);

        std::string strSecret = request.params[0].get_str();
        std::string strLabel = "";
        if (!request.params[1].isNull())
            strLabel = request.params[1].get_str();

        // Whether to perform rescan after import
        if (!request.params[2].isNull())
            fRescan = request.params[2].get_bool();

        if (fRescan && pwallet->chain().getPruneMode()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Rescan is disabled in pruned mode");
        }

        if (fRescan && !reserver.reserve()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is currently rescanning. Abort existing rescan or wait.");
        }

        CKey key = DecodeSecret(strSecret);
        if (!key.IsValid()) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key encoding");

        CPubKey pubkey = key.GetPubKey();
        assert(key.VerifyPubKey(pubkey));
        CKeyID vchAddress = pubkey.GetID();
        {
            pwallet->MarkDirty();
            pwallet->SetAddressBook(vchAddress, strLabel, "receive");

            // Don't throw error in case a key is already there
            if (pwallet->HaveKey(vchAddress)) {
                return NullUniValue;
            }

            // whenever a key is imported, we need to scan the whole chain
            pwallet->UpdateTimeFirstKey(1);
            pwallet->mapKeyMetadata[vchAddress].nCreateTime = 1;

            if (!pwallet->AddKeyPubKeyWithDB(batch, key, pubkey)) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");
            }
        }
    }
    if (fRescan) {
        int64_t scanned_time = pwallet->RescanFromTime(TIMESTAMP_MIN, reserver, true /* update */);
        if (pwallet->IsAbortingRescan()) {
            throw JSONRPCError(RPC_MISC_ERROR, "Rescan aborted by user.");
        }
        if (scanned_time > TIMESTAMP_MIN) {
            throw JSONRPCError(RPC_WALLET_ERROR,
                               "Rescan was unable to fully rescan the blockchain. Some transactions may be missing.");
        }
    }
    return NullUniValue;
}

UniValue abortrescan(const JSONRPCRequest &request) {
    RPCHelpMan{"abortrescan",
               "\nStops current wallet rescan triggered by an RPC call, e.g. by an importprivkey call.\n",
               {},
               RPCResult{RPCResult::Type::BOOL, "", "Whether the abort was successful"},
               RPCExamples{
                       "\nImport a private key\n"
                       + HelpExampleCli("importprivkey", "\"mykey\"") +
                       "\nAbort the running wallet rescan\n"
                       + HelpExampleCli("abortrescan", "") +
                       "\nAs a JSON-RPC call\n"
                       + HelpExampleRpc("abortrescan", "")
               },
    }.Check(request);

    std::shared_ptr <CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet *const pwallet = wallet.get();

    if (!pwallet->IsScanning() || pwallet->IsAbortingRescan()) return false;
    pwallet->AbortRescan();
    return true;
}

void ImportAddress(CWallet *, const CTxDestination &dest, const std::string &strLabel);

void ImportScript(CWallet *const pwallet, const CScript &script, const std::string &strLabel, bool isRedeemScript)

EXCLUSIVE_LOCKS_REQUIRED(pwallet
->cs_wallet)
{
WalletBatch batch(pwallet->GetDBHandle());

if (!
isRedeemScript &&::IsMine(*pwallet, script)
== ISMINE_SPENDABLE) {
throw
JSONRPCError(RPC_WALLET_ERROR,
"The wallet already contains the private key for this address or script");
}

pwallet->

MarkDirty();

if (!pwallet->
HaveWatchOnly(script)
&& !pwallet->
AddWatchOnlyWithDB(batch, script,
0 /* nCreateTime */)) {
throw
JSONRPCError(RPC_WALLET_ERROR,
"Error adding address to wallet");
}

if (isRedeemScript) {
const CScriptID id(script);
if (!pwallet->
HaveCScript(id)
&& !pwallet->
AddCScriptWithDB(batch, script
)) {
throw
JSONRPCError(RPC_WALLET_ERROR,
"Error adding p2sh redeemScript to wallet");
}
ImportAddress(pwallet, CScriptID(script), strLabel
);
} else {
CTxDestination destination;
if (
ExtractDestination(script, destination
)) {
pwallet->
SetAddressBook(destination, strLabel,
"receive");
}
}
}

void ImportAddress(CWallet *const pwallet, const CTxDestination &dest, const std::string &strLabel)

EXCLUSIVE_LOCKS_REQUIRED(pwallet
->cs_wallet)
{
CScript script = GetScriptForDestination(dest);
ImportScript(pwallet, script, strLabel,
false);
// add to address book or update label
if (
IsValidDestination(dest)
)
pwallet->
SetAddressBook(dest, strLabel,
"receive");
}

UniValue importaddress(const JSONRPCRequest &request) {
    RPCHelpMan{"importaddress",
               "\nAdds an address or script (in hex) that can be watched as if it were in your wallet but cannot be used to spend. Requires a new wallet backup.\n"
               "\nNote: This call can take over an hour to complete if rescan is true, during that time, other rpc calls\n"
               "may report that the imported address exists but related transactions are still missing, leading to temporarily incorrect/bogus balances and unspent outputs until rescan completes.\n"
               "If you have the full public key, you should call importpubkey instead of this.\n"
               "\nNote: If you import a non-standard raw script in hex form, outputs sending to it will be treated\n"
               "as change, and not show up in many RPCs.\n",
               {
                       {"address", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "The Raptoreum address (or hex-encoded script)"},
                       {"label", RPCArg::Type::STR, /* default */ "\"\"", "An optional label"},
                       {"rescan", RPCArg::Type::BOOL, /* default */ "true", "Rescan the wallet for transactions"},
                       {"p2sh", RPCArg::Type::BOOL, /* default */ "false",
                        "Add the P2SH version of the script as well"},
               },
               RPCResult{RPCResult::Type::NONE, "", ""},
               RPCExamples{
                       "\nImport an address with rescan\n"
                       + HelpExampleCli("importaddress", "\"myaddress\"") +
                       "\nImport using a label without rescan\n"
                       + HelpExampleCli("importaddress", "\"myaddress\" \"testing\" false") +
                       "\nAs a JSON-RPC call\n"
                       + HelpExampleRpc("importaddress", "\"myaddress\", \"testing\", false")
               },
    }.Check(request);

    std::shared_ptr <CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet *const pwallet = wallet.get();

    std::string strLabel;
    if (!request.params[1].isNull())
        strLabel = request.params[1].get_str();

    // Whether to perform rescan after import
    bool fRescan = true;
    if (!request.params[2].isNull())
        fRescan = request.params[2].get_bool();

    if (fRescan && pwallet->chain().getPruneMode())
        throw JSONRPCError(RPC_WALLET_ERROR, "Rescan is disabled in pruned mode");

    WalletRescanReserver reserver(pwallet);
    if (fRescan && !reserver.reserve()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is currently rescanning. Abort existing rescan or wait.");
    }

    // Whether to import a p2sh version, too
    bool fP2SH = false;
    if (!request.params[3].isNull())
        fP2SH = request.params[3].get_bool();

    {
        LOCK(pwallet->cs_wallet);

        CTxDestination dest = DecodeDestination(request.params[0].get_str());
        if (IsValidDestination(dest)) {
            if (fP2SH) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                                   "Cannot use the p2sh flag with an address - use a script instead");
            }
            ImportAddress(pwallet, dest, strLabel);
        } else if (IsHex(request.params[0].get_str())) {
            std::vector<unsigned char> data(ParseHex(request.params[0].get_str()));
            ImportScript(pwallet, CScript(data.begin(), data.end()), strLabel, fP2SH);
        } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Raptoreum address or script");
        }
    }
    if (fRescan) {
        int64_t scanned_time = pwallet->RescanFromTime(TIMESTAMP_MIN, reserver, true /* update */);
        if (pwallet->IsAbortingRescan()) {
            throw JSONRPCError(RPC_MISC_ERROR, "Rescan aborted by user.");
        }
        if (scanned_time > TIMESTAMP_MIN) {
            throw JSONRPCError(RPC_WALLET_ERROR,
                               "Rescan was unable to fully rescan the blockchain. Some transactions may be missing.");
        }
        LOCK(pwallet->cs_wallet);
        pwallet->ReacceptWalletTransactions();
    }

    return NullUniValue;
}

UniValue importprunedfunds(const JSONRPCRequest &request) {
    RPCHelpMan{"importprunedfunds",
               "\nImports funds without rescan. Corresponding address or script must previously be included in wallet. Aimed towards pruned wallets. The end-user is responsible to import additional transactions that subsequently spend the imported outputs or rescan after the point in the blockchain the transaction is included.\n",
               {
                       {"rawtransaction", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                        "A raw transaction in hex funding an already-existing address in wallet"},
                       {"txoutproof", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                        "The hex output from gettxoutproof that contains the transaction"},
               },
               RPCResult{RPCResult::Type::NONE, "", ""},
               RPCExamples{""},
    }.Check(request);

    std::shared_ptr <CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet *const pwallet = wallet.get();

    CMutableTransaction tx;
    if (!DecodeHexTx(tx, request.params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    uint256 hashTx = tx.GetHash();
    CWalletTx wtx(pwallet, MakeTransactionRef(std::move(tx)));

    CDataStream ssMB(ParseHexV(request.params[1], "proof"), SER_NETWORK, PROTOCOL_VERSION);
    CMerkleBlock merkleBlock;
    ssMB >> merkleBlock;

    //Search partial merkle tree in proof for our transaction and index in valid block
    std::vector <uint256> vMatch;
    std::vector<unsigned int> vIndex;
    if (merkleBlock.txn.ExtractMatches(vMatch, vIndex) != merkleBlock.header.hashMerkleRoot) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Something wrong with merkleblock");
    }

    Optional<int> height = pwallet->chain().getBlockHeight(merkleBlock.header.GetHash());
    if (height == nullopt) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found in chain");
    }

    std::vector<uint256>::const_iterator it;
    if ((it = std::find(vMatch.begin(), vMatch.end(), hashTx)) == vMatch.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction given doesn't exist in proof");
    }

    unsigned int txnIndex = vIndex[it - vMatch.begin()];

    CWalletTx::Confirmation confirm(CWalletTx::Status::CONFIRMED, *height, merkleBlock.header.GetHash(), txnIndex);
    wtx.m_confirm = confirm;

    LOCK(pwallet->cs_wallet);

    if (pwallet->IsMine(*wtx.tx)) {
        pwallet->AddToWallet(wtx, false);
        return NullUniValue;
    }

    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No addresses in wallet correspond to included transaction");
}

UniValue removeprunedfunds(const JSONRPCRequest &request) {
    RPCHelpMan{"removeprunedfunds",
               "\nDeletes the specified transaction from the wallet. Meant for use with pruned wallets and as a companion to importprunedfunds. This will affect wallet balances.\n",
               {
                       {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                        "The hex-encoded id of the transaction you are deleting"},
               },
               RPCResult{RPCResult::Type::NONE, "", ""},
               RPCExamples{
                       HelpExampleCli("removeprunedfunds",
                                      "\"a8d0c0184dde994a09ec054286f1ce581bebf46446a512166eae7628734ea0a5\"") +
                       "\nAs a JSON-RPC call\n"
                       + HelpExampleRpc("removeprunedfunds",
                                        "\"a8d0c0184dde994a09ec054286f1ce581bebf46446a512166eae7628734ea0a5\"")
               },
    }.Check(request);

    std::shared_ptr <CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet *const pwallet = wallet.get();

    LOCK(pwallet->cs_wallet);

    uint256 hash;
    hash.SetHex(request.params[0].get_str());
    std::vector <uint256> vHash;
    vHash.push_back(hash);
    std::vector <uint256> vHashOut;

    if (pwallet->ZapSelectTx(vHash, vHashOut) != DBErrors::LOAD_OK) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Could not properly delete the transaction.");
    }

    if (vHashOut.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Transaction does not exist in wallet.");
    }

    return NullUniValue;
}

UniValue importpubkey(const JSONRPCRequest &request) {
    RPCHelpMan{"importpubkey",
               "\nAdds a public key (in hex) that can be watched as if it were in your wallet but cannot be used to spend. Requires a new wallet backup.\n"
               "\nNote: This call can take over an hour to complete if rescan is true, during that time, other rpc calls\n"
               "may report that the imported pubkey exists but related transactions are still missing, leading to temporarily incorrect/bogus balances and unspent outputs until rescan completes.\n",
               {
                       {"pubkey", RPCArg::Type::STR, RPCArg::Optional::NO, "The hex-encoded public key"},
                       {"label", RPCArg::Type::STR, /* default */ "\"\"", "An optional label"},
                       {"rescan", RPCArg::Type::BOOL, /* default */ "true", "Rescan the wallet for transactions"},
               },
               RPCResult{RPCResult::Type::NONE, "", ""},
               RPCExamples{
                       "\nImport a public key with rescan\n"
                       + HelpExampleCli("importpubkey", "\"mypubkey\"") +
                       "\nImport using a label without rescan\n"
                       + HelpExampleCli("importpubkey", "\"mypubkey\" \"testing\" false") +
                       "\nAs a JSON-RPC call\n"
                       + HelpExampleRpc("importpubkey", "\"mypubkey\", \"testing\", false")
               },
    }.Check(request);

    std::shared_ptr <CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet *const pwallet = wallet.get();

    std::string strLabel;
    if (!request.params[1].isNull())
        strLabel = request.params[1].get_str();

    // Whether to perform rescan after import
    bool fRescan = true;
    if (!request.params[2].isNull())
        fRescan = request.params[2].get_bool();

    if (fRescan && pwallet->chain().getPruneMode())
        throw JSONRPCError(RPC_WALLET_ERROR, "Rescan is disabled in pruned mode");

    WalletRescanReserver reserver(pwallet);
    if (fRescan && !reserver.reserve()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is currently rescanning. Abort existing rescan or wait.");
    }

    if (!IsHex(request.params[0].get_str()))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Pubkey must be a hex string");
    std::vector<unsigned char> data(ParseHex(request.params[0].get_str()));
    CPubKey pubKey(data.begin(), data.end());
    if (!pubKey.IsFullyValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Pubkey is not a valid public key");

    {
        LOCK(pwallet->cs_wallet);

        ImportAddress(pwallet, pubKey.GetID(), strLabel);
        ImportScript(pwallet, GetScriptForRawPubKey(pubKey), strLabel, false);
    }
    if (fRescan) {
        int64_t scanned_time = pwallet->RescanFromTime(TIMESTAMP_MIN, reserver, true /* update */);
        if (pwallet->IsAbortingRescan()) {
            throw JSONRPCError(RPC_MISC_ERROR, "Rescan aborted by user.");
        }
        if (scanned_time > TIMESTAMP_MIN) {
            throw JSONRPCError(RPC_WALLET_ERROR,
                               "Rescan was unable to fully rescan the blockchain. Some transactions may be missing.");
        }
        LOCK(pwallet->cs_wallet);
        pwallet->ReacceptWalletTransactions();
    }

    return NullUniValue;
}


UniValue importwallet(const JSONRPCRequest &request) {
    RPCHelpMan{"importwallet",
               "\nImports keys from a wallet dump file (see dumpwallet). Requires a new wallet backup to include imported keys.\n",
               {
                       {"filename", RPCArg::Type::STR, RPCArg::Optional::NO, "The wallet file"},
               },
               RPCResult{RPCResult::Type::NONE, "", ""},
               RPCExamples{
                       "\nDump the wallet\n"
                       + HelpExampleCli("dumpwallet", "\"test\"") +
                       "\nImport the wallet\n"
                       + HelpExampleCli("importwallet", "\"test\"") +
                       "\nImport using the json rpc call\n"
                       + HelpExampleRpc("importwallet", "\"test\"")
               },
    }.Check(request);

    std::shared_ptr <CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet *const pwallet = wallet.get();

    if (pwallet->chain().getPruneMode())
        throw JSONRPCError(RPC_WALLET_ERROR, "Importing wallets is disabled in pruned mode");

    WalletBatch batch(pwallet->GetDBHandle());
    WalletRescanReserver reserver(pwallet);
    if (!reserver.reserve()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is currently rescanning. Abort existing rescan or wait.");
    }

    int64_t nTimeBegin = 0;
    bool fGood = true;
    {
        LOCK(pwallet->cs_wallet);

        EnsureWalletIsUnlocked(pwallet);

        fsbridge::ifstream file;
        file.open(request.params[0].get_str(), std::ios::in | std::ios::ate);
        if (!file.is_open()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");
        }
        Optional<int> tip_height = pwallet->chain().getHeight();
        nTimeBegin = tip_height ? pwallet->chain().getBlockTime(*tip_height) : 0;

        int64_t nFilesize = std::max((int64_t) 1, (int64_t) file.tellg());
        file.seekg(0, file.beg);

        // Use uiInterface.ShowProgress instead of pwallet.ShowProgress because pwallet.ShowProgress has a cancel button tied to AbortRescan which
        // we don't want for this progress bar showing the import progress. uiInterface.ShowProgress does not have a cancel button.
        pwallet->chain().showProgress(strprintf("%s " + _("Importing..."), pwallet->GetDisplayName()), 0,
                                      false); // show progress dialog in GUI
        std::vector <std::tuple<CKey, int64_t, bool, std::string>> keys;
        std::vector <std::pair<CScript, int64_t>> scripts;
        while (file.good()) {
            pwallet->chain().showProgress("", std::max(1, std::min(50,
                                                                   (int) (((double) file.tellg() / (double) nFilesize) *
                                                                          100))), false);
            std::string line;
            std::getline(file, line);
            if (line.empty() || line[0] == '#')
                continue;

            std::vector <std::string> vstr;
            boost::split(vstr, line, boost::is_any_of(" "));
            if (vstr.size() < 2)
                continue;
            CKey key = DecodeSecret(vstr[0]);
            if (key.IsValid()) {
                int64_t nTime = ParseISO8601DateTime(vstr[1]);
                std::string strLabel;
                bool fLabel = true;
                for (unsigned int nStr = 2; nStr < vstr.size(); nStr++) {
                    if (vstr[nStr].front() == '#')
                        break;
                    if (vstr[nStr] == "change=1")
                        fLabel = false;
                    if (vstr[nStr] == "reserve=1")
                        fLabel = false;
                    if (vstr[nStr].substr(0, 6) == "label=") {
                        strLabel = DecodeDumpString(vstr[nStr].substr(6));
                        fLabel = true;
                    }
                }
                keys.push_back(std::make_tuple(key, nTime, fLabel, strLabel));
            } else if (IsHex(vstr[0])) {
                std::vector<unsigned char> vData(ParseHex(vstr[0]));
                CScript script = CScript(vData.begin(), vData.end());
                int64_t birth_time = ParseISO8601DateTime(vstr[1]);
                scripts.push_back(std::pair<CScript, int64_t>(script, birth_time));
            }
        }
        file.close();
        // We now know whether we are importing private keys, so we can error if private keys are disabled
        if (keys.size() > 0 && pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
            pwallet->chain().showProgress("", 100, false); // hide progress dialog in GUI
            throw JSONRPCError(RPC_WALLET_ERROR, "Importing wallets is disabled when private keys are disabled");
        }
        double total = (double) (keys.size() + scripts.size());
        double progress = 0;
        for (const auto &key_tuple: keys) {
            pwallet->chain().showProgress("", std::max(50, std::min(75, (int) ((progress / total) * 100) + 50)), false);
            const CKey &key = std::get<0>(key_tuple);
            int64_t time = std::get<1>(key_tuple);
            bool has_label = std::get<2>(key_tuple);
            std::string label = std::get<3>(key_tuple);

            CPubKey pubkey = key.GetPubKey();
            assert(key.VerifyPubKey(pubkey));
            CKeyID keyid = pubkey.GetID();
            if (pwallet->HaveKey(keyid)) {
                pwallet->WalletLogPrintf("Skipping import of %s (key already present)\n", EncodeDestination(keyid));
                continue;
            }
            pwallet->WalletLogPrintf("Importing %s...\n", EncodeDestination(keyid));
            if (!pwallet->AddKeyPubKeyWithDB(batch, key, pubkey)) {
                fGood = false;
                continue;
            }
            pwallet->mapKeyMetadata[keyid].nCreateTime = time;
            if (has_label)
                pwallet->SetAddressBook(keyid, label, "receive");
            nTimeBegin = std::min(nTimeBegin, time);
            progress++;
        }
        for (const auto &script_pair: scripts) {
            pwallet->chain().showProgress("", std::max(50, std::min(75, (int) ((progress / total) * 100) + 50)), false);
            const CScript &script = script_pair.first;
            int64_t time = script_pair.second;
            CScriptID id(script);
            if (pwallet->HaveCScript(id)) {
                pwallet->WalletLogPrintf("Skipping import of %s (script already present)\n", HexStr(script));
                continue;
            }
            if (!pwallet->AddCScriptWithDB(batch, script)) {
                pwallet->WalletLogPrintf("Error importing script %s\n", HexStr(script));
                fGood = false;
                continue;
            }
            if (time > 0) {
                pwallet->m_script_metadata[id].nCreateTime = time;
                nTimeBegin = std::min(nTimeBegin, time);
            }
            progress++;
        }
        pwallet->chain().showProgress("", 100, false); // hide progress dialog in GUI
        pwallet->UpdateTimeFirstKey(nTimeBegin);
    }
    pwallet->chain().showProgress("", 100, false); // hide progress dialog in GUI
    RescanWallet(*pwallet, reserver, nTimeBegin, false /* update */);
    pwallet->MarkDirty();

    if (!fGood)
        throw JSONRPCError(RPC_WALLET_ERROR, "Error adding some keys/scripts to wallet");

    return NullUniValue;
}

UniValue importelectrumwallet(const JSONRPCRequest &request) {
    RPCHelpMan{"importselectrumwallet",
               "\nImports keys from an Electrum wallet export file (.csv or .json)\n",
               {
                       {"filename", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "The Electrum wallet export file, should be in csv or json format"},
                       {"index", RPCArg::Type::NUM, /* default */ "0",
                        "Rescan the wallet for transactions starting from this block index"},
               },
               RPCResult{RPCResult::Type::NONE, "", ""},
               RPCExamples{
                       "\nImport the wallet\n"
                       + HelpExampleCli("importelectrumwallet", "\"test.csv\"")
                       + HelpExampleCli("importelectrumwallet", "\"test.json\"") +
                       "\nImport using the json rpc call\n"
                       + HelpExampleRpc("importelectrumwallet", "\"test.csv\"")
                       + HelpExampleRpc("importelectrumwallet", "\"test.json\"")
               },
    }.Check(request);

    std::shared_ptr <CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet *const pwallet = wallet.get();

    if (fPruneMode)
        throw JSONRPCError(RPC_WALLET_ERROR, "Importing wallets is disabled in pruned mode");

    if (pwallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Private keys are disabled for this wallet");
    }

    LOCK(pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    fsbridge::ifstream file;
    std::string strFileName = request.params[0].get_str();
    size_t nDotPos = strFileName.find_last_of(".");
    if (nDotPos == std::string::npos)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "File has no extension, should be .json or .csv");

    std::string strFileExt = strFileName.substr(nDotPos + 1);
    if (strFileExt != "json" && strFileExt != "csv")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "File has wrong extension, should be .json or .csv");

    file.open(strFileName, std::ios::in | std::ios::ate);
    if (!file.is_open())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open Electrum wallet export file");

    bool fGood = true;

    WalletBatch batch(pwallet->GetDBHandle());

    int64_t nFilesize = std::max((int64_t) 1, (int64_t) file.tellg());
    file.seekg(0, file.beg);

    pwallet->ShowProgress(_("Importing..."), 0); // show progress dialog in GUI

    if (strFileExt == "csv") {
        while (file.good()) {
            pwallet->ShowProgress("", std::max(1, std::min(99, (int) (((double) file.tellg() / (double) nFilesize) *
                                                                      100))));
            std::string line;
            std::getline(file, line);
            if (line.empty() || line == "address,private_key")
                continue;
            std::vector <std::string> vstr;
            boost::split(vstr, line, boost::is_any_of(","));
            if (vstr.size() < 2)
                continue;
            CKey key = DecodeSecret(vstr[1]);
            if (!key.IsValid()) {
                continue;
            }
            CPubKey pubkey = key.GetPubKey();
            assert(key.VerifyPubKey(pubkey));
            CKeyID keyid = pubkey.GetID();
            if (pwallet->HaveKey(keyid)) {
                pwallet->WalletLogPrintf("Skipping import of %s (key already present)\n", EncodeDestination(keyid));
                continue;
            }
            pwallet->WalletLogPrintf("Importing %s...\n", EncodeDestination(keyid));
            if (!pwallet->AddKeyPubKeyWithDB(batch, key, pubkey)) {
                fGood = false;
                continue;
            }
        }
    } else {
        // json
        char *buffer = new char[nFilesize];
        file.read(buffer, nFilesize);
        UniValue data(UniValue::VOBJ);
        if (!data.read(buffer))
            throw JSONRPCError(RPC_TYPE_ERROR, "Cannot parse Electrum wallet export file");
        delete[] buffer;

        std::vector <std::string> vKeys = data.getKeys();

        for (size_t i = 0; i < data.size(); i++) {
            pwallet->ShowProgress("", std::max(1, std::min(99, int(i * 100 / data.size()))));
            if (!data[vKeys[i]].isStr())
                continue;
            CKey key = DecodeSecret(data[vKeys[i]].get_str());
            if (!key.IsValid()) {
                continue;
            }
            CPubKey pubkey = key.GetPubKey();
            assert(key.VerifyPubKey(pubkey));
            CKeyID keyid = pubkey.GetID();
            if (pwallet->HaveKey(keyid)) {
                pwallet->WalletLogPrintf("Skipping import of %s (key already present)\n", EncodeDestination(keyid));
                continue;
            }
            pwallet->WalletLogPrintf("Importing %s...\n", EncodeDestination(keyid));
            if (!pwallet->AddKeyPubKeyWithDB(batch, key, pubkey)) {
                fGood = false;
                continue;
            }
        }
    }
    file.close();
    pwallet->ShowProgress("", 100); // hide progress dialog in GUI

    const int32_t tip_height = pwallet->chain().getHeight().value_or(std::numeric_limits<int32_t>::max());

    // Whether to perform rescan after import
    int nStartHeight = 0;
    if (!request.params[1].isNull())
        nStartHeight = request.params[1].get_int();
    if (tip_height < nStartHeight)
        nStartHeight = tip_height;

    // Assume that electrum wallet was created at that block
    int nTimeBegin = pwallet->chain().getBlockTime(nStartHeight);
    pwallet->UpdateTimeFirstKey(nTimeBegin);

    pwallet->WalletLogPrintf("Rescanning %i blocks\n", tip_height - nStartHeight + 1);
    WalletRescanReserver reserver(pwallet);
    if (!reserver.reserve()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is currently rescanning. Abort existing rescan or wait.");
    }
    pwallet->ScanForWalletTransactions(pwallet->chain().getBlockHash(nStartHeight), {}, reserver, true);

    if (!fGood)
        throw JSONRPCError(RPC_WALLET_ERROR, "Error adding some keys to wallet");

    return NullUniValue;
}

UniValue dumpprivkey(const JSONRPCRequest &request) {
    RPCHelpMan{"dumpprivkey",
               "\nReveals the private key corresponding to 'address'.\n"
               "Then the importprivkey can be used with this output\n",
               {
                       {"address", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "The Raptoreum address for the private key"},
               },
               RPCResult{
                       RPCResult::Type::STR, "key", "The private key"
               },
               RPCExamples{
                       HelpExampleCli("dumpprivkey", "\"myaddress\"")
                       + HelpExampleCli("importprivkey", "\"mykey\"")
                       + HelpExampleRpc("dumpprivkey", "\"myaddress\"")
               },
    }.Check(request);

    std::shared_ptr <CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet *const pwallet = wallet.get();

    LOCK(pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    std::string strAddress = request.params[0].get_str();
    CTxDestination dest = DecodeDestination(strAddress);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Raptoreum address");
    }
    const CKeyID *keyID = boost::get<CKeyID>(&dest);
    if (!keyID) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
    }
    CKey vchSecret;
    if (!pwallet->GetKey(*keyID, vchSecret)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + strAddress + " is not known");
    }
    return EncodeSecret(vchSecret);
}

UniValue dumphdinfo(const JSONRPCRequest &request) {
    RPCHelpMan{"dumphdinfo",
               "Returns an object containing sensitive private info about this HD wallet.\n",
               {},
               RPCResult{
                       RPCResult::Type::OBJ, "", "",
                       {
                               {RPCResult::Type::STR_HEX, "hdseed", "The HD seed (bip32, in hex)"},
                               {RPCResult::Type::STR, "mnemonic",
                                "The mnemonic for this HD wallet (bip39, english words)"},
                               {RPCResult::Type::STR, "mnemonicpassphrase",
                                "The mnemonic passphrase for this HD wallet (bip39)"},
                       }
               },
               RPCExamples{
                       HelpExampleCli("dumphdinfo", "")
                       + HelpExampleRpc("dumphdinfo", "")
               },
    }.Check(request);

    std::shared_ptr <CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet *const pwallet = wallet.get();

    LOCK(pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    CHDChain hdChainCurrent;
    if (!pwallet->GetHDChain(hdChainCurrent))
        throw JSONRPCError(RPC_WALLET_ERROR, "This wallet is not a HD wallet.");

    if (!pwallet->GetDecryptedHDChain(hdChainCurrent))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Cannot decrypt HD seed");

    SecureString ssMnemonic;
    SecureString ssMnemonicPassphrase;
    hdChainCurrent.GetMnemonic(ssMnemonic, ssMnemonicPassphrase);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("hdseed", HexStr(hdChainCurrent.GetSeed()));
    obj.pushKV("mnemonic", ssMnemonic.c_str());
    obj.pushKV("mnemonicpassphrase", ssMnemonicPassphrase.c_str());

    return obj;
}

UniValue dumpwallet(const JSONRPCRequest &request) {
    RPCHelpMan{"dumpwallet",
               "\nDumps all wallet keys in a human-readable format to a server-side file. This does not allow overwriting existing files.\n"
               "Imported scripts are included in the dumpfile too, their corresponding addresses will be added automatically by importwallet.\n"
               "Note that if your wallet contains keys which are not derived from your HD seed (e.g. imported keys), these are not covered by\n"
               "only backing up the seed itself, and must be backed up too (e.g. ensure you back up the whole dumpfile).\n",
               {
                       {"filename", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "The filename with path (either absolute or relative to raptoreumd)"},
               },
               RPCResult{
                       RPCResult::Type::OBJ, "", "",
                       {
                               {RPCResult::Type::NUM, "keys", "The number of keys contained in the wallet dump"},
                               {RPCResult::Type::STR, "filename", "The filename with full absolute path"},
                               {RPCResult::Type::STR, "warning",
                                "A warning about not sharing the wallet dump with anyone"},
                       }
               },
               RPCExamples{
                       HelpExampleCli("dumpwallet", "\"test\"")
                       + HelpExampleRpc("dumpwallet", "\"test\"")
               },
    }.Check(request);

    std::shared_ptr <CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    if (!wallet) return NullUniValue;
    CWallet *const pwallet = wallet.get();

    LOCK(pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    fs::path filepath = request.params[0].get_str();
    filepath = fs::absolute(filepath);

    /* Prevent arbitrary files from being overwritten. There have been reports
     * that users have overwritten wallet files this way:
     * https://github.com/bitcoin/bitcoin/issues/9934
     * It may also avoid other security issues.
     */
    if (fs::exists(filepath)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, filepath.string() +
                                                  " already exists. If you are sure this is what you want, move it out of the way first");
    }

    fsbridge::ofstream file;
    file.open(filepath);
    if (!file.is_open())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");

    std::map <CTxDestination, int64_t> mapKeyBirth;
    const std::map <CKeyID, int64_t> &mapKeyPool = pwallet->GetAllReserveKeys();
    pwallet->GetKeyBirthTimes(mapKeyBirth);

    std::set <CScriptID> scripts = pwallet->GetCScripts();
    // TODO: include scripts in GetKeyBirthTimes() output instead of separate

    // sort time/key pairs
    std::vector <std::pair<int64_t, CKeyID>> vKeyBirth;
    for (const auto &entry: mapKeyBirth) {
        if (const CKeyID *keyID = boost::get<CKeyID>(&entry.first)) { // set and test
            vKeyBirth.push_back(std::make_pair(entry.second, *keyID));
        }
    }
    mapKeyBirth.clear();
    std::sort(vKeyBirth.begin(), vKeyBirth.end());

    // produce output
    file << strprintf("# Wallet dump created by Raptoreum Core %s\n", CLIENT_BUILD);
    file << strprintf("# * Created on %s\n", FormatISO8601DateTime(GetTime()));
    const Optional<int> tip_height = pwallet->chain().getHeight();
    file << strprintf("# * Best block at time of backup was %i (%s),\n", tip_height.value_or(-1),
                      tip_height ? pwallet->chain().getBlockHash(*tip_height).ToString() : "(missing block hash)");
    file << strprintf("#   mined on %s\n",
                      tip_height ? FormatISO8601DateTime(pwallet->chain().getBlockTime(*tip_height))
                                 : "(missing block time)");
    file << "\n";

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("raptoreumcoreversion", CLIENT_BUILD);
    obj.pushKV("lastblockheight", tip_height.value_or(-1));
    obj.pushKV("lastblockhash", tip_height ? pwallet->chain().getBlockHash(*tip_height).ToString() : NullUniValue);
    obj.pushKV("lastblocktime",
               tip_height ? FormatISO8601DateTime(pwallet->chain().getBlockTime(*tip_height)) : NullUniValue);

    // add the base58check encoded extended master if the wallet uses HD
    CHDChain hdChainCurrent;
    if (pwallet->GetHDChain(hdChainCurrent)) {

        if (!pwallet->GetDecryptedHDChain(hdChainCurrent))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Cannot decrypt HD chain");

        SecureString ssMnemonic;
        SecureString ssMnemonicPassphrase;
        hdChainCurrent.GetMnemonic(ssMnemonic, ssMnemonicPassphrase);
        file << "# mnemonic: " << ssMnemonic << "\n";
        file << "# mnemonic passphrase: " << ssMnemonicPassphrase << "\n\n";

        SecureVector vchSeed = hdChainCurrent.GetSeed();
        file << "# HD seed: " << HexStr(vchSeed) << "\n\n";

        CExtKey masterKey;
        masterKey.SetSeed(&vchSeed[0], vchSeed.size());

        file << "# extended private masterkey: " << EncodeExtKey(masterKey) << "\n";

        CExtPubKey masterPubkey;
        masterPubkey = masterKey.Neuter();

        file << "# extended public masterkey: " << EncodeExtPubKey(masterPubkey) << "\n\n";

        for (size_t i = 0; i < hdChainCurrent.CountAccounts(); ++i) {
            CHDAccount acc;
            if (hdChainCurrent.GetAccount(i, acc)) {
                file << "# external chain counter: " << acc.nExternalChainCounter << "\n";
                file << "# internal chain counter: " << acc.nInternalChainCounter << "\n\n";
            } else {
                file << "# WARNING: ACCOUNT " << i << " IS MISSING!" << "\n\n";
            }
        }
        obj.pushKV("hdaccounts", int(hdChainCurrent.CountAccounts()));
    }

    for (std::vector < std::pair < int64_t, CKeyID > > ::const_iterator it = vKeyBirth.begin(); it != vKeyBirth.end();
    it++) {
        const CKeyID &keyid = it->second;
        std::string strTime = FormatISO8601DateTime(it->first);
        std::string strAddr = EncodeDestination(keyid);
        CKey key;
        if (pwallet->GetKey(keyid, key)) {
            file << strprintf("%s %s ", EncodeSecret(key), strTime);
            if (pwallet->mapAddressBook.count(keyid)) {
                file << strprintf("label=%s", EncodeDumpString(pwallet->mapAddressBook[keyid].name));
            } else if (mapKeyPool.count(keyid)) {
                file << "reserve=1";
            } else {
                file << "change=1";
            }
            file << strprintf(" # addr=%s%s\n", strAddr,
                              (pwallet->mapKeyMetadata[keyid].has_key_origin ? " hdkeypath=" + WriteHDKeypath(
                                      pwallet->mapKeyMetadata[keyid].key_origin.path) : ""));
        }
    }
    file << "\n";
    for (const CScriptID &scriptid: scripts) {
        CScript script;
        std::string create_time = "0";
        std::string address = EncodeDestination(scriptid);
        // get birth times for scripts with metadata
        auto it = pwallet->m_script_metadata.find(scriptid);
        if (it != pwallet->m_script_metadata.end()) {
            create_time = FormatISO8601DateTime(it->second.nCreateTime);
        }
        if (pwallet->GetCScript(scriptid, script)) {
            file << strprintf("%s %s script=1", HexStr(script), create_time);
            file << strprintf(" # addr=%s\n", address);
        }
    }
    file << "\n";
    file << "# End of dump\n";
    file.close();

    std::string strWarning = strprintf(
            _("%s file contains all private keys from this wallet. Do not share it with anyone!"),
            request.params[0].get_str());
    obj.pushKV("keys", int(vKeyBirth.size()));
    obj.pushKV("filename", filepath.string());
    obj.pushKV("warning", strWarning);

    return obj;
}

struct ImportData {
    // Input data
    std::unique_ptr <CScript> redeemscript; //!< Provided redeemScript; will be moved to `import_scripts` if relevant.

    // Output data
    std::set <CScript> import_scripts;
    std::map<CKeyID, bool> used_keys; //!< Import these private keys if available (the value indicates whether if the key is required for solvability)
    std::map <CKeyID, std::pair<CPubKey, KeyOriginInfo>> key_origins;
};

enum class ScriptContext {
    TOP, //! Top-level scriptPubKey
    P2SH, //! P2SH redeemScript
};

// Analyse the provided scriptPubKey, determining which keys and which redeem scripts from the ImportData struct are needed to spend it, and mark them as used.
// Returns an error string, or the empty string for success.
static std::string RecurseImportData(const CScript &script, ImportData &import_data, const ScriptContext script_ctx) {
    // Use Solver to obtain script type and parsed pubkeys or hashes:
    std::vector <std::vector<unsigned char>> solverdata;
    txnouttype script_type = Solver(script, solverdata);

    switch (script_type) {
        case TX_PUBKEY: {
            CPubKey pubkey(solverdata[0].begin(), solverdata[0].end());
            import_data.used_keys.emplace(pubkey.GetID(), false);
            return "";
        }
        case TX_PUBKEYHASH: {
            CKeyID id = CKeyID(uint160(solverdata[0]));
            import_data.used_keys[id] = true;
            return "";
        }
        case TX_SCRIPTHASH: {
            if (script_ctx == ScriptContext::P2SH)
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Trying to nest P2SH inside another P2SH");
            assert(script_ctx == ScriptContext::TOP);
            CScriptID id = CScriptID(uint160(solverdata[0]));
            auto subscript = std::move(
                    import_data.redeemscript); // Remove redeemscript from import_data to check for superfluous script later.
            if (!subscript) return "missing redeemscript";
            if (CScriptID(*subscript) != id) return "redeemScript does not match the scriptPubKey";
            import_data.import_scripts.emplace(*subscript);
            return RecurseImportData(*subscript, import_data, ScriptContext::P2SH);
        }
        case TX_MULTISIG: {
            for (size_t i = 1; i + 1 < solverdata.size(); ++i) {
                CPubKey pubkey(solverdata[i].begin(), solverdata[i].end());
                import_data.used_keys.emplace(pubkey.GetID(), false);
            }
            return "";
        }
        case TX_NULL_DATA:
            return "unspendable script";
        case TX_NONSTANDARD:
        default:
            return "unrecognized script";
    }
}

static UniValue ProcessImportLegacy(ImportData &import_data, std::map <CKeyID, CPubKey> &pubkey_map,
                                    std::map <CKeyID, CKey> &privkey_map, std::set <CScript> &script_pub_keys,
                                    bool &have_solving_data, const UniValue &data) {
    UniValue warnings(UniValue::VARR);

    // First ensure scriptPubKey has either a script or JSON with "address" string
    const UniValue &scriptPubKey = data["scriptPubKey"];
    bool isScript = scriptPubKey.getType() == UniValue::VSTR;
    if (!isScript && !(scriptPubKey.getType() == UniValue::VOBJ && scriptPubKey.exists("address"))) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "scriptPubKey must be string with script or JSON with address string");
    }
    const std::string &output = isScript ? scriptPubKey.get_str() : scriptPubKey["address"].get_str();

    // Optional fields.
    const std::string &strRedeemScript = data.exists("redeemscript") ? data["redeemscript"].get_str() : "";
    const UniValue &pubKeys = data.exists("pubkeys") ? data["pubkeys"].get_array() : UniValue();
    const UniValue &keys = data.exists("keys") ? data["keys"].get_array() : UniValue();
    const bool internal = data.exists("internal") ? data["internal"].get_bool() : false;
    const bool watchOnly = data.exists("watchonly") ? data["watchonly"].get_bool() : false;

    if (data.exists("range")) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Range should not be specified for a non-descriptor import");
    }

    // Generate the script and destination for the scriptPubKey provided
    CScript script;
    if (!isScript) {
        CTxDestination dest = DecodeDestination(output);
        if (!IsValidDestination(dest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address \"" + output + "\"");
        }
        script = GetScriptForDestination(dest);
    } else {
        if (!IsHex(output)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid scriptPubKey \"" + output + "\"");
        }
        std::vector<unsigned char> vData(ParseHex(output));
        script = CScript(vData.begin(), vData.end());
        CTxDestination dest;
        if (!ExtractDestination(script, dest) && !internal) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Internal must be set to true for nonstandard scriptPubKey imports.");
        }
    }
    script_pub_keys.emplace(script);

    // Parse all arguments
    if (strRedeemScript.size()) {
        if (!IsHex(strRedeemScript)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                               "Invalid redeem script \"" + strRedeemScript + "\": must be hex string");
        }
        auto parsed_redeemscript = ParseHex(strRedeemScript);
        import_data.redeemscript = MakeUnique<CScript>(parsed_redeemscript.begin(), parsed_redeemscript.end());
    }
    for (size_t i = 0; i < pubKeys.size(); ++i) {
        const auto &str = pubKeys[i].get_str();
        if (!IsHex(str)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Pubkey \"" + str + "\" must be a hex string");
        }
        auto parsed_pubkey = ParseHex(str);
        CPubKey pubkey(parsed_pubkey.begin(), parsed_pubkey.end());
        if (!pubkey.IsFullyValid()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Pubkey \"" + str + "\" is not a valid public key");
        }
        pubkey_map.emplace(pubkey.GetID(), pubkey);
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        const auto &str = keys[i].get_str();
        CKey key = DecodeSecret(str);
        if (!key.IsValid()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key encoding");
        }
        CPubKey pubkey = key.GetPubKey();
        CKeyID id = pubkey.GetID();
        if (pubkey_map.count(id)) {
            pubkey_map.erase(id);
        }
        privkey_map.emplace(id, key);
    }

    // Verify and process input data
    have_solving_data = import_data.redeemscript || pubkey_map.size() || privkey_map.size();
    if (have_solving_data) {
        // Match up data in import_data with the scriptPubKey in script.
        auto error = RecurseImportData(script, import_data, ScriptContext::TOP);

        // Verify whether the watchonly option corresponds to the availability of private keys.
        bool spendable = std::all_of(import_data.used_keys.begin(), import_data.used_keys.end(),
                                     [&](const std::pair<CKeyID, bool> &used_key) {
                                         return privkey_map.count(used_key.first) > 0;
                                     });
        if (!watchOnly && !spendable) {
            warnings.push_back(
                    "Some private keys are missing, outputs will be considered watchonly. If this is intentional, specify the watchonly flag.");
        }
        if (watchOnly && spendable) {
            warnings.push_back(
                    "All private keys are provided, outputs will be considered spendable. If this is intentional, do not specify the watchonly flag.");
        }

        // Check thst all required kesy for solvability are provided.
        if (error.empty()) {
            for (const auto &require_key: import_data.used_keys) {
                if (!require_key.second) continue; // Not required.
                if (pubkey_map.count(require_key.first) == 0 && privkey_map.count(require_key.first) == 0) {
                    error = "some required keys are missing";
                }
            }
        }

        if (!error.empty()) {
            warnings.push_back("Importing as non-solvable: " + error +
                               ". If this is intentional, don't provide any keys, pubkeys, or redeemscript.");
            import_data = ImportData();
            pubkey_map.clear();
            privkey_map.clear();
            have_solving_data = false;
        } else {
            // RecurseImportData() removes any relevant redeemscript from import_data, so we can use that to discover if a superfluous one was provided.
            if (import_data.redeemscript) warnings.push_back("Ignoring redeemscript as this is not a P2SH script.");
            for (auto it = privkey_map.begin(); it != privkey_map.end();) {
                auto oldit = it++;
                if (import_data.used_keys.count(oldit->first) == 0) {
                    warnings.push_back("Ignoring irrelevant private key.");
                    privkey_map.erase(oldit);
                }
            }
            for (auto it = pubkey_map.begin(); it != pubkey_map.end();) {
                auto oldit = it++;
                auto key_data_it = import_data.used_keys.find(oldit->first);
                if (key_data_it == import_data.used_keys.end() || !key_data_it->second) {
                    warnings.push_back(
                            "Ignoring public key \"" + HexStr(oldit->first) + "\" as it doesn't appear inside P2PKH.");
                    pubkey_map.erase(oldit);
                }
            }
        }
    }

    return warnings;
}

static UniValue ProcessImportDescriptor(ImportData &import_data, std::map <CKeyID, CPubKey> &pubkey_map,
                                        std::map <CKeyID, CKey> &privkey_map, std::set <CScript> &script_pub_keys,
                                        bool &have_solving_data, const UniValue &data) {
    UniValue warnings(UniValue::VARR);

    const std::string &descriptor = data["desc"].get_str();
    FlatSigningProvider keys;
    std::string error;
    auto parsed_desc = Parse(descriptor, keys, error, /* require_checksum = */ true);
    if (!parsed_desc) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, error);
    }

    have_solving_data = parsed_desc->IsSolvable();
    const bool watch_only = data.exists("watchonly") ? data["watchonly"].get_bool() : false;

    int64_t range_start = 0, range_end = 0;
    if (!parsed_desc->IsRange() && data.exists("range")) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Range should not be specified for an un-ranged descriptor");
    } else if (parsed_desc->IsRange()) {
        if (!data.exists("range")) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Descriptor is ranged, please specify the range");
        }
        std::tie(range_start, range_end) = ParseDescriptorRange(data["range"]);
    }

    const UniValue &priv_keys = data.exists("keys") ? data["keys"].get_array() : UniValue();

    FlatSigningProvider out_keys;

    // Expand all descriptors to get public keys and scripts.
    // TODO: get private keys from descriptors too
    for (int i = range_start; i <= range_end; ++i) {
        std::vector <CScript> scripts_temp;
        parsed_desc->Expand(i, keys, scripts_temp, out_keys);
        std::copy(scripts_temp.begin(), scripts_temp.end(), std::inserter(script_pub_keys, script_pub_keys.end()));
    }

    for (const auto &x: out_keys.scripts) {
        import_data.import_scripts.emplace(x.second);
    }

    std::copy(out_keys.pubkeys.begin(), out_keys.pubkeys.end(), std::inserter(pubkey_map, pubkey_map.end()));
    import_data.key_origins.insert(out_keys.origins.begin(), out_keys.origins.end());
    for (size_t i = 0; i < priv_keys.size(); ++i) {
        const auto &str = priv_keys[i].get_str();
        CKey key = DecodeSecret(str);
        if (!key.IsValid()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key encoding");
        }
        CPubKey pubkey = key.GetPubKey();
        CKeyID id = pubkey.GetID();

        // Check if this private key corresponds to a public key from the descriptor
        if (!pubkey_map.count(id)) {
            warnings.push_back("Ignoring irrelevant private key.");
        } else {
            privkey_map.emplace(id, key);
        }
    }

    // Check if all the public keys have corresponding private keys in the import for spendability.
    // This does not take into account threshold multisigs which could be spendable without all keys.
    // Thus, threshold multisigs without all keys will be considered not spendable here, even if they are,
    // perhaps triggering a false warning message. This is consistent with the current wallet IsMine check.
    bool spendable = std::all_of(pubkey_map.begin(), pubkey_map.end(),
                                 [&](const std::pair <CKeyID, CPubKey> &used_key) {
                                     return privkey_map.count(used_key.first) > 0;
                                 }) && std::all_of(import_data.key_origins.begin(), import_data.key_origins.end(),
                                                   [&](const std::pair <CKeyID, std::pair<CPubKey, KeyOriginInfo>> &entry) {
                                                       return privkey_map.count(entry.first) > 0;
                                                   });
    if (!watch_only && !spendable) {
        warnings.push_back(
                "Some private keys are missing, outputs will be considered watchonly. If this is intentional, specify the watchonly flag.");
    }
    if (watch_only && spendable) {
        warnings.push_back(
                "All private keys are provided, outputs will be considered spendable. If this is intentional, do not specify the watchonly flag.");
    }

    return warnings;
}

static UniValue ProcessImport(CWallet *const pwallet, const UniValue &data, const int64_t timestamp)

EXCLUSIVE_LOCKS_REQUIRED(pwallet
->cs_wallet)
{
UniValue warnings(UniValue::VARR);
UniValue result(UniValue::VOBJ);

try {
const bool internal = data.exists("internal") ? data["internal"].get_bool() : false;
// Internal addresses should not have a label
if (
internal &&data
.exists("label")) {
throw
JSONRPCError(RPC_INVALID_PARAMETER,
"Internal addresses should not have a label");
}
const std::string &label = data.exists("label") ? data["label"].get_str() : "";

ImportData import_data;
std::map <CKeyID, CPubKey> pubkey_map;
std::map <CKeyID, CKey> privkey_map;
std::set <CScript> script_pub_keys;
bool have_solving_data;

if (data.exists("scriptPubKey") && data.exists("desc")) {
throw
JSONRPCError(RPC_INVALID_PARAMETER,
"Both a descriptor and a scriptPubKey should not be provided.");
} else if (data.exists("scriptPubKey")) {
warnings = ProcessImportLegacy(import_data, pubkey_map, privkey_map, script_pub_keys, have_solving_data, data);
} else if (data.exists("desc")) {
warnings = ProcessImportDescriptor(import_data, pubkey_map, privkey_map, script_pub_keys, have_solving_data, data);
} else {
throw
JSONRPCError(RPC_INVALID_PARAMETER,
"Either a descriptor or scriptPubKey must be provided.");
}

// If private keys are disabled, abort if private keys are being imported
if (pwallet->
IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)
&& !privkey_map.

empty()

) {
throw
JSONRPCError(RPC_WALLET_ERROR,
"Cannot import private keys to a wallet with private keys disabled");
}

// Check whether we have any work to do
for (
const CScript &script
: script_pub_keys) {
if (
::IsMine(*pwallet, script
) & ISMINE_SPENDABLE) {
throw
JSONRPCError(RPC_WALLET_ERROR,
"The wallet already contains the private key for this address or script (\"" +
HexStr(script)
+ "\")");
}
}

// All good. Time to import
pwallet->

MarkDirty();

if (!pwallet->
ImportScripts(import_data
.import_scripts)) {
throw
JSONRPCError(RPC_WALLET_ERROR,
"Error adding script to wallet");
}
if (!pwallet->
ImportPrivKeys(privkey_map, timestamp
)) {
throw
JSONRPCError(RPC_WALLET_ERROR,
"Error adding key to wallet");
}
if (!pwallet->
ImportPubKeys(pubkey_map, timestamp, import_data
.key_origins)) {
throw
JSONRPCError(RPC_WALLET_ERROR,
"Error adding address to wallet");
}
if (!pwallet->
ImportScriptPubKeys(label, script_pub_keys, have_solving_data, internal, timestamp
)) {
throw
JSONRPCError(RPC_WALLET_ERROR,
"Error adding address to wallet");
}

result.pushKV("success", UniValue(true));
} catch (
const UniValue &e
) {
result.pushKV("success", UniValue(false));
result.pushKV("error", e);
} catch (...) {
result.pushKV("success", UniValue(false));

result.pushKV("error",
JSONRPCError(RPC_MISC_ERROR,
"Missing required fields"));
}
if (warnings.

size()

) result.pushKV("warnings", warnings);
return
result;
}

int64_t GetImportTimestamp(const UniValue &data, int64_t now) {
    if (data.exists("timestamp")) {
        const UniValue &timestamp = data["timestamp"];
        if (timestamp.isNum()) {
            return timestamp.get_int64();
        } else if (timestamp.isStr() && timestamp.get_str() == "now") {
            return now;
        }
        throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Expected number or \"now\" timestamp value for key. got type %s",
                                                     uvTypeName(timestamp.type())));
    }
    throw JSONRPCError(RPC_TYPE_ERROR, "Missing required timestamp field for key");
}

UniValue importmulti(const JSONRPCRequest &mainRequest) {
    RPCHelpMan{"importmulti",
               "\nImport addresses/scripts (with private or public keys, redeem script (P2SH)), rescanning all addresses in one-shot-only (rescan can be disabled via options). Requires a new wallet backup.\n"
               "\nNote: This call can take over an hour to complete if rescan is true, during that time, other rpc calls\n"
               "may report that the imported keys, addresses or scripts exists but related transactions are still missing.\n",
               {
                       {"requests", RPCArg::Type::ARR, RPCArg::Optional::NO, "Data to be imported",
                        {
                                {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                 {
                                         {"desc", RPCArg::Type::STR, /* default */ "",
                                          "Descriptor to import. If using descriptor, do not also provide address/scriptPubKey, scripts, or pubkeys"
                                         },
                                         {"scriptPubKey", RPCArg::Type::STR, RPCArg::Optional::NO,
                                          "Type of scriptPubKey (string for script, json for address). Should not be provided if using a descriptor",
                                                 /* oneline_description */ "",
                                          {"\"<script>\" | { \"address\":\"<address>\" }", "string / json"}
                                         },
                                         {"timestamp", RPCArg::Type::NUM, RPCArg::Optional::NO,
                                          "Creation time of the key expressed in " + UNIX_EPOCH_TIME + ",\n"
                                                                                                       "                                                              or the string \"now\" to substitute the current synced blockchain time. The timestamp of the oldest\n"
                                                                                                       "                                                              key will determine how far back blockchain rescans need to begin for missing wallet transactions.\n"
                                                                                                       "                                                              \"now\" can be specified to bypass scanning, for keys which are known to never have been used, and\n"
                                                                                                       "                                                              0 can be specified to scan the entire blockchain. Blocks up to 2 hours before the earliest key\n"
                                                                                                       "                                                              creation time of all keys being imported by the importmulti call will be scanned.",
                                                 /* oneline_description */ "",
                                          {"timestamp | \"now\"", "integer / string"}
                                         },
                                         {"redeemscript", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                          "Allowed only if the scriptPubKey is a P2SH address or  a P2SH scriptPubKey"},
                                         {"pubkeys", RPCArg::Type::ARR, /* default */ "empty array",
                                          "Array of strings giving pubkeys to import. They must occur in P2PKH scripts. They are not required when the private key is also provided (see the \"keys\" argument).",
                                          {
                                                  {"pubKey", RPCArg::Type::STR, RPCArg::Optional::OMITTED, ""},
                                          }
                                         },
                                         {"keys", RPCArg::Type::ARR, /* default */ "empty array",
                                          "Array of strings giving private keys whose  corresponding public keys must occur in the output or redeemscript.",
                                          {
                                                  {"key", RPCArg::Type::STR, RPCArg::Optional::OMITTED, ""},
                                          }
                                         },
                                         {"range", RPCArg::Type::RANGE, RPCArg::Optional::OMITTED,
                                          "If a ranged descriptor is used, this specifies the end or the range (in the form [begin,end]) to import"},
                                         {"internal", RPCArg::Type::BOOL, /* default */ "false",
                                          "Stating whether matching outputs should be treated as not incoming payments (also known as change)"},
                                         {"watchonly", RPCArg::Type::BOOL, /* default */ "false",
                                          "Stating whether matching outputs should be considered watched even when not all private keys are provided."},
                                         {"label", RPCArg::Type::STR, /* default */ "''",
                                          "Label to assign to the address, only allowed with internal=false"},
                                         {"keypool", RPCArg::Type::BOOL, /* default */ "false",
                                          "Stating whether imported public keys should be added to the keypool for when users request new addresses. Only allowed when wallet private keys are disabled"},
                                 },
                                },
                        },
                        "\"requests\""},
                       {"options", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED_NAMED_ARG, "",
                        {
                                {"rescan", RPCArg::Type::BOOL, /* default */ "true",
                                 "Stating if should rescan the blockchain after all imports"},
                        },
                        "\"options\""},
               },
               RPCResult{
                       RPCResult::Type::ARR, "",
                       "Response is an array with the same size as the input that has the execution result",
                       {
                               {RPCResult::Type::OBJ, "", "",
                                {
                                        {RPCResult::Type::BOOL, "success", ""},
                                        {RPCResult::Type::ARR, "warnings", /* optional */ true, "",
                                         {
                                                 {RPCResult::Type::STR, "", ""},
                                         }},
                                        {RPCResult::Type::OBJ, "error", /* optional */ true, "",
                                         {
                                                 {RPCResult::Type::ELISION, "", "JSONRPC error"},
                                         }},
                                }},
                       }
               },
               RPCExamples{
                       HelpExampleCli("importmulti",
                                      "'[{ \"scriptPubKey\": { \"address\": \"<my address>\" }, \"timestamp\":1455191478 }, "
                                      "{ \"scriptPubKey\": { \"address\": \"<my 2nd address>\" }, \"label\": \"example 2\", \"timestamp\": 1455191480 }]'") +
                       HelpExampleCli("importmulti",
                                      "'[{ \"scriptPubKey\": { \"address\": \"<my address>\" }, \"timestamp\":1455191478 }]' '{ \"rescan\": false}'")
               },
    }.Check(mainRequest);

    RPCTypeCheck(mainRequest.params, {UniValue::VARR, UniValue::VOBJ});

    const UniValue &requests = mainRequest.params[0];

    std::shared_ptr <CWallet> const wallet = GetWalletForJSONRPCRequest(mainRequest);
    if (!wallet) return NullUniValue;
    CWallet *const pwallet = wallet.get();

    //Default options
    bool fRescan = true;

    if (!mainRequest.params[1].isNull()) {
        const UniValue &options = mainRequest.params[1];

        if (options.exists("rescan")) {
            fRescan = options["rescan"].get_bool();
        }
    }

    WalletRescanReserver reserver(pwallet);
    if (fRescan && !reserver.reserve()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is currently rescanning. Abort existing rescan or wait.");
    }

    int64_t now = 0;
    bool fRunScan = false;
    int64_t nLowestTimestamp = 0;
    UniValue response(UniValue::VARR);
    {
        LOCK(pwallet->cs_wallet);
        EnsureWalletIsUnlocked(pwallet);

        // Verify all timestamps are present before importing any keys.
        const Optional<int> tip_height = pwallet->chain().getHeight();
        now = tip_height ? pwallet->chain().getBlockMedianTimePast(*tip_height) : 0;
        for (const UniValue &data: requests.getValues()) {
            GetImportTimestamp(data, now);
        }

        const int64_t minimumTimestamp = 1;

        if (fRescan && tip_height) {
            nLowestTimestamp = pwallet->chain().getBlockTime(*tip_height);
        } else {
            fRescan = false;
        }

        for (const UniValue &data: requests.getValues()) {
            const int64_t timestamp = std::max(GetImportTimestamp(data, now), minimumTimestamp);
            const UniValue result = ProcessImport(pwallet, data, timestamp);
            response.push_back(result);

            if (!fRescan) {
                continue;
            }

            // If at least one request was successful then allow rescan.
            if (result["success"].get_bool()) {
                fRunScan = true;
            }

            // Get the lowest timestamp.
            if (timestamp < nLowestTimestamp) {
                nLowestTimestamp = timestamp;
            }
        }
    }
    if (fRescan && fRunScan && requests.size()) {
        int64_t scannedTime = pwallet->RescanFromTime(nLowestTimestamp, reserver, true /* update */);
        {
            LOCK(pwallet->cs_wallet);
            pwallet->ReacceptWalletTransactions();
        }

        if (pwallet->IsAbortingRescan()) {
            throw JSONRPCError(RPC_MISC_ERROR, "Rescan aborted by user.");
        }
        if (scannedTime > nLowestTimestamp) {
            std::vector <UniValue> results = response.getValues();
            response.clear();
            response.setArray();
            size_t i = 0;
            for (const UniValue &request: requests.getValues()) {
                // If key creation date is within the successfully scanned
                // range, or if the import result already has an error set, let
                // the result stand unmodified. Otherwise replace the result
                // with an error message.
                if (scannedTime <= GetImportTimestamp(request, now) || results.at(i).exists("error")) {
                    response.push_back(results.at(i));
                } else {
                    UniValue result = UniValue(UniValue::VOBJ);
                    result.pushKV("success", UniValue(false));
                    result.pushKV(
                            "error",
                            JSONRPCError(
                                    RPC_MISC_ERROR,
                                    strprintf(
                                            "Rescan failed for key with creation timestamp %d. There was an error reading a "
                                            "block from time %d, which is after or within %d seconds of key creation, and "
                                            "could contain transactions pertaining to the key. As a result, transactions "
                                            "and coins using this key may not appear in the wallet. This error could be "
                                            "caused by pruning or data corruption (see raptoreumd log for details) and could "
                                            "be dealt with by downloading and rescanning the relevant blocks (see -reindex "
                                            "and -rescan options).",
                                            GetImportTimestamp(request, now), scannedTime - TIMESTAMP_WINDOW - 1,
                                            TIMESTAMP_WINDOW)));
                    response.push_back(std::move(result));
                }
                ++i;
            }
        }
    }

    return response;
}
