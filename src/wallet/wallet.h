// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_WALLET_H
#define BITCOIN_WALLET_WALLET_H

#include <amount.h>
#include <policy/feerate.h>
#include <saltedhasher.h>
#include <streams.h>
#include <tinyformat.h>
#include <ui_interface.h>
#include <util.h>
#include <utilstrencodings.h>
#include <validationinterface.h>
#include <script/ismine.h>
#include <wallet/coincontrol.h>
#include <wallet/crypter.h>
#include <wallet/coinselection.h>
#include <wallet/walletdb.h>
#include <wallet/walletutil.h>
#include <wallet/rpcwallet.h>


#include <coinjoin/coinjoin.h>
#include <governance/governance-object.h>
#include <evo/providertx.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

bool AddWallet(const std::shared_ptr<CWallet>& wallet);
bool RemoveWallet(const std::shared_ptr<CWallet>& wallet);
bool HasWallets();
CWallet *GetFirstWallet();
std::vector<std::shared_ptr<CWallet>> GetWallets();
std::shared_ptr<CWallet> GetWallet(const std::string& name);

/**
 * Settings
 */
extern CFeeRate payTxFee;
extern unsigned int nTxConfirmTarget;
extern bool bSpendZeroConfChange;

static const unsigned int DEFAULT_KEYPOOL_SIZE = 1000;
//! -paytxfee default
static const CAmount DEFAULT_TRANSACTION_FEE = 0;
//! -fallbackfee default
static const CAmount DEFAULT_FALLBACK_FEE = 1000;
//! -m_discard_rate default
static const CAmount DEFAULT_DISCARD_FEE = 10000;
//! -mintxfee default
static const CAmount DEFAULT_TRANSACTION_MINFEE = 1000;
//! minimum recommended increment for BIP 125 replacement txs
static const CAmount WALLET_INCREMENTAL_RELAY_FEE = 5000;
//! Default for -spendzeroconfchange
static const bool DEFAULT_SPEND_ZEROCONF_CHANGE = true;
//! Default for -walletrejectlongchains
static const bool DEFAULT_WALLET_REJECT_LONG_CHAINS = false;
//! -txconfirmtarget default
static const unsigned int DEFAULT_TX_CONFIRM_TARGET = 6;
static const bool DEFAULT_WALLETBROADCAST = true;
static const bool DEFAULT_DISABLE_WALLET = false;

static const int64_t TIMESTAMP_MIN = 0;

//! if set, all keys will be derived by using BIP39/BIP44
static const bool DEFAULT_USE_HD_WALLET = false;

class CBlockIndex;
class CCoinControl;
class CKey;
class COutput;
class CReserveKey;
class CScript;
class CTxMemPool;
class CBlockPolicyEstimator;
class CWalletTx;
struct FeeCalculation;
enum class FeeEstimateMode;

/** (client) version numbers for particular wallet features */
enum WalletFeature
{
    FEATURE_BASE = 10500, // the earliest version new wallets supports (only useful for getwalletinfo's clientversion output)

    FEATURE_WALLETCRYPT = 40000, // wallet encryption
    FEATURE_COMPRPUBKEY = 60000, // compressed public keys
    FEATURE_HD = 120200,    // Hierarchical key derivation after BIP32 (HD Wallet), BIP44 (multi-coin), BIP39 (mnemonic)
                            // which uses on-the-fly private key derivation

    FEATURE_LATEST = FEATURE_HD
};

struct CompactTallyItem
{
    CTxDestination txdest;
    CAmount nAmount;
    std::vector<CInputCoin> vecInputCoins;
    CompactTallyItem()
    {
        nAmount = 0;
    }
};

struct FuturePartialPayload
{
	CScript futureRecScript;
	int32_t maturity;
	int32_t locktime;
};
/** A key pool entry */
class CKeyPool
{
public:
    int64_t nTime;
    CPubKey vchPubKey;
    bool fInternal; // for change outputs

    CKeyPool();
    CKeyPool(const CPubKey& vchPubKeyIn, bool fInternalIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(nTime);
        READWRITE(vchPubKey);
        if (ser_action.ForRead()) {
            try {
                READWRITE(fInternal);
            }
            catch (std::ios_base::failure&) {
                /* flag as external address if we can't read the internal boolean
                   (this will be the case for any wallet before the HD chain split version) */
                fInternal = false;
            }
        }
        else {
            READWRITE(fInternal);
        }
    }
};

/** Address book data */
class CAddressBookData
{
public:
    std::string name;
    std::string purpose;

    CAddressBookData() : purpose("unknown") {}

    typedef std::map<std::string, std::string> StringMap;
    StringMap destdata;
};

struct CRecipient
{
    CScript scriptPubKey;
    CAmount nAmount;
    bool fSubtractFeeFromAmount;
};

typedef std::map<std::string, std::string> mapValue_t;


static inline void ReadOrderPos(int64_t& nOrderPos, mapValue_t& mapValue)
{
    if (!mapValue.count("n"))
    {
        nOrderPos = -1; // TODO: calculate elsewhere
        return;
    }
    nOrderPos = atoi64(mapValue["n"].c_str());
}


static inline void WriteOrderPos(const int64_t& nOrderPos, mapValue_t& mapValue)
{
    if (nOrderPos == -1)
        return;
    mapValue["n"] = i64tostr(nOrderPos);
}

struct COutputEntry
{
    CTxDestination destination;
    CAmount amount;
    int vout;
};

struct CAssetOutputEntry
{
    std::string assetId;
    CTxDestination destination;
    CAmount nAmount;
    int vout;
};

/** A transaction with a merkle branch linking it to the block chain. */
class CMerkleTx
{
private:
  /** Constant used in hashBlock to indicate tx has been abandoned */
    static const uint256 ABANDON_HASH;

    mutable bool fIsChainlocked{false};
    mutable bool fIsInstantSendLocked{false};

public:
    CTransactionRef tx;
    uint256 hashBlock;

    /* An nIndex == -1 means that hashBlock (in nonzero) refers to the earliest
     * block in the chain we know this or any in-wallet dependency conflicts
     * with. Older clients interpret nIndex == -1 as unconfirmed for backward
     * compatibility.
     */
    int nIndex;

    CMerkleTx()
    {
        SetTx(MakeTransactionRef());
        Init();
    }

    explicit CMerkleTx(CTransactionRef arg)
    {
        SetTx(std::move(arg));
        Init();
    }

    void Init()
    {
        hashBlock = uint256();
        nIndex = -1;
    }

    void SetTx(CTransactionRef arg)
    {
        tx = std::move(arg);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        std::vector<uint256> vMerkleBranch; // For compatibility with older versions.
        READWRITE(tx);
        READWRITE(hashBlock);
        READWRITE(vMerkleBranch);
        READWRITE(nIndex);
    }

    void SetMerkleBranch(const CBlockIndex* pIndex, int posInBlock);

