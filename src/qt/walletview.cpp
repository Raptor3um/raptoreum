// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/walletview.h>

#include <qt/addressbookpage.h>
#include <qt/askpassphrasedialog.h>
#include <qt/bitcoingui.h>
#include <qt/clientmodel.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/overviewpage.h>
#include <qt/receivecoinsdialog.h>
#include <qt/sendcoinsdialog.h>
#include <qt/sendassetsdialog.h>
#include <qt/createassetsdialog.h>
#include <qt/assetsdialog.h>
#include <qt/signverifymessagedialog.h>
#include <qt/transactionrecord.h>
#include <qt/transactiontablemodel.h>
#include <qt/transactionview.h>
#include <qt/walletmodel.h>

#include <interfaces/node.h>
#include <ui_interface.h>

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

WalletView::WalletView(QWidget *parent) :
        QStackedWidget(parent),
        clientModel(nullptr),
        walletModel(nullptr) {
    // Create tabs
    overviewPage = new OverviewPage();

    transactionsPage = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout();
    QHBoxLayout *hbox_buttons = new QHBoxLayout();
    transactionView = new TransactionView(this);
    vbox->addWidget(transactionView);
    QPushButton *exportButton = new QPushButton(tr("&Export"), this);
    exportButton->setToolTip(tr("Export the data in the current tab to a file"));
    hbox_buttons->addStretch();

    // Sum of selected transactions
    QLabel *transactionSumLabel = new QLabel(); // Label
    transactionSumLabel->setObjectName("transactionSumLabel"); // Label ID as CSS-reference
    transactionSumLabel->setText(tr("Selected amount:"));
    hbox_buttons->addWidget(transactionSumLabel);

    transactionSum = new QLabel(); // Amount
    transactionSum->setObjectName("transactionSum"); // Label ID as CSS-reference
    transactionSum->setMinimumSize(200, 8);
    transactionSum->setTextInteractionFlags(Qt::TextSelectableByMouse);

    GUIUtil::setFont({transactionSumLabel,
                      transactionSum,
                     }, GUIUtil::FontWeight::Bold, 14);
    GUIUtil::updateFonts();

    hbox_buttons->addWidget(transactionSum);

    hbox_buttons->addWidget(exportButton);
    vbox->addLayout(hbox_buttons);
    transactionsPage->setLayout(vbox);

    receiveCoinsPage = new ReceiveCoinsDialog();
    sendCoinsPage = new SendCoinsDialog();
    sendAssetsPage = new SendAssetsDialog();
    createAssetsPage = new CreateAssetsDialog();
    myAssetsPage = new AssetsDialog();
    coinJoinCoinsPage = new SendCoinsDialog(true);

    usedSendingAddressesPage = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::SendingTab, this);
    usedReceivingAddressesPage = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::ReceivingTab, this);

    addWidget(overviewPage);
    addWidget(transactionsPage);
    addWidget(receiveCoinsPage);
    addWidget(sendCoinsPage);
    addWidget(coinJoinCoinsPage);
    addWidget(myAssetsPage);
    addWidget(sendAssetsPage);
    addWidget(createAssetsPage);

    QSettings settings;
    if (settings.value("fShowSmartnodesTab").toBool()) {
        smartnodeListPage = new SmartnodeList();
        addWidget(smartnodeListPage);
    }

    // Clicking on a transaction on the overview pre-selects the transaction on the transaction history page
    connect(overviewPage, &OverviewPage::transactionClicked, transactionView,
            static_cast<void (TransactionView::*)(const QModelIndex &)>(&TransactionView::focusTransaction));
    connect(overviewPage, &OverviewPage::outOfSyncWarningClicked, this, &WalletView::requestedSyncWarningInfo);

    connect(myAssetsPage, SIGNAL(assetSendClicked(std::string)), sendAssetsPage, SLOT(focusAsset(std::string)));

    // Highlight transaction after send
    connect(sendCoinsPage, &SendCoinsDialog::coinsSent, transactionView,
            static_cast<void (TransactionView::*)(const uint256 &)>(&TransactionView::focusTransaction));
    connect(sendAssetsPage, &SendAssetsDialog::coinsSent, transactionView,
            static_cast<void (TransactionView::*)(const uint256 &)>(&TransactionView::focusTransaction));
    connect(coinJoinCoinsPage, &SendCoinsDialog::coinsSent, transactionView,
            static_cast<void (TransactionView::*)(const uint256 &)>(&TransactionView::focusTransaction));

    // Update wallet with sum of selected transactions
    connect(transactionView, &TransactionView::trxAmount, this, &WalletView::trxAmount);

    // Clicking on "Export" allows to export the transaction list
    connect(exportButton, &QPushButton::clicked, transactionView, &TransactionView::exportClicked);

    // Pass through messages from SendCoinsDialog
    connect(sendCoinsPage, &SendCoinsDialog::message, this, &WalletView::message);
    connect(sendAssetsPage, &SendAssetsDialog::message, this, &WalletView::message);
    connect(coinJoinCoinsPage, &SendCoinsDialog::message, this, &WalletView::message);

    // Pass through messages from transactionView
    connect(transactionView, &TransactionView::message, this, &WalletView::message);

    GUIUtil::disableMacFocusRect(this);
}

