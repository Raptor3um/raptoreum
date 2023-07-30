// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_WALLET_H
#define BITCOIN_WALLET_WALLET_H

#include <amount.h>
#include <interfaces/chain.h>
#include <interfaces/handler.h>
#include <policy/feerate.h>
#include <saltedhasher.h>
#include <streams.h>
#include <tinyformat.h>
#include <ui_interface.h>
#include <util/system.h>
#include <util/strencodings.h>
#include <validationinterface.h>
#include <wallet/coincontrol.h>
#include <wallet/crypter.h>
#include <wallet/coinselection.h>
#include <wallet/ismine.h>
#include <wallet/walletdb.h>
#include <wallet/walletutil.h>
#include <wallet/rpcwallet.h>

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

using LoadWalletFn = std::function<void(std::unique_ptr < interfaces::Wallet > wallet)>;

//! Explicitly unload and delete the wallet.
//  Blocks the current thread after signaling the unload intent so that all
//  wallet clients release the wallet.
//  Note that, when blocking is not required, the wallet is implicitly unloaded
//  by the shared pointer deleter.
void UnloadWallet(std::shared_ptr <CWallet> &&wallet);

bool AddWallet(const std::shared_ptr <CWallet> &wallet);

bool RemoveWallet(const std::shared_ptr <CWallet> &wallet);

std::vector <std::shared_ptr<CWallet>> GetWallets();

std::shared_ptr <CWallet> GetWallet(const std::string &name);

std::shared_ptr <CWallet>
LoadWallet(interfaces::Chain &chain, const WalletLocation &location, std::string &error, std::string &warning);

std::unique_ptr <interfaces::Handler> HandleLoadWallet(LoadWalletFn load_wallet);
CWallet *GetFirstWallet();

enum class WalletCreationStatus {
    SUCCESS,
    CREATION_FAILED,
    ENCRYPTION_FAILED
};

WalletCreationStatus
CreateWallet(interfaces::Chain &chain, const SecureString &passphrase, uint64_t wallet_creation_flags,
             const std::string &name, std::string &error, std::string &warning, std::shared_ptr <CWallet> &result);

static const unsigned int DEFAULT_KEYPOOL_SIZE = 1000;
//! -paytxfee default
constexpr CAmount
DEFAULT_PAY_TX_FEE = 0;
//! -fallbackfee default
static const CAmount DEFAULT_FALLBACK_FEE = 1000;
//! -discardfee default
static const CAmount DEFAULT_DISCARD_FEE = 10000;
//! -mintxfee default
static const CAmount DEFAULT_TRANSACTION_MINFEE = 1000;
//! minimum recommended increment for BIP 125 replacement txs
static const CAmount WALLET_INCREMENTAL_RELAY_FEE = 5000;
//! Default for -spendzeroconfchange
static const bool DEFAULT_SPEND_ZEROCONF_CHANGE = true;
//! Default for -walletrejectlongchains
static const bool DEFAULT_WALLET_REJECT_LONG_CHAINS = false;
//! Default for -avoidpartialspends
static const bool DEFAULT_AVOIDPARTIALSPENDS = false;
//! -txconfirmtarget default
static const unsigned int DEFAULT_TX_CONFIRM_TARGET = 6;
static const bool DEFAULT_WALLETBROADCAST = true;
static const bool DEFAULT_DISABLE_WALLET = false;
//! -maxtxfee default
static const CAmount DEFAULT_TRANSACTION_MAXFEE = COIN / 10;
//! Discourage users to set fees higher than this amount (in duffs) per kB
static const CAmount HIGH_TX_FEE_PER_KB = COIN / 100;
//! -maxtxfee will warn if called with a higher fee than this amount (in duffs)
static const CAmount HIGH_MAX_TX_FEE = 100 * HIGH_TX_FEE_PER_KB;

//! if set, all keys will be derived by using BIP39/BIP44
static const bool DEFAULT_USE_HD_WALLET = false;

class CCoinControl;

class CKey;

class COutput;

class CReserveKey;

class CScript;

class CTxDSIn;

class CWalletTx;

struct FeeCalculation;
enum class FeeEstimateMode;

/** (client) version numbers for particular wallet features */
enum WalletFeature {
    FEATURE_BASE = 10500, // the earliest version new wallets supports (only useful for getwalletinfo's clientversion output)

    FEATURE_WALLETCRYPT = 40000, // wallet encryption
    FEATURE_COMPRPUBKEY = 60000, // compressed public keys
    FEATURE_HD = 120200,    // Hierarchical key derivation after BIP32 (HD Wallet), BIP44 (multi-coin), BIP39 (mnemonic)
    // which uses on-the-fly private key derivation

    FEATURE_LATEST = FEATURE_HD
};

struct CompactTallyItem {
    CTxDestination txdest;
    CAmount nAmount{0};
    std::vector <CInputCoin> vecInputCoins;

    CompactTallyItem() = default;
};

struct FuturePartialPayload {
    CScript futureRecScript;
    int32_t maturity;
    int32_t locktime;
};

enum WalletFlags : uint64_t {
    // wallet flags in the upper section (> 1 << 31) will lead to not opening the wallet if flag is unknown
    // unknown wallet flags in the lower section <= (1 << 31) will be tolerated

    // Indicates that the metadata has already been upgraded to contain key origins
    WALLET_FLAG_KEY_ORIGIN_METADATA = (1ULL << 1),

    // will enforce the rule that the wallet can't contain any private keys (only watch-only/pubkeys)
    WALLET_FLAG_DISABLE_PRIVATE_KEYS = (1ULL << 32),

    //! Flag set when a wallet contains no HD seed and no private keys, scripts,
    //! addresses, and other watch only things, and is therefore "blank."
    //!
    //! The only function this flag serves is to distinguish a blank wallet from
    //! a newly created wallet when the wallet database is loaded, to avoid
    //! initialization that should only happen on first run.
    //!
    //! This flag is also a mandatory flag to prevent previous versions of
    //! bitcoin from opening the wallet, thinking it was newly created, and
    //! then improperly reinitializing it.
    WALLET_FLAG_BLANK_WALLET = (1ULL << 33),
};

static constexpr uint64_t
g_known_wallet_flags = WALLET_FLAG_DISABLE_PRIVATE_KEYS | WALLET_FLAG_BLANK_WALLET | WALLET_FLAG_KEY_ORIGIN_METADATA;

/** A key pool entry */
class CKeyPool {
public:
    int64_t nTime;
    CPubKey vchPubKey;
    bool fInternal; // for change outputs

    CKeyPool();

    CKeyPool(const CPubKey &vchPubKeyIn, bool fInternalIn);

    template<typename Stream>
    void Serialize(Stream &s) const {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH)) {
            s << nVersion;
        }
        s << nTime << vchPubKey << fInternal;
    }

    template<typename Stream>
    void Unserialize(Stream &s) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH)) {
            s >> nVersion;
        }
        s >> nTime >> vchPubKey;
        try {
            s >> fInternal;
        } catch (std::ios_base::failure &) {
            /* flag as external address if we can't read the internal boolean
               (this will be the case for any wallet vefore the HD chain split version) */
            fInternal = false;
        }
    }
};

/** Address book data */
class CAddressBookData {
public:
    std::string name;
    std::string purpose;

    CAddressBookData() : purpose("unknown") {}

    typedef std::map <std::string, std::string> StringMap;
    StringMap destdata;
};

struct CRecipient {
    CScript scriptPubKey;
    CAmount nAmount;
    bool fSubtractFeeFromAmount;
};

typedef std::map <std::string, std::string> mapValue_t;


static inline void ReadOrderPos(int64_t &nOrderPos, mapValue_t &mapValue) {
    if (!mapValue.count("n")) {
        nOrderPos = -1; // TODO: calculate elsewhere
        return;
    }
    nOrderPos = atoi64(mapValue["n"].c_str());
}