    /**
     * Return depth of transaction in blockchain:
     * <0  : conflicts with a transaction this deep in the blockchain
     *  0  : in memory pool, waiting to be included in a block
     * >=1 : this many blocks deep in the main chain
     */
    int GetDepthInMainChain() const;
    /**
     *
     * @return -1 if it is not yet confirmed otherwise return time it was included in the block
     */
    int64_t GetConfirmationTime() const;
    bool IsInMainChain() const { return GetDepthInMainChain() > 0; }
    bool IsLockedByInstantSend() const;
    bool IsChainLocked() const;
    int GetBlocksToMaturity() const;
    bool hashUnset() const { return (hashBlock.IsNull() || hashBlock == ABANDON_HASH); }
    bool isAbandoned() const { return (hashBlock == ABANDON_HASH); }
    void setAbandoned() { hashBlock = ABANDON_HASH; }

    const uint256& GetHash() const { return tx->GetHash(); }
    bool IsCoinBase() const { return tx->IsCoinBase(); }
};

//Get the marginal bytes of spending the specified output
int CalculateMaximumSignedInputSize(const CTxOut& txout, const CWallet* pwallet);

/**
 * A transaction with a bunch of additional info that only the owner cares about.
 * It includes any unrecorded transactions needed to link it back to the block chain.
 */
class CWalletTx : public CMerkleTx
{
private:
    const CWallet* pwallet;

public:
    /**
     * Key/value map with information about the transaction.
     *
     * The following keys can be read and written through the map and are
     * serialized in the wallet database:
     *
     *     "comment", "to"   - comment strings provided to sendtoaddress,
     *                         sendfrom, sendmany wallet RPCs
     *     "replaces_txid"   - txid (as HexStr) of transaction replaced by
     *                         bumpfee on transaction created by bumpfee
     *     "replaced_by_txid" - txid (as HexStr) of transaction created by
     *                         bumpfee on transaction replaced by bumpfee
     *     "from", "message" - obsolete fields that could be set in UI prior to
     *                         2011 (removed in commit 4d9b223)
     *
     * The following keys are serialized in the wallet database, but shouldn't
     * be read or written through the map (they will be temporarily added and
     * removed from the map during serialization):
     *
     *     "fromaccount"     - serialized strFromAccount value
     *     "n"               - serialized nOrderPos value
     *     "timesmart"       - serialized nTimeSmart value
     *     "spent"           - serialized vfSpent value that existed prior to
     *                         2014 (removed in commit 93a18a3)
     */
    mapValue_t mapValue;
    std::vector<std::pair<std::string, std::string> > vOrderForm;
    unsigned int fTimeReceivedIsTxTime;
    unsigned int nTimeReceived; //!< time received by this node
    /**
     * Stable timestamp that never changes, and reflects the order a transaction
     * was added to the wallet. Timestamp is based on the block time for a
     * transaction added as part of a block, or else the time when the
     * transaction was received if it wasn't part of a block, with the timestamp
     * adjusted in both cases so timestamp order matches the order transactions
     * were added to the wallet. More details can be found in
     * CWallet::ComputeTimeSmart().
     */
    unsigned int nTimeSmart;
    /**
     * From me flag is set to 1 for transactions that were created by the wallet
     * on this bitcoin node, and set to 0 for transactions that were created
     * externally and came in through the network or sendrawtransaction RPC.
     */
    char fFromMe;
    std::string strFromAccount;
    int64_t nOrderPos; //!< position in ordered transaction list
    std::multimap<int64_t, std::pair<CWalletTx*, CAccountingEntry*>>::const_iterator m_it_wtxOrdered;

    // memory only
    mutable bool fDebitCached;
    mutable bool fCreditCached;
    mutable bool fImmatureCreditCached;
    mutable bool fAvailableCreditCached;
    mutable bool fAnonymizedCreditCached;
    mutable bool fDenomUnconfCreditCached;
    mutable bool fDenomConfCreditCached;
    mutable bool fWatchDebitCached;
    mutable bool fWatchCreditCached;
    mutable bool fImmatureWatchCreditCached;
    mutable bool fAvailableWatchCreditCached;
    mutable bool fChangeCached;
    mutable bool fInMempool;
    mutable CAmount nDebitCached;
    mutable CAmount nCreditCached;
    mutable CAmount nImmatureCreditCached;
    mutable CAmount nAvailableCreditCached;
    mutable CAmount nAnonymizedCreditCached;
    mutable CAmount nDenomUnconfCreditCached;
    mutable CAmount nDenomConfCreditCached;
    mutable CAmount nWatchDebitCached;
    mutable CAmount nWatchCreditCached;
    mutable CAmount nImmatureWatchCreditCached;
    mutable CAmount nAvailableWatchCreditCached;
    mutable CAmount nChangeCached;

    CWalletTx(const CWallet* pwalletIn, CTransactionRef arg) : CMerkleTx(std::move(arg))
    {
        Init(pwalletIn);
    }

    void Init(const CWallet* pwalletIn)
    {
        pwallet = pwalletIn;
        mapValue.clear();
        vOrderForm.clear();
        fTimeReceivedIsTxTime = false;
        nTimeReceived = 0;
        nTimeSmart = 0;
        fFromMe = false;
        strFromAccount.clear();
        fDebitCached = false;
        fCreditCached = false;
        fImmatureCreditCached = false;
        fAvailableCreditCached = false;
        fAnonymizedCreditCached = false;
        fDenomUnconfCreditCached = false;
        fDenomConfCreditCached = false;
        fWatchDebitCached = false;
        fWatchCreditCached = false;
        fImmatureWatchCreditCached = false;
        fAvailableWatchCreditCached = false;
        fChangeCached = false;
        fInMempool = false;
        nDebitCached = 0;
        nCreditCached = 0;
        nImmatureCreditCached = 0;
        nAvailableCreditCached = 0;
        nAnonymizedCreditCached = 0;
        nDenomUnconfCreditCached = 0;
        nDenomConfCreditCached = 0;
        nWatchDebitCached = 0;
        nWatchCreditCached = 0;
        nAvailableWatchCreditCached = 0;
        nImmatureWatchCreditCached = 0;
        nChangeCached = 0;
        nOrderPos = -1;
    }

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        char fSpent = false;
        mapValue_t mapValueCopy = mapValue;

        mapValueCopy["fromaccount"] = strFromAccount;
        WriteOrderPos(nOrderPos, mapValueCopy);
        if (nTimeSmart) {
            mapValueCopy["timesmart"] = strprintf("%u", nTimeSmart);
        }

