// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_INTERFACES_WALLET_H
#define BITCOIN_INTERFACES_WALLET_H

#include <amount.h>                    // For CAmount
#include <fs.h>                        // For fs::path
#include <interfaces/chain.h>          // For ChainClient
#include <pubkey.h>                    // For CKeyID and CScriptID (definitions needed in CTxDestination instantiation)
#include <script/standard.h>           // For CTxDestination
#include <support/allocators/secure.h> // For SecureString
#include <ui_interface.h>              // For ChangeType
#include <util/system.h>

#include <functional>
#include <map>
#include <memory>
#include <stdint.h>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

class CCoinControl;

class CFeeRate;

class CKey;

class CWallet;

class COutput;

enum class FeeReason;
enum class TransactionError;
enum class WalletCreationStatus;
enum isminetype : unsigned int;
struct CRecipient;
struct FuturePartialPayload;
class CNewAssetTx;
class CMintAssetTx;
struct WalletContext;
typedef uint8_t isminefilter;

namespace interfaces {

    class Handler;

    struct WalletAddress;
    struct WalletBalances;
    struct WalletTx;
    struct WalletTxOut;
    struct WalletTxStatus;
    using WalletOrderForm = std::vector <std::pair<std::string, std::string>>;
    using WalletValueMap = std::map<std::string, std::string>;

    namespace CoinJoin {
//! Interface for the wallet constrained src/coinjoin part of a raptoreum node (raptoreumd process).
        class Client {
        public:
            virtual ~Client() {}

            virtual void resetCachedBlocks() = 0;

            virtual void resetPool() = 0;

            virtual int getCachedBlocks() = 0;

            virtual std::string getSessionDenoms() = 0;

            virtual void setCachedBlocks(int nCachedBlocks) = 0;

            virtual void disableAutobackups() = 0;

            virtual bool isMixing() = 0;

            virtual bool startMixing() = 0;

            virtual void stopMixing() = 0;
        };
    }

//! Interface for accessing a wallet.
    class Wallet {
    public:
        virtual ~Wallet() {}

        //! Mark wallet as dirty
        virtual void markDirty() = 0;

        //! Encrypt wallet.
        virtual bool encryptWallet(const SecureString &wallet_passphrase) = 0;

        //! Return whether wallet is encrypted.
        virtual bool isCrypted() = 0;

        //! Lock wallet.
        virtual bool lock(bool fAllowMixing = false) = 0;

        //! Unlock wallet.
        virtual bool unlock(const SecureString &wallet_passphrase, bool fAllowMixing = false) = 0;

        //! Return whether wallet is locked.
        virtual bool isLocked(bool fForMixing = false) = 0;

        //! Change wallet passphrase.
        virtual bool changeWalletPassphrase(const SecureString &old_wallet_passphrase,
                                            const SecureString &new_wallet_passphrase) = 0;

        //! Abort a rescan.
        virtual void abortRescan() = 0;

        //! Back up wallet.
        virtual bool backupWallet(const std::string &filename) = 0;

        //! Automatically backup up wallet.
        virtual bool autoBackupWallet(const fs::path &wallet_path, std::string &strBackupWarningRet,
                                      std::string &strBackupErrorRet) = 0;

        //! Get the number of keys since the last auto backup
        virtual int64_t getKeysLeftSinceAutoBackup() = 0;

        //! Get wallet name.
        virtual std::string getWalletName() = 0;

        // Get key from pool.
        virtual bool getKeyFromPool(bool internal, CPubKey &pub_key) = 0;

        //! Get public key.
        virtual bool getPubKey(const CKeyID &address, CPubKey &pub_key) = 0;

        //! Get private key.
        virtual bool getPrivKey(const CKeyID &address, CKey &key) = 0;

        //! Return whether wallet has private key.
        virtual bool isSpendable(const CScript &script) = 0;

        virtual bool isSpendable(const CTxDestination &dest) = 0;

        //! Return whether wallet has watch only keys.
        virtual bool haveWatchOnly() = 0;

        //! Add or update address.
        virtual bool
        setAddressBook(const CTxDestination &dest, const std::string &name, const std::string &purpose) = 0;

        // Remove address.
        virtual bool delAddressBook(const CTxDestination &dest) = 0;

        //! Look up address in wallet, return whether exists.
        virtual bool getAddress(const CTxDestination &dest,
                                std::string *name = nullptr,
                                isminetype *is_mine = nullptr) = 0;

        //! Get wallet address list.
        virtual std::vector <WalletAddress> getAddresses() = 0;

        //! Add dest data.
        virtual bool addDestData(const CTxDestination &dest, const std::string &key, const std::string &value) = 0;