static inline void WriteOrderPos(const int64_t &nOrderPos, mapValue_t &mapValue) {
    if (nOrderPos == -1)
        return;
    mapValue["n"] = i64tostr(nOrderPos);
}

struct COutputEntry {
    CTxDestination destination;
    CAmount amount;
    int vout;
};

struct CAssetOutputEntry {
    std::string assetId;
    CTxDestination destination;
    CAmount nAmount;
    int vout;
};

/** Legacy class used for deserializing vtxPrev for backwards compatibility.
 * vtxPrev was removed in commit 93a18a3650292afbb441a47d1fa1b94aeb0164e3,
 * but old wallet.dat files may still contain vtxPrev vectors of CMerkleTxs.
 * These need to get deserialized for field alignment when deserializing
 * a CWalletTx, but the deserialized values are discarded.**/
class CMerkleTx {
public:
    template<typename Stream>
    void Unserialize(Stream &s) {
        CTransactionRef tx;
        uint256 hashBlock;
        std::vector <uint256> vMerkleBranch;
        int nIndex;

        s >> tx >> hashBlock >> vMerkleBranch >> nIndex;
    }
};

//Get the marginal bytes of spending the specified output
int CalculateMaximumSignedInputSize(const CTxOut &txout, const CWallet *pwallet);

/**
 * A transaction with a bunch of additional info that only the owner cares about.
 * It includes any unrecorded transactions needed to link it back to the block chain.
 */
class CWalletTx {
private:
    const CWallet *pwallet;

    /** Constant used in hashBlock to indicate tx has been abandoned, only used at
     * serialization/deserialization to avoid ambiguity with conflicted.
     */
    static const uint256 ABANDON_HASH;

    mutable bool fIsChainlocked{false};
    mutable bool fIsInstantSendLocked{false};

public:
    /**
     * Key/value map with information about the transaction.
     *
     * The following keys can be read and written through the map and are
     * serialized in the wallet database:
     *
     *     "comment", "to"   - comment strings provided to sendtoaddress,
     *                         and sendmany wallet RPCs
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
    std::vector <std::pair<std::string, std::string>> vOrderForm;
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
    int64_t nOrderPos; //!< position in ordered transaction list
    std::multimap<int64_t, CWalletTx *>::const_iterator m_it_wtxOrdered;

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

    CWalletTx(const CWallet *pwalletIn, CTransactionRef arg)
            : tx(std::move(arg)) {
        Init(pwalletIn);
    }

    void Init(const CWallet *pwalletIn) {
        pwallet = pwalletIn;
        mapValue.clear();
        vOrderForm.clear();
        fTimeReceivedIsTxTime = false;
        nTimeReceived = 0;
        nTimeSmart = 0;
        fFromMe = false;
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
        m_confirm = Confirmation{};
    }

    CTransactionRef tx;

    /* New transactions start as UNCONFIRMED. At BlockConnected,
     * they will transition to CONFIRMED. In case of reorg, at BlockDisconnected,
     * they roll back to UNCONFIRMED. If we detect a conflicting transaction at
     * block connection, we update conflicted tx and its dependencies as CONFLICTED.
     * If tx isn't confirmed and outside of mempool, the user may switch it to ABANDONED
     * by using the abandontransaction call. This last status may be override by a CONFLICTED
     * or CONFIRMED transition.
     */
    enum Status {
        UNCONFIRMED,
        CONFIRMED,
        CONFLICTED,
        ABANDONED
    };

    /* Confirmation includes tx status and a triplet of {block height/block hash/tx index in block}
     * at which tx has been confirmed. All three are set to 0 if tx is unconfirmed or abandoned.
     * Meaning of these fields changes with CONFLICTED state where they instead point to block hash
     * and block height of the deepest conflicting tx.
     */
    struct Confirmation {
        Status status;
        int block_height;
        uint256 hashBlock;
        int nIndex;

        Confirmation(Status s = UNCONFIRMED, int b = 0, uint256 h = uint256(), int i = 0) : status(s), block_height(b),
                                                                                            hashBlock(h), nIndex(i) {}
    };

    Confirmation m_confirm;

    template<typename Stream>
    void Serialize(Stream &s) const {
        mapValue_t mapValueCopy = mapValue;

        mapValueCopy["fromaccount"] = "";
        WriteOrderPos(nOrderPos, mapValueCopy);
        if (nTimeSmart) {
            mapValueCopy["timesmart"] = strprintf("%u", nTimeSmart);
        }

        std::vector<char> dummy_vector1; //!< Used to be vMerkleBranch
        std::vector<char> dummy_vector2; //!< Used to be vtxPrev
        bool dummy_bool = false; //!< Used to be fSpent
        uint256 serializedHash = isAbandoned() ? ABANDON_HASH : m_confirm.hashBlock;
        int serializedIndex = isAbandoned() || isConflicted() ? -1 : m_confirm.nIndex;
        s << tx << serializedHash << dummy_vector1 << serializedIndex << dummy_vector2 << mapValueCopy << vOrderForm
          << fTimeReceivedIsTxTime << nTimeReceived << fFromMe << dummy_bool;
    }

    template<typename Stream>
    void Unserialize(Stream &s) {
        Init(nullptr);

        std::vector <uint256> dummy_vector1; //!< Used to be vMerkleBranch
        std::vector <CMerkleTx> dummy_vector2; //!< Used to be vtxPrev
        bool dummy_bool; //! Used to be fSpent
        int serializedIndex;
        s >> tx >> m_confirm.hashBlock >> dummy_vector1 >> serializedIndex >> dummy_vector2 >> mapValue >> vOrderForm
          >> fTimeReceivedIsTxTime >> nTimeReceived >> fFromMe >> dummy_bool;

        /* At serialization/deserialization, an nIndex == -1 means that hashBlock refers to
         * the earliest block in the chain we know this or any in-wallet ancestor conflicts
         * with. If nIndex == -1 and hashBlock is ABANDON_HASH, it means transaction is abandoned.
         * In same context, an nIndex >= 0 refers to a confirmed transaction (if hashBlock set) or
         * unconfirmed one. Older clients interpret nIndex == -1 as unconfirmed for backward
         * compatibility (pre-commit 9ac63d6).
         */
        if (serializedIndex == -1 && m_confirm.hashBlock == ABANDON_HASH) {
            setAbandoned();
        } else if (serializedIndex == -1) {
            setConflicted();
        } else if (!m_confirm.hashBlock.IsNull()) {
            m_confirm.nIndex = serializedIndex;
            setConfirmed();
        }

        ReadOrderPos(nOrderPos, mapValue);
        nTimeSmart = mapValue.count("timesmart") ? (unsigned int) atoi64(mapValue["timesmart"]) : 0;

        mapValue.erase("fromaccount");
        mapValue.erase("spent");
        mapValue.erase("n");
        mapValue.erase("timesmart");
    }

    void SetTx(CTransactionRef arg) {
        tx = std::move(arg);
    }