        s << static_cast<const CMerkleTx&>(*this);
        std::vector<CMerkleTx> vUnused; //!< Used to be vtxPrev
        s << vUnused << mapValueCopy << vOrderForm << fTimeReceivedIsTxTime << nTimeReceived << fFromMe << fSpent;
    }

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        Init(nullptr);
        char fSpent;

        s >> static_cast<CMerkleTx&>(*this);
        std::vector<CMerkleTx> vUnused; //!< Used to be vtxPrev
        s >> vUnused >> mapValue >> vOrderForm >> fTimeReceivedIsTxTime >> nTimeReceived >> fFromMe >> fSpent;

        strFromAccount = std::move(mapValue["fromaccount"]);
        ReadOrderPos(nOrderPos, mapValue);
        nTimeSmart = mapValue.count("timesmart") ? (unsigned int)atoi64(mapValue["timesmart"]) : 0;

        mapValue.erase("fromaccount");
        mapValue.erase("spent");
        mapValue.erase("n");
        mapValue.erase("timesmart");
    }

    //! make sure balances are recalculated
    void MarkDirty()
    {
        fCreditCached = false;
        fAvailableCreditCached = false;
        fImmatureCreditCached = false;
        fAnonymizedCreditCached = false;
        fDenomUnconfCreditCached = false;
        fDenomConfCreditCached = false;
        fWatchDebitCached = false;
        fWatchCreditCached = false;
        fAvailableWatchCreditCached = false;
        fImmatureWatchCreditCached = false;
        fDebitCached = false;
        fChangeCached = false;
    }

    void BindWallet(CWallet *pwalletIn)
    {
        pwallet = pwalletIn;
        MarkDirty();
    }

    const CWallet* GetWallet() const
    {
        return pwallet;
    }

    //! filter decides which addresses will count towards the debit
    CAmount GetDebit(const isminefilter& filter, isminefilter* mineTypes = nullptr) const;
    CAmount GetCredit(const isminefilter& filter, isminefilter* mineTypes = nullptr) const;
    CAmount GetImmatureCredit(bool fUseCache=true) const;
    CAmount GetAvailableCredit(bool fUseCache=true, const isminefilter& filter=ISMINE_SPENDABLE) const;
    CAmount GetImmatureWatchOnlyCredit(const bool fUseCache=true) const;
    CAmount GetChange() const;

    CAmount GetAnonymizedCredit(const CCoinControl* coinControl = nullptr) const;
    CAmount GetDenominatedCredit(bool unconfirmed, bool fUseCache=true) const;
    bool isFutureSpendable(unsigned int outputIndex) const;

    // Get the marginal bytes if spending the specified output from this transaction
    int GetSpendSize(unsigned int out) const
    {
        return CalculateMaximumSignedInputSize(tx->vout[out], pwallet);
    }

    void GetAmounts(std::list<COutputEntry>& listReceived,
                    std::list<COutputEntry>& listSent, CAmount& nFee, std::string& strSentAccount, const isminefilter& filter) const;
    
    void GetAmounts(std::list<COutputEntry>& listReceived,
                    std::list<COutputEntry>& listSent, CAmount& nFee, std::string& strSentAccount, const isminefilter& filter, std::list<CAssetOutputEntry>& assetsReceived, std::list<CAssetOutputEntry>& assetsSent) const;

    bool IsFromMe(const isminefilter& filter) const
    {
        return (GetDebit(filter) > 0);
    }

    // True if only scriptSigs are different
    bool IsEquivalentTo(const CWalletTx& tx) const;

    bool InMempool() const;
    bool IsTrusted() const;

    int64_t GetTxTime() const;

    // RelayWalletTransaction may only be called if fBroadcastTransactions!
    bool RelayWalletTransaction(CConnman* connman);

    /** Pass this transaction to the mempool. Fails if absolute fee exceeds absurd fee. */
    bool AcceptToMemoryPool(const CAmount& nAbsurdFee, CValidationState& state);

    std::set<uint256> GetConflicts() const;
};

struct WalletTxHasher
{
    StaticSaltedHasher h;
    size_t operator()(const CWalletTx* a) const
    {
        return h(a->GetHash());
    }
};

struct CompareInputCoinBIP69
{
    inline bool operator()(const CInputCoin& a, const CInputCoin& b) const
    {
        // Note: CInputCoin-s are essentially inputs, their txouts are used for informational purposes only
        // that's why we use CompareInputBIP69 to sort them in a BIP69 compliant way.
        return CompareInputBIP69()(CTxIn(a.outpoint), CTxIn(b.outpoint));
    }
};

class COutput
{
public:
    const CWalletTx *tx;
    int i;
    int nDepth;

    /** Pre-computed estimated size of this output as a fully-signed input in a transaction. Can be -1 if it could not be calculated */
    int nInputBytes;

    /** Whether we have the private keys to spend this output */
    bool fSpendable;

    /** Whether we know how to spend this output, ignoring the lack of keys */
    bool fSolvable;

    /**
     * Whether this output is considered safe to spend. Unconfirmed transactions
     * from outside keys and unconfirmed replacement transactions are considered
     * unsafe and will not be used to fund new spending transactions.
     */
    bool fSafe;

    bool isFuture;
    bool isFutureSpendable;

    COutput(const CWalletTx *txIn, int iIn, int nDepthIn, bool fSpendableIn, bool fSolvableIn, bool fSafeIn, bool future = false, bool futureSpendable = true)
    {
        tx = txIn;
        i = iIn;
        nDepth = nDepthIn;
        fSpendable = fSpendableIn;
        fSolvable = fSolvableIn;
        fSafe = fSafeIn;
        isFuture = future;
        isFutureSpendable = futureSpendable;
    }

    //Used with Darksend. Will return largest nondenom, then denominations, then very small inputs
    int Priority() const;

    std::string ToString() const;
};




/** Private key that includes an expiration date in case it never gets used. */
class CWalletKey
{
public:
    CPrivKey vchPrivKey;
    int64_t nTimeCreated;
    int64_t nTimeExpires;
    std::string strComment;
    //! todo: add something to note what created it (user, getnewaddress, change)
    //!   maybe should have a map<string, string> property map

    explicit CWalletKey(int64_t nExpires=0);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vchPrivKey);
        READWRITE(nTimeCreated);
        READWRITE(nTimeExpires);
        READWRITE(LIMITED_STRING(strComment, 65536));
    }
};

/**
 * DEPRECATED Internal transfers.
 * Database key is acentry<account><counter>.
 */
class CAccountingEntry
{
public:
    std::string strAccount;
    CAmount nCreditDebit;
    int64_t nTime;
    std::string strOtherAccount;
    std::string strComment;
    mapValue_t mapValue;
    int64_t nOrderPos; //!< position in ordered transaction list
    uint64_t nEntryNo;

    CAccountingEntry()
    {
        SetNull();
    }

    void SetNull()
    {
        nCreditDebit = 0;
        nTime = 0;
        strAccount.clear();
        strOtherAccount.clear();
        strComment.clear();
        nOrderPos = -1;
        nEntryNo = 0;
    }

    template <typename Stream>
    void Serialize(Stream& s) const {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH)) {
            s << nVersion;
        }
        //! Note: strAccount is serialized as part of the key, not here.
        s << nCreditDebit << nTime << strOtherAccount;

        mapValue_t mapValueCopy = mapValue;
        WriteOrderPos(nOrderPos, mapValueCopy);

        std::string strCommentCopy = strComment;
        if (!mapValueCopy.empty() || !_ssExtra.empty()) {
            CDataStream ss(s.GetType(), s.GetVersion());
            ss.insert(ss.begin(), '\0');
            ss << mapValueCopy;
            ss.insert(ss.end(), _ssExtra.begin(), _ssExtra.end());
            strCommentCopy.append(ss.str());
        }
        s << strCommentCopy;
    }

    template <typename Stream>
    void Unserialize(Stream& s) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH)) {
            s >> nVersion;
        }
        //! Note: strAccount is serialized as part of the key, not here.
        s >> nCreditDebit >> nTime >> LIMITED_STRING(strOtherAccount, 65536) >> LIMITED_STRING(strComment, 65536);

        size_t nSepPos = strComment.find("\0", 0, 1);
        mapValue.clear();
        if (std::string::npos != nSepPos) {
            CDataStream ss(std::vector<char>(strComment.begin() + nSepPos + 1, strComment.end()), s.GetType(), s.GetVersion());
            ss >> mapValue;
            _ssExtra = std::vector<char>(ss.begin(), ss.end());
        }
        ReadOrderPos(nOrderPos, mapValue);
        if (std::string::npos != nSepPos) {
            strComment.erase(nSepPos);
        }

        mapValue.erase("n");
    }

