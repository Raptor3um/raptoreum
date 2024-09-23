// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_WALLETDB_H
#define BITCOIN_WALLET_WALLETDB_H

#include <amount.h>
#include <script/sign.h>
#include <wallet/bdb.h>
#include <wallet/db.h>
#include <key.h>

#include <stdint.h>
#include <string>
#include <vector>

/**
 * Overview of wallet database classes:
 *
 * - WalletBatch is an abstract modifier object for the wallet database, and encapsulates a database
 *   batch update as well as methods to act on the database. It should be agnostic to the database implementation.
 *
 * The following classes are implementation specific:
 * - BerkeleyEnvironment is an environment in which the database exists.
 * - BerkeleyDatabase represents a wallet database.
 * - BerkeleyBatch is a low-level database batch update.
 */

static const bool DEFAULT_FLUSHWALLET = true;

struct CBlockLocator;

class CGovernanceObject;

class CHDChain;

class CHDPubKey;

class CKeyPool;

class CMasterKey;

class CScript;

class CWallet;

class CWalletTx;

class uint160;

class uint256;

/** Error statuses for the wallet database */
enum class DBErrors {
    LOAD_OK,
    CORRUPT,
    NONCRITICAL_ERROR,
    TOO_NEW,
    LOAD_FAIL,
    NEED_REWRITE
};

namespace DBKeys {
    extern const std::string ACENTRY;
    extern const std::string BESTBLOCK;
    extern const std::string BESTBLOCK_NOMERKLE;
    extern const std::string CRYPTED_HDCHAIN;
    extern const std::string CRYPTED_KEY;
    extern const std::string COINJOIN_SALT;
    extern const std::string CSCRIPT;
    extern const std::string DEFAULTKEY;
    extern const std::string DESTDATA;
    extern const std::string FLAGS;
    extern const std::string G_OBJECT;
    extern const std::string HDCHAIN;
    extern const std::string HDPUBKEY;
    extern const std::string KEY;
    extern const std::string KEYMETA;
    extern const std::string MASTER_KEY;
    extern const std::string MINVERSION;
    extern const std::string NAME;
    extern const std::string OLD_KEY;
    extern const std::string ORDERPOSNEXT;
    extern const std::string POOL;
    extern const std::string PURPOSE;
    extern const std::string PRIVATESEND_SALT;
    extern const std::string TX;
    extern const std::string VERSION;
    extern const std::string WATCHMETA;
    extern const std::string WATCHS;
} // namespace DBKeys

class CKeyMetadata {
public:
    static const int VERSION_BASIC = 1;
    static const int VERSION_WITH_KEY_ORIGIN = 12;
    static const int CURRENT_VERSION = VERSION_WITH_KEY_ORIGIN;
    int nVersion;
    int64_t nCreateTime; // 0 means unknown
    KeyOriginInfo key_origin; // Key origin info with path and fingerprint
    bool has_key_origin = false; //< Whether the key_origin is useful

    CKeyMetadata() {
        SetNull();
    }

    explicit CKeyMetadata(int64_t nCreateTime_) {
        SetNull();
        nCreateTime = nCreateTime_;
    }

    SERIALIZE_METHODS(CKeyMetadata, obj
    )
    {
        READWRITE(obj.nVersion, obj.nCreateTime);
        if (obj.nVersion >= VERSION_WITH_KEY_ORIGIN) {
            READWRITE(obj.key_origin, obj.has_key_origin);
        }
    }

    void SetNull() {
        nVersion = CKeyMetadata::CURRENT_VERSION;
        nCreateTime = 0;
        key_origin.clear();
        has_key_origin = false;
    }
};

/** Access to the wallet database.
 * Opens the database and provides read and write access to it. Each read and write is its own transaction.
 * Multiple operation transactions can be started using TxnBegin() and committed using TxnCommit()
 * Otherwise the transaction will be committed when the object goes out of scope.
 * Optionally (on by default) it will flush to disk on close.
 * Every 1000 writes will automatically trigger a flush to disk.
 */
class WalletBatch {
private:
    template<typename K, typename T>
    bool WriteIC(const K &key, const T &value, bool fOverwrite = true) {
        if (!m_batch->Write(key, value, fOverwrite)) {
            return false;
        }
        m_database.IncrementUpdateCounter();
        if (m_database.nUpdateCounter % 1000 == 0) {
            m_batch->Flush();
        }
        return true;
    }