    //! make sure balances are recalculated
    void MarkDirty() {
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

    void BindWallet(CWallet *pwalletIn) {
        pwallet = pwalletIn;
        MarkDirty();
    }

    const CWallet *GetWallet() const {
        return pwallet;
    }

    //! filter decides which addresses will count towards the debit
    CAmount GetDebit(const isminefilter &filter, isminefilter *mineTypes = nullptr) const;

    CAmount GetCredit(const isminefilter &filter, isminefilter *mineTypes = nullptr) const;

    CAmount GetImmatureCredit(bool fUseCache = true) const;

    // TODO: Remove "NO_THREAD_SAFETY_ANALYSIS" and replace it with the correct
    // annotation "EXCLUSIVE_LOCKS_REQUIRED(pwallet->cs_wallet)". The
    // annotation "NO_THREAD_SAFETY_ANALYSIS" was temporarily added to avoid
    // having to resolve the issue of member access into incomplete type CWallet.
    CAmount GetAvailableCredit(bool fUseCache = true, const isminefilter &filter = ISMINE_SPENDABLE) const

    NO_THREAD_SAFETY_ANALYSIS;

    CAmount GetImmatureWatchOnlyCredit(const bool fUseCache = true) const;

    CAmount GetChange() const;

    CAmount GetAnonymizedCredit(const CCoinControl *coinControl = nullptr) const;

    CAmount GetDenominatedCredit(bool unconfirmed, bool fUseCache = true) const;

    bool isFutureSpendable(unsigned int outputIndex) const;

    // Get the marginal bytes if spending the specified output from this transaction
    int GetSpendSize(unsigned int out) const {
        return CalculateMaximumSignedInputSize(tx->vout[out], pwallet);
    }

    void GetAmounts(std::list <COutputEntry> &listReceived,
                    std::list <COutputEntry> &listSent, CAmount &nFee, const isminefilter &filter) const;

    void GetAmounts(std::list <COutputEntry> &listReceived,
                    std::list <COutputEntry> &listSent, CAmount &nFee, const isminefilter &filter,
                    std::list <CAssetOutputEntry> &assetsReceived, std::list <CAssetOutputEntry> &assetsSent) const;

    bool IsFromMe(const isminefilter &filter) const {
        return (GetDebit(filter) > 0);
    }

    // True if only scriptSigs are different
    bool IsEquivalentTo(const CWalletTx &tx) const;

    bool InMempool() const;

    bool IsTrusted() const;

    int64_t GetTxTime() const;

    int64_t GetConfirmationTime() const;

    // Pass this transaction to node for mempool insertion and relay to peers if flag set to true
    bool SubmitMemoryPoolAndRelay(std::string &err_string, bool relay);

    // TODO: Remove "NO_THREAD_SAFETY_ANALYSIS" and replace it with the correct
    // annotation "EXCLUSIVE_LOCKS_REQUIRED(pwallet->cs_wallet)". The annotation
    // "NO_THREAD_SAFETY_ANALYSIS" was temporarily added to avoid having to
    // resolve the issue of member access into incomplete type CWallet. Note
    // that we still have the runtime check "AssertLockHeld(pwallet->cs_wallet)"
    // in place.
    std::set <uint256> GetConflicts() const

    NO_THREAD_SAFETY_ANALYSIS;

    /**
     * Return depth of transaction in blockchain:
     * <0  : conflicts with a transaction this deep in the blockchain
     *  0  : in memory pool, waiting to be included in a block
     * >=1 : this many blocks deep in the main chain
     */
    // TODO: Remove "NO_THREAD_SAFETY_ANALYSIS" and replace it with the correct
    // annotation "EXCLUSIVE_LOCKS_REQUIRED(pwallet->cs_wallet)". The annotation
    // "NO_THREAD_SAFETY_ANALYSIS" was temporarily added to avoid having to
    // resolve the issue of member access into incomplete type CWallet. Note
    // that we still have the runtime check "AssertLockHeld(pwallet->cs_wallet)"
    // in place.
    int GetDepthInMainChain() const

    NO_THREAD_SAFETY_ANALYSIS;

    bool IsInMainChain() const { return GetDepthInMainChain() > 0; }

    bool IsLockedByInstantSend() const;

    bool IsChainLocked() const;

    /**
     * @return number of blocks to maturity for this transaction:
     *  0 : is not a coinbase transaction, or is a mature coinbase transaction
     * >0 : is a coinbase transaction which matures in this many blocks
     */
    int GetBlocksToMaturity() const;

    bool isAbandoned() const { return m_confirm.status == CWalletTx::ABANDONED; }

    void setAbandoned() {
        m_confirm.status = CWalletTx::ABANDONED;
        m_confirm.hashBlock = uint256();
        m_confirm.block_height = 0;
        m_confirm.nIndex = 0;
    }

    bool isConflicted() const { return m_confirm.status == CWalletTx::CONFLICTED; }

    void setConflicted() { m_confirm.status = CWalletTx::CONFLICTED; }

    bool isUnconfirmed() const { return m_confirm.status == CWalletTx::UNCONFIRMED; }

    void setUnconfirmed() { m_confirm.status = CWalletTx::UNCONFIRMED; }

    bool isConfirmed() const { return m_confirm.status == CWalletTx::CONFIRMED; }

    void setConfirmed() { m_confirm.status = CWalletTx::CONFIRMED; }

    const uint256 &GetHash() const { return tx->GetHash(); }

    bool IsCoinBase() const { return tx->IsCoinBase(); }

    bool IsImmatureCoinBase() const;
};

struct WalletTxHasher {
    StaticSaltedHasher h;

    size_t operator()(const CWalletTx *a) const {
        return h(a->GetHash());
    }
};

struct CompareInputCoinBIP69 {
    inline bool operator()(const CInputCoin &a, const CInputCoin &b) const {
        // Note: CInputCoin-s are essentially inputs, their txouts are used for informational purposes only
        // that's why we use CompareInputBIP69 to sort them in a BIP69 compliant way.
        return CompareInputBIP69()(CTxIn(a.outpoint), CTxIn(b.outpoint));
    }
};

class COutput {
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

    COutput(const CWalletTx *txIn, int iIn, int nDepthIn, bool fSpendableIn, bool fSolvableIn, bool fSafeIn,
            bool future = false, bool futureSpendable = true) {
        tx = txIn;
        i = iIn;
        nDepth = nDepthIn;
        fSpendable = fSpendableIn;
        fSolvable = fSolvableIn;
        fSafe = fSafeIn;
        isFuture = future;
        isFutureSpendable = futureSpendable;
    }

    std::string ToString() const;

    inline CInputCoin GetInputCoin() const {
        return CInputCoin(tx->tx, i, nInputBytes);
    }
};

struct CoinSelectionParams {
    bool use_bnb = true;
    size_t change_output_size = 0;
    size_t change_spend_size = 0;
    CFeeRate effective_fee = CFeeRate(0);
    size_t tx_noinputs_size = 0;

    CoinSelectionParams(bool use_bnb, size_t change_output_size, size_t change_spend_size, CFeeRate effective_fee,
                        size_t tx_noinputs_size) : use_bnb(use_bnb), change_output_size(change_output_size),
                                                   change_spend_size(change_spend_size), effective_fee(effective_fee),
                                                   tx_noinputs_size(tx_noinputs_size) {}

    CoinSelectionParams() {}
};

class WalletRescanReserver; //forward declarations for ScanForWalletTransactions/RescanFromTime
/**
 * A CWallet is an extension of a keystore, which also maintains a set of transactions and balances,
 * and provides the ability to create new transactions.
 */
class CWallet final : public CCryptoKeyStore, public interfaces::Chain::Notifications {
private:
    std::atomic<bool> fAbortRescan{false};
    std::atomic<bool> fScanningWallet{false}; // controlled by WalletRescanReserver
    std::atomic <int64_t> m_scanning_start{0};
    std::atomic<double> m_scanning_progress{0};

    friend class WalletRescanReserver;


    /**
     * Select a set of coins such that nValueRet >= nTargetValue and at least
     * all coins from coinControl are selected; Never select unconfirmed coins
     * if they are not ours
     */
    bool SelectCoins(const std::vector <COutput> &vAvailableCoins, const CAmount &nTargetValue,
                     std::set <CInputCoin> &setCoinsRet, CAmount &nValueRet,
                     const CCoinControl &coin_control, const CoinSelectionParams &coin_selection_params,
                     bool &bnb_used) const;