private:
    std::vector<char> _ssExtra;
};

struct CoinSelectionParams
{
    bool use_bnb = true;
    size_t change_output_size = 0;
    size_t change_spend_size = 0;
    CFeeRate effective_fee = CFeeRate(0);
    size_t tx_noinputs_size = 0;

    CoinSelectionParams(bool use_bnb, size_t change_output_size, size_t change_spend_size, CFeeRate effective_fee, size_t tx_noinputs_size) : use_bnb(use_bnb), change_output_size(change_output_size), change_spend_size(change_spend_size), effective_fee(effective_fee), tx_noinputs_size(tx_noinputs_size) {}
    CoinSelectionParams() {}
};

struct CoinEligibilityFilter
{
    const int conf_mine;
    const int conf_theirs;
    const uint64_t max_ancestors;
    const uint64_t max_descendants;

    CoinEligibilityFilter(int conf_mine, int conf_theirs, uint64_t max_ancestors) : conf_mine(conf_mine), conf_theirs(conf_theirs), max_ancestors(max_ancestors), max_descendants(max_ancestors) {}
    CoinEligibilityFilter(int conf_mine, int conf_theirs, uint64_t max_ancestors, uint64_t max_descendants) : conf_mine(conf_mine), conf_theirs(conf_theirs), max_ancestors(max_ancestors), max_descendants(max_descendants) {}
};

class WalletRescanReserver; //forward declarations for ScanForWalletTransactions/RescanFromTime
/**
 * A CWallet is an extension of a keystore, which also maintains a set of transactions and balances,
 * and provides the ability to create new transactions.
 */
class CWallet final : public CCryptoKeyStore, public CValidationInterface
{
private:
    std::atomic<bool> fAbortRescan{false};
    std::atomic<bool> fScanningWallet{false}; // controlled by WalletRescanReserver
    std::atomic<int64_t> m_scanning_start{0};
    std::atomic<double> m_scanning_progress{0};
    std::mutex mutexScanning;
    friend class WalletRescanReserver;


    /**
     * Select a set of coins such that nValueRet >= nTargetValue and at least
     * all coins from coinControl are selected; Never select unconfirmed coins
     * if they are not ours
     */
    bool SelectCoins(const std::vector<COutput>& vAvailableCoins, const CAmount& nTargetValue, std::set<CInputCoin>& setCoinsRet, CAmount& nValueRet,
                    const CCoinControl& coin_control, const CoinSelectionParams& coin_selection_params, bool& bnb_used) const;
    
    /**
     * Select a set of asset coins such that nValueRet >= nTargetValue and at least
     * all asset coins from coinControl are selected; Never select unconfirmed coins
     * if they are not ours
     */
    bool SelectAssets(const std::map<std::string, std::vector<COutput> >& mapAvailableAssets, const std::map<std::string, CAmount>& mapAssetTargetValue, std::set<CInputCoin>& setCoinsRet, std::map<std::string, CAmount>& nValueRet) const;

    WalletBatch *encrypted_batch = nullptr;

    //! the current wallet version: clients below this version are not able to load the wallet
    int nWalletVersion = FEATURE_BASE;

    //! the maximum wallet format version: memory-only variable that specifies to what version this wallet may be upgraded
    int nWalletMaxVersion = FEATURE_BASE;

    int64_t nNextResend = 0;
    int64_t nLastResend = 0;
    bool fBroadcastTransactions = false;

    mutable bool fAnonymizableTallyCached = false;
    mutable std::vector<CompactTallyItem> vecAnonymizableTallyCached;
    mutable bool fAnonymizableTallyCachedNonDenom = false;
    mutable std::vector<CompactTallyItem> vecAnonymizableTallyCachedNonDenom;

    /**
     * Used to keep track of spent outpoints, and
     * detect and report conflicts (double-spends or
     * mutated transactions where the mutant gets mined).
     */
    typedef std::multimap<COutPoint, uint256> TxSpends;
    TxSpends mapTxSpends;
    void AddToSpends(const COutPoint& outpoint, const uint256& wtxid);
    void AddToSpends(const uint256& wtxid);

    std::set<COutPoint> setWalletUTXO;
    mutable std::map<COutPoint, int> mapOutpointRoundsCache;

    /* Mark a transaction (and its in-wallet descendants) as conflicting with a particular block. */
    void MarkConflicted(const uint256& hashBlock, const uint256& hashTx);

    void SyncMetaData(std::pair<TxSpends::iterator, TxSpends::iterator>);

    /* Used by TransactionAddedToMemorypool/BlockConnected/Disconnected.
     * Should be called with pindexBlock and posInBlock if this is for a transaction that is included in a block. */
    void SyncTransaction(const CTransactionRef& tx, const CBlockIndex *pindex = nullptr, int posInBlock = 0, bool rescanningOldBlock = false) EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    /* HD derive new child key (on internal or external chain) */
    void DeriveNewChildKey(WalletBatch &batch, const CKeyMetadata& metadata, CKey& secretRet, uint32_t nAccountIndex, bool fInternal /*= false*/) EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    std::set<int64_t> setInternalKeyPool;
    std::set<int64_t> setExternalKeyPool;
    int64_t m_max_keypool_index = 0;
    std::map<CKeyID, int64_t> m_pool_key_to_index;

    int64_t nTimeFirstKey = 0;

    /**
     * Private version of AddWatchOnly method which does not accept a
     * timestamp, and which will reset the wallet's nTimeFirstKey value to 1 if
     * the watch key did not previously have a timestamp associated with it.
     * Because this is an inherited virtual method, it is accessible despite
     * being marked private, but it is marked private anyway to encourage use
     * of the other AddWatchOnly which accepts a timestamp and sets
     * nTimeFirstKey more intelligently for more efficient rescans.
     */
    bool AddWatchOnly(const CScript& dest) override EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    /** Wallet location which includes wallet name (see WalletLocation). */
    WalletLocation m_location;

    /** Internal database handle. */
    std::unique_ptr<WalletDatabase> database;

    // Used to NotifyTransactionChanged of the previous block's coinbase when
    // the next block comes in
    uint256 hashPrevBestCoinbase;

    // A helper function which loops through wallet UTXOs
    std::unordered_set<const CWalletTx*, WalletTxHasher> GetSpendableTXs() const EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    /**
     * The following is used to keep track of how far behind the wallet is
     * from the chain sync, and to allow clients to block on us being caught up.
     *
     * Note that this is *not* how far we've processed, we may need some rescan
     * to have seen all transactions in the chain, but is only used to track
     * live BlockConnected callbacks.
     *
     * Protected by cs_main (see BlockUntilSyncedToCurrentChain)
     */
    const CBlockIndex* m_last_block_processed = nullptr;

