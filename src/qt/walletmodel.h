// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_WALLETMODEL_H
#define BITCOIN_QT_WALLETMODEL_H

#if defined(HAVE_CONFIG_H)
#include <config/raptoreum-config.h>
#endif

#include <amount.h>
#include <key.h>
#include <serialize.h>
#include <script/standard.h>

#include <qt/walletmodeltransaction.h>
#include <qt/walletmodelfuturestransaction.h>

#include <interfaces/wallet.h>
#include <support/allocators/secure.h>

#include <vector>

#include <QObject>

class AddressTableModel;

class OptionsModel;

class RecentRequestsTableModel;

class TransactionTableModel;

class WalletModelTransaction;

class WalletModelFuturesTransaction;

class CCoinControl;

class CKeyID;

class COutPoint;

class COutput;

class CPubKey;

class uint256;

namespace interfaces {
    class Node;
} // namespace interfaces

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

class SendCoinsRecipient {
public:
    explicit SendCoinsRecipient() : amount(0), fSubtractFeeFromAmount(false),
                                    nVersion(SendCoinsRecipient::CURRENT_VERSION) {}

    explicit SendCoinsRecipient(const QString &addr, const QString &_label, const CAmount &_amount,
                                const QString &_message)
            : address(addr), label(_label), amount(_amount), message(_message), fSubtractFeeFromAmount(false),
              nVersion(SendCoinsRecipient::CURRENT_VERSION) {}

    // If from an unauthenticated payment request, this is used for storing
    // the addresses, e.g. address-A<br />address-B<br />address-C.
    // Info: As we don't need to process addresses in here when using
    // payment requests, we can abuse it for displaying an address list.
    // Todo: This is a hack, should be replaced with a cleaner solution!
    QString assetId;
    int uniqueId;
    QString address;
    QString label;
    CAmount amount;
    CAmount assetAmount;
    // If from a payment request, this is used for storing the memo
    QString message;
    std::string sPaymentRequest;
    QString authenticatedMerchant;

    bool fSubtractFeeFromAmount; // memory only

    bool isFutureOutput;
    int maturity;
    int64_t locktime;

    static const int CURRENT_VERSION = 1;
    int nVersion;

    SERIALIZE_METHODS(SendCoinsRecipient, obj
    )
    {
        bool isFutureOutput{false};
        std::string address_str, label_str, message_str, auth_merchant_str;

        SER_WRITE(obj, address_str = obj.address.toStdString());
        SER_WRITE(obj, label_str = obj.label.toStdString());
        SER_WRITE(obj, message_str = obj.message.toStdString());
        SER_WRITE(obj, auth_merchant_str = obj.authenticatedMerchant.toStdString());

        READWRITE(obj.nVersion, address_str, label_str, obj.amount, message_str, obj.sPaymentRequest,
                  auth_merchant_str);

        SER_READ(obj, obj.address = QString::fromStdString(address_str));
        SER_READ(obj, obj.label = QString::fromStdString(label_str));
        SER_READ(obj, obj.message = QString::fromStdString(message_str));
        SER_READ(obj, obj.authenticatedMerchant = QString::fromStdString(auth_merchant_str));

        if (isFutureOutput) {
            READWRITE(obj.isFutureOutput, obj.maturity, obj.locktime);
        }
    }
};

class SendFuturesRecipient {
public:
    explicit SendFuturesRecipient() : amount(0), nVersion(SendFuturesRecipient::CURRENT_VERSION) {}

    explicit SendFuturesRecipient(const QString &addr, const QString &_label, const CAmount &_amount,
                                  const QString &_message, const int &_maturity, const int64_t &_locktime)
            : address(addr), label(_label), amount(_amount), message(_message), maturity(_maturity),
              locktime(_locktime), nVersion(SendCoinsRecipient::CURRENT_VERSION) {}

    // If from an unauthenticated payment request, this is used for storing
    // the addresses, e.g. address-A<br />address-B<br />address-C.
    // Info: As we don't need to process addresses in here when using
    // payment requests, we can abuse it for displaying an address list.
    // Todo: This is a hack, should be replaced with a cleaner solution!
    QString address;
    QString label;
    CAmount amount;
    // If from a payment request, this is used for storing the memo
    QString message;
    std::string sPaymentRequest;
    QString authenticatedMerchant;

    //Futures strings
    QString payFrom;

    //Future TX maturity fields
    int maturity;
    int64_t locktime;

    static const int CURRENT_VERSION = 1;
    int nVersion;

