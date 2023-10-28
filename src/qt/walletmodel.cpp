// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined (HAVE_CONFIG_H)
#include <config/raptoreum-config.h>
#endif

#include <qt/walletmodel.h>

#include <qt/addresstablemodel.h>
#include <qt/guiconstants.h>
#include <qt/optionsmodel.h>
#include <qt/paymentserver.h>
#include <qt/recentrequeststablemodel.h>
#include <qt/transactiontablemodel.h>

#include <chainparams.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <ui_interface.h>
#include <util/system.h> // for GetBoolArg
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>
#include <spork.h>
#include <validation.h>
#include <assets/assets.h>
#include <assets/assetstype.h>

#include <stdint.h>
#include <functional>

#include <QDebug>
#include <QSet>
#include <QTimer>

static int64_t nLastUpdateNotification = 0;
static bool ninitialSync = false;

namespace pl = std::placeholders;

WalletModel::WalletModel(std::unique_ptr <interfaces::Wallet> wallet, interfaces::Node &node,
                         OptionsModel *_optionsModel, QObject *parent) :
        QObject(parent), m_wallet(std::move(wallet)), m_node(node), optionsModel(_optionsModel),
        addressTableModel(nullptr),
        transactionTableModel(nullptr),
        recentRequestsTableModel(nullptr),
        cachedEncryptionStatus(Unencrypted),
        cachedNumBlocks(-1),
        cachedNumISLocks(0),
        cachedCoinJoinRounds(0) {
    fHaveWatchOnly = m_wallet->haveWatchOnly();
    fForceCheckBalanceChanged = false;

    addressTableModel = new AddressTableModel(this);
    transactionTableModel = new TransactionTableModel(this);
    recentRequestsTableModel = new RecentRequestsTableModel(this);

    subscribeToCoreSignals();
}

WalletModel::~WalletModel() {
    unsubscribeFromCoreSignals();
}

void WalletModel::startPollBalance() {
    // This timer will be fired repeatedly to update the balance
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &WalletModel::pollBalanceChanged);
    timer->start(MODEL_UPDATE_DELAY);
}

void WalletModel::updateStatus() {
    EncryptionStatus newEncryptionStatus = getEncryptionStatus();

    if (cachedEncryptionStatus != newEncryptionStatus) {
        Q_EMIT encryptionStatusChanged();
    }
}

void WalletModel::pollBalanceChanged() {

    // Try to get balances and return early if locks can't be acquired. This
    // avoids the GUI from getting stuck on periodical polls if the core is
    // holding the locks for a longer time - for example, during a wallet
    // rescan.
    interfaces::WalletBalances new_balances;
    int numBlocks = -1;
    if (!m_wallet->tryGetBalances(new_balances, numBlocks)) {
        return;
    }
    int64_t now = 0;
    if (ninitialSync)
        now = GetTimeMillis();

    // if we are in-sync, update the UI regardless of last update time
    if (!ninitialSync || now - nLastUpdateNotification > MODEL_UPDATE_DELAY_SYNC) {

        nLastUpdateNotification = now;

        if (fForceCheckBalanceChanged || numBlocks != cachedNumBlocks ||
            node().coinJoinOptions().getRounds() != cachedCoinJoinRounds) {

            fForceCheckBalanceChanged = false;

            // Balance and number of transactions might have changed
            cachedNumBlocks = numBlocks;
            cachedCoinJoinRounds = node().coinJoinOptions().getRounds();

            checkBalanceChanged(new_balances);
            if (transactionTableModel)
                transactionTableModel->updateConfirmations();
        }
    }
}

void WalletModel::checkBalanceChanged(const interfaces::WalletBalances &new_balances) {
    if (new_balances.balanceChanged(m_cached_balances)) {
        m_cached_balances = new_balances;
        Q_EMIT balanceChanged(new_balances);
    }
}

void WalletModel::updateTransaction() {
    // Balance and number of transactions might have changed
    fForceCheckBalanceChanged = true;
}