WalletView::~WalletView() {
}

void WalletView::setBitcoinGUI(BitcoinGUI *gui) {
    if (gui) {
        // Clicking on a transaction on the overview page simply sends you to transaction history page
        connect(overviewPage, &OverviewPage::transactionClicked, gui, &BitcoinGUI::gotoHistoryPage);

        // Navigate to transaction history page after send
        connect(sendCoinsPage, &SendCoinsDialog::coinsSent, gui, &BitcoinGUI::gotoHistoryPage);
        connect(sendAssetsPage, &SendAssetsDialog::coinsSent, gui, &BitcoinGUI::gotoHistoryPage);
        connect(coinJoinCoinsPage, &SendCoinsDialog::coinsSent, gui, &BitcoinGUI::gotoHistoryPage);
        connect(myAssetsPage, SIGNAL(assetSendClicked(std::string)), gui, SLOT(gotoSendAssetsPage()));
        // Receive and report messages
        connect(this, &WalletView::message, [gui](const QString &title, const QString &message, unsigned int style) {
            gui->message(title, message, style);
        });

        // Pass through encryption status changed signals
        connect(this, &WalletView::encryptionStatusChanged, gui, &BitcoinGUI::updateWalletStatus);

        // Pass through transaction notifications
        connect(this, &WalletView::incomingTransaction, gui, &BitcoinGUI::incomingTransaction);

        // Connect HD enabled state signal
        connect(this, &WalletView::hdEnabledStatusChanged, gui, &BitcoinGUI::updateWalletStatus);
    }
}

void WalletView::setClientModel(ClientModel *_clientModel) {
    this->clientModel = _clientModel;

    if (overviewPage != nullptr) {
        overviewPage->setClientModel(_clientModel);
    }
    if (sendCoinsPage != nullptr) {
        sendCoinsPage->setClientModel(_clientModel);
    }
    if (sendAssetsPage != nullptr) {
        sendAssetsPage->setClientModel(_clientModel);
    }
    if (createAssetsPage != nullptr) {
        createAssetsPage->setClientModel(_clientModel);
    }
    if (myAssetsPage != nullptr) {
        myAssetsPage->setClientModel(_clientModel);
    }
    if (coinJoinCoinsPage != nullptr) {
        coinJoinCoinsPage->setClientModel(_clientModel);
    }

    overviewPage->setClientModel(_clientModel);
    sendCoinsPage->setClientModel(_clientModel);
    coinJoinCoinsPage->setClientModel(_clientModel);
    QSettings settings;
    if (settings.value("fShowSmartnodesTab").toBool() && smartnodeListPage != nullptr) {
        smartnodeListPage->setClientModel(_clientModel);
    }
}