    template<typename K>
    bool EraseIC(const K &key) {
        if (!m_batch->Erase(key)) {
            return false;
        }
        m_database.IncrementUpdateCounter();
        if (m_database.nUpdateCounter % 1000 == 0) {
            m_batch->Flush();
        }
        return true;
    }

public:
    explicit WalletBatch(WalletDatabase &database, const char *pszMode = "r+", bool _fFlushOnClose = true) :
            m_batch(database.MakeBatch(pszMode, _fFlushOnClose)),
            m_database(database) {
    }

    WalletBatch(const WalletBatch &) = delete;

    WalletBatch &operator=(const WalletBatch &) = delete;

    bool WriteName(const std::string &strAddress, const std::string &strName);

    bool EraseName(const std::string &strAddress);

    bool WritePurpose(const std::string &strAddress, const std::string &purpose);

    bool ErasePurpose(const std::string &strAddress);

    bool WriteTx(const CWalletTx &wtx);

    bool EraseTx(uint256 hash);

    bool WriteKeyMetadata(const CKeyMetadata &keyMeta, const CPubKey &pubkey, const bool overwrite);

    bool WriteKey(const CPubKey &vchPubKey, const CPrivKey &vchPrivKey, const CKeyMetadata &keyMeta);

    bool WriteCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret,
                         const CKeyMetadata &keyMeta);

    bool WriteMasterKey(unsigned int nID, const CMasterKey &kMasterKey);

    bool WriteCScript(const uint160 &hash, const CScript &redeemScript);

    bool WriteWatchOnly(const CScript &script, const CKeyMetadata &keymeta);

    bool EraseWatchOnly(const CScript &script);

    bool WriteBestBlock(const CBlockLocator &locator);

    bool ReadBestBlock(CBlockLocator &locator);

    bool WriteOrderPosNext(int64_t nOrderPosNext);

    bool ReadPool(int64_t nPool, CKeyPool &keypool);

    bool WritePool(int64_t nPool, const CKeyPool &keypool);

    bool ErasePool(int64_t nPool);

    bool WriteMinVersion(int nVersion);

    bool ReadCoinJoinSalt(uint256 &salt, bool fLegacy = false);

    bool WriteCoinJoinSalt(const uint256 &salt);

    /** Write a CGovernanceObject to the database */
    bool WriteGovernanceObject(const CGovernanceObject &obj);

    bool ReadPrivateSendSalt(uint256 &salt);

    bool WritePrivateSendSalt(const uint256 &salt);

    /// Write destination data key,value tuple to database
    bool WriteDestData(const std::string &address, const std::string &key, const std::string &value);

    /// Erase destination data tuple from wallet database
    bool EraseDestData(const std::string &address, const std::string &key);

    DBErrors LoadWallet(CWallet *pwallet);

    DBErrors FindWalletTx(std::vector <uint256> &vTxHash, std::vector <CWalletTx> &vWtx);

    DBErrors ZapSelectTx(std::vector <uint256> &vHashIn, std::vector <uint256> &vHashOut);

    /* Function to determine if a certain KV/key-type is a key (cryptographical key) type */
    static bool IsKeyType(const std::string &strType);

    //! write the hdchain model (external chain child index counter)
    bool WriteHDChain(const CHDChain &chain);

    bool WriteCryptedHDChain(const CHDChain &chain);

    bool WriteHDPubKey(const CHDPubKey &hdPubKey, const CKeyMetadata &keyMeta);

    bool WriteWalletFlags(const uint64_t flags);

    //! Begin a new transaction
    bool TxnBegin();

    //! Commit current transaction
    bool TxnCommit();

    //! Abort current transaction
    bool TxnAbort();

private:
    std::unique_ptr <DatabaseBatch> m_batch;
    WalletDatabase &m_database;
};

//! Compacts BDB state so that wallet.dat is self-contained (if there are changes)
void MaybeCompactWalletDB();

//! Callback for filtering key types to deserialize in ReadKeyValue
using KeyFilterFn = std::function<bool(const std::string &)>;

//! Unserialize a given Key-Value pair and load it into the wallet
bool ReadKeyValue(CWallet *pwallet, CDataStream &ssKey, CDataStream &ssValue, std::string &strType, std::string &strErr,
                  const KeyFilterFn &filter_fn = nullptr);

/** Return whether a wallet database is currently loaded. */
bool IsWalletLoaded(const fs::path &wallet_path);

/** Return object for accessing database at specified path. */
std::unique_ptr <WalletDatabase> CreateWalletDatabase(const fs::path &path);

/** Return object for accessing dummy database with no read/write capabilities. */
std::unique_ptr <WalletDatabase> CreateDummyWalletDatabase();

/** Return object for accessing temporary in-memory database. */
std::unique_ptr <WalletDatabase> CreateMockWalletDatabase();

#endif // BITCOIN_WALLET_WALLETDB_H