    /** Pulled from wallet DB ("ps_salt") and used when mixing a random number of rounds.
     *  This salt is needed to prevent an attacker from learning how many extra times
     *  the input was mixed based only on information in the blockchain.
     */
    uint256 nCoinJoinSalt;

    /**
     * Fetches CoinJoin salt from database or generates and saves a new one if no salt was found in the db
     */
    void InitCoinJoinSalt();

public:
    /*
     * Main wallet lock.
     * This lock protects all the fields added by CWallet.
     */
    mutable CCriticalSection cs_wallet;

    /** Get database handle used by this wallet. Ideally this function would
     * not be necessary.
     */
    WalletDatabase& GetDBHandle()
    {
        return *database;
    }

    const WalletLocation& GetLocation() const { return m_location; }

    /** Get a name for this wallet for logging/debugging purposes.
     */
    const std::string& GetName() const { return m_location.GetName(); }

    void LoadKeyPool(int64_t nIndex, const CKeyPool &keypool) EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    // Map from Key ID to key metadata.
    std::map<CKeyID, CKeyMetadata> mapKeyMetadata;

    // Map from Script ID to key metadata (for watch-only keys).
    std::map<CScriptID, CKeyMetadata> m_script_metadata;

    // Map from governance object hash to governance object, they are added by gobject_prepare.
    std::map<uint256, CGovernanceObject> m_gobjects;

    typedef std::map<unsigned int, CMasterKey> MasterKeyMap;
    MasterKeyMap mapMasterKeys;
    unsigned int nMasterKeyMaxID = 0;

    /** Construct wallet with specified name and database implementation. */
    CWallet(const WalletLocation& location, std::unique_ptr<WalletDatabase> database) : m_location(location), database(std::move(database))
    {
    }

    ~CWallet()
    {
        delete encrypted_batch;
        encrypted_batch = nullptr;
    }

    std::map<uint256, CWalletTx> mapWallet;
    std::list<CAccountingEntry> laccentries;

    typedef std::pair<CWalletTx*, CAccountingEntry*> TxPair;
    typedef std::multimap<int64_t, TxPair > TxItems;
    TxItems wtxOrdered;

    int64_t nOrderPosNext = 0;
    uint64_t nAccountingEntryNumber = 0;

    std::map<CTxDestination, CAddressBookData> mapAddressBook;

    std::set<COutPoint> setLockedCoins;

    int64_t nKeysLeftSinceAutoBackup;

    std::map<CKeyID, CHDPubKey> mapHdPubKeys; //<! memory map of HD extended pubkeys

    const CWalletTx* GetWalletTx(const uint256& hash) const;

    //! check whether we are allowed to upgrade (or already support) to the named feature
    bool CanSupportFeature(enum WalletFeature wf) const EXCLUSIVE_LOCKS_REQUIRED(cs_wallet) { AssertLockHeld(cs_wallet); return nWalletMaxVersion >= wf; }

    /**
     * populate vCoins with vector of available COutputs.
     */
    void AvailableCoins(std::vector<COutput>& vCoins, bool fOnlySafe=true, const CCoinControl *coinControl = nullptr, const CAmount& nMinimumAmount = 1, const CAmount& nMaximumAmount = MAX_MONEY, const CAmount& nMinimumSumAmount = MAX_MONEY, const uint64_t nMaximumCount = 0, const int nMinDepth = 0, const int nMaxDepth = 9999999) const;
    void AvailableCoins(std::vector<COutput>& vCoins, std::map<std::string, std::vector<COutput>>& mapAssetCoins, bool fGetRTM = true, bool fOnlyAssets = false, bool fOnlySafe=true, const CCoinControl *coinControl = nullptr, const CAmount& nMinimumAmount = 1, const CAmount& nMaximumAmount = MAX_MONEY, const CAmount& nMinimumSumAmount = MAX_MONEY, const uint64_t nMaximumCount = 0, const int nMinDepth = 0, const int nMaxDepth = 9999999) const EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    void AvailableAssets(std::map<std::string, std::vector<COutput> >& mapAssetCoins, bool fOnlySafe=true, const CCoinControl *coinControl = nullptr, const CAmount& nMinimumAmount = 1, const CAmount& nMaximumAmount = MAX_MONEY, const CAmount& nMinimumSumAmount = MAX_MONEY, const uint64_t nMaximumCount = 0, const int nMinDepth = 0, const int nMaxDepth = 9999999) const;

    /**
     * Return list of available coins and locked coins grouped by non-change output address.
     */
    std::map<CTxDestination, std::vector<COutput>> ListCoins() const;

    /**
     * Find non-change parent output.
     */
    const CTxOut& FindNonChangeParentOutput(const CTransaction& tx, int output) const;

    /**
     * Shuffle and select coins until nTargetValue is reached while avoiding
     * small change; This method is stochastic for some inputs and upon
     * completion the coin set and corresponding actual target value is
     * assembled
     */
    bool SelectCoinsMinConf(const CAmount& nTargetValue, const CoinEligibilityFilter& eligibility_filter, std::vector<COutput> vCoins, std::set<CInputCoin>& setCoinsRet, CAmount& nValueRet, const CoinSelectionParams& coin_selection_params, bool& bnb_used, CoinType nCoinType = CoinType::ALL_COINS) const;
    bool SelectAssetsMinConf(const CAmount& nTargetValue, const CoinEligibilityFilter& eligibility_filter, const std::string& strAssetName, std::vector<COutput> vCoins, std::set<CInputCoin>& setCoinsRet, CAmount& nValueRet) const;

    // Coin selection
    bool SelectTxDSInsByDenomination(int nDenom, CAmount nValueMax, std::vector<CTxDSIn>& vecTxDSInRet);
    bool SelectDenominatedAmounts(CAmount nValueMax, std::set<CAmount>& setAmountsRet) const;

    bool SelectCoinsGroupedByAddresses(std::vector<CompactTallyItem>& vecTallyRet, bool fSkipDenominated = true, bool fAnonymizable = true, bool fSkipUnconfirmed = true, int nMaxOupointsPerAddress = -1) const;

    /// Get collateral RTM output and keys which can be used for the Smartnode
    bool GetSmartnodeOutpointAndKeys(COutPoint& outpointRet, CPubKey& pubKeyRet, CKey& keyRet, const std::string& strTxHash = "", const std::string& strOutputIndex = "");
    /// Extract txin information and keys from output
    bool GetOutpointAndKeysFromOutput(const COutput& out, COutPoint& outpointRet, CPubKey& pubKeyRet, CKey& keyRet);

    bool HasCollateralInputs(bool fOnlyConfirmed = true) const;
    int  CountInputsWithAmount(CAmount nInputAmount) const;

    // get the CoinJoin chain depth for a given input
    int GetRealOutpointCoinJoinRounds(const COutPoint& outpoint, int nRounds = 0) const;
    // respect current settings
    int GetCappedOutpointCoinJoinRounds(const COutPoint& outpoint) const;

    bool IsDenominated(const COutPoint& outpoint) const;
    bool IsFullyMixed(const COutPoint& outpoint) const;

    bool IsSpent(const uint256& hash, unsigned int n) const;