void WalletModel::updateNumISLocks() {
    cachedNumISLocks++;
}

void WalletModel::updateChainLockHeight(int chainLockHeight) {
    if (transactionTableModel)
        transactionTableModel->updateChainLockHeight(chainLockHeight);
    // Number and status of confirmations might have changed (WalletModel::pollBalanceChanged handles this as well)
    fForceCheckBalanceChanged = true;
}

int WalletModel::getNumBlocks() const {
    return cachedNumBlocks;
}

int WalletModel::getNumISLocks() const {
    return cachedNumISLocks;
}

int WalletModel::getRealOutpointCoinJoinRounds(const COutPoint &outpoint) const {
    return m_wallet->getRealOutpointCoinJoinRounds(outpoint);
}

bool WalletModel::isFullyMixed(const COutPoint &outpoint) const {
    return m_wallet->isFullyMixed(outpoint);
}

void WalletModel::updateAddressBook(const QString &address, const QString &label,
                                    bool isMine, const QString &purpose, int status) {
    if (addressTableModel)
        addressTableModel->updateEntry(address, label, isMine, purpose, status);
}

void WalletModel::updateWatchOnlyFlag(bool fHaveWatchonly) {
    fHaveWatchOnly = fHaveWatchonly;
    Q_EMIT notifyWatchonlyChanged(fHaveWatchonly);
}

bool WalletModel::validateAddress(const QString &address) {
    return IsValidDestinationString(address.toStdString());
}

WalletModel::SendCoinsReturn
WalletModel::prepareTransaction(WalletModelTransaction &transaction, const CCoinControl &coinControl) {
    CAmount total = 0;
    bool fSubtractFeeFromAmount = false;
    QList <SendCoinsRecipient> recipients = transaction.getRecipients();
    std::vector <CRecipient> vecSend;

    if (recipients.empty()) {
        return OK;
    }

    // This should never really happen, yet another safety check, just in case.
    if (m_wallet->isLocked(false)) {
        return TransactionCreationFailed;
    }

    QSet <QString> setAddress; // Used to detect duplicates
    int nAddresses = 0;
    FuturePartialPayload fpp;
    bool hasFuture = false;
    // Pre-check input data for validity
    for (const SendCoinsRecipient &rcp: recipients) {
        if (rcp.fSubtractFeeFromAmount)
            fSubtractFeeFromAmount = true;
        {   // User-entered raptoreum address / amount:
            if (!validateAddress(rcp.address)) {
                return InvalidAddress;
            }
            if (rcp.amount <= 0) {
                return InvalidAmount;
            }
            setAddress.insert(rcp.address);
            ++nAddresses;
            CScript scriptPubKey = GetScriptForDestination(DecodeDestination(rcp.address.toStdString()));
            CRecipient recipient = {scriptPubKey, rcp.amount, rcp.fSubtractFeeFromAmount};
            if (rcp.isFutureOutput) {
                hasFuture = true;
                fpp.futureRecScript = scriptPubKey;
                fpp.maturity = rcp.maturity;
                fpp.locktime = rcp.locktime;
            }

            vecSend.push_back(recipient);

            total += rcp.amount;
        }
    }
    if (setAddress.size() != nAddresses) {
        return DuplicateAddress;
    }

    CAmount nBalance = m_wallet->getAvailableBalance(coinControl);

    if (total > nBalance) {
        return AmountExceedsBalance;
    }

    CAmount nFeeRequired = 0;
    std::string strFailReason;
    int nChangePosRet = -1;

    auto &newTx = transaction.getWtx();
    newTx = m_wallet->createTransaction(vecSend, coinControl, true /* sign */, nChangePosRet, nFeeRequired,
                                        strFailReason, 0, hasFuture ? &fpp : nullptr);
    transaction.setTransactionFee(nFeeRequired);
    if (fSubtractFeeFromAmount && newTx)
        transaction.reassignAmounts();

    if (!newTx) {
        if (!fSubtractFeeFromAmount && (total + nFeeRequired) > nBalance) {
            return SendCoinsReturn(AmountWithFeeExceedsBalance);
        }
        Q_EMIT message(tr("Send Coins"), QString::fromStdString(strFailReason),
                       CClientUIInterface::MSG_ERROR);
        return TransactionCreationFailed;
    }

    // reject absurdly high fee. (This can never happen because the
    // wallet caps the fee at m_default_max_tx_fee. This merely serves as a
    // belt-and-suspenders check)
    if (nFeeRequired > m_wallet->getDefaultMaxTxFee())
        return AbsurdFee;

    return SendCoinsReturn(OK);
}