    SERIALIZE_METHODS(SendFuturesRecipient, obj
    )
    {
        std::string address_str, label_str, message_str, auth_merchant_str;

        SER_WRITE(obj, address_str = obj.address.toStdString());
        SER_WRITE(obj, label_str = obj.label.toStdString());
        SER_WRITE(obj, message_str = obj.message.toStdString());
        SER_WRITE(obj, auth_merchant_str = obj.authenticatedMerchant.toStdString());

        READWRITE(obj.nVersion, address_str, label_str, obj.amount, message_str, obj.sPaymentRequest, auth_merchant_str,
                  obj.maturity, obj.locktime);

        SER_READ(obj, obj.address = QString::fromStdString(address_str));
        SER_READ(obj, obj.label = QString::fromStdString(label_str));
        SER_READ(obj, obj.message = QString::fromStdString(message_str));
        SER_READ(obj, obj.authenticatedMerchant = QString::fromStdString(auth_merchant_str));
    }
};

/** Interface to Bitcoin wallet from Qt view code. */
class WalletModel : public QObject {
    Q_OBJECT

public:
    explicit WalletModel(std::unique_ptr <interfaces::Wallet> wallet, interfaces::Node &node,
                         OptionsModel *optionsModel, QObject *parent = nullptr);

    ~WalletModel();

    enum StatusCode // Returned by sendCoins
    {
        OK,
        InvalidAmount,
        InvalidAddress,
        AmountExceedsBalance,
        AmountWithFeeExceedsBalance,
        DuplicateAddress,
        TransactionCreationFailed, // Error returned when wallet is still locked
        AbsurdFee,
        PaymentRequestExpired,
        AmountExceedsmaxmoney
    };

    enum EncryptionStatus {
        Unencrypted,            // !wallet->IsCrypted()
        Locked,                 // wallet->IsCrypted() && wallet->IsLocked(true)
        UnlockedForMixingOnly,  // wallet->IsCrypted() && !wallet->IsLocked(true) && wallet->IsLocked()
        Unlocked,               // wallet->IsCrypted() && !wallet->IsLocked()
    };

    OptionsModel *getOptionsModel();

    AddressTableModel *getAddressTableModel();

    TransactionTableModel *getTransactionTableModel();

    RecentRequestsTableModel *getRecentRequestsTableModel();

    std::map <CTxDestination, CAmount> getAddressBalances() const;

    CAmount getBalance(const CCoinControl &coinControl) const;


    EncryptionStatus getEncryptionStatus() const;

    // Check address for validity
    bool validateAddress(const QString &address);

    // Return status record for SendCoins, contains error id + information
    struct SendCoinsReturn {
        SendCoinsReturn(StatusCode _status = OK, QString _reasonCommitFailed = "")
                : status(_status),
                  reasonCommitFailed(_reasonCommitFailed) {
        }

        StatusCode status;
        QString reasonCommitFailed;
    };

    // prepare transaction for getting txfee before sending coins
    SendCoinsReturn prepareTransaction(WalletModelTransaction &transaction, const CCoinControl &coinControl);

    // Send coins to a list of recipients
    SendCoinsReturn sendCoins(WalletModelTransaction &transaction, bool fIsCoinJoin);

    // Return status record for SendAssets, contains error id + information
    struct SendAssetsReturn {
        SendAssetsReturn(StatusCode _status = OK, QString _reasonCommitFailed = "")
                : status(_status),
                  reasonCommitFailed(_reasonCommitFailed) {
        }

        StatusCode status;
        QString reasonCommitFailed;
    };

    // prepare transaction for getting txfee before sending coins
    SendAssetsReturn prepareAssetTransaction(WalletModelTransaction &transaction, const CCoinControl &coinControl);

    // Send coins to a list of recipients
    SendAssetsReturn sendAssets(WalletModelTransaction &transaction, bool fIsCoinJoin);

    // Wallet encryption
    bool setWalletEncrypted(bool encrypted, const SecureString &passphrase);

    // Passphrase only needed when unlocking
    bool setWalletLocked(bool locked, const SecureString &passPhrase = SecureString(), bool fMixing = false);

    bool changePassphrase(const SecureString &oldPass, const SecureString &newPass);

    // Wallet backup
    bool autoBackupWallet(QString &strBackupWarningRet, QString &strBackupErrorRet);

    int64_t getKeysLeftSinceAutoBackup() const;

    // RAI object for unlocking wallet, returned by requestUnlock()
    class UnlockContext {
    public:
        UnlockContext(WalletModel *wallet, bool valid, bool was_locked, bool was_mixing);

        ~UnlockContext();

        bool isValid() const { return valid; }

        // Copy constructor is disabled.
        UnlockContext(const UnlockContext &) = delete;

        // Move operator and constructor transfer the context.
        UnlockContext(UnlockContext &&obj) { CopyFrom(std::move(obj)); }

        UnlockContext &operator=(UnlockContext &&rhs) {
            CopyFrom(std::move(rhs));
            return *this;
        }