    bool IsLockedCoin(uint256 hash, unsigned int n) const EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    void LockCoin(const COutPoint& output) EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    void UnlockCoin(const COutPoint& output) EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    void UnlockAllCoins() EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    void ListLockedCoins(std::vector<COutPoint>& vOutpts) const EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    void GetProTxCoins(const CDeterministicMNList& mnList, std::vector<COutPoint>& vOutpts) const EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    void ListProTxCoins(int height, std::vector<COutPoint>& vOutpts)  const EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    void ListProTxCoins(std::vector<COutPoint>& vOutpts) const EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    /*
     * Rescan abort properties
     */
    void AbortRescan() { fAbortRescan = true; }
    bool IsAbortingRescan() { return fAbortRescan; }
    bool IsScanning() { return fScanningWallet; }
    int64_t ScanningDuration() const { return fScanningWallet ? GetTimeMillis() - m_scanning_start : 0; }
    double ScanningProgress() const { return fScanningWallet ? (double) m_scanning_progress : 0; }

    /**
     * keystore implementation
     * Generate a new key
     */
    CPubKey GenerateNewKey(WalletBatch& batch, uint32_t nAccountIndex, bool fInternal /*= false*/) EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    //! HaveKey implementation that also checks the mapHdPubKeys
    bool HaveKey(const CKeyID &address) const override;
    //! GetPubKey implementation that also checks the mapHdPubKeys
    bool GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const override;
    //! GetKey implementation that can derive a HD private key on the fly
    bool GetKey(const CKeyID &address, CKey& keyOut) const override;
    //! Adds a HDPubKey into the wallet(database)
    bool AddHDPubKey(WalletBatch &batch, const CExtPubKey &extPubKey, bool fInternal);
    //! loads a HDPubKey into the wallets memory
    bool LoadHDPubKey(const CHDPubKey &hdPubKey) EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    //! Adds a key to the store, and saves it to disk.
    bool AddKeyPubKey(const CKey& key, const CPubKey &pubkey) override EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    bool AddKeyPubKeyWithDB(WalletBatch &batch, const CKey& key, const CPubKey &pubkey) EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    //! Adds a key to the store, without saving it to disk (used by LoadWallet)
    bool LoadKey(const CKey& key, const CPubKey &pubkey) { return CCryptoKeyStore::AddKeyPubKey(key, pubkey); }
    //! Load metadata (used by LoadWallet)
    bool LoadKeyMetadata(const CKeyID& keyID, const CKeyMetadata &metadata) EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    bool LoadScriptMetadata(const CScriptID& script_id, const CKeyMetadata &metadata) EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    bool LoadMinVersion(int nVersion) EXCLUSIVE_LOCKS_REQUIRED(cs_wallet) { AssertLockHeld(cs_wallet); nWalletVersion = nVersion; nWalletMaxVersion = std::max(nWalletMaxVersion, nVersion); return true; }
    void UpdateTimeFirstKey(int64_t nCreateTime) EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    int64_t GetTimeFirstKey() const EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    //! Adds an encrypted key to the store, and saves it to disk.
    bool AddCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret) override;
    //! Adds an encrypted key to the store, without saving it to disk (used by LoadWallet)
    bool LoadCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret);
    bool AddCScript(const CScript& redeemScript) override;
    bool LoadCScript(const CScript& redeemScript);

    //! Adds a destination data tuple to the store, and saves it to disk
    bool AddDestData(const CTxDestination &dest, const std::string &key, const std::string &value);
    //! Erases a destination data tuple in the store and on disk
    bool EraseDestData(const CTxDestination &dest, const std::string &key);
    //! Adds a destination data tuple to the store, without saving it to disk
    bool LoadDestData(const CTxDestination &dest, const std::string &key, const std::string &value);
    //! Look up a destination data tuple in the store, return true if found false otherwise
    bool GetDestData(const CTxDestination &dest, const std::string &key, std::string *value) const;
    //! Get all destination values matching a prefix.
    std::vector<std::string> GetDestValues(const std::string& prefix) const;

    //! Adds a watch-only address to the store, and saves it to disk.
    bool AddWatchOnly(const CScript& dest, int64_t nCreateTime) EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    bool RemoveWatchOnly(const CScript &dest) override EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    //! Adds a watch-only address to the store, without saving it to disk (used by LoadWallet)
    bool LoadWatchOnly(const CScript &dest);

    //! Holds a timestamp at which point the wallet is scheduled (externally) to be relocked. Caller must arrange for actual relocking to occur via Lock().
    int64_t nRelockTime = 0;

    bool Unlock(const SecureString& strWalletPassphrase, bool fForMixingOnly = false);
    bool ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase);
    bool EncryptWallet(const SecureString& strWalletPassphrase);

    void GetKeyBirthTimes(std::map<CTxDestination, int64_t> &mapKeyBirth) const EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    unsigned int ComputeTimeSmart(const CWalletTx& wtx, bool rescanningOldBlock) const;

    /**
     * Increment the next transaction order id
     * @return next transaction order id
     */
    int64_t IncOrderPosNext(WalletBatch *batch = nullptr) EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    DBErrors ReorderTransactions();
    bool AccountMove(std::string strFrom, std::string strTo, CAmount nAmount, std::string strComment = "") EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    bool GetLabelDestination(CTxDestination &dest, const std::string& label, bool bForceNew = false);

    void MarkDirty();
    bool AddToWallet(const CWalletTx& wtxIn, bool fFlushOnClose=true, bool rescanningOldBlock = false);
    bool LoadToWallet(const CWalletTx& wtxIn);
    void TransactionAddedToMempool(const CTransactionRef& tx, int64_t nAcceptTime) override;
    void BlockConnected(const std::shared_ptr<const CBlock>& pblock, const CBlockIndex *pindex, const std::vector<CTransactionRef>& vtxConflicted) override;
    void BlockDisconnected(const std::shared_ptr<const CBlock>& pblock, const CBlockIndex* pindexDisconnected) override;
    bool AddToWalletIfInvolvingMe(const CTransactionRef& tx, const CBlockIndex* pIndex, int posInBlock, bool fUpdate, bool rescanningOldBlock) EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    int64_t RescanFromTime(int64_t startTime, const WalletRescanReserver& reserver, bool update);
    CBlockIndex* ScanForWalletTransactions(CBlockIndex* pindexStart, CBlockIndex* pindexStop, const WalletRescanReserver& reserver, bool fUpdate = false);
    void TransactionRemovedFromMempool(const CTransactionRef &ptx, MemPoolRemovalReason reason) override;
    void ReacceptWalletTransactions();
    void ResendWalletTransactions(int64_t nBestBlockTime, CConnman* connman) override;
    // ResendWalletTransactionsBefore may only be called if fBroadcastTransactions!
    std::vector<uint256> ResendWalletTransactionsBefore(int64_t nTime, CConnman* connman);
    CAmount GetBalance(const isminefilter& filter=ISMINE_SPENDABLE, const int min_depth=0, bool fAddLocked = false) const;
    CAmount GetUnconfirmedBalance() const;
    CAmount GetImmatureBalance() const;
    CAmount GetUnconfirmedWatchOnlyBalance() const;
    CAmount GetImmatureWatchOnlyBalance() const;
    CAmount GetLegacyBalance(const isminefilter& filter, int minDepth, const std::string* account, const bool fAddLocked) const;

    CAmount GetAnonymizableBalance(bool fSkipDenominated = false, bool fSkipUnconfirmed = true) const;
    CAmount GetAnonymizedBalance(const CCoinControl* coinControl = nullptr) const;
    float GetAverageAnonymizedRounds() const;
    CAmount GetNormalizedAnonymizedBalance() const;
    CAmount GetDenominatedBalance(bool unconfirmed=false) const;

    bool GetBudgetSystemCollateralTX(CTransactionRef& tx, uint256 hash, CAmount amount, const COutPoint& outpoint=COutPoint()/*defaults null*/);
    CAmount GetAvailableBalance(const CCoinControl* coinControl = nullptr) const;

    /**
     * Insert additional inputs into the transaction by
     * calling CreateTransaction();
     */
    bool FundTransaction(CMutableTransaction& tx, CAmount& nFeeRet, int& nChangePosInOut, std::string& strFailReason, bool lockUnspents, const std::set<int>& setSubtractFeeFromOutputs, CCoinControl);
    bool SignTransaction(CMutableTransaction& tx) EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    /**
     * Create a new transaction paying the recipients with a set of coins
     * selected by SelectCoins(); Also create the change output, when needed
     * @note passing nChangePosInOut as -1 will result in setting a random position
     */
    bool CreateTransaction(const std::vector<CRecipient>& vecSend, CTransactionRef& tx, CReserveKey& reservekey, CAmount& nFeeRet, int& nChangePosInOut, std::string& strFailReason, const CCoinControl& coin_control, bool sign = true, int nExtraPayloadSize = 0, FuturePartialPayload* fpp = nullptr, CNewAssetTx* newasset = nullptr, CMintAssetTx* mint = nullptr);
    bool CommitTransaction(CTransactionRef tx, mapValue_t mapValue, std::vector<std::pair<std::string, std::string>> orderForm, std::string fromAccount, CReserveKey& reservekey, CConnman* connman, CValidationState& state);

    bool CreateCollateralTransaction(CMutableTransaction& txCollateral, std::string& strReason);

    void ListAccountCreditDebit(const std::string& strAccount, std::list<CAccountingEntry>& entries);
    bool AddAccountingEntry(const CAccountingEntry&);
    bool AddAccountingEntry(const CAccountingEntry&, WalletBatch *batch);
    bool DummySignTx(CMutableTransaction &txNew, const std::set<CTxOut> &txouts) const
    {
        std::vector<CTxOut> v_txouts(txouts.size());
        std::copy(txouts.begin(), txouts.end(), v_txouts.begin());
        return DummySignTx(txNew, v_txouts);
    }
    bool DummySignTx(CMutableTransaction &txNew, const std::vector<CTxOut> &txouts) const;
    bool DummySignInput(CTxIn &tx_in, const CTxOut &txout) const;

    static CFeeRate minTxFee;
    static CFeeRate fallbackFee;
    static CFeeRate m_discard_rate;

    bool NewKeyPool();
    size_t KeypoolCountExternalKeys() EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    size_t KeypoolCountInternalKeys() EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    bool TopUpKeyPool(unsigned int kpSize = 0);
    void ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool, bool fInternal);
    void KeepKey(int64_t nIndex);
    void ReturnKey(int64_t nIndex, bool fInternal, const CPubKey& pubkey);
    bool GetKeyFromPool(CPubKey &key, bool fInternal /*= false*/);
    int64_t GetOldestKeyPoolTime();
    /**
     * Marks all keys in the keypool up to and including reserve_key as used.
     */
    void MarkReserveKeysAsUsed(int64_t keypool_id) EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    const std::map<CKeyID, int64_t>& GetAllReserveKeys() const { return m_pool_key_to_index; }

    std::set<std::set<CTxDestination>> GetAddressGroupings() EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);
    std::map<CTxDestination, CAmount> GetAddressBalances();

    std::set<CTxDestination> GetLabelAddresses(const std::string& label) const;
    void DeleteLabel(const std::string& label);

    isminetype IsMine(const CTxIn& txin) const;
    /**
     * Returns amount of debit if the input matches the
     * filter, otherwise returns 0
     */
    CAmount GetDebit(const CTxIn& txin, const isminefilter& filter, isminefilter* mineTypes = nullptr) const;
    isminetype IsMine(const CTxOut& txout) const;
    CAmount GetCredit(const CTxOut& txout, const isminefilter& filter, isminefilter* mineTypes = nullptr) const;
    bool IsChange(const CTxOut& txout) const;
    CAmount GetChange(const CTxOut& txout) const;
    bool IsMine(const CTransaction& tx) const;
    /** should probably be renamed to IsRelevantToMe */
    bool IsFromMe(const CTransaction& tx) const;
    CAmount GetDebit(const CTransaction& tx, const isminefilter& filter, isminefilter* mineTypes = nullptr) const;
    /** Returns whether all of the inputs match the filter */
    bool IsAllFromMe(const CTransaction& tx, const isminefilter& filter) const;
    CAmount GetCredit(const CTransaction& tx, const isminefilter& filter, isminefilter* mineTypes = nullptr) const;
    CAmount GetChange(const CTransaction& tx) const;
    void SetBestChain(const CBlockLocator& loc) override;

    DBErrors LoadWallet(bool& fFirstRunRet);
    void AutoLockSmartnodeCollaterals();
    DBErrors ZapWalletTx(std::vector<CWalletTx>& vWtx);
    DBErrors ZapSelectTx(std::vector<uint256>& vHashIn, std::vector<uint256>& vHashOut) EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    bool SetAddressBook(const CTxDestination& address, const std::string& strName, const std::string& purpose);

    bool DelAddressBook(const CTxDestination& address);

    const std::string& GetLabelName(const CScript& scriptPubKey) const;

    void GetScriptForMining(std::shared_ptr<CReserveScript> &script);

    unsigned int GetKeyPoolSize() EXCLUSIVE_LOCKS_REQUIRED(cs_wallet)
    {
        AssertLockHeld(cs_wallet); // set{Ex,In}ternalKeyPool
        return setInternalKeyPool.size() + setExternalKeyPool.size();
    }

    //! signify that a particular wallet feature is now used. this may change nWalletVersion and nWalletMaxVersion if those are lower
    bool SetMinVersion(enum WalletFeature, WalletBatch* batch_in = nullptr, bool fExplicit = false);

    //! change which version we're allowed to upgrade to (note that this does not immediately imply upgrading to that format)
    bool SetMaxVersion(int nVersion);

    //! get the current wallet format (the oldest client version guaranteed to understand this wallet)
    int GetVersion() { LOCK(cs_wallet); return nWalletVersion; }

    //! Get wallet transactions that conflict with given transaction (spend same outputs)
    std::set<uint256> GetConflicts(const uint256& txid) const EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    //! Flush wallet (bitdb flush)
    void Flush(bool shutdown=false);

    /** Wallet is about to be unloaded */
    boost::signals2::signal<void ()> NotifyUnload;

    /**
     * Address book entry changed.
     * @note called with lock cs_wallet held.
     */
    boost::signals2::signal<void (CWallet *wallet, const CTxDestination
            &address, const std::string &label, bool isMine,
            const std::string &purpose,
            ChangeType status)> NotifyAddressBookChanged;

    /**
     * Wallet transaction added, removed or updated.
     * @note called with lock cs_wallet held.
     */
    boost::signals2::signal<void (CWallet *wallet, const uint256 &hashTx,
            ChangeType status)> NotifyTransactionChanged;

    /** Show progress e.g. for rescan */
    boost::signals2::signal<void (const std::string &title, int nProgress)> ShowProgress;

    /** Watch-only address added */
    boost::signals2::signal<void (bool fHaveWatchOnly)> NotifyWatchonlyChanged;

    /** IS-lock received */
    boost::signals2::signal<void ()> NotifyISLockReceived;

    /** ChainLock received */
    boost::signals2::signal<void (int height)> NotifyChainLockReceived;

    /** Inquire whether this wallet broadcasts transactions. */
    bool GetBroadcastTransactions() const { return fBroadcastTransactions; }
    /** Set whether this wallet broadcasts transactions. */
    void SetBroadcastTransactions(bool broadcast) { fBroadcastTransactions = broadcast; }

    /** Return whether transaction can be abandoned */
    bool TransactionCanBeAbandoned(const uint256& hashTx) const;

    /* Mark a transaction (and it in-wallet descendants) as abandoned so its inputs may be respent. */
    bool AbandonTransaction(const uint256& hashTx);

    //! Verify wallet naming and perform salvage on the wallet if required
    static bool Verify(const WalletLocation& location, bool salvage_wallet, std::string& error_string, std::string& warning_string);

    /* Initializes the wallet, returns a new CWallet instance or a null pointer in case of an error */
    static std::shared_ptr<CWallet> CreateWalletFromFile(const WalletLocation& location);

    /**
     * Wallet post-init setup
     * Gives the wallet a chance to register repetitive tasks and complete post-init tasks
     */
    void postInitProcess();

    /* AutoBackup functionality */
    static bool InitAutoBackup();
    bool AutoBackupWallet(const fs::path& wallet_path, std::string& strBackupWarningRet, std::string& strBackupErrorRet);

    bool BackupWallet(const std::string& strDest);

    /**
     * HD Wallet Functions
     */

    /* Returns true if HD is enabled */
    bool IsHDEnabled() const;
    /* Generates a new HD chain */
    void GenerateNewHDChain(const SecureString& secureMnemonic, const SecureString& secureMnemonicPassphrase);
    bool GenerateNewHDChainEncrypted(const SecureString& secureMnemonic, const SecureString& secureMnemonicPassphrase, const SecureString& secureWalletPassphrase);
    /* Set the HD chain model (chain child index counters) */
    bool SetHDChain(WalletBatch &batch, const CHDChain& chain, bool memonly);
    bool SetCryptedHDChain(WalletBatch &batch, const CHDChain& chain, bool memonly);
    /**
     * Set the HD chain model (chain child index counters) using temporary wallet db object
     * which causes db flush every time these methods are used
     */
    bool SetHDChainSingle(const CHDChain& chain, bool memonly);
    bool SetCryptedHDChainSingle(const CHDChain& chain, bool memonly);

    bool GetDecryptedHDChain(CHDChain& hdChainRet);

    void NotifyTransactionLock(const CTransactionRef &tx, const std::shared_ptr<const llmq::CInstantSendLock>& islock) override;
    void NotifyChainLock(const CBlockIndex* pindexChainLock, const std::shared_ptr<const llmq::CChainLockSig>& clsig) override;

    /** Load a CGovernanceObject into m_gobjects. */
    bool LoadGovernanceObject(const CGovernanceObject& obj);
    /** Store a CGovernanceObject in the wallet database. This should only be used by governance objects that are created by this wallet via `gobject prepare`. */
    bool WriteGovernanceObject(const CGovernanceObject& obj);
    /** Returns a vector containing pointers to the governance objects in m_gobjects */
    std::vector<const CGovernanceObject*> GetGovernanceObjects();

    /**
     * Blocks until the wallet state is up-to-date to /at least/ the current
     * chain at the time this function is entered
     * Obviously holding cs_main/cs_wallet when going into this call may cause
     * deadlock
     */
    void BlockUntilSyncedToCurrentChain() LOCKS_EXCLUDED(cs_wallet);

    /** Whether a given output is spendable by this wallet */
    bool OutputEligibleForSpending(const COutput& output, const CoinEligibilityFilter& eligibility_filter) const;
};