WalletModel::SendCoinsReturn WalletModel::sendCoins(WalletModelTransaction &transaction, bool fIsCoinJoin) {
    QByteArray transaction_array; /* store serialized transaction */
    {
        std::vector <std::pair<std::string, std::string>> vOrderForm;
        for (const SendCoinsRecipient &rcp: transaction.getRecipients()) {
            if (!rcp.message.isEmpty()) // Message from normal raptoreum:URI (raptoreum:XyZ...?message=example)
                vOrderForm.emplace_back("Message", rcp.message.toStdString());
        }

        mapValue_t mapValue;
        if (fIsCoinJoin) {
            mapValue["DS"] = "1";
        }

        auto &newTx = transaction.getWtx();
        wallet().commitTransaction(newTx, std::move(mapValue), std::move(vOrderForm));

        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
        ssTx << *newTx;
        transaction_array.append(ssTx.data(), ssTx.size());
    }

    // Add addresses / update labels that we've sent to the address book,
    // and emit coinsSent signal for each recipient
    for (const SendCoinsRecipient &rcp: transaction.getRecipients()) {
        {
            std::string strAddress = rcp.address.toStdString();
            CTxDestination dest = DecodeDestination(strAddress);
            std::string strLabel = rcp.label.toStdString();
            {
                // Check if we have a new address or an updated label
                std::string name;
                if (!m_wallet->getAddress(dest, &name)) {
                    m_wallet->setAddressBook(dest, strLabel, "send");
                } else if (name != strLabel) {
                    m_wallet->setAddressBook(dest, strLabel, ""); // "" means don't change purpose
                }
            }
        }
        Q_EMIT coinsSent(this, rcp, transaction_array);
    }

    checkBalanceChanged(
            m_wallet->getBalances()); // update balance immediately, otherwise there could be a short noticeable delay until pollBalanceChanged hits

    return SendCoinsReturn(OK);
}