void WalletView::setWalletModel(WalletModel *_walletModel) {
    this->walletModel = _walletModel;

    // Put transaction list in tabs
    transactionView->setModel(_walletModel);
    overviewPage->setWalletModel(_walletModel);
    QSettings settings;
    if (settings.value("fShowSmartnodesTab").toBool()) {
        smartnodeListPage->setWalletModel(_walletModel);
    }
    receiveCoinsPage->setModel(_walletModel);
    sendCoinsPage->setModel(_walletModel);
    sendAssetsPage->setModel(_walletModel);
    createAssetsPage->setModel(_walletModel);
    myAssetsPage->setModel(_walletModel);
    coinJoinCoinsPage->setModel(_walletModel);
    usedReceivingAddressesPage->setModel(_walletModel ? _walletModel->getAddressTableModel() : nullptr);
    usedSendingAddressesPage->setModel(_walletModel ? _walletModel->getAddressTableModel() : nullptr);

    if (_walletModel) {
        // Receive and pass through messages from wallet model
        connect(_walletModel, &WalletModel::message, this, &WalletView::message);

        // Handle changes in encryption status
        connect(_walletModel, &WalletModel::encryptionStatusChanged, this, &WalletView::encryptionStatusChanged);
        updateEncryptionStatus();

        // update HD status
        Q_EMIT hdEnabledStatusChanged();

        // Balloon pop-up for new transaction
        connect(_walletModel->getTransactionTableModel(), &TransactionTableModel::rowsInserted, this,
                &WalletView::processNewTransaction);

        // Ask for passphrase if needed
        connect(_walletModel, &WalletModel::requireUnlock, this, &WalletView::unlockWallet);

        // Show progress dialog
        connect(_walletModel, &WalletModel::showProgress, this, &WalletView::showProgress);
    }
}

void WalletView::processNewTransaction(const QModelIndex &parent, int start, int /*end*/) {
    // Prevent balloon-spam when initial block download is in progress
    if (!walletModel || !clientModel || clientModel->node().isInitialBlockDownload())
        return;

    TransactionTableModel *ttm = walletModel->getTransactionTableModel();
    if (!ttm || ttm->processingQueuedTransactions())
        return;

    QModelIndex index = ttm->index(start, 0, parent);
    QSettings settings;
    if (!settings.value("fShowCoinJoinPopups").toBool()) {
        QVariant nType = ttm->data(index, TransactionTableModel::TypeRole);
        if (nType == TransactionRecord::CoinJoinMixing ||
            nType == TransactionRecord::CoinJoinCollateralPayment ||
            nType == TransactionRecord::CoinJoinMakeCollaterals ||
            nType == TransactionRecord::CoinJoinCreateDenominations)
            return;
    }

    QString date = ttm->index(start, TransactionTableModel::Date, parent).data().toString();
    qint64 amount = ttm->index(start, TransactionTableModel::Amount, parent).data(Qt::EditRole).toULongLong();
    QString type = ttm->index(start, TransactionTableModel::Type, parent).data().toString();
    QString address = ttm->data(index, TransactionTableModel::AddressRole).toString();
    QString label = GUIUtil::HtmlEscape(ttm->data(index, TransactionTableModel::LabelRole).toString());

    Q_EMIT incomingTransaction(date, walletModel->getOptionsModel()->getDisplayUnit(), amount, type, address, label,
                               GUIUtil::HtmlEscape(walletModel->getWalletName()));

    sendAssetsPage->updateAssetList();
    myAssetsPage->updateAssetBalance();
}

void WalletView::gotoOverviewPage() {
    setCurrentWidget(overviewPage);
}

void WalletView::gotoHistoryPage() {
    setCurrentWidget(transactionsPage);
}

void WalletView::gotoSmartnodePage() {
    QSettings settings;
    if (settings.value("fShowSmartnodesTab").toBool()) {
        setCurrentWidget(smartnodeListPage);
    }
}

void WalletView::gotoReceiveCoinsPage() {
    setCurrentWidget(receiveCoinsPage);
}

void WalletView::gotoSendCoinsPage(QString addr) {
    setCurrentWidget(sendCoinsPage);
    sendCoinsPage->OnDisplay();
    if (!addr.isEmpty()) {
        sendCoinsPage->setAddress(addr);
    }
}

void WalletView::gotoSendAssetsPage(QString addr) {
    static bool fFirstVisit = true;

    if (fFirstVisit) {
        fFirstVisit = false;
        sendAssetsPage->updateAssetList();
    }

    setCurrentWidget(sendAssetsPage);
    sendAssetsPage->OnDisplay();
    if (!addr.isEmpty()) {
        sendAssetsPage->setAddress(addr);
    }
}

void WalletView::gotoCreateAssetsPage() {
    setCurrentWidget(createAssetsPage);
}

void WalletView::gotoMyAssetsPage() {
    setCurrentWidget(myAssetsPage);
}

void WalletView::gotoCoinJoinCoinsPage(QString addr) {
    setCurrentWidget(coinJoinCoinsPage);

    if (!addr.isEmpty())
        coinJoinCoinsPage->setAddress(addr);
}