        //! Erase dest data.
        virtual bool eraseDestData(const CTxDestination &dest, const std::string &key) = 0;

        //! Get dest values with prefix.
        virtual std::vector <std::string> getDestValues(const std::string &prefix) = 0;

        //! Lock coin.
        virtual void lockCoin(const COutPoint &output) = 0;

        //! Unlock coin.
        virtual void unlockCoin(const COutPoint &output) = 0;

        //! Return whether coin is locked.
        virtual bool isLockedCoin(const COutPoint &output) = 0;

        //! List locked coins.
        virtual void listLockedCoins(std::vector <COutPoint> &outputs) = 0;

        //! List protx coins.
        virtual void listProTxCoins(std::vector <COutPoint> &vOutpts) = 0;

        //! Create transaction.
        virtual CTransactionRef createTransaction(const std::vector <CRecipient> &recipients,
                                                  const CCoinControl &coin_control,
                                                  bool sign,
                                                  int &change_pos,
                                                  CAmount &fee,
                                                  std::string &fail_reason,
                                                  int nExtraPayloadSize = 0,
                                                  FuturePartialPayload *fpp = nullptr) = 0;
        
        //! Create transaction.
        virtual bool createTransaction(const std::vector <CRecipient> &recipients,
                                                  CTransactionRef &tx,
                                                  const CCoinControl &coin_control,
                                                  bool sign,
                                                  int &change_pos,
                                                  CAmount &fee,
                                                  std::string &fail_reason,
                                                  int nExtraPayloadSize = 0,
                                                  CNewAssetTx *newAsset = nullptr,
                                                  CMintAssetTx *mint = nullptr) = 0;

        //! Commit transaction.
        virtual void commitTransaction(CTransactionRef tx, WalletValueMap value_map, WalletOrderForm order_form) = 0;

        //! Return whether transaction can be abandoned.
        virtual bool transactionCanBeAbandoned(const uint256 &txid) = 0;

        //! Abandon transaction.
        virtual bool abandonTransaction(const uint256 &txid) = 0;

        //! Get a transaction.
        virtual CTransactionRef getTx(const uint256 &txid) = 0;

        //! Get transaction information.
        virtual WalletTx getWalletTx(const uint256 &txid) = 0;

        //! Get list of all wallet transactions.
        virtual std::vector <WalletTx> getWalletTxs() = 0;

        //! Try to get updated status for a particular transaction, if possible without blocking.
        virtual bool tryGetTxStatus(const uint256 &txid,
                                    WalletTxStatus &tx_status,
                                    int64_t &adjusted_time,
                                    int64_t &block_time) = 0;

        //! Get transaction details.
        virtual WalletTx getWalletTxDetails(const uint256 &txid,
                                            WalletTxStatus &tx_status,
                                            WalletOrderForm &order_form,
                                            bool &in_mempool,
                                            int &num_blocks,
                                            int64_t &adjusted_time) = 0;

        // Get the number of coinjoin rounds an output went through
        virtual int getRealOutpointCoinJoinRounds(const COutPoint &outpoint) = 0;

        // Check if an outpoint is fully mixed
        virtual bool isFullyMixed(const COutPoint &outpoint) = 0;

        //! Get balances.
        virtual WalletBalances getBalances() = 0;

        //! Get balances if possible without blocking.
        virtual bool tryGetBalances(WalletBalances &balances, int &num_blocks) = 0;

        //! Get balance.
        virtual CAmount getBalance() = 0;

        //! Get anonymizable balance.
        virtual CAmount getAnonymizableBalance(bool fSkipDenominated, bool fSkipUnconfirmed) = 0;

        //! Get anonymized balance.
        virtual CAmount getAnonymizedBalance() = 0;

        //! Get denominated balance.
        virtual CAmount getDenominatedBalance(bool unconfirmed) = 0;

        //! Get normalized anonymized balance.
        virtual CAmount getNormalizedAnonymizedBalance() = 0;

        //! Get average anonymized rounds.
        virtual CAmount getAverageAnonymizedRounds() = 0;

        //! Get available balance.
        virtual CAmount getAvailableBalance(const CCoinControl &coin_control) = 0;

        //! Return whether transaction input belongs to wallet.
        virtual isminetype txinIsMine(const CTxIn &txin) = 0;

        //! Return whether transaction output belongs to wallet.
        virtual isminetype txoutIsMine(const CTxOut &txout) = 0;

        //! Return debit amount if transaction input belongs to wallet.
        virtual CAmount getDebit(const CTxIn &txin, isminefilter filter) = 0;

        //! Return credit amount if transaction input belongs to wallet.
        virtual CAmount getCredit(const CTxOut &txout, isminefilter filter) = 0;