WalletModel::SendAssetsReturn
WalletModel::prepareAssetTransaction(WalletModelTransaction &transaction, const CCoinControl &coinControl) {
    CAmount total = 0;
    bool fSubtractFeeFromAmount = false;
    QList <SendCoinsRecipient> recipients = transaction.getRecipients();
    std::vector <CRecipient> vecSend;
    std::map <std::string, CAmount> assetamount;

    if (recipients.empty()) {
        return OK;
    }

    // This should never really happen, yet another safety check, just in case.
    if (m_wallet->isLocked(false)) {
        return TransactionCreationFailed;
    }

    QSet <QString> setAddress; // Used to detect duplicates
    int nAddresses = 0;
    FuturePartialPayload fpp;
    bool hasFuture = false;
    // Pre-check input data for validity
    for (const SendCoinsRecipient &rcp: recipients) {
        if (rcp.fSubtractFeeFromAmount)
            fSubtractFeeFromAmount = true;

        {   // User-entered raptoreum address / amount:
            if (!validateAddress(rcp.address)) {
                return InvalidAddress;
            }
            if (rcp.amount <= 0 && rcp.assetAmount <= 0) {
                return InvalidAmount;
            }
            setAddress.insert(rcp.address);
            ++nAddresses;
            CRecipient recipient;
            CScript scriptPubKey = GetScriptForDestination(DecodeDestination(rcp.address.toStdString()));
            if (rcp.assetAmount > 0 && rcp.amount == 0) {
                std::string assetId;
                if (!passetsCache->GetAssetId(rcp.assetId.toStdString(), assetId))
                    return InvalidAmount;
                if (rcp.uniqueId != MAX_UNIQUE_ID) {
                    //unique asset transaction
                    CAssetTransfer assetTransfer(assetId, rcp.assetAmount, rcp.uniqueId);
                    assetTransfer.BuildAssetTransaction(scriptPubKey);
                    recipient = {scriptPubKey, 0, false};
                } else {
                    //regular asset transaction
                    CAssetTransfer assetTransfer(assetId, rcp.assetAmount);
                    assetTransfer.BuildAssetTransaction(scriptPubKey);
                    recipient = {scriptPubKey, 0, false};
                }
            } else {
                recipient = {scriptPubKey, rcp.amount, rcp.fSubtractFeeFromAmount};
                total += rcp.amount;
            }
            if (rcp.isFutureOutput) {
                hasFuture = true;
                fpp.futureRecScript = scriptPubKey;
                fpp.maturity = rcp.maturity;
                fpp.locktime = rcp.locktime;
            }
            vecSend.push_back(recipient);
        }
    }
    if (setAddress.size() != nAddresses) {
        return DuplicateAddress;
    }

    CAmount nBalance = m_wallet->getAvailableBalance(coinControl);

    if (total > nBalance) {
        return AmountExceedsBalance;
    }
    std::map <std::string, CAmount> assetsbalance = m_wallet->getAssetsBalance(&coinControl);

    for (auto asset: assetamount) {
        if (asset.second > assetsbalance[asset.first])
            return AmountExceedsBalance;
    }

    CAmount nFeeRequired = 0;
    std::string strFailReason;
    int nChangePosRet = -1;

    auto &newTx = transaction.getWtx();
    newTx = m_wallet->createTransaction(vecSend, coinControl, true /* sign */, nChangePosRet, nFeeRequired,
                                        strFailReason, 0, hasFuture ? &fpp : nullptr);
    transaction.setTransactionFee(nFeeRequired);
    if (fSubtractFeeFromAmount && newTx)
        transaction.reassignAmounts();

    if (!newTx) {
        if (!fSubtractFeeFromAmount && (total + nFeeRequired) > nBalance) {
            return SendAssetsReturn(AmountWithFeeExceedsBalance);
        }
        Q_EMIT message(tr("Send Coins"), QString::fromStdString(strFailReason),
                       CClientUIInterface::MSG_ERROR);
        return TransactionCreationFailed;
    }

    // reject absurdly high fee. (This can never happen because the
    // wallet caps the fee at maxTxFee. This merely serves as a
    // belt-and-suspenders check)
    if (nFeeRequired > m_wallet->getDefaultMaxTxFee())
        return AbsurdFee;

    return SendAssetsReturn(OK);
}

WalletModel::SendAssetsReturn WalletModel::sendAssets(WalletModelTransaction &transaction, bool fIsCoinJoin) {
    QByteArray transaction_array; /* store serialized transaction */
    {
        std::vector <std::pair<std::string, std::string>> vOrderForm;
        for (const SendCoinsRecipient &rcp: transaction.getRecipients()) {
            if (!rcp.message.isEmpty()) // Message from normal raptoreum:URI (raptoreum:XyZ...?message=example)
                vOrderForm.emplace_back("Message", rcp.message.toStdString());
        }

        mapValue_t mapValue;
        if (fIsCoinJoin) {
            mapValue["DS"] = "1";
        }

        auto &newTx = transaction.getWtx();
        std::string rejectReason;
        wallet().commitTransaction(newTx, std::move(mapValue), std::move(vOrderForm));

        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
        ssTx << *newTx;
        transaction_array.append(ssTx.data(), ssTx.size());
    }

    // Add addresses / update labels that we've sent to the address book,
    // and emit coinsSent signal for each recipient
    for (const SendCoinsRecipient &rcp: transaction.getRecipients()) {
        {
            std::string strAddress = rcp.address.toStdString();
            CTxDestination dest = DecodeDestination(strAddress);
            std::string strLabel = rcp.label.toStdString();
            {
                // Check if we have a new address or an updated label
                std::string name;
                if (!m_wallet->getAddress(dest, &name)) {
                    m_wallet->setAddressBook(dest, strLabel, "send");
                } else if (name != strLabel) {
                    m_wallet->setAddressBook(dest, strLabel, ""); // "" means don't change purpose
                }
            }
        }
        Q_EMIT coinsSent(this, rcp, transaction_array);
    }

    checkBalanceChanged(
            m_wallet->getBalances()); // update balance immediately, otherwise there could be a short noticeable delay until pollBalanceChanged hits

    return SendAssetsReturn(OK);
}