    /**
     * Select a set of asset coins such that nValueRet >= nTargetValue and at least
     * all asset coins from coinControl are selected; Never select unconfirmed coins
     * if they are not ours
     */
    bool SelectAssets(const std::map <std::string, std::vector<COutput>> &mapAvailableAssets,
                      const std::map <std::string, CAmount> &mapAssetTargetValue,
                      const std::map <std::string, std::vector<uint16_t>> mapAssetUniqueId,
                      std::set <CInputCoin> &setCoinsRet, std::map <std::string, CAmount> &nValueRet) const;

    WalletBatch *encrypted_batch = nullptr;

    //! the current wallet version: clients below this version are not able to load the wallet
    int nWalletVersion
    GUARDED_BY(cs_wallet){FEATURE_BASE};

    //! the maximum wallet format version: memory-only variable that specifies to what version this wallet may be upgraded
    int nWalletMaxVersion = FEATURE_BASE;

    int64_t nNextResend = 0;
    int64_t nLastResend = 0;
    bool fBroadcastTransactions = false;
    // Local time that the tip block was received. Used to schedule wallet rebroadcasts.
    std::atomic <int64_t> m_best_block_time{0};

    mutable bool fAnonymizableTallyCached = false;
    mutable std::vector <CompactTallyItem> vecAnonymizableTallyCached;
    mutable bool fAnonymizableTallyCachedNonDenom = false;
    mutable std::vector <CompactTallyItem> vecAnonymizableTallyCachedNonDenom;

    /**
     * Used to keep track of spent outpoints, and
     * detect and report conflicts (double-spends or
     * mutated transactions where the mutant gets mined).
     */
    typedef std::multimap <COutPoint, uint256> TxSpends;
    TxSpends mapTxSpends;

    void AddToSpends(const COutPoint &outpoint, const uint256 &wtxid);

    void AddToSpends(const uint256 &wtxid);

    std::set <COutPoint> setWalletUTXO;
    mutable std::map<COutPoint, int> mapOutpointRoundsCache;

    /**
     * Add a transaction to the wallet, or update it.  pIndex and posInBlock should
     * be set when the transaction was known to be included in a block.  When
     * pIndex == nullptr, then wallet state is not updated in AddToWallet, but
     * notifications happen and cached balances are marked dirty.
     *
     * If fUpdate is true, existing transactions will be updated.
     * TODO: One exception to this is that the abandoned state is cleared under the
     * assumption that any further notification of a transaction that was considered
     * abandoned is an indication that it is not safe to be considered abandoned.
     * Abandoned state should probably be more carefully tracked via different
     * posInBlock signals or by checking mempool presence when necessary.
     */
    bool AddToWalletIfInvolvingMe(const CTransactionRef &tx, CWalletTx::Confirmation confirm, bool fUpdate,
                                  bool rescanningOldBlock)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    /* Mark a transaction (and its in-wallet descendants) as conflicting with a particular block. */
    void MarkConflicted(const uint256 &hashBlock, int conflicting_height, const uint256 &hashTx);

    /* Mark a transaction's inputs dorty, thus forcing the outputs to be recomputed */
    void MarkInputsDirty(const CTransactionRef &tx);

    void SyncMetaData(std::pair <TxSpends::iterator, TxSpends::iterator>);

    /* Used by TransactionAddedToMemorypool/BlockConnected/Disconnected/ScanForWalletTransactions.
     * Should be called with non-zero block_hash and posInBlock if this is for a transaction that is included in a block. */
    void SyncTransaction(const CTransactionRef &tx, CWalletTx::Confirmation confirm, bool update_tx = true,
                         bool rescanningOldBlock = false)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    /* HD derive new child key (on internal or external chain) */
    void DeriveNewChildKey(WalletBatch &batch, CKeyMetadata &metadata, CKey &secretRet, uint32_t nAccountIndex,
                           bool fInternal /*= false*/)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    std::set <int64_t> setInternalKeyPool
    GUARDED_BY(cs_wallet);
    std::set <int64_t> setExternalKeyPool
    GUARDED_BY(cs_wallet);
    int64_t m_max_keypool_index
    GUARDED_BY(cs_wallet) = 0;
    std::map <CKeyID, int64_t> m_pool_key_to_index;
    std::atomic <uint64_t> m_wallet_flags{0};

    int64_t nTimeFirstKey
    GUARDED_BY(cs_wallet) = 0;

    /**
     * Private version of AddWatchOnly method which does not accept a
     * timestamp, and which will reset the wallet's nTimeFirstKey value to 1 if
     * the watch key did not previously have a timestamp associated with it.
     * Because this is an inherited virtual method, it is accessible despite
     * being marked private, but it is marked private anyway to encourage use
     * of the other AddWatchOnly which accepts a timestamp and sets
     * nTimeFirstKey more intelligently for more efficient rescans.
     */
    bool AddWatchOnly(const CScript &dest) override

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    /** Interface for accessing chain state. */
    interfaces::Chain *m_chain;

    /** Wallet location which includes wallet name (see WalletLocation). */
    WalletLocation m_location;

    /** Internal database handle. */
    std::unique_ptr <WalletDatabase> database;

    // A helper function which loops through wallet UTXOs
    std::unordered_set<const CWalletTx *, WalletTxHasher> GetSpendableTXs() const

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    /**
     * The following is used to keep track of how far behind the wallet is
     * from the chain sync, and to allow clients to block on us being caught up.
     *
     * Processed hash is a pointer on node's tip and doesn't imply that the wallet
     * has scanned sequentially all blocks up to this one.
     */
    uint256 m_last_block_processed
    GUARDED_BY(cs_wallet);

    /** Pulled from wallet DB ("ps_salt") and used when mixing a random number of rounds.
     *  This salt is needed to prevent an attacker from learning how many extra times
     *  the input was mixed based only on information in the blockchain.
     */
    uint256 nCoinJoinSalt;

    /**
     * Fetches CoinJoin salt from database or generates and saves a new one if no salt was found in the db
     */
    void InitCoinJoinSalt();

    /* Height of last block processed is used by wallet to know depth of transactions
     * without relying on Chain interface beyond asynchronous updates. For safety, we
     * initialize it to -1. Height is a pointer on node's tip and doesn't imply
     * that the wallet has scanned sequentially all blocks up to this one.
     */
    int m_last_block_processed_height
    GUARDED_BY(cs_wallet) = -1;

public:
    /*
     * Main wallet lock.
     * This lock protects all the fields added by CWallet.
     */
    mutable RecursiveMutex cs_wallet;

    /** Get database handle used by this wallet. Ideally this function would
     * not be necessary.
     */
    WalletDatabase &GetDBHandle() {
        return *database;
    }

    const WalletLocation &GetLocation() const { return m_location; }

    /** Get a name for this wallet for logging/debugging purposes.
     */
    const std::string &GetName() const { return m_location.GetName(); }

    void LoadKeyPool(int64_t nIndex, const CKeyPool &keypool)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    // Map from Key ID to key metadata.
    std::map <CKeyID, CKeyMetadata> mapKeyMetadata;

    // Map from Script ID to key metadata (for watch-only keys).
    std::map <CScriptID, CKeyMetadata> m_script_metadata;

    // Map from governance object hash to governance object, they are added by gobject_prepare.
    std::map <uint256, CGovernanceObject> m_gobjects;

    bool WriteKeyMetadata(const CKeyMetadata &meta, const CPubKey &pubkey, bool overwrite);

    typedef std::map<unsigned int, CMasterKey> MasterKeyMap;
    MasterKeyMap mapMasterKeys;
    unsigned int nMasterKeyMaxID = 0;