        //! Return AvailableCoins + LockedCoins grouped by wallet address.
        //! (put change in one group with wallet address)
        using CoinsList = std::map <CTxDestination, std::vector<std::tuple < COutPoint, WalletTxOut>>>;

        virtual CoinsList listCoins() = 0;

        //return AvailableCoins
        virtual void AvailableCoins(std::vector <COutput> &vCoins, bool fOnlySafe = true, const CCoinControl *coinControl = nullptr,
                        const CAmount &nMinimumAmount = 1, const CAmount &nMaximumAmount = MAX_MONEY,
                        const CAmount &nMinimumSumAmount = MAX_MONEY, const uint64_t nMaximumCount = 0,
                        const int nMinDepth = 0, const int nMaxDepth = 9999999) = 0;

        //! Return AvailableAssets + LockedAssets grouped by wallet address.
        //! (put change in one group with wallet address)
        virtual CoinsList listAssets() = 0;

        //! Return list of avaylable assets
        using AssetList = std::vector<std::string>;

        virtual AssetList listMyAssets(const CCoinControl *coinControl = nullptr) = 0;

        //! Return list of avaylable assets
        using UniqueIdList = std::vector<uint16_t>;

        virtual UniqueIdList listAssetUniqueId(std::string assetId, const CCoinControl *coinControl = nullptr) = 0;

        //!Return asset balance
        using AssetBalance = std::map<std::string, CAmount>;

        virtual AssetBalance getAssetsBalance(const CCoinControl *coinControl = nullptr, bool fSpendable = false) = 0;

        virtual std::map<std::string, std::pair<CAmount, CAmount>> getAssetsBalanceAll() = 0;

        //return list of assets that the wallet has ownership
        virtual std::map <uint256, std::pair<std::string, CKeyID>> getMyAssets() = 0;

        //! Return wallet transaction output information.
        virtual std::vector <WalletTxOut> getCoins(const std::vector <COutPoint> &outputs) = 0;

        //! Get required fee
        virtual CAmount getRequiredFee(unsigned int tx_bytes) = 0;

        //! Get minimum fee.
        virtual CAmount getMinimumFee(unsigned int tx_bytes, const CCoinControl &coin_control, int *returned_target,
                                      FeeReason *reason) = 0;

        //! Get tx confirm target.
        virtual unsigned int getConfirmTarget() = 0;

        // Return whether HD enabled.
        virtual bool hdEnabled() = 0;

        // Return whether the wallet is blank.
        virtual bool canGetAddresses() = 0;

        // Check is a creation wallet flag is set.
        virtual bool IsWalletFlagSet(uint64_t flag) = 0;

        virtual CoinJoin::Client &coinJoin() = 0;

        //! Get max tx fee
        virtual CAmount getDefaultMaxTxFee() = 0;

        // Remove wallet
        virtual void remove() = 0;

        //! Register handler for unload message.
        using UnloadFn = std::function<void()>;

        virtual std::unique_ptr <Handler> handleUnload(UnloadFn fn) = 0;

        //! Register handler for show progress messages.
        using ShowProgressFn = std::function<void(const std::string &title, int progress)>;

        virtual std::unique_ptr <Handler> handleShowProgress(ShowProgressFn fn) = 0;

        //! Register handler for status changed messages.
        using StatusChangedFn = std::function<void()>;

        virtual std::unique_ptr <Handler> handleStatusChanged(StatusChangedFn fn) = 0;

        //! Register handler for address book changed messages.
        using AddressBookChangedFn = std::function<void(const CTxDestination &address,
                                                        const std::string &label,
                                                        bool is_mine,
                                                        const std::string &purpose,
                                                        ChangeType status)>;

        virtual std::unique_ptr <Handler> handleAddressBookChanged(AddressBookChangedFn fn) = 0;

        //! Register handler for transaction changed messages.
        using TransactionChangedFn = std::function<void(const uint256 &txid, ChangeType status)>;

        virtual std::unique_ptr <Handler> handleTransactionChanged(TransactionChangedFn fn) = 0;

        //! Register handler for instantlock messages.
        using InstantLockReceivedFn = std::function<void()>;

        virtual std::unique_ptr <Handler> handleInstantLockReceived(InstantLockReceivedFn fn) = 0;

        //! Register handler for chainlock messages.
        using ChainLockReceivedFn = std::function<void(int chainLockHeight)>;

        virtual std::unique_ptr <Handler> handleChainLockReceived(ChainLockReceivedFn fn) = 0;

        //! Register handler for watchonly changed messages.
        using WatchOnlyChangedFn = std::function<void(bool have_watch_only)>;