OptionsModel *WalletModel::getOptionsModel() {
    return optionsModel;
}

AddressTableModel *WalletModel::getAddressTableModel() {
    return addressTableModel;
}

TransactionTableModel *WalletModel::getTransactionTableModel() {
    return transactionTableModel;
}

RecentRequestsTableModel *WalletModel::getRecentRequestsTableModel() {
    return recentRequestsTableModel;
}

WalletModel::EncryptionStatus WalletModel::getEncryptionStatus() const {
    if (!m_wallet->isCrypted()) {
        return Unencrypted;
    } else if (m_wallet->isLocked(true)) {
        return Locked;
    } else if (m_wallet->isLocked()) {
        return UnlockedForMixingOnly;
    } else {
        return Unlocked;
    }
}

bool WalletModel::setWalletEncrypted(bool encrypted, const SecureString &passphrase) {
    if (encrypted) {
        // Encrypt
        return m_wallet->encryptWallet(passphrase);
    } else {
        // Decrypt -- TODO; not supported yet
        return false;
    }
}

bool WalletModel::setWalletLocked(bool locked, const SecureString &passPhrase, bool fMixing) {
    if (locked) {
        // Lock
        return m_wallet->lock(fMixing);
    } else {
        // Unlock
        return m_wallet->unlock(passPhrase, fMixing);
    }
}

bool WalletModel::changePassphrase(const SecureString &oldPass, const SecureString &newPass) {
    m_wallet->lock(); // Make sure wallet is locked before attempting pass change
    return m_wallet->changeWalletPassphrase(oldPass, newPass);
}

bool WalletModel::autoBackupWallet(QString &strBackupWarningRet, QString &strBackupErrorRet) {
    std::string strBackupWarning;
    std::string strBackupError;
    bool result = m_wallet->autoBackupWallet("", strBackupWarning, strBackupError);
    strBackupWarningRet = QString::fromStdString(strBackupWarning);
    strBackupErrorRet = QString::fromStdString(strBackupError);
    return result;
}

int64_t WalletModel::getKeysLeftSinceAutoBackup() const {
    return m_wallet->getKeysLeftSinceAutoBackup();
}

// Handlers for core signals
static void NotifyUnload(WalletModel *walletModel) {
    qDebug() << "NotifyUnload";
    bool invoked = QMetaObject::invokeMethod(walletModel, "unload", Qt::QueuedConnection);
    assert(invoked);
}

static void NotifyKeyStoreStatusChanged(WalletModel *walletmodel) {
    qDebug() << "NotifyKeyStoreStatusChanged";
    bool invoked = QMetaObject::invokeMethod(walletmodel, "updateStatus", Qt::QueuedConnection);
    assert(invoked);
}