    /** Construct wallet with specified name and database implementation. */
    CWallet(interfaces::Chain *chain, const WalletLocation &location, std::unique_ptr <WalletDatabase> database)
            : m_chain(chain),
              m_location(location),
              database(std::move(database)) {
    }

    ~CWallet() {
        // Should not have slots connected at this point.
        assert(NotifyUnload.empty());
        delete encrypted_batch;
        encrypted_batch = nullptr;
    }

    /** Interface to assert chain access */
    bool HaveChain() const { return m_chain ? true : false; }

    std::map <uint256, CWalletTx> mapWallet
    GUARDED_BY(cs_wallet);

    typedef std::multimap<int64_t, CWalletTx *> TxItems;
    TxItems wtxOrdered;

    int64_t nOrderPosNext = 0;
    uint64_t nAccountingEntryNumber = 0;

    std::map <CTxDestination, CAddressBookData> mapAddressBook;

    std::set <COutPoint> setLockedCoins;

    int64_t nKeysLeftSinceAutoBackup;

    std::map <CKeyID, CHDPubKey> mapHdPubKeys; //<! memory map of HD extended pubkeys

    /** Registered interfaces::Chain::Notifications handler. */
    std::unique_ptr <interfaces::Handler> m_chain_notifications_handler;

    /** Interface for accessing chain state. */
    interfaces::Chain &chain() const {
        assert(m_chain);
        return *m_chain;
    }

    const CWalletTx *GetWalletTx(const uint256 &hash) const;

    //! check whether we are allowed to upgrade (or already support) to the named feature
    bool CanSupportFeature(enum WalletFeature wf) const

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet) {AssertLockHeld(cs_wallet); return nWalletMaxVersion >= wf;}

    /**
     * populate vCoins with vector of available COutputs.
     */
    void AvailableCoins(std::vector <COutput> &vCoins, bool fOnlySafe = true, const CCoinControl *coinControl = nullptr,
                        const CAmount &nMinimumAmount = 1, const CAmount &nMaximumAmount = MAX_MONEY,
                        const CAmount &nMinimumSumAmount = MAX_MONEY, const uint64_t nMaximumCount = 0,
                        const int nMinDepth = 0, const int nMaxDepth = 9999999) const;

    void AvailableCoins(std::vector <COutput> &vCoins, std::map <std::string, std::vector<COutput>> &mapAssetCoins,
                        bool fGetRTM = true, bool fOnlyAssets = false, bool fOnlySafe = true,
                        const CCoinControl *coinControl = nullptr, const CAmount &nMinimumAmount = 1,
                        const CAmount &nMaximumAmount = MAX_MONEY, const CAmount &nMinimumSumAmount = MAX_MONEY,
                        const uint64_t nMaximumCount = 0, const int nMinDepth = 0, const int nMaxDepth = 9999999) const

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    void AvailableAssets(std::map <std::string, std::vector<COutput>> &mapAssetCoins, bool fOnlySafe = true,
                         const CCoinControl *coinControl = nullptr, const CAmount &nMinimumAmount = 1,
                         const CAmount &nMaximumAmount = MAX_MONEY, const CAmount &nMinimumSumAmount = MAX_MONEY,
                         const uint64_t nMaximumCount = 0, const int nMinDepth = 0,
                         const int nMaxDepth = 9999999) const;

    /**
     * Return list of available coins and locked coins grouped by non-change output address.
     */
    std::map <CTxDestination, std::vector<COutput>> ListCoins() const

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    /**
     * Return list of available assets and locked assets grouped by non-change output address.
     */
    std::map <CTxDestination, std::vector<COutput>> ListAssets() const;

    /**
     * Return list of assets balances.
     */
    std::map <std::string, CAmount>
    getAssetsBalance(const CCoinControl *coinControl = nullptr, bool fSpendable = false) const;

    /**
     * Find non-change parent output.
     */
    const CTxOut &FindNonChangeParentOutput(const CTransaction &tx, int output) const;

    /**
     * Shuffle and select coins until nTargetValue is reached while avoiding
     * small change; This method is stochastic for some inputs and upon
     * completion the coin set and corresponding actual target value is
     * assembled
     */
    bool SelectCoinsMinConf(const CAmount &nTargetValue, const CoinEligibilityFilter &eligibility_filter,
                            std::vector <OutputGroup> groups, std::set <CInputCoin> &setCoinsRet, CAmount &nValueRet,
                            const CoinSelectionParams &coin_selection_params, bool &bnb_used,
                            CoinType nCoinType = CoinType::ALL_COINS) const;

    bool SelectAssetsMinConf(const CAmount &nTargetValue, const CoinEligibilityFilter &eligibility_filter,
                             const std::string &strAssetName, std::vector <COutput> vCoins,
                             std::set <CInputCoin> &setCoinsRet, CAmount &nValueRet) const;

    // Coin selection
    bool SelectTxDSInsByDenomination(int nDenom, CAmount nValueMax, std::vector <CTxDSIn> &vecTxDSInRet);

    bool SelectDenominatedAmounts(CAmount nValueMax, std::set <CAmount> &setAmountsRet) const;

    std::vector <CompactTallyItem>
    SelectCoinsGroupedByAddresses(bool fSkipDenominated = true, bool fAnonymizable = true, bool fSkipUnconfirmed = true,
                                  int nMaxOupointsPerAddress = -1) const;

    /// Get collateral RTM output and keys which can be used for the Smartnode
    bool GetSmartnodeOutpointAndKeys(COutPoint &outpointRet, CPubKey &pubKeyRet, CKey &keyRet,
                                     const std::string &strTxHash = "", const std::string &strOutputIndex = "");

    /// Extract txin information and keys from output
    bool GetOutpointAndKeysFromOutput(const COutput &out, COutPoint &outpointRet, CPubKey &pubKeyRet, CKey &keyRet);

    bool HasCollateralInputs(bool fOnlyConfirmed = true) const;

    int CountInputsWithAmount(CAmount nInputAmount) const;

    // get the CoinJoin chain depth for a given input
    int GetRealOutpointCoinJoinRounds(const COutPoint &outpoint, int nRounds = 0) const;

    // respect current settings
    int GetCappedOutpointCoinJoinRounds(const COutPoint &outpoint) const;

    bool IsDenominated(const COutPoint &outpoint) const;

    bool IsFullyMixed(const COutPoint &outpoint) const;

    bool IsSpent(const uint256 &hash, unsigned int n) const

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    std::vector <OutputGroup> GroupOutputs(const std::vector <COutput> &outputs, bool single_coin) const;

    bool IsLockedCoin(uint256 hash, unsigned int n) const

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    void LockCoin(const COutPoint &output)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    void UnlockCoin(const COutPoint &output)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    void UnlockAllCoins()

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    void ListLockedCoins(std::vector <COutPoint> &vOutpts) const

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    void GetProTxCoins(const CDeterministicMNList &mnList, std::vector <COutPoint> &vOutpts) const

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    void ListProTxCoins(int height, std::vector <COutPoint> &vOutpts) const

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    void ListProTxCoins(std::vector <COutPoint> &vOutpts) const

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

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
    CPubKey GenerateNewKey(WalletBatch &batch, uint32_t nAccountIndex, bool fInternal /*= false*/)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    //! HaveKey implementation that also checks the mapHdPubKeys
    bool HaveKey(const CKeyID &address) const override;

    //! GetPubKey implementation that also checks the mapHdPubKeys
    bool GetPubKey(const CKeyID &address, CPubKey &vchPubKeyOut) const override;

    //! GetKey implementation that can derive a HD private key on the fly
    bool GetKey(const CKeyID &address, CKey &keyOut) const override;

    //! Adds a HDPubKey into the wallet(database)
    bool AddHDPubKey(WalletBatch &batch, const CExtPubKey &extPubKey, bool fInternal);