/** A key allocated from the key pool. */
class CReserveKey final : public CReserveScript
{
protected:
    CWallet* pwallet;
    int64_t nIndex;
    CPubKey vchPubKey;
    bool fInternal;
public:
    explicit CReserveKey(CWallet* pwalletIn)
    {
        nIndex = -1;
        pwallet = pwalletIn;
        fInternal = false;
    }

    CReserveKey() = default;
    CReserveKey(const CReserveKey&) = delete;
    CReserveKey& operator=(const CReserveKey&) = delete;

    ~CReserveKey()
    {
        ReturnKey();
    }

    void ReturnKey();
    bool GetReservedKey(CPubKey &pubkey, bool fInternalIn /*= false*/);
    void KeepKey();
    void KeepScript() override { KeepKey(); }
};


/**
 * DEPRECATED Account information.
 * Stored in wallet with key "acc"+string account name.
 */
class CAccount
{
public:
    CPubKey vchPubKey;

    CAccount()
    {
        SetNull();
    }

    void SetNull()
    {
        vchPubKey = CPubKey();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vchPubKey);
    }
};


/** RAII object to check and reserve a wallet rescan */
class WalletRescanReserver
{
private:
    CWallet* m_wallet;
    bool m_could_reserve;
public:
    explicit WalletRescanReserver(CWallet* w) : m_wallet(w), m_could_reserve(false) {}