static void NotifyAddressBookChanged(WalletModel *walletmodel,
                                     const CTxDestination &address, const std::string &label, bool isMine,
                                     const std::string &purpose, ChangeType status) {
    QString strAddress = QString::fromStdString(EncodeDestination(address));
    QString strLabel = QString::fromStdString(label);
    QString strPurpose = QString::fromStdString(purpose);

    qDebug() << "NotifyAddressBookChanged: " + strAddress + " " + strLabel + " isMine=" + QString::number(isMine) +
                " purpose=" + strPurpose + " status=" + QString::number(status);
    bool invoked = QMetaObject::invokeMethod(walletmodel, "updateAddressBook", Qt::QueuedConnection,
                                             Q_ARG(QString, strAddress),
                                             Q_ARG(QString, strLabel),
                                             Q_ARG(bool, isMine),
                                             Q_ARG(QString, strPurpose),
                                             Q_ARG(int, status));
    assert(invoked);
}

static void NotifyTransactionChanged(WalletModel *walletmodel, const uint256 &hash, ChangeType status) {
    Q_UNUSED(hash);
    Q_UNUSED(status);
    bool invoked = QMetaObject::invokeMethod(walletmodel, "updateTransaction", Qt::QueuedConnection);
    assert(invoked);
}

static void NotifyISLockReceived(WalletModel *walletmodel) {
    bool invoked = QMetaObject::invokeMethod(walletmodel, "updateNumISLocks", Qt::QueuedConnection);
    assert(invoked);
}

static void NotifyChainLockReceived(WalletModel *walletmodel, int chainLockHeight) {
    bool invoked = QMetaObject::invokeMethod(walletmodel, "updateChainLockHeight", Qt::QueuedConnection,
                                             Q_ARG(int, chainLockHeight));
    assert(invoked);
}

static void ShowProgress(WalletModel *walletmodel, const std::string &title, int nProgress) {
    // emits signal "showProgress"
    bool invoked = QMetaObject::invokeMethod(walletmodel, "showProgress", Qt::QueuedConnection,
                                             Q_ARG(QString, QString::fromStdString(title)),
                                             Q_ARG(int, nProgress));
    assert(invoked);
}

static void NotifyWatchonlyChanged(WalletModel *walletmodel, bool fHaveWatchonly) {
    bool invoked = QMetaObject::invokeMethod(walletmodel, "updateWatchOnlyFlag", Qt::QueuedConnection,
                                             Q_ARG(bool, fHaveWatchonly));
    assert(invoked);
}

static void NotifyCanGetAddressesChanged(WalletModel *walletmodel) {
    bool invoked = QMetaObject::invokeMethod(walletmodel, "canGetAddressesChanged");
    assert(invoked);
}

void WalletModel::subscribeToCoreSignals() {
    // Connect signals to wallet
    m_handler_unload = m_wallet->handleUnload(std::bind(&NotifyUnload, this));
    m_handler_status_changed = m_wallet->handleStatusChanged(std::bind(&NotifyKeyStoreStatusChanged, this));
    m_handler_address_book_changed = m_wallet->handleAddressBookChanged(
            std::bind(NotifyAddressBookChanged, this, pl::_1, pl::_2, pl::_3, pl::_4, pl::_5));
    m_handler_transaction_changed = m_wallet->handleTransactionChanged(
            std::bind(NotifyTransactionChanged, this, pl::_1, pl::_2));
    m_handler_islock_received = m_wallet->handleInstantLockReceived(std::bind(NotifyISLockReceived, this));
    m_handler_chainlock_received = m_wallet->handleChainLockReceived(std::bind(NotifyChainLockReceived, this, pl::_1));
    m_handler_show_progress = m_wallet->handleShowProgress(std::bind(ShowProgress, this, pl::_1, pl::_2));
    m_handler_watch_only_changed = m_wallet->handleWatchOnlyChanged(std::bind(NotifyWatchonlyChanged, this, pl::_1));
    m_handler_can_get_addrs_changed = m_wallet->handleCanGetAddressesChanged(
            std::bind(NotifyCanGetAddressesChanged, this));
}