    //! loads a HDPubKey into the wallets memory
    bool LoadHDPubKey(const CHDPubKey &hdPubKey)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    //! Adds a key to the store, and saves it to disk.
    bool AddKeyPubKey(const CKey &key, const CPubKey &pubkey) override

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    //! Adds a key to the store, without saving it to disk (used by LoadWallet)
    bool LoadKey(const CKey &key, const CPubKey &pubkey) { return CCryptoKeyStore::AddKeyPubKey(key, pubkey); }

    //! Load metadata (used by LoadWallet)
    void LoadKeyMetadata(const CKeyID &keyID, const CKeyMetadata &metadata)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    void LoadScriptMetadata(const CScriptID &script_id, const CKeyMetadata &metadata)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    //! Upgrade stored CKeyMetadata objects to store key origin info as KeyOriginInfo
    void UpgradeKeyMetadata()

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    bool LoadMinVersion(int nVersion)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet) {AssertLockHeld(
            cs_wallet); nWalletVersion = nVersion; nWalletMaxVersion = std::max(nWalletMaxVersion, nVersion); return true;}

    void UpdateTimeFirstKey(int64_t nCreateTime)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    int64_t GetTimeFirstKey() const

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    //! Adds an encrypted key to the store, and saves it to disk.
    bool AddCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret) override;

    //! Adds an encrypted key to the store, without saving it to disk (used by LoadWallet)
    bool LoadCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret);

    bool AddCScript(const CScript &redeemScript) override;

    bool LoadCScript(const CScript &redeemScript);

    //! Adds a destination data tuple to the store, and saves it to disk
    bool AddDestData(const CTxDestination &dest, const std::string &key, const std::string &value)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    //! Erases a destination data tuple in the store and on disk
    bool EraseDestData(const CTxDestination &dest, const std::string &key)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    //! Adds a destination data tuple to the store, without saving it to disk
    void LoadDestData(const CTxDestination &dest, const std::string &key, const std::string &value)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    //! Look up a destination data tuple in the store, return true if found false otherwise
    bool GetDestData(const CTxDestination &dest, const std::string &key, std::string *value) const

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    //! Get all destination values matching a prefix.
    std::vector <std::string> GetDestValues(const std::string &prefix) const

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    //! Adds a watch-only address to the store, and saves it to disk.
    bool AddWatchOnly(const CScript &dest, int64_t nCreateTime)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    bool AddWatchOnlyWithDB(WalletBatch &batch, const CScript &dest)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    bool RemoveWatchOnly(const CScript &dest) override

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    //! Adds a watch-only address to the store, without saving it to disk (used by LoadWallet)
    bool LoadWatchOnly(const CScript &dest);

    //! Adds a key to the store, and saves it to disk.
    bool AddKeyPubKeyWithDB(WalletBatch &batch, const CKey &key, const CPubKey &pubkey)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    //! Adds a watch-only address to the store, and saves it to disk.
    bool AddWatchOnlyWithDB(WalletBatch &batch, const CScript &dest, int64_t create_time)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    //! Adds a script to the store and saves it to disk
    bool AddCScriptWithDB(WalletBatch &batch, const CScript &script);

    bool SetAddressBookWithDB(WalletBatch &batch, const CTxDestination &address, const std::string &strName,
                              const std::string &strPurpose);

    //! Holds a timestamp at which point the wallet is scheduled (externally) to be relocked. Caller must arrange for actual relocking to occur via Lock().
    int64_t nRelockTime = 0;

    bool Unlock(const SecureString &strWalletPassphrase, bool fForMixingOnly = false, bool accept_no_keys = false);

    bool ChangeWalletPassphrase(const SecureString &strOldWalletPassphrase, const SecureString &strNewWalletPassphrase);

    bool EncryptWallet(const SecureString &strWalletPassphrase);

    void GetKeyBirthTimes(std::map <CTxDestination, int64_t> &mapKeyBirth) const

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    unsigned int ComputeTimeSmart(const CWalletTx &wtx, bool rescanningOldBlock) const;

    /**
     * Increment the next transaction order id
     * @return next transaction order id
     */
    int64_t IncOrderPosNext(WalletBatch *batch = nullptr)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    DBErrors ReorderTransactions();

    void MarkDirty();

    bool AddToWallet(const CWalletTx &wtxIn, bool fFlushOnClose = true, bool rescanningOldBlock = false);

    void LoadToWallet(CWalletTx &wtxIn)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    void TransactionAddedToMempool(const CTransactionRef &tx, int64_t nAcceptTime) override;

    void BlockConnected(const CBlock &block, const std::vector <CTransactionRef> &vtxConflicted, int height) override;

    void BlockDisconnected(const CBlock &block, int height) override;

    void UpdatedBlockTip() override;

    int64_t RescanFromTime(int64_t startTime, const WalletRescanReserver &reserver, bool update);

    struct ScanResult {
        enum {
            SUCCESS, FAILURE, USER_ABORT
        } status = SUCCESS;

        //! Hash and height of most recent block that was successfully scanned.
        //! Unset if no blocks were scanned due to read errors or the chain
        //! being empty.
        uint256 last_scanned_block;
        Optional<int> last_scanned_height;

        //! Height of the most recent block that could not be scanned due to
        //! read errors or pruning. Will be set if status is FAILURE, unset if
        //! status is SUCCESS, and may or may not be set if status is
        //! USER_ABORT.
        uint256 last_failed_block;
    };

    ScanResult ScanForWalletTransactions(const uint256 &first_block, const uint256 &last_block,
                                         const WalletRescanReserver &reserver, bool fUpdate);

    void TransactionRemovedFromMempool(const CTransactionRef &ptx, MemPoolRemovalReason reason) override;

    void ReacceptWalletTransactions()

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    void ResendWalletTransactions();

    struct Balance {
        CAmount m_mine_trusted{0};
        CAmount m_mine_untrusted_pending{0};
        CAmount m_mine_immature{0};
        CAmount m_watchonly_trusted{0};
        CAmount m_watchonly_untrusted_pending{0};
        CAmount m_watchonly_immature{0};
        CAmount m_anonymized{0};
        CAmount m_denominated_trusted{0};
        CAmount m_denominated_untrusted_pending{0};
    };

    CAmount GetLegacyBalance(const isminefilter &filter, int minDepth, const bool fAddLocked) const;

    Balance
    GetBalance(int min_depth = 0, const bool fAddLocked = false, const CCoinControl *coinControl = nullptr) const;

    CAmount GetAnonymizableBalance(bool fSkipDenominated = false, bool fSkipUnconfirmed = true) const;

    float GetAverageAnonymizedRounds() const;

    CAmount GetNormalizedAnonymizedBalance() const;

    bool GetBudgetSystemCollateralTX(CTransactionRef &tx, uint256 hash, CAmount amount,
                                     const COutPoint &outpoint = COutPoint()/*defaults null*/);

    CAmount GetAvailableBalance(const CCoinControl *coinControl = nullptr) const;

    /**
     * Insert additional inputs into the transaction by
     * calling CreateTransaction();
     */
    bool FundTransaction(CMutableTransaction &tx, CAmount &nFeeRet, int &nChangePosInOut, std::string &strFailReason,
                         bool lockUnspents, const std::set<int> &setSubtractFeeFromOutputs, CCoinControl);

    bool SignTransaction(CMutableTransaction &tx)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    /**
     * Create a new transaction paying the recipients with a set of coins
     * selected by SelectCoins(); Also create the change output, when needed
     * @note passing nChangePosInOut as -1 will result in setting a random position
     */
    bool CreateTransaction(const std::vector <CRecipient> &vecSend, CTransactionRef &tx, CAmount &nFeeRet,
                           int &nChangePosInOut, std::string &strFailReason, const CCoinControl &coin_control,
                           bool sign = true, int nExtraPayloadSize = 0, FuturePartialPayload *fpp = nullptr,
                           CNewAssetTx *newAsset = nullptr, CMintAssetTx *mint = nullptr);

    /**
     * Submit the transaction to the node's mempool and then relay to peers.
     * Should be called after CreateTransaction unless you want to abort
     * broadcasting the transaction.
     *
     * @param tx[in] The transaction to be broadcast.
     * @param mapValue[in] key-values to be set on the transaction.
     * @param orderForm[in] BIP 70 / BIP 21 order form details to be set on the transaction.
     */
    void CommitTransaction(CTransactionRef tx, mapValue_t mapValue,
                           std::vector <std::pair<std::string, std::string>> orderForm);

    bool CreateCollateralTransaction(CMutableTransaction &txCollateral, std::string &strReason);

    bool DummySignTx(CMutableTransaction &txNew, const std::set <CTxOut> &txouts) const {
        std::vector <CTxOut> v_txouts(txouts.size());
        std::copy(txouts.begin(), txouts.end(), v_txouts.begin());
        return DummySignTx(txNew, v_txouts);
    }

    bool DummySignTx(CMutableTransaction &txNew, const std::vector <CTxOut> &txouts) const;

    bool DummySignInput(CTxIn &tx_in, const CTxOut &txout) const;

    bool ImportScripts(const std::set <CScript> scripts)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    bool ImportPrivKeys(const std::map <CKeyID, CKey> &privkey_map, const int64_t timestamp)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    bool ImportPubKeys(const std::map <CKeyID, CPubKey> &pubkey_map, const int64_t timestamp,
                       const std::map <CKeyID, std::pair<CPubKey, KeyOriginInfo>> &key_origins)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    bool ImportScriptPubKeys(const std::string &label, const std::set <CScript> &script_pub_keys,
                             const bool have_solving_data, const bool internal, const int64_t timestamp)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    CFeeRate m_pay_tx_fee{DEFAULT_PAY_TX_FEE};
    unsigned int m_confirm_target{DEFAULT_TX_CONFIRM_TARGET};
    bool m_spend_zero_conf_change{DEFAULT_SPEND_ZEROCONF_CHANGE};
    bool m_allow_fallback_fee{true}; //<! will be defined via chainparams
    CFeeRate m_min_fee{DEFAULT_TRANSACTION_MINFEE}; //!< Override with -mintxfee
    /**
     * If fee estimation does not have enough data to provide estimates, use this fee instead.
     * Has no effect if not using fee estimation
     * Override with -fallbackfee
     */
    CFeeRate m_fallback_fee{DEFAULT_FALLBACK_FEE};
    CFeeRate m_discard_rate{DEFAULT_DISCARD_FEE};
    /** Absolute maximum transaction fee (in satoshis) used by default for the wallet */
    CAmount m_default_max_tx_fee{DEFAULT_TRANSACTION_MAXFEE};

    bool NewKeyPool();

    size_t KeypoolCountExternalKeys()

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    size_t KeypoolCountInternalKeys()

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    bool TopUpKeyPool(unsigned int kpSize = 0);

    void ReserveKeyFromKeyPool(int64_t &nIndex, CKeyPool &keypool, bool fInternal);

    void KeepKey(int64_t nIndex);

    void ReturnKey(int64_t nIndex, bool fInternal, const CPubKey &pubkey);

    bool GetKeyFromPool(CPubKey &key, bool fInternal /*= false*/);

    int64_t GetOldestKeyPoolTime();

    /**
     * Marks all keys in the keypool up to and including reserve_key as used.
     */
    void MarkReserveKeysAsUsed(int64_t keypool_id)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    const std::map <CKeyID, int64_t> &GetAllReserveKeys() const { return m_pool_key_to_index; }

    std::set <std::set<CTxDestination>> GetAddressGroupings()

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    std::map <CTxDestination, CAmount> GetAddressBalances();

    std::set <CTxDestination> GetLabelAddresses(const std::string &label) const;

    isminetype IsMine(const CTxIn &txin) const;

    /**
     * Returns amount of debit if the input matches the
     * filter, otherwise returns 0
     */
    CAmount GetDebit(const CTxIn &txin, const isminefilter &filter, isminefilter *mineTypes = nullptr) const;

    isminetype IsMine(const CTxOut &txout) const;

    CAmount GetCredit(const CTxOut &txout, const isminefilter &filter, isminefilter *mineTypes = nullptr) const;

    bool IsChange(const CTxOut &txout) const;

    CAmount GetChange(const CTxOut &txout) const;

    bool IsMine(const CTransaction &tx) const;

    /** should probably be renamed to IsRelevantToMe */
    bool IsFromMe(const CTransaction &tx) const;

    CAmount GetDebit(const CTransaction &tx, const isminefilter &filter, isminefilter *mineTypes = nullptr) const;

    /** Returns whether all of the inputs match the filter */
    bool IsAllFromMe(const CTransaction &tx, const isminefilter &filter) const;

    CAmount GetCredit(const CTransaction &tx, const isminefilter &filter, isminefilter *mineTypes = nullptr) const;

    CAmount GetChange(const CTransaction &tx) const;

    void ChainStateFlushed(const CBlockLocator &loc) override;

    DBErrors LoadWallet(bool &fFirstRunRet);

    void AutoLockSmartnodeCollaterals();

    DBErrors ZapSelectTx(std::vector <uint256> &vHashIn, std::vector <uint256> &vHashOut)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    bool SetAddressBook(const CTxDestination &address, const std::string &strName, const std::string &purpose);

    bool DelAddressBook(const CTxDestination &address);

    void GetScriptForMining(std::shared_ptr<CReserveScript> &script);

    unsigned int GetKeyPoolSize()

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet)
            {
                    AssertLockHeld(cs_wallet);
            return setInternalKeyPool.size() + setExternalKeyPool.size();
            }

    //! signify that a particular wallet feature is now used. this may change nWalletVersion and nWalletMaxVersion if those are lower
    void SetMinVersion(enum WalletFeature, WalletBatch *batch_in = nullptr, bool fExplicit = false);

    //! change which version we're allowed to upgrade to (note that this does not immediately imply upgrading to that format)
    bool SetMaxVersion(int nVersion);

    //! get the current wallet format (the oldest client version guaranteed to understand this wallet)
    int GetVersion() {
        LOCK(cs_wallet);
        return nWalletVersion;
    }

    //! Get wallet transactions that conflict with given transaction (spend same outputs)
    std::set <uint256> GetConflicts(const uint256 &txid) const

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet);

    //! Flush wallet (bitdb flush)
    void Flush();

    //! Close wallet database
    void Close();

    /** Wallet is about to be unloaded */
    boost::signals2::signal<void()> NotifyUnload;

    /**
     * Address book entry changed.
     * @note called with lock cs_wallet held.
     */
    boost::signals2::signal<void(CWallet *wallet, const CTxDestination
    &address, const std::string &label, bool isMine,
                                 const std::string &purpose,
                                 ChangeType status)> NotifyAddressBookChanged;

    /**
     * Wallet transaction added, removed or updated.
     * @note called with lock cs_wallet held.
     */
    boost::signals2::signal<void(CWallet *wallet, const uint256 &hashTx,
                                 ChangeType status)> NotifyTransactionChanged;

    /** Show progress e.g. for rescan */
    boost::signals2::signal<void(const std::string &title, int nProgress)> ShowProgress;

    /** Watch-only address added */
    boost::signals2::signal<void(bool fHaveWatchOnly)> NotifyWatchonlyChanged;

    /** Keypool has new keys */
    boost::signals2::signal<void()> NotifyCanGetAddressesChanged;

    /** IS-lock received */
    boost::signals2::signal<void()> NotifyISLockReceived;

    /** ChainLock received */
    boost::signals2::signal<void(int height)> NotifyChainLockReceived;

    /** Inquire whether this wallet broadcasts transactions. */
    bool GetBroadcastTransactions() const { return fBroadcastTransactions; }

    /** Set whether this wallet broadcasts transactions. */
    void SetBroadcastTransactions(bool broadcast) { fBroadcastTransactions = broadcast; }

    /** Return whether transaction can be abandoned */
    bool TransactionCanBeAbandoned(const uint256 &hashTx) const;

    /* Mark a transaction (and it in-wallet descendants) as abandoned so its inputs may be respent. */
    bool AbandonTransaction(const uint256 &hashTx);

    //! Verify wallet naming and perform salvage on the wallet if required
    static bool Verify(interfaces::Chain &chain, const WalletLocation &location, std::string &error_string,
                       std::string &warning_string);

    /* Initializes the wallet, returns a new CWallet instance or a null pointer in case of an error */
    static std::shared_ptr <CWallet>
    CreateWalletFromFile(interfaces::Chain &chain, const WalletLocation &location, uint64_t wallet_creation_flags = 0);

    /**
     * Wallet post-init setup
     * Gives the wallet a chance to register repetitive tasks and complete post-init tasks
     */
    void postInitProcess();

    /* AutoBackup functionality */
    static bool InitAutoBackup();

    bool
    AutoBackupWallet(const fs::path &wallet_path, std::string &strBackupWarningRet, std::string &strBackupErrorRet);

    bool BackupWallet(const std::string &strDest);

    /**
     * HD Wallet Functions
     */

    /* Returns true if HD is enabled */
    bool IsHDEnabled() const;

    /* Returns true if the wallet can generate new keys */
    bool CanGenerateKeys();

    /* Returns true if the wallet can give out new addresses. This means it has keys in the keypool or can generate new keys */
    bool CanGetAddresses(bool internal = false);

    /* Generates a new HD chain */
    void GenerateNewHDChain(const SecureString &secureMnemonic, const SecureString &secureMnemonicPassphrase);

    bool GenerateNewHDChainEncrypted(const SecureString &secureMnemonic, const SecureString &secureMnemonicPassphrase,
                                     const SecureString &secureWalletPassphrase);

    /* Set the HD chain model (chain child index counters) */
    bool SetHDChain(WalletBatch &batch, const CHDChain &chain, bool memonly);

    bool SetCryptedHDChain(WalletBatch &batch, const CHDChain &chain, bool memonly);

    /**
     * Set the HD chain model (chain child index counters) using temporary wallet db object
     * which causes db flush every time these methods are used
     */
    bool SetHDChainSingle(const CHDChain &chain, bool memonly);

    bool SetCryptedHDChainSingle(const CHDChain &chain, bool memonly);

    bool GetDecryptedHDChain(CHDChain &hdChainRet);

    void NotifyTransactionLock(const CTransactionRef &tx,
                               const std::shared_ptr<const llmq::CInstantSendLock> &islock) override;

    void NotifyChainLock(const CBlockIndex *pindexChainLock,
                         const std::shared_ptr<const llmq::CChainLockSig> &clsig) override;

    /** Load a CGovernanceObject into m_gobjects. */
    bool LoadGovernanceObject(const CGovernanceObject &obj);

    /** Store a CGovernanceObject in the wallet database. This should only be used by governance objects that are created by this wallet via `gobject prepare`. */
    bool WriteGovernanceObject(const CGovernanceObject &obj);

    /** Returns a vector containing pointers to the governance objects in m_gobjects */
    std::vector<const CGovernanceObject *> GetGovernanceObjects();

    /**
     * Blocks until the wallet state is up-to-date to /at least/ the current
     * chain at the time this function is entered
     * Obviously holding cs_main/cs_wallet when going into this call may cause
     * deadlock
     */
    void BlockUntilSyncedToCurrentChain()

    LOCKS_EXCLUDED(cs_wallet);

    void SetWalletFlag(uint64_t flags);

    /** Unsets a single wallet flag */
    void UnsetWalletFlag(uint64_t flag);

    void UnsetWalletFlag(WalletBatch &batch, uint64_t flag);

    bool IsWalletFlagSet(uint64_t flag);

    bool SetWalletFlags(uint64_t overwriteFlags, bool memonly);

    const std::string GetDisplayName() const {
        std::string wallet_name = GetName().length() == 0 ? "default wallet" : GetName();
        return strprintf("[%s]", wallet_name);
    }

    template<typename... Params>
    void WalletLogPrintf(std::string fmt, Params... parameters) const {
        LogPrintf(("%s " + fmt).c_str(), GetDisplayName(), parameters...);
    };

    /** Implement lookup of key origin information through wallet key metadata. */
    bool GetKeyOrigin(const CKeyID &keyid, KeyOriginInfo &info) const override;

    /** Add a keyOriginInfo to the wallet */
    bool AddKeyOrigin(const CPubKey &pubkey, const KeyOriginInfo &info);

    /** Get last block processed height */
    int GetLastBlockHeight() const

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet)
            {
                    AssertLockHeld(cs_wallet);
            assert(m_last_block_processed_height >= 0);
            return m_last_block_processed_height;
            };

    /** Set last block processed height, currently only use in unit test */
    void SetLastBlockProcessed(int block_height, uint256 block_hash)

    EXCLUSIVE_LOCKS_REQUIRED(cs_wallet)
            {
                    AssertLockHeld(cs_wallet);
            m_last_block_processed_height = block_height;
            m_last_block_processed = block_hash;
            };

    //used by assets need to be changed
    /** Whether a given output is spendable by this wallet */
    bool OutputEligibleForSpending(const COutput &output, const CoinEligibilityFilter &eligibility_filter) const;

    friend struct WalletTestingSetup;
};