void WalletView::gotoSignMessageTab(QString addr) {
    // calls show() in showTab_SM()
    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(this);
    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
    signVerifyMessageDialog->setModel(walletModel);
    signVerifyMessageDialog->showTab_SM(true);

    if (!addr.isEmpty())
        signVerifyMessageDialog->setAddress_SM(addr);
}

void WalletView::gotoVerifyMessageTab(QString addr) {
    // calls show() in showTab_VM()
    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(this);
    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
    signVerifyMessageDialog->setModel(walletModel);
    signVerifyMessageDialog->showTab_VM(true);

    if (!addr.isEmpty())
        signVerifyMessageDialog->setAddress_VM(addr);
}

bool WalletView::handlePaymentRequest(const SendCoinsRecipient &recipient) {
    return sendCoinsPage->handlePaymentRequest(recipient);
}

void WalletView::showOutOfSyncWarning(bool fShow) {
    overviewPage->showOutOfSyncWarning(fShow);
}

void WalletView::updateEncryptionStatus() {
    Q_EMIT encryptionStatusChanged();
}

void WalletView::encryptWallet() {
    if (!walletModel)
        return;
    AskPassphraseDialog dlg(AskPassphraseDialog::Encrypt, this);
    dlg.setModel(walletModel);
    dlg.exec();

    updateEncryptionStatus();
}

void WalletView::backupWallet() {
    QString filename = GUIUtil::getSaveFileName(this,
                                                tr("Backup Wallet"), QString(),
                                                tr("Wallet Data (*.dat)"), nullptr);

    if (filename.isEmpty())
        return;

    if (!walletModel->wallet().backupWallet(filename.toLocal8Bit().data())) {
        Q_EMIT message(tr("Backup Failed"),
                       tr("There was an error trying to save the wallet data to %1.").arg(filename),
                       CClientUIInterface::MSG_ERROR);
    } else {
        Q_EMIT message(tr("Backup Successful"), tr("The wallet data was successfully saved to %1.").arg(filename),
                       CClientUIInterface::MSG_INFORMATION);
    }
}

void WalletView::changePassphrase() {
    AskPassphraseDialog dlg(AskPassphraseDialog::ChangePass, this);
    dlg.setModel(walletModel);
    dlg.exec();
}

void WalletView::unlockWallet(bool fForMixingOnly) {
    if (!walletModel)
        return;
    // Unlock wallet when requested by wallet model

    if (walletModel->getEncryptionStatus() == WalletModel::Locked ||
        walletModel->getEncryptionStatus() == WalletModel::UnlockedForMixingOnly) {
        AskPassphraseDialog dlg(fForMixingOnly ? AskPassphraseDialog::UnlockMixing : AskPassphraseDialog::Unlock, this);
        dlg.setModel(walletModel);
        dlg.exec();
    }
}

void WalletView::lockWallet() {
    if (!walletModel)
        return;

    walletModel->setWalletLocked(true);
}

void WalletView::usedSendingAddresses() {
    if (!walletModel)
        return;

    GUIUtil::bringToFront(usedSendingAddressesPage);
}

void WalletView::usedReceivingAddresses() {
    if (!walletModel)
        return;

    GUIUtil::bringToFront(usedReceivingAddressesPage);
}

void WalletView::showProgress(const QString &title, int nProgress) {
    if (nProgress == 0) {
        progressDialog = new QProgressDialog(title, tr("Cancel"), 0, 100, this);
        GUIUtil::PolishProgressDialog(progressDialog);
        progressDialog->setWindowModality(Qt::ApplicationModal);
        progressDialog->setMinimumDuration(0);
        progressDialog->setAutoClose(false);
        progressDialog->setValue(0);
    } else if (nProgress == 100) {
        if (progressDialog) {
            progressDialog->close();
            progressDialog->deleteLater();
            progressDialog = nullptr;
        }
    } else if (progressDialog) {
        if (progressDialog->wasCanceled()) {
            getWalletModel()->wallet().abortRescan();
        } else {
            progressDialog->setValue(nProgress);
        }
    }
}

void WalletView::requestedSyncWarningInfo() {
    Q_EMIT outOfSyncWarningClicked();
}

/** Update wallet with the sum of the selected transactions */
void WalletView::trxAmount(QString amount) {
    transactionSum->setText(amount);
}