void WalletModel::unsubscribeFromCoreSignals() {
    // Disconnect signals from wallet
    m_handler_unload->disconnect();
    m_handler_status_changed->disconnect();
    m_handler_address_book_changed->disconnect();
    m_handler_transaction_changed->disconnect();
    m_handler_islock_received->disconnect();
    m_handler_chainlock_received->disconnect();
    m_handler_show_progress->disconnect();
    m_handler_watch_only_changed->disconnect();
    m_handler_can_get_addrs_changed->disconnect();
}

// WalletModel::UnlockContext implementation
WalletModel::UnlockContext WalletModel::requestUnlock(bool fForMixingOnly) {
    EncryptionStatus encStatusOld = getEncryptionStatus();

    // Wallet was completely locked
    bool was_locked = (encStatusOld == Locked);
    // Wallet was unlocked for mixing
    bool was_mixing = (encStatusOld == UnlockedForMixingOnly);
    // Wallet was unlocked for mixing and now user requested to fully unlock it
    bool fMixingToFullRequested = !fForMixingOnly && was_mixing;

    if (was_locked || fMixingToFullRequested) {
        // Request UI to unlock wallet
        Q_EMIT requireUnlock(fForMixingOnly);
    }

    EncryptionStatus encStatusNew = getEncryptionStatus();

    // Wallet was locked, user requested to unlock it for mixing and failed to do so
    bool fMixingUnlockFailed = fForMixingOnly && !(encStatusNew == UnlockedForMixingOnly);
    // Wallet was unlocked for mixing, user requested to fully unlock it and failed
    bool fMixingToFullFailed = fMixingToFullRequested && !(encStatusNew == Unlocked);
    // If wallet is still locked, unlock failed or was cancelled, mark context as invalid
    bool fInvalid = (encStatusNew == Locked) || fMixingUnlockFailed || fMixingToFullFailed;
    // Wallet was not locked in any way or user tried to unlock it for mixing only and succeeded, keep it unlocked
    bool fKeepUnlocked = !was_locked || (fForMixingOnly && !fMixingUnlockFailed);

    return UnlockContext(this, !fInvalid, !fKeepUnlocked, was_mixing);
}

WalletModel::UnlockContext::UnlockContext(WalletModel *_wallet, bool _valid, bool _was_locked, bool _was_mixing) :
        wallet(_wallet),
        valid(_valid),
        was_locked(_was_locked),
        was_mixing(_was_mixing) {
}

WalletModel::UnlockContext::~UnlockContext() {
    if (valid && (was_locked || was_mixing)) {
        wallet->setWalletLocked(true, "", was_mixing);
    }
}

void WalletModel::UnlockContext::CopyFrom(UnlockContext &&rhs) {
    // Transfer context; old object no longer relocks wallet
    *this = rhs;
    rhs.was_locked = false;
    rhs.was_mixing = false;
}

void WalletModel::loadReceiveRequests(std::vector <std::string> &vReceiveRequests) {
    vReceiveRequests = m_wallet->getDestValues("rr"); // receive request
}

bool WalletModel::saveReceiveRequest(const std::string &sAddress, const int64_t nId, const std::string &sRequest) {
    CTxDestination dest = DecodeDestination(sAddress);

    std::stringstream ss;
    ss << nId;
    std::string key = "rr" + ss.str(); // "rr" prefix = "receive request" in destdata

    if (sRequest.empty())
        return m_wallet->eraseDestData(dest, key);
    else
        return m_wallet->addDestData(dest, key, sRequest);
}

bool WalletModel::isWalletEnabled() {
    return !gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET);
}

bool WalletModel::privateKeysDisabled() const {
    return m_wallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS);
}

bool WalletModel::canGetAddresses() const {
    return m_wallet->canGetAddresses();
}

QString WalletModel::getWalletName() const {
    return QString::fromStdString(m_wallet->getWalletName());
}

QString WalletModel::getDisplayName() const {
    const QString name = getWalletName();
    return name.isEmpty() ? "[" + tr("default wallet") + "]" : name;
}

bool WalletModel::isMultiwallet() {
    return m_node.walletClient().getWallets().size() > 1;
}