/**
 * Called periodically by the schedule thread. Prompts individual wallets to resend
 * their transactions. Actual rebroadcast schedule is managed by the wallets themselves.
 */
void MaybeResendWalletTxs();

/** A key allocated from the key pool. */
class CReserveKey final : public CReserveScript {
protected:
    CWallet *pwallet;
    int64_t nIndex;
    CPubKey vchPubKey;
    bool fInternal;
public:
    explicit CReserveKey(CWallet *pwalletIn) {
        nIndex = -1;
        pwallet = pwalletIn;
        fInternal = false;
    }

    CReserveKey() = default;

    CReserveKey(const CReserveKey &) = delete;

    CReserveKey &operator=(const CReserveKey &) = delete;

    ~CReserveKey() {
        ReturnKey();
    }

    void ReturnKey();

    bool GetReservedKey(CPubKey &pubkey, bool fInternalIn /*= false*/);

    void KeepKey();

    void KeepScript() override { KeepKey(); }
};

/** RAII object to check and reserve a wallet rescan */
class WalletRescanReserver {
private:
    CWallet *m_wallet;
    bool m_could_reserve;
public:
    explicit WalletRescanReserver(CWallet *w) : m_wallet(w), m_could_reserve(false) {}

    bool reserve() {
        assert(!m_could_reserve);
        if (m_wallet->fScanningWallet.exchange(true)) {
            return false;
        }
        m_wallet->m_scanning_start = GetTimeMillis();
        m_wallet->m_scanning_progress = 0;
        m_could_reserve = true;
        return true;
    }

    bool isReserved() const {
        return (m_could_reserve && m_wallet->fScanningWallet);
    }

    ~WalletRescanReserver() {
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

int64_t CalculateMaximumSignedTxSize(const CTransaction &tx, const CWallet *wallet, const std::vector <CTxOut> &txouts);

#endif // BITCOIN_WALLET_WALLET_H