    bool reserve()
    {
        assert(!m_could_reserve);
        std::lock_guard<std::mutex> lock(m_wallet->mutexScanning);
        if (m_wallet->fScanningWallet) {
            return false;
        }
        m_wallet->m_scanning_start = GetTimeMillis();
        m_wallet->m_scanning_progress = 0;
        m_wallet->fScanningWallet = true;
        m_could_reserve = true;
        return true;
    }

    bool isReserved() const
    {
        return (m_could_reserve && m_wallet->fScanningWallet);
    }

    ~WalletRescanReserver()
    {
        std::lock_guard<std::mutex> lock(m_wallet->mutexScanning);
        if (m_could_reserve) {
            m_wallet->fScanningWallet = false;
        }
    }
};

// Calculate the size of the transaction assuming all signatures are max size
// Use DummySignatureCreator, which inserts 72 byte signatures everywhere.
// NOTE: this requires that all inputs must be in mapWallet (eg the tx should
// be IsAllFromMe).
int64_t CalculateMaximumSignedTxSize(const CTransaction &tx, const CWallet *wallet);
int64_t CalculateMaximumSignedTxSize(const CTransaction &tx, const CWallet *wallet, const std::vector<CTxOut>& txouts);
#endif // BITCOIN_WALLET_WALLET_H