        virtual std::unique_ptr <Handler> handleWatchOnlyChanged(WatchOnlyChangedFn fn) = 0;

        //! Register handler for keypool changed messages.
        using CanGetAddressesChangedFn = std::function<void()>;

        virtual std::unique_ptr <Handler> handleCanGetAddressesChanged(CanGetAddressesChangedFn fn) = 0;

        //! Return pointer to internal wallet class, useful for testing.
        virtual CWallet *wallet() { return nullptr; }
    };

//! Wallet chain client that in addition to having chain client methods
//! for starting up, shutting down and registering RPCs, also has
//! additional methods (called by the GUI) to load and create wallets.
    class WalletClient : public ChainClient {
    public:
        //! Create new wallet.
        virtual std::unique_ptr <Wallet>
        createWallet(const std::string &name, const SecureString &passphrase, uint64_t wallet_creation_flags,
                     WalletCreationStatus &status, std::string &error, std::string &warning) = 0;

        //! Load existing wallet.
        virtual std::unique_ptr <Wallet>
        loadWallet(const std::string &name, std::string &error, std::string &warning) = 0;

        //! Return default wallet directory.
        virtual std::string getWalletDir() = 0;

        //! Return available wallets in wallet directory.
        virtual std::vector <std::string> listWalletDir() = 0;

        //! Return interfaces for accessing wallets (if any).
        virtual std::vector <std::unique_ptr<Wallet>> getWallets() = 0;

        //! Register handler for load wallet messages. This callback is triggered
        //! by createWallet and loadWallet above, and also triggered when wallets
        //! are loaded at startup or by RPC.
        using LoadWalletFn = std::function<void(std::unique_ptr < Wallet > wallet)>;

        virtual std::unique_ptr <Handler> handleLoadWallet(LoadWalletFn fn) = 0;
    };

//! Information about one wallet address.
    struct WalletAddress {
        CTxDestination dest;
        isminetype is_mine;
        std::string name;
        std::string purpose;

        WalletAddress(CTxDestination dest, isminetype is_mine, std::string name, std::string purpose)
                : dest(std::move(dest)), is_mine(is_mine), name(std::move(name)), purpose(std::move(purpose)) {
        }
    };

//! Collection of wallet balances.
    struct WalletBalances {
        CAmount balance = 0;
        CAmount unconfirmed_balance = 0;
        CAmount immature_balance = 0;
        CAmount anonymized_balance = 0;
        bool have_watch_only = false;
        CAmount watch_only_balance = 0;
        CAmount unconfirmed_watch_only_balance = 0;
        CAmount immature_watch_only_balance = 0;

        bool balanceChanged(const WalletBalances &prev) const {
            return balance != prev.balance || unconfirmed_balance != prev.unconfirmed_balance ||
                   anonymized_balance != prev.anonymized_balance ||
                   immature_balance != prev.immature_balance || watch_only_balance != prev.watch_only_balance ||
                   unconfirmed_watch_only_balance != prev.unconfirmed_watch_only_balance ||
                   immature_watch_only_balance != prev.immature_watch_only_balance;
        }
    };

// Wallet transaction information.
    struct WalletTx {
        CTransactionRef tx;
        std::vector <isminetype> txin_is_mine;
        std::vector <isminetype> txout_is_mine;
        std::vector <CTxDestination> txout_address;
        std::vector <isminetype> txout_address_is_mine;
        CAmount credit;
        CAmount debit;
        CAmount change;
        int64_t time;
        std::map <std::string, std::string> value_map;
        bool is_coinbase;
        bool is_denominate;
    };

//! Updated transaction status.
    struct WalletTxStatus {
        int block_height;
        int blocks_to_maturity;
        int depth_in_main_chain;
        int request_count;
        unsigned int time_received;
        uint32_t lock_time;
        bool is_final;
        bool is_trusted;
        bool is_abandoned;
        bool is_coinbase;
        bool is_in_main_chain;
        bool is_chainlocked;
        bool is_islocked;
    };

//! Wallet transaction output.
    struct WalletTxOut {
        CTxOut txout;
        int64_t time;
        int depth_in_main_chain = -1;
        bool is_spent = false;
    };

//! Return implementation of Wallet interface. This function is defined is
//! dummywallet.cpp and throws if the wallet component is not compiled.
    std::unique_ptr <Wallet> MakeWallet(const std::shared_ptr <CWallet> &wallet);

//! Return implementation of ChainClient interface for a wallet client.
//! This function will be undefined in builds where ENABLE_WALLET is false.
    std::unique_ptr <WalletClient> MakeWalletClient(Chain &chain, std::vector <std::string> wallet_filenames);

} // namespace interfaces

#endif // BITCOIN_INTERFACES_WALLET_H