    private:
        WalletModel *wallet;
        bool valid;
        mutable bool was_locked; // mutable, as it can be set to false by copying
        mutable bool was_mixing; // mutable, as it can be set to false by copying

        UnlockContext &operator=(const UnlockContext &) = default;

        void CopyFrom(UnlockContext &&rhs);
    };

    UnlockContext requestUnlock(bool fForMixingOnly = false);

    void loadReceiveRequests(std::vector <std::string> &vReceiveRequests);

    bool saveReceiveRequest(const std::string &sAddress, const int64_t nId, const std::string &sRequest);

    static bool isWalletEnabled();

    bool privateKeysDisabled() const;

    bool canGetAddresses() const;

    int getNumBlocks() const;

    int getNumISLocks() const;

    int getRealOutpointCoinJoinRounds(const COutPoint &outpoint) const;

    bool isFullyMixed(const COutPoint &outpoint) const;

    interfaces::Node &node() const { return m_node; }

    interfaces::Wallet &wallet() const { return *m_wallet; }

    interfaces::CoinJoin::Client &coinJoin() const { return m_wallet->coinJoin(); }

    QString getWalletName() const;

    QString getDisplayName() const;

    bool isMultiwallet();

private:
    std::unique_ptr <interfaces::Wallet> m_wallet;
    std::unique_ptr <interfaces::Handler> m_handler_unload;
    std::unique_ptr <interfaces::Handler> m_handler_status_changed;
    std::unique_ptr <interfaces::Handler> m_handler_address_book_changed;
    std::unique_ptr <interfaces::Handler> m_handler_transaction_changed;
    std::unique_ptr <interfaces::Handler> m_handler_islock_received;
    std::unique_ptr <interfaces::Handler> m_handler_chainlock_received;
    std::unique_ptr <interfaces::Handler> m_handler_show_progress;
    std::unique_ptr <interfaces::Handler> m_handler_watch_only_changed;
    std::unique_ptr <interfaces::Handler> m_handler_can_get_addrs_changed;
    interfaces::Node &m_node;

    bool fHaveWatchOnly;
    bool fForceCheckBalanceChanged;

    // Wallet has an options model for wallet-specific options
    // (transaction fee, for example)
    OptionsModel *optionsModel;

    AddressTableModel *addressTableModel;
    TransactionTableModel *transactionTableModel;
    RecentRequestsTableModel *recentRequestsTableModel;

    // Cache some values to be able to detect changes
    interfaces::WalletBalances m_cached_balances;
    EncryptionStatus cachedEncryptionStatus;
    int cachedNumBlocks;
    int cachedNumISLocks;
    int cachedCoinJoinRounds;

    void subscribeToCoreSignals();

    void unsubscribeFromCoreSignals();

    void checkBalanceChanged(const interfaces::WalletBalances &new_balances);

    Q_SIGNALS:
            // Signal that balance in wallet changed
            void balanceChanged(
    const interfaces::WalletBalances &balances
    );

    // Encryption status of wallet changed
    void encryptionStatusChanged();

    // Signal emitted when wallet needs to be unlocked
    // It is valid behaviour for listeners to keep the wallet locked after this signal;
    // this means that the unlocking failed or was cancelled.
    void requireUnlock(bool fForMixingOnly = false);

    // Fired when a message should be reported to the user
    void message(const QString &title, const QString &message, unsigned int style);

    // Coins sent: from wallet, to recipient, in (serialized) transaction:
    void coinsSent(WalletModel *wallet, SendCoinsRecipient recipient, QByteArray transaction);

    /* //Futures sent: from wallet, to recipient, in (serialized) transaction:
    void futuresSent(CWallet* wallet, SendFuturesRecipient recipient, QByteArray transaction); */

    // Show progress dialog e.g. for rescan
    void showProgress(const QString &title, int nProgress);

    // Watch-only address added
    void notifyWatchonlyChanged(bool fHaveWatchonly);

    void assetListChanged();

    // Signal that wallet is about to be removed
    void unload();

    // Notify that there are now keys in the keypool
    void canGetAddressesChanged();

public
    Q_SLOTS:
            /* Starts a timer to periodically update the balance */
            void startPollBalance();

    /* Wallet status might have changed */
    void updateStatus();

    /* New transaction, or transaction changed status */
    void updateTransaction();

    /* IS-Lock received */
    void updateNumISLocks();

    /* ChainLock received */
    void updateChainLockHeight(int chainLockHeight);

    /* New, updated or removed address book entry */
    void
    updateAddressBook(const QString &address, const QString &label, bool isMine, const QString &purpose, int status);

    /* Watch-only added */
    void updateWatchOnlyFlag(bool fHaveWatchonly);

    /* Current, immature or unconfirmed balance might have changed - emit 'balanceChanged' if so */
    void pollBalanceChanged();
};

#endif // BITCOIN_QT_WALLETMODEL_H
