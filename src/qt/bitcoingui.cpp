// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2019 The Dash Core developers
// Copyright (c) 2020 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/raptoreum-config.h"
#endif

#include "bitcoingui.h"

#include "bitcoinunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "modaloverlay.h"
#include "networkstyle.h"
#include "notificator.h"
#include "openuridialog.h"
#include "optionsdialog.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "rpcconsole.h"
#include "utilitydialog.h"

#ifdef ENABLE_WALLET
#include "privatesend/privatesend-client.h"
#include "walletframe.h"
#include "walletmodel.h"
#endif // ENABLE_WALLET

#ifdef Q_OS_MAC
#include "macdockiconhandler.h"
#endif

#include "chainparams.h"
#include "init.h"
#include "ui_interface.h"
#include "util.h"
#include "smartnode/smartnode-sync.h"
#include "smartnodelist.h"

#include <iostream>

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopWidget>
#include <QDragEnterEvent>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#if QT_VERSION < 0x050000
#include <QTextDocument>
#include <QUrl>
#else
#include <QUrlQuery>
#endif

const std::string BitcoinGUI::DEFAULT_UIPLATFORM =
#if defined(Q_OS_MAC)
        "macosx"
#elif defined(Q_OS_WIN)
        "windows"
#else
        "other"
#endif
        ;

/** Display name for default wallet name. Uses tilde to avoid name
 * collisions in the future with additional wallets */
const QString BitcoinGUI::DEFAULT_WALLET = "~Default";

BitcoinGUI::BitcoinGUI(const PlatformStyle *_platformStyle, const NetworkStyle *networkStyle, QWidget *parent) :
    QMainWindow(parent),
    enableWallet(false),
    clientModel(0),
    walletFrame(0),
    unitDisplayControl(0),
    labelWalletEncryptionIcon(0),
    labelWalletHDStatusIcon(0),
    labelConnectionsIcon(0),
    labelBlocksIcon(0),
    progressBarLabel(0),
    progressBar(0),
    progressDialog(0),
    appMenuBar(0),
    overviewAction(0),
    historyAction(0),
    smartnodeAction(0),
    quitAction(0),
    sendCoinsAction(0),
    sendCoinsMenuAction(0),
    sendFuturesAction(0),
    usedSendingAddressesAction(0),
    usedReceivingAddressesAction(0),
    signMessageAction(0),
    verifyMessageAction(0),
    aboutAction(0),
    receiveCoinsAction(0),
    receiveCoinsMenuAction(0),
    optionsAction(0),
    toggleHideAction(0),
    encryptWalletAction(0),
    backupWalletAction(0),
    changePassphraseAction(0),
    aboutQtAction(0),
    openRPCConsoleAction(0),
    openAction(0),
    showHelpMessageAction(0),
    showPrivateSendHelpAction(0),
    trayIcon(0),
    trayIconMenu(0),
    dockIconMenu(0),
    notificator(0),
    rpcConsole(0),
    helpMessageDialog(0),
    modalOverlay(0),
    prevBlocks(0),
    spinnerFrame(0),
    platformStyle(_platformStyle)
{
    /* Open CSS when configured */
    this->setStyleSheet(GUIUtil::loadStyleSheet());

    QSettings settings;
    if (!restoreGeometry(settings.value("MainWindowGeometry").toByteArray())) {
        // Restore failed (perhaps missing setting), center the window
        move(QApplication::desktop()->availableGeometry().center() - frameGeometry().center());
    }

    QString windowTitle = tr(PACKAGE_NAME) + " - ";
#ifdef ENABLE_WALLET
    enableWallet = WalletModel::isWalletEnabled();
#endif // ENABLE_WALLET
    if(enableWallet)
    {
        windowTitle += tr("Wallet");
    } else {
        windowTitle += tr("Node");
    }
    QString userWindowTitle = QString::fromStdString(gArgs.GetArg("-windowtitle", ""));
    if(!userWindowTitle.isEmpty()) windowTitle += " - " + userWindowTitle;
    windowTitle += " " + networkStyle->getTitleAddText();
#ifndef Q_OS_MAC
    QApplication::setWindowIcon(networkStyle->getTrayAndWindowIcon());
    setWindowIcon(networkStyle->getTrayAndWindowIcon());
#else
    MacDockIconHandler::instance()->setIcon(networkStyle->getAppIcon());
#endif
    setWindowTitle(windowTitle);

#if defined(Q_OS_MAC) && QT_VERSION < 0x050000
    // This property is not implemented in Qt 5. Setting it has no effect.
    // A replacement API (QtMacUnifiedToolBar) is available in QtMacExtras.
    setUnifiedTitleAndToolBarOnMac(true);
#endif

    rpcConsole = new RPCConsole(_platformStyle, 0);
    helpMessageDialog = new HelpMessageDialog(this, HelpMessageDialog::cmdline);
#ifdef ENABLE_WALLET
    if(enableWallet)
    {
        /** Create wallet frame*/
        walletFrame = new WalletFrame(_platformStyle, this);
    } else
#endif // ENABLE_WALLET
    {
        /* When compiled without wallet or -disablewallet is provided,
         * the central widget is the rpc console.
         */
        setCentralWidget(rpcConsole);
    }

    // Accept D&D of URIs
    setAcceptDrops(true);

    // Create actions for the toolbar, menu bar and tray/dock icon
    // Needs walletFrame to be initialized
    createActions();

    // Create application menu bar
    createMenuBar();

    // Create the toolbars
    createToolBars();

    // Create system tray icon and notification
    createTrayIcon(networkStyle);

    // Create status bar
    statusBar();

    // Disable size grip because it looks ugly and nobody needs it
    statusBar()->setSizeGripEnabled(false);

    // Status bar notification icons
    QFrame *frameBlocks = new QFrame();
    frameBlocks->setContentsMargins(0,0,0,0);
    frameBlocks->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    QHBoxLayout *frameBlocksLayout = new QHBoxLayout(frameBlocks);
    frameBlocksLayout->setContentsMargins(3,0,3,0);
    frameBlocksLayout->setSpacing(3);
    unitDisplayControl = new UnitDisplayStatusBarControl(platformStyle);
    labelWalletEncryptionIcon = new QLabel();
    labelWalletHDStatusIcon = new QLabel();
    labelConnectionsIcon = new GUIUtil::ClickableLabel();

    labelBlocksIcon = new GUIUtil::ClickableLabel();
    if(enableWallet)
    {
        frameBlocksLayout->addStretch();
        frameBlocksLayout->addWidget(unitDisplayControl);
        frameBlocksLayout->addStretch();
        frameBlocksLayout->addWidget(labelWalletEncryptionIcon);
        frameBlocksLayout->addWidget(labelWalletHDStatusIcon);
    }
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelConnectionsIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelBlocksIcon);
    frameBlocksLayout->addStretch();

    // Progress bar and label for blocks download
    progressBarLabel = new QLabel();
    progressBarLabel->setVisible(true);
    progressBar = new GUIUtil::ProgressBar();
    progressBar->setAlignment(Qt::AlignCenter);
    progressBar->setVisible(true);

    // Override style sheet for progress bar for styles that have a segmented progress bar,
    // as they make the text unreadable (workaround for issue #1071)
    // See https://qt-project.org/doc/qt-4.8/gallery.html
    QString curStyle = QApplication::style()->metaObject()->className();
    if(curStyle == "QWindowsStyle" || curStyle == "QWindowsXPStyle")
    {
        progressBar->setStyleSheet("QProgressBar { background-color: #F8F8F8; border: 1px solid grey; border-radius: 7px; padding: 1px; text-align: center; } QProgressBar::chunk { background: QLinearGradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #00CCFF, stop: 1 #33CCFF); border-radius: 7px; margin: 0px; }");
    }

#ifndef Q_OS_MAC
    // Apply some styling to scrollbars
    QString theme = settings.value("theme", "").toString();
    if (theme != "trad") { // No scrollbar styling for the traditional theme
        QFile qFile(QString(":/css/scrollbars"));
        QString styleSheet;
        if (qFile.open(QFile::ReadOnly)) {
            styleSheet = QLatin1String(qFile.readAll());
        }
        this->setStyleSheet(this->styleSheet().append(styleSheet));
    }
#endif

    statusBar()->addWidget(progressBarLabel);
    statusBar()->addWidget(progressBar);
    statusBar()->addPermanentWidget(frameBlocks);

    // Install event filter to be able to catch status tip events (QEvent::StatusTip)
    this->installEventFilter(this);

    // Initially wallet actions should be disabled
    setWalletActionsEnabled(false);

    // Subscribe to notifications from core
    subscribeToCoreSignals();

    // Jump to peers tab by clicking on connections icon
    connect(labelConnectionsIcon, SIGNAL(clicked(QPoint)), this, SLOT(showPeers()));

    modalOverlay = new ModalOverlay(this->centralWidget());
#ifdef ENABLE_WALLET
    if(enableWallet) {
        connect(walletFrame, SIGNAL(requestedSyncWarningInfo()), this, SLOT(showModalOverlay()));
        connect(labelBlocksIcon, SIGNAL(clicked(QPoint)), this, SLOT(showModalOverlay()));
        connect(progressBar, SIGNAL(clicked(QPoint)), this, SLOT(showModalOverlay()));
    }
#endif

#ifdef Q_OS_MAC
    m_app_nap_inhibitor = new CAppNapInhibitor;
#endif

    incomingTransactionsTimer = new QTimer(this);
    incomingTransactionsTimer->setSingleShot(true);
    connect(incomingTransactionsTimer, SIGNAL(timeout()), SLOT(showIncomingTransactions()));
}

BitcoinGUI::~BitcoinGUI()
{
    // Unsubscribe from notifications from core
    unsubscribeFromCoreSignals();

    QSettings settings;
    settings.setValue("MainWindowGeometry", saveGeometry());
    if(trayIcon) // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
        trayIcon->hide();
#ifdef Q_OS_MAC
    delete m_app_nap_inhibitor;
    delete appMenuBar;
    MacDockIconHandler::cleanup();
#endif

    delete rpcConsole;
}

void BitcoinGUI::createActions()
{
    QActionGroup *tabGroup = new QActionGroup(this);

    overviewAction = new QAction(tr("&Overview"), this);
    overviewAction->setStatusTip(tr("Show general overview of wallet"));
    overviewAction->setToolTip(overviewAction->statusTip());
    overviewAction->setCheckable(true);
#ifdef Q_OS_MAC
    overviewAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_1));
#else
    overviewAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_1));
#endif
    tabGroup->addAction(overviewAction);

    sendCoinsAction = new QAction(tr("&Send"), this);
    sendCoinsAction->setStatusTip(tr("Send coins to a Raptoreum address"));
    sendCoinsAction->setToolTip(sendCoinsAction->statusTip());
    sendCoinsAction->setCheckable(true);
#ifdef Q_OS_MAC
    sendCoinsAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_2));
#else
    sendCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_2));
#endif
    tabGroup->addAction(sendCoinsAction);

    sendCoinsMenuAction = new QAction(QIcon(":/icons/send"), sendCoinsAction->text(), this);
    sendCoinsMenuAction->setStatusTip(sendCoinsAction->statusTip());
    sendCoinsMenuAction->setToolTip(sendCoinsMenuAction->statusTip());

    receiveCoinsAction = new QAction(tr("&Receive"), this);
    receiveCoinsAction->setStatusTip(tr("Request payments (generates QR codes and raptoreum: URIs)"));
    receiveCoinsAction->setToolTip(receiveCoinsAction->statusTip());
    receiveCoinsAction->setCheckable(true);
#ifdef Q_OS_MAC
    receiveCoinsAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_3));
#else
    receiveCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_3));
#endif
    tabGroup->addAction(receiveCoinsAction);

    receiveCoinsMenuAction = new QAction(QIcon(":/icons/receiving_addresses"), receiveCoinsAction->text(), this);
    receiveCoinsMenuAction->setStatusTip(receiveCoinsAction->statusTip());
    receiveCoinsMenuAction->setToolTip(receiveCoinsMenuAction->statusTip());

    historyAction = new QAction(tr("&Transactions"), this);
    historyAction->setStatusTip(tr("Browse transaction history"));
    historyAction->setToolTip(historyAction->statusTip());
    historyAction->setCheckable(true);
#ifdef Q_OS_MAC
    historyAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_4));
#else
    historyAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_4));
#endif
    tabGroup->addAction(historyAction);

    sendFuturesAction = new QAction(tr("&Futures"), this);
    sendFuturesAction->setStatusTip(tr("Send futures to a Raptoreum address"));
    sendFuturesAction->setToolTip(sendFuturesAction->statusTip());
    sendFuturesAction->setCheckable(true);
#ifdef Q_OS_MAC
    sendFuturesAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_5));
#else
    sendFuturesAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_5));
#endif
    tabGroup->addAction(sendFuturesAction);

#ifdef ENABLE_WALLET
    QSettings settings;
    if (!fLiteMode && settings.value("fShowSmartnodesTab").toBool()) {
        smartnodeAction = new QAction(tr("&Smartnodes"), this);
        smartnodeAction->setStatusTip(tr("Browse smartnodes"));
        smartnodeAction->setToolTip(smartnodeAction->statusTip());
        smartnodeAction->setCheckable(true);
#ifdef Q_OS_MAC
        smartnodeAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_6));
#else
        smartnodeAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_6));
#endif
        tabGroup->addAction(smartnodeAction);
        connect(smartnodeAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
        connect(smartnodeAction, SIGNAL(triggered()), this, SLOT(gotoSmartnodePage()));
    }

    // These showNormalIfMinimized are needed because Send Coins and Receive Coins
    // can be triggered from the tray menu, and need to show the GUI to be useful.
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(gotoOverviewPage()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect(sendCoinsMenuAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendCoinsMenuAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect(sendFuturesAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendFuturesAction, SIGNAL(triggered()), this, SLOT(gotoSendFuturesPage()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(gotoReceiveCoinsPage()));
    connect(receiveCoinsMenuAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(receiveCoinsMenuAction, SIGNAL(triggered()), this, SLOT(gotoReceiveCoinsPage()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(gotoHistoryPage()));
#endif // ENABLE_WALLET

    quitAction = new QAction(QIcon(":/icons/quit"), tr("E&xit"), this);
    quitAction->setStatusTip(tr("Quit application"));
    quitAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    quitAction->setMenuRole(QAction::QuitRole);
    aboutAction = new QAction(QIcon(":/icons/about"), tr("&About %1").arg(tr(PACKAGE_NAME)), this);
    aboutAction->setStatusTip(tr("Show information about Raptoreum Core"));
    aboutAction->setMenuRole(QAction::AboutRole);
    aboutAction->setEnabled(false);
    aboutQtAction = new QAction(QIcon(":/icons/about_qt"), tr("About &Qt"), this);
    aboutQtAction->setStatusTip(tr("Show information about Qt"));
    aboutQtAction->setMenuRole(QAction::AboutQtRole);
    optionsAction = new QAction(QIcon(":/icons/options"), tr("&Options..."), this);
    optionsAction->setStatusTip(tr("Modify configuration options for %1").arg(tr(PACKAGE_NAME)));
    optionsAction->setMenuRole(QAction::PreferencesRole);
    optionsAction->setEnabled(false);
    toggleHideAction = new QAction(QIcon(":/icons/about"), tr("&Show / Hide"), this);
    toggleHideAction->setStatusTip(tr("Show or hide the main Window"));

    encryptWalletAction = new QAction(QIcon(":/icons/lock_closed"), tr("&Encrypt Wallet..."), this);
    encryptWalletAction->setStatusTip(tr("Encrypt the private keys that belong to your wallet"));
    encryptWalletAction->setCheckable(true);
    backupWalletAction = new QAction(QIcon(":/icons/filesave"), tr("&Backup Wallet..."), this);
    backupWalletAction->setStatusTip(tr("Backup wallet to another location"));
    changePassphraseAction = new QAction(QIcon(":/icons/key"), tr("&Change Passphrase..."), this);
    changePassphraseAction->setStatusTip(tr("Change the passphrase used for wallet encryption"));
    unlockWalletAction = new QAction(tr("&Unlock Wallet..."), this);
    unlockWalletAction->setToolTip(tr("Unlock wallet"));
    lockWalletAction = new QAction(tr("&Lock Wallet"), this);
    signMessageAction = new QAction(QIcon(":/icons/edit"), tr("Sign &message..."), this);
    signMessageAction->setStatusTip(tr("Sign messages with your Raptoreum addresses to prove you own them"));
    verifyMessageAction = new QAction(QIcon(":/icons/transaction_0"), tr("&Verify message..."), this);
    verifyMessageAction->setStatusTip(tr("Verify messages to ensure they were signed with specified Raptoreum addresses"));

    openInfoAction = new QAction(QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation), tr("&Information"), this);
    openInfoAction->setStatusTip(tr("Show diagnostic information"));
    openRPCConsoleAction = new QAction(QIcon(":/icons/debugwindow"), tr("&Debug console"), this);
    openRPCConsoleAction->setStatusTip(tr("Open debugging console"));
    openGraphAction = new QAction(QIcon(":/icons/connect_4"), tr("&Network Monitor"), this);
    openGraphAction->setStatusTip(tr("Show network monitor"));
    openPeersAction = new QAction(QIcon(":/icons/connect_4"), tr("&Peers list"), this);
    openPeersAction->setStatusTip(tr("Show peers info"));
    openRepairAction = new QAction(QIcon(":/icons/options"), tr("Wallet &Repair"), this);
    openRepairAction->setStatusTip(tr("Show wallet repair options"));
    openConfEditorAction = new QAction(QIcon(":/icons/edit"), tr("Open Wallet &Configuration File"), this);
    openConfEditorAction->setStatusTip(tr("Open configuration file"));
    showBackupsAction = new QAction(QIcon(":/icons/browse"), tr("Show Automatic &Backups"), this);
    showBackupsAction->setStatusTip(tr("Show automatically created wallet backups"));
    // initially disable the debug window menu items
    openInfoAction->setEnabled(false);
    openRPCConsoleAction->setEnabled(false);
    openGraphAction->setEnabled(false);
    openPeersAction->setEnabled(false);
    openRepairAction->setEnabled(false);

    usedSendingAddressesAction = new QAction(QIcon(":/icons/address-book"), tr("&Sending addresses..."), this);
    usedSendingAddressesAction->setStatusTip(tr("Show the list of used sending addresses and labels"));
    usedReceivingAddressesAction = new QAction(QIcon(":/icons/address-book"), tr("&Receiving addresses..."), this);
    usedReceivingAddressesAction->setStatusTip(tr("Show the list of used receiving addresses and labels"));

    openAction = new QAction(QApplication::style()->standardIcon(QStyle::SP_DirOpenIcon), tr("Open &URI..."), this);
    openAction->setStatusTip(tr("Open a raptoreum: URI or payment request"));

    showHelpMessageAction = new QAction(QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation), tr("&Command-line options"), this);
    showHelpMessageAction->setMenuRole(QAction::NoRole);
    showHelpMessageAction->setStatusTip(tr("Show the %1 help message to get a list with possible Raptoreum command-line options").arg(tr(PACKAGE_NAME)));

    showPrivateSendHelpAction = new QAction(QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation), tr("&PrivateSend information"), this);
    showPrivateSendHelpAction->setMenuRole(QAction::NoRole);
    showPrivateSendHelpAction->setStatusTip(tr("Show the PrivateSend basic information"));

    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(aboutClicked()));
    connect(aboutQtAction, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    connect(optionsAction, SIGNAL(triggered()), this, SLOT(optionsClicked()));
    connect(toggleHideAction, SIGNAL(triggered()), this, SLOT(toggleHidden()));
    connect(showHelpMessageAction, SIGNAL(triggered()), this, SLOT(showHelpMessageClicked()));
    connect(showPrivateSendHelpAction, SIGNAL(triggered()), this, SLOT(showPrivateSendHelpClicked()));

    // Jump directly to tabs in RPC-console
    connect(openInfoAction, SIGNAL(triggered()), this, SLOT(showInfo()));
    connect(openRPCConsoleAction, SIGNAL(triggered()), this, SLOT(showConsole()));
    connect(openGraphAction, SIGNAL(triggered()), this, SLOT(showGraph()));
    connect(openPeersAction, SIGNAL(triggered()), this, SLOT(showPeers()));
    connect(openRepairAction, SIGNAL(triggered()), this, SLOT(showRepair()));

    // Open configs and backup folder from menu
    connect(openConfEditorAction, SIGNAL(triggered()), this, SLOT(showConfEditor()));
    connect(showBackupsAction, SIGNAL(triggered()), this, SLOT(showBackups()));

    // Get restart command-line parameters and handle restart
    connect(rpcConsole, SIGNAL(handleRestart(QStringList)), this, SLOT(handleRestart(QStringList)));
    
    // prevents an open debug window from becoming stuck/unusable on client shutdown
    connect(quitAction, SIGNAL(triggered()), rpcConsole, SLOT(hide()));

#ifdef ENABLE_WALLET
    if(walletFrame)
    {
        connect(encryptWalletAction, SIGNAL(triggered(bool)), walletFrame, SLOT(encryptWallet(bool)));
        connect(backupWalletAction, SIGNAL(triggered()), walletFrame, SLOT(backupWallet()));
        connect(changePassphraseAction, SIGNAL(triggered()), walletFrame, SLOT(changePassphrase()));
        connect(unlockWalletAction, SIGNAL(triggered()), walletFrame, SLOT(unlockWallet()));
        connect(lockWalletAction, SIGNAL(triggered()), walletFrame, SLOT(lockWallet()));
        connect(signMessageAction, SIGNAL(triggered()), this, SLOT(gotoSignMessageTab()));
        connect(verifyMessageAction, SIGNAL(triggered()), this, SLOT(gotoVerifyMessageTab()));
        connect(usedSendingAddressesAction, SIGNAL(triggered()), walletFrame, SLOT(usedSendingAddresses()));
        connect(usedReceivingAddressesAction, SIGNAL(triggered()), walletFrame, SLOT(usedReceivingAddresses()));
        connect(openAction, SIGNAL(triggered()), this, SLOT(openClicked()));
    }
#endif // ENABLE_WALLET

    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_I), this, SLOT(showInfo()));
    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_C), this, SLOT(showConsole()));
    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_G), this, SLOT(showGraph()));
    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_P), this, SLOT(showPeers()));
    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_R), this, SLOT(showRepair()));
}

void BitcoinGUI::createMenuBar()
{
#ifdef Q_OS_MAC
    // Create a decoupled menu bar on Mac which stays even if the window is closed
    appMenuBar = new QMenuBar();
#else
    // Get the main window's menu bar on other platforms
    appMenuBar = menuBar();
#endif

    // Configure the menus
    QMenu *file = appMenuBar->addMenu(tr("&File"));
    if(walletFrame)
    {
        file->addAction(openAction);
        file->addAction(backupWalletAction);
        file->addAction(signMessageAction);
        file->addAction(verifyMessageAction);
        file->addSeparator();
        file->addAction(usedSendingAddressesAction);
        file->addAction(usedReceivingAddressesAction);
        file->addSeparator();
    }
    file->addAction(quitAction);

    QMenu *settings = appMenuBar->addMenu(tr("&Settings"));
    if(walletFrame)
    {
        settings->addAction(encryptWalletAction);
        settings->addAction(changePassphraseAction);
        settings->addAction(unlockWalletAction);
        settings->addAction(lockWalletAction);
        settings->addSeparator();
    }
    settings->addAction(optionsAction);

    if(walletFrame)
    {
        QMenu *tools = appMenuBar->addMenu(tr("&Tools"));
        tools->addAction(openInfoAction);
        tools->addAction(openRPCConsoleAction);
        tools->addAction(openGraphAction);
        tools->addAction(openPeersAction);
        tools->addAction(openRepairAction);
        tools->addSeparator();
        tools->addAction(openConfEditorAction);
        tools->addAction(showBackupsAction);
    }

    QMenu *help = appMenuBar->addMenu(tr("&Help"));
    help->addAction(showHelpMessageAction);
    help->addAction(showPrivateSendHelpAction);
    help->addSeparator();
    help->addAction(aboutAction);
    help->addAction(aboutQtAction);
}

void BitcoinGUI::createToolBars()
{
#ifdef ENABLE_WALLET
    if(walletFrame)
    {
        QToolBar *toolbar = new QToolBar(tr("Tabs toolbar"));
        toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toolbar->addAction(overviewAction);
        toolbar->addAction(sendCoinsAction);
        toolbar->addAction(receiveCoinsAction);
        toolbar->addAction(historyAction);
        toolbar->addAction(sendFuturesAction);
        QSettings settings;
        if (!fLiteMode && settings.value("fShowSmartnodesTab").toBool() && smartnodeAction)
        {
            toolbar->addAction(smartnodeAction);
        }
        toolbar->setMovable(false); // remove unused icon in upper left corner
        overviewAction->setChecked(true);

        // Add Raptoreum logo on the right side
        QWidget* spacer = new QWidget();
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        toolbar->addWidget(spacer);

        QLabel *logoLabel = new QLabel();
        QPixmap logoPixmap(":/images/raptoreum_logo_toolbar");
        logoLabel->setPixmap(logoPixmap);
        toolbar->addWidget(logoLabel);

        /** Create additional container for toolbar and walletFrame and make it the central widget.
            This is a workaround mostly for toolbar styling on Mac OS but should work fine for every other OSes too.
        */
        QVBoxLayout *layout = new QVBoxLayout;
        layout->addWidget(toolbar);
        layout->addWidget(walletFrame);
        layout->setSpacing(0);
        layout->setContentsMargins(QMargins());
        QWidget *containerWidget = new QWidget();
        containerWidget->setLayout(layout);
        setCentralWidget(containerWidget);
    }
#endif // ENABLE_WALLET
}

void BitcoinGUI::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;
    if(_clientModel)
    {
        // Create system tray menu (or setup the dock menu) that late to prevent users from calling actions,
        // while the client has not yet fully loaded
        if (trayIcon) {
            // do so only if trayIcon is already set
            trayIconMenu = new QMenu(this);
            trayIcon->setContextMenu(trayIconMenu);
            createIconMenu(trayIconMenu);

#ifndef Q_OS_MAC
            // Show main window on tray icon click
            // Note: ignore this on Mac - this is not the way tray should work there
            connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
                    this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
#else
            // Note: On Mac, the dock icon is also used to provide menu functionality
            // similar to one for tray icon
            MacDockIconHandler *dockIconHandler = MacDockIconHandler::instance();
            dockIconHandler->setMainWindow((QMainWindow *)this);
            dockIconMenu = dockIconHandler->dockMenu();
 
            createIconMenu(dockIconMenu);
#endif
        }

        // Keep up to date with client
        updateNetworkState();
        setNumConnections(_clientModel->getNumConnections());
        connect(_clientModel, SIGNAL(numConnectionsChanged(int)), this, SLOT(setNumConnections(int)));
        connect(_clientModel, SIGNAL(networkActiveChanged(bool)), this, SLOT(setNetworkActive(bool)));

        modalOverlay->setKnownBestHeight(_clientModel->getHeaderTipHeight(), QDateTime::fromTime_t(_clientModel->getHeaderTipTime()));
        setNumBlocks(_clientModel->getNumBlocks(), _clientModel->getLastBlockDate(), _clientModel->getVerificationProgress(nullptr), false);
        connect(_clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(setNumBlocks(int,QDateTime,double,bool)));

        connect(_clientModel, SIGNAL(additionalDataSyncProgressChanged(double)), this, SLOT(setAdditionalDataSyncProgress(double)));

        // Receive and report messages from client model
        connect(_clientModel, SIGNAL(message(QString,QString,unsigned int)), this, SLOT(message(QString,QString,unsigned int)));

        // Show progress dialog
        connect(_clientModel, SIGNAL(showProgress(QString,int)), this, SLOT(showProgress(QString,int)));

        rpcConsole->setClientModel(_clientModel);
#ifdef ENABLE_WALLET
        if(walletFrame)
        {
            walletFrame->setClientModel(_clientModel);
        }
#endif // ENABLE_WALLET
        unitDisplayControl->setOptionsModel(_clientModel->getOptionsModel());
        
        OptionsModel* optionsModel = _clientModel->getOptionsModel();
        if(optionsModel)
        {
            // be aware of the tray icon disable state change reported by the OptionsModel object.
            connect(optionsModel,SIGNAL(hideTrayIconChanged(bool)),this,SLOT(setTrayIconVisible(bool)));
        
            // initialize the disable state of the tray icon with the current value in the model.
            setTrayIconVisible(optionsModel->getHideTrayIcon());
        }
    } else {
        // Disable possibility to show main window via action
        toggleHideAction->setEnabled(false);
        if(trayIconMenu)
        {
            // Disable context menu on tray icon
            trayIconMenu->clear();
        }
        // Propagate cleared model to child objects
        rpcConsole->setClientModel(nullptr);
#ifdef ENABLE_WALLET
        if (walletFrame)
        {
            walletFrame->setClientModel(nullptr);
        }
#endif // ENABLE_WALLET
        unitDisplayControl->setOptionsModel(nullptr);

#ifdef Q_OS_MAC
        if(dockIconMenu)
        {
            // Disable context menu on dock icon
            dockIconMenu->clear();
        }
#endif
    }
}

#ifdef ENABLE_WALLET
bool BitcoinGUI::addWallet(const QString& name, WalletModel *walletModel)
{
    if(!walletFrame)
        return false;
    setWalletActionsEnabled(true);
    return walletFrame->addWallet(name, walletModel);
}

bool BitcoinGUI::setCurrentWallet(const QString& name)
{
    if(!walletFrame)
        return false;
    return walletFrame->setCurrentWallet(name);
}

void BitcoinGUI::removeAllWallets()
{
    if(!walletFrame)
        return;
    setWalletActionsEnabled(false);
    walletFrame->removeAllWallets();
}
#endif // ENABLE_WALLET

void BitcoinGUI::setWalletActionsEnabled(bool enabled)
{
    overviewAction->setEnabled(enabled);
    sendCoinsAction->setEnabled(enabled);
    sendFuturesAction->setEnabled(enabled);
    sendCoinsMenuAction->setEnabled(enabled);
    receiveCoinsAction->setEnabled(enabled);
    receiveCoinsMenuAction->setEnabled(enabled);
    historyAction->setEnabled(enabled);
    QSettings settings;
    if (!fLiteMode && settings.value("fShowSmartnodesTab").toBool() && smartnodeAction) {
        smartnodeAction->setEnabled(enabled);
    }
    encryptWalletAction->setEnabled(enabled);
    backupWalletAction->setEnabled(enabled);
    changePassphraseAction->setEnabled(enabled);
    signMessageAction->setEnabled(enabled);
    verifyMessageAction->setEnabled(enabled);
    usedSendingAddressesAction->setEnabled(enabled);
    usedReceivingAddressesAction->setEnabled(enabled);
    openAction->setEnabled(enabled);
}

void BitcoinGUI::createTrayIcon(const NetworkStyle *networkStyle)
{
    trayIcon = new QSystemTrayIcon(this);
    QString toolTip = tr("%1 client").arg(tr(PACKAGE_NAME)) + " " + networkStyle->getTitleAddText();
    trayIcon->setToolTip(toolTip);
    trayIcon->setIcon(networkStyle->getTrayAndWindowIcon());
    trayIcon->hide();
    notificator = new Notificator(QApplication::applicationName(), trayIcon, this);
}

void BitcoinGUI::createIconMenu(QMenu *pmenu)
{
    // Configuration of the tray icon (or dock icon) icon menu
    pmenu->addAction(toggleHideAction);
    pmenu->addSeparator();
    pmenu->addAction(sendCoinsMenuAction);
    pmenu->addAction(receiveCoinsMenuAction);
    pmenu->addSeparator();
    pmenu->addAction(signMessageAction);
    pmenu->addAction(verifyMessageAction);
    pmenu->addSeparator();
    pmenu->addAction(optionsAction);
    pmenu->addAction(openInfoAction);
    pmenu->addAction(openRPCConsoleAction);
    pmenu->addAction(openGraphAction);
    pmenu->addAction(openPeersAction);
    pmenu->addAction(openRepairAction);
    pmenu->addSeparator();
    pmenu->addAction(openConfEditorAction);
    pmenu->addAction(showBackupsAction);
#ifndef Q_OS_MAC // This is built-in on Mac
    pmenu->addSeparator();
    pmenu->addAction(quitAction);
#endif
}

#ifndef Q_OS_MAC
void BitcoinGUI::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::Trigger)
    {
        // Click on system tray icon triggers show/hide of the main window
        toggleHidden();
    }
}
#endif

void BitcoinGUI::optionsClicked()
{
    if(!clientModel || !clientModel->getOptionsModel())
        return;

    OptionsDialog dlg(this, enableWallet);
    dlg.setModel(clientModel->getOptionsModel());
    dlg.exec();
}

void BitcoinGUI::aboutClicked()
{
    if(!clientModel)
        return;

    HelpMessageDialog dlg(this, HelpMessageDialog::about);
    dlg.exec();
}

void BitcoinGUI::showDebugWindow()
{
    rpcConsole->showNormal();
    rpcConsole->show();
    rpcConsole->raise();
    rpcConsole->activateWindow();
}

void BitcoinGUI::showInfo()
{
    rpcConsole->setTabFocus(RPCConsole::TAB_INFO);
    showDebugWindow();
}

void BitcoinGUI::showConsole()
{
    rpcConsole->setTabFocus(RPCConsole::TAB_CONSOLE);
    showDebugWindow();
}

void BitcoinGUI::showGraph()
{
    rpcConsole->setTabFocus(RPCConsole::TAB_GRAPH);
    showDebugWindow();
}

void BitcoinGUI::showPeers()
{
    rpcConsole->setTabFocus(RPCConsole::TAB_PEERS);
    showDebugWindow();
}

void BitcoinGUI::showRepair()
{
    rpcConsole->setTabFocus(RPCConsole::TAB_REPAIR);
    showDebugWindow();
}

void BitcoinGUI::showConfEditor()
{
    GUIUtil::openConfigfile();
}

void BitcoinGUI::showBackups()
{
    GUIUtil::showBackups();
}

void BitcoinGUI::showHelpMessageClicked()
{
    helpMessageDialog->show();
}

void BitcoinGUI::showPrivateSendHelpClicked()
{
    if(!clientModel)
        return;

    HelpMessageDialog dlg(this, HelpMessageDialog::pshelp);
    dlg.exec();
}

#ifdef ENABLE_WALLET
void BitcoinGUI::openClicked()
{
    OpenURIDialog dlg(this);
    if(dlg.exec())
    {
        Q_EMIT receivedURI(dlg.getURI());
    }
}

void BitcoinGUI::gotoOverviewPage()
{
    overviewAction->setChecked(true);
    if (walletFrame) walletFrame->gotoOverviewPage();
}

void BitcoinGUI::gotoHistoryPage()
{
    historyAction->setChecked(true);
    if (walletFrame) walletFrame->gotoHistoryPage();
}

void BitcoinGUI::gotoSmartnodePage()
{
    QSettings settings;
    if (!fLiteMode && settings.value("fShowSmartnodesTab").toBool() && smartnodeAction) {
        smartnodeAction->setChecked(true);
        if (walletFrame) walletFrame->gotoSmartnodePage();
    }
}

void BitcoinGUI::gotoReceiveCoinsPage()
{
    receiveCoinsAction->setChecked(true);
    if (walletFrame) walletFrame->gotoReceiveCoinsPage();
}

void BitcoinGUI::gotoSendCoinsPage(QString addr)
{
    sendCoinsAction->setChecked(true);
    if (walletFrame) walletFrame->gotoSendCoinsPage(addr);
}

void BitcoinGUI::gotoSendFuturesPage(QString addr)
{
    sendFuturesAction->setChecked(true);
    if (walletFrame) walletFrame->gotoSendFuturesPage(addr);
}

void BitcoinGUI::gotoSignMessageTab(QString addr)
{
    if (walletFrame) walletFrame->gotoSignMessageTab(addr);
}

void BitcoinGUI::gotoVerifyMessageTab(QString addr)
{
    if (walletFrame) walletFrame->gotoVerifyMessageTab(addr);
}
#endif // ENABLE_WALLET

void BitcoinGUI::updateNetworkState()
{
    int count = clientModel->getNumConnections();
    QString icon;
    switch(count)
    {
    case 0: icon = ":/icons/connect_0"; break;
    case 1: case 2: case 3: icon = ":/icons/connect_1"; break;
    case 4: case 5: case 6: icon = ":/icons/connect_2"; break;
    case 7: case 8: case 9: icon = ":/icons/connect_3"; break;
    default: icon = ":/icons/connect_4"; break;
    }

    if (clientModel->getNetworkActive()) {
        labelConnectionsIcon->setToolTip(tr("%n active connection(s) to Raptoreum network", "", count));
    } else {
        labelConnectionsIcon->setToolTip(tr("Network activity disabled"));
        icon = ":/icons/network_disabled";
    }

    labelConnectionsIcon->setPixmap(platformStyle->SingleColorIcon(icon).pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
}

void BitcoinGUI::setNumConnections(int count)
{
    updateNetworkState();
}

void BitcoinGUI::setNetworkActive(bool networkActive)
{
    updateNetworkState();
}

void BitcoinGUI::updateHeadersSyncProgressLabel()
{
    int64_t headersTipTime = clientModel->getHeaderTipTime();
    int headersTipHeight = clientModel->getHeaderTipHeight();
    int estHeadersLeft = (GetTime() - headersTipTime) / Params().GetConsensus().nPowTargetSpacing;
    if (estHeadersLeft > HEADER_HEIGHT_DELTA_SYNC)
        progressBarLabel->setText(tr("Syncing Headers (%1%)...").arg(QString::number(100.0 / (headersTipHeight+estHeadersLeft)*headersTipHeight, 'f', 1)));
}

void BitcoinGUI::setNumBlocks(int count, const QDateTime& blockDate, double nVerificationProgress, bool header)
{
#ifdef Q_OS_MAC
    // Disabling macOS App Nap on initial sync, disk, reindex operations and mixing.
    bool disableAppNap = !smartnodeSync.IsSynced();
#ifdef ENABLE_WALLET
    disableAppNap |= privateSendClient.fPrivateSendRunning;
#endif // ENABLE_WALLET
    if (disableAppNap) {
        m_app_nap_inhibitor->disableAppNap();
    } else {
        m_app_nap_inhibitor->enableAppNap();
    }
#endif // Q_OS_MAC

    if (modalOverlay)
    {
        if (header)
            modalOverlay->setKnownBestHeight(count, blockDate);
        else
            modalOverlay->tipUpdate(count, blockDate, nVerificationProgress);
    }
    if (!clientModel)
        return;

    // Prevent orphan statusbar messages (e.g. hover Quit in main menu, wait until chain-sync starts -> garbled text)
    statusBar()->clearMessage();

    // Acquire current block source
    enum BlockSource blockSource = clientModel->getBlockSource();
    switch (blockSource) {
        case BLOCK_SOURCE_NETWORK:
            if (header) {
                updateHeadersSyncProgressLabel();
                return;
            }
            progressBarLabel->setText(tr("Synchronizing with network..."));
            updateHeadersSyncProgressLabel();
            break;
        case BLOCK_SOURCE_DISK:
            if (header) {
                progressBarLabel->setText(tr("Indexing blocks on disk..."));
            } else {
                progressBarLabel->setText(tr("Processing blocks on disk..."));
            }
            break;
        case BLOCK_SOURCE_REINDEX:
            progressBarLabel->setText(tr("Reindexing blocks on disk..."));
            break;
        case BLOCK_SOURCE_NONE:
            if (header) {
                return;
            }
            progressBarLabel->setText(tr("Connecting to peers..."));
            break;
    }

    QString tooltip;

    QDateTime currentDate = QDateTime::currentDateTime();
    qint64 secs = blockDate.secsTo(currentDate);

    tooltip = tr("Processed %n block(s) of transaction history.", "", count);

    // Set icon state: spinning if catching up, tick otherwise
#ifdef ENABLE_WALLET
    if (walletFrame)
    {
        if(secs < 25*60) // 90*60 in bitcoin
        {
            modalOverlay->showHide(true, true);
            // TODO instead of hiding it forever, we should add meaningful information about MN sync to the overlay
            modalOverlay->hideForever();
        }
        else
        {
            modalOverlay->showHide();
        }
    }
#endif // ENABLE_WALLET

    if(!smartnodeSync.IsBlockchainSynced())
    {
        QString timeBehindText = GUIUtil::formatNiceTimeOffset(secs);

        progressBarLabel->setVisible(true);
        progressBar->setFormat(tr("%1 behind").arg(timeBehindText));
        progressBar->setMaximum(1000000000);
        progressBar->setValue(nVerificationProgress * 1000000000.0 + 0.5);
        progressBar->setVisible(true);

        tooltip = tr("Catching up...") + QString("<br>") + tooltip;
        if(count != prevBlocks)
        {
            labelBlocksIcon->setPixmap(platformStyle->SingleColorIcon(QString(
                ":/movies/spinner-%1").arg(spinnerFrame, 3, 10, QChar('0')))
                .pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
            spinnerFrame = (spinnerFrame + 1) % SPINNER_FRAMES;
        }
        prevBlocks = count;

#ifdef ENABLE_WALLET
        if(walletFrame)
        {
            walletFrame->showOutOfSyncWarning(true);
        }
#endif // ENABLE_WALLET

        tooltip += QString("<br>");
        tooltip += tr("Last received block was generated %1 ago.").arg(timeBehindText);
        tooltip += QString("<br>");
        tooltip += tr("Transactions after this will not yet be visible.");
    } else if (fLiteMode) {
        setAdditionalDataSyncProgress(1);
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");

    labelBlocksIcon->setToolTip(tooltip);
    progressBarLabel->setToolTip(tooltip);
    progressBar->setToolTip(tooltip);
}

void BitcoinGUI::setAdditionalDataSyncProgress(double nSyncProgress)
{
    if(!clientModel)
        return;

    // No additional data sync should be happening while blockchain is not synced, nothing to update
    if(!smartnodeSync.IsBlockchainSynced())
        return;

    // Prevent orphan statusbar messages (e.g. hover Quit in main menu, wait until chain-sync starts -> garbelled text)
    statusBar()->clearMessage();

    QString tooltip;

    // Set icon state: spinning if catching up, tick otherwise
    QString strSyncStatus;
    tooltip = tr("Up to date") + QString(".<br>") + tooltip;

#ifdef ENABLE_WALLET
    if(walletFrame)
        walletFrame->showOutOfSyncWarning(false);
#endif // ENABLE_WALLET

    if(smartnodeSync.IsSynced()) {
        progressBarLabel->setVisible(false);
        progressBar->setVisible(false);
        labelBlocksIcon->setPixmap(QIcon(":/icons/synced").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
    } else {

        labelBlocksIcon->setPixmap(platformStyle->SingleColorIcon(QString(
            ":/movies/spinner-%1").arg(spinnerFrame, 3, 10, QChar('0')))
            .pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
        spinnerFrame = (spinnerFrame + 1) % SPINNER_FRAMES;

        progressBar->setFormat(tr("Synchronizing additional data: %p%"));
        progressBar->setMaximum(1000000000);
        progressBar->setValue(nSyncProgress * 1000000000.0 + 0.5);
    }

    strSyncStatus = QString(smartnodeSync.GetSyncStatus().c_str());
    progressBarLabel->setText(strSyncStatus);
    tooltip = strSyncStatus + QString("<br>") + tooltip;

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");

    labelBlocksIcon->setToolTip(tooltip);
    progressBarLabel->setToolTip(tooltip);
    progressBar->setToolTip(tooltip);
}

void BitcoinGUI::message(const QString &title, const QString &message, unsigned int style, bool *ret)
{
    QString strTitle = tr("Raptoreum Core"); // default title
    // Default to information icon
    int nMBoxIcon = QMessageBox::Information;
    int nNotifyIcon = Notificator::Information;

    QString msgType;

    // Prefer supplied title over style based title
    if (!title.isEmpty()) {
        msgType = title;
    }
    else {
        switch (style) {
        case CClientUIInterface::MSG_ERROR:
            msgType = tr("Error");
            break;
        case CClientUIInterface::MSG_WARNING:
            msgType = tr("Warning");
            break;
        case CClientUIInterface::MSG_INFORMATION:
            msgType = tr("Information");
            break;
        default:
            break;
        }
    }
    // Append title to "Raptoreum Core - "
    if (!msgType.isEmpty())
        strTitle += " - " + msgType;

    // Check for error/warning icon
    if (style & CClientUIInterface::ICON_ERROR) {
        nMBoxIcon = QMessageBox::Critical;
        nNotifyIcon = Notificator::Critical;
    }
    else if (style & CClientUIInterface::ICON_WARNING) {
        nMBoxIcon = QMessageBox::Warning;
        nNotifyIcon = Notificator::Warning;
    }

    // Display message
    if (style & CClientUIInterface::MODAL) {
        // Check for buttons, use OK as default, if none was supplied
        QMessageBox::StandardButton buttons;
        if (!(buttons = (QMessageBox::StandardButton)(style & CClientUIInterface::BTN_MASK)))
            buttons = QMessageBox::Ok;

        showNormalIfMinimized();
        QMessageBox mBox((QMessageBox::Icon)nMBoxIcon, strTitle, message, buttons, this);
        int r = mBox.exec();
        if (ret != nullptr)
            *ret = r == QMessageBox::Ok;
    }
    else
        notificator->notify((Notificator::Class)nNotifyIcon, strTitle, message);
}

void BitcoinGUI::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
#ifndef Q_OS_MAC // Ignored on Mac
    if(e->type() == QEvent::WindowStateChange)
    {
        if(clientModel && clientModel->getOptionsModel() && clientModel->getOptionsModel()->getMinimizeToTray())
        {
            QWindowStateChangeEvent *wsevt = static_cast<QWindowStateChangeEvent*>(e);
            if(!(wsevt->oldState() & Qt::WindowMinimized) && isMinimized())
            {
                QTimer::singleShot(0, this, SLOT(hide()));
                e->ignore();
            }
        }
    }
#endif
}

void BitcoinGUI::closeEvent(QCloseEvent *event)
{
#ifndef Q_OS_MAC // Ignored on Mac
    if(clientModel && clientModel->getOptionsModel())
    {
        if(!clientModel->getOptionsModel()->getMinimizeOnClose())
        {
            // close rpcConsole in case it was open to make some space for the shutdown window
            rpcConsole->close();

            QApplication::quit();
        }
        else
        {
            QMainWindow::showMinimized();
            event->ignore();
        }
    }
#else
    QMainWindow::closeEvent(event);
#endif
}

void BitcoinGUI::showEvent(QShowEvent *event)
{
    // enable the debug window when the main window shows up
    openInfoAction->setEnabled(true);
    openRPCConsoleAction->setEnabled(true);
    openGraphAction->setEnabled(true);
    openPeersAction->setEnabled(true);
    openRepairAction->setEnabled(true);
    aboutAction->setEnabled(true);
    optionsAction->setEnabled(true);
}

#ifdef ENABLE_WALLET
void BitcoinGUI::incomingTransaction(const QString& date, int unit, const CAmount& amount, const QString& type, const QString& address, const QString& label)
{
    IncomingTransactionMessage itx = {
            date, unit, amount, type, address, label
    };
    incomingTransactions.emplace_back(itx);

    if (incomingTransactions.size() == 1) {
        // first TX since we last showed pending messages, let's wait 100ms and then show each individual message
        incomingTransactionsTimer->start(100);
    } else if (incomingTransactions.size() == 10) {
        // we seem to have received 10 TXs in 100ms and we can expect even more, so let's pause for 1 sec and
        // show a "Multiple TXs sent/received!" message instead of individual messages
        incomingTransactionsTimer->start(1000);
    }
}
void BitcoinGUI::showIncomingTransactions()
{
    auto txs = std::move(this->incomingTransactions);

    if (txs.empty()) {
        return;
    }

    if (txs.size() >= 100) {
        // Show one balloon for all transactions instead of showing one for each individual one
        // (which would kill some systems)

        CAmount sentAmount = 0;
        CAmount receivedAmount = 0;
        int sentCount = 0;
        int receivedCount = 0;
        for (auto& itx : txs) {
            if (itx.amount < 0) {
                sentAmount += itx.amount;
                sentCount++;
            } else {
                receivedAmount += itx.amount;
                receivedCount++;
            }
        }

        QString title;
        if (sentCount > 0 && receivedCount > 0) {
            title = tr("Received and sent multiple transactions");
        } else if (sentCount > 0) {
            title = tr("Sent multiple transactions");
        } else if (receivedCount > 0) {
            title = tr("Received multiple transactions");
        } else {
            return;
        }

        // Use display unit of last entry
        int unit = txs.back().unit;

        QString msg;
        if (sentCount > 0) {
            msg += tr("Sent Amount: %1\n").arg(BitcoinUnits::formatWithUnit(unit, sentAmount, true));
        }
        if (receivedCount > 0) {
            msg += tr("Received Amount: %1\n").arg(BitcoinUnits::formatWithUnit(unit, receivedAmount, true));
        }

        message(title, msg, CClientUIInterface::MSG_INFORMATION);
    } else {
        for (auto& itx : txs) {
            // On new transaction, make an info balloon
            QString msg = tr("Date: %1\n").arg(itx.date) +
                          tr("Amount: %1\n").arg(BitcoinUnits::formatWithUnit(itx.unit, itx.amount, true)) +
                          tr("Type: %1\n").arg(itx.type);
            if (!itx.label.isEmpty())
                msg += tr("Label: %1\n").arg(itx.label);
            else if (!itx.address.isEmpty())
                msg += tr("Address: %1\n").arg(itx.address);
            message((itx.amount)<0 ? tr("Sent transaction") : tr("Incoming transaction"),
                    msg, CClientUIInterface::MSG_INFORMATION);
        }
    }
}
#endif // ENABLE_WALLET

void BitcoinGUI::dragEnterEvent(QDragEnterEvent *event)
{
    // Accept only URIs
    if(event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void BitcoinGUI::dropEvent(QDropEvent *event)
{
    if(event->mimeData()->hasUrls())
    {
        for (const QUrl &uri : event->mimeData()->urls())
        {
            Q_EMIT receivedURI(uri.toString());
        }
    }
    event->acceptProposedAction();
}

bool BitcoinGUI::eventFilter(QObject *object, QEvent *event)
{
    // Catch status tip events
    if (event->type() == QEvent::StatusTip)
    {
        // Prevent adding text from setStatusTip(), if we currently use the status bar for displaying other stuff
        if (progressBarLabel->isVisible() || progressBar->isVisible())
            return true;
    }
    return QMainWindow::eventFilter(object, event);
}

#ifdef ENABLE_WALLET
bool BitcoinGUI::handlePaymentRequest(const SendCoinsRecipient& recipient)
{
    // URI has to be valid
    if (walletFrame && walletFrame->handlePaymentRequest(recipient))
    {
        showNormalIfMinimized();
        gotoSendCoinsPage();
        return true;
    }
    return false;
}

void BitcoinGUI::setHDStatus(int hdEnabled)
{
    labelWalletHDStatusIcon->setPixmap(platformStyle->SingleColorIcon(hdEnabled ? ":/icons/hd_enabled" : ":/icons/hd_disabled").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
    labelWalletHDStatusIcon->setToolTip(hdEnabled ? tr("HD key generation is <b>enabled</b>") : tr("HD key generation is <b>disabled</b>"));

    // eventually disable the QLabel to set its opacity to 50%
    labelWalletHDStatusIcon->setEnabled(hdEnabled);
}

void BitcoinGUI::setEncryptionStatus(int status)
{
    switch(status)
    {
    case WalletModel::Unencrypted:
        labelWalletEncryptionIcon->hide();
        encryptWalletAction->setChecked(false);
        changePassphraseAction->setEnabled(false);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setVisible(false);
        encryptWalletAction->setEnabled(true);
        break;
    case WalletModel::Unlocked:
        labelWalletEncryptionIcon->show();
        labelWalletEncryptionIcon->setPixmap(QIcon(":/icons/lock_open").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelWalletEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked</b>"));
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setVisible(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    case WalletModel::UnlockedForMixingOnly:
        labelWalletEncryptionIcon->show();
        labelWalletEncryptionIcon->setPixmap(QIcon(":/icons/lock_open").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelWalletEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked</b> for mixing only"));
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        unlockWalletAction->setVisible(true);
        lockWalletAction->setVisible(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    case WalletModel::Locked:
        labelWalletEncryptionIcon->show();
        labelWalletEncryptionIcon->setPixmap(QIcon(":/icons/lock_closed").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelWalletEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>locked</b>"));
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        unlockWalletAction->setVisible(true);
        lockWalletAction->setVisible(false);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    }
}
#endif // ENABLE_WALLET

void BitcoinGUI::showNormalIfMinimized(bool fToggleHidden)
{
    if(!clientModel)
        return;

    // activateWindow() (sometimes) helps with keyboard focus on Windows
    if (isHidden())
    {
        show();
        activateWindow();
    }
    else if (isMinimized())
    {
        showNormal();
        activateWindow();
    }
    else if (GUIUtil::isObscured(this))
    {
        raise();
        activateWindow();
    }
    else if(fToggleHidden)
        hide();
}

void BitcoinGUI::toggleHidden()
{
    showNormalIfMinimized(true);
}

void BitcoinGUI::detectShutdown()
{
    if (ShutdownRequested())
    {
        if(rpcConsole)
            rpcConsole->hide();
        qApp->quit();
    }
}

void BitcoinGUI::showProgress(const QString &title, int nProgress)
{
    if (nProgress == 0)
    {
        progressDialog = new QProgressDialog(title, "", 0, 100);
        progressDialog->setStyleSheet(GUIUtil::loadStyleSheet());
        progressDialog->setWindowModality(Qt::ApplicationModal);
        progressDialog->setMinimumDuration(0);
        progressDialog->setCancelButton(0);
        progressDialog->setAutoClose(false);
        progressDialog->setValue(0);
    }
    else if (nProgress == 100)
    {
        if (progressDialog)
        {
            progressDialog->close();
            progressDialog->deleteLater();
        }
    }
    else if (progressDialog)
        progressDialog->setValue(nProgress);
}

void BitcoinGUI::setTrayIconVisible(bool fHideTrayIcon)
{
    if (trayIcon)
    {
        trayIcon->setVisible(!fHideTrayIcon);
    }
}

void BitcoinGUI::showModalOverlay()
{
    if (modalOverlay && (progressBar->isVisible() || modalOverlay->isLayerVisible()))
        modalOverlay->toggleVisibility();
}

static bool ThreadSafeMessageBox(BitcoinGUI *gui, const std::string& message, const std::string& caption, unsigned int style)
{
    bool modal = (style & CClientUIInterface::MODAL);
    // The SECURE flag has no effect in the Qt GUI.
    // bool secure = (style & CClientUIInterface::SECURE);
    style &= ~CClientUIInterface::SECURE;
    bool ret = false;
    // In case of modal message, use blocking connection to wait for user to click a button
    QMetaObject::invokeMethod(gui, "message",
                               modal ? GUIUtil::blockingGUIThreadConnection() : Qt::QueuedConnection,
                               Q_ARG(QString, QString::fromStdString(caption)),
                               Q_ARG(QString, QString::fromStdString(message)),
                               Q_ARG(unsigned int, style),
                               Q_ARG(bool*, &ret));
    return ret;
}

void BitcoinGUI::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.ThreadSafeMessageBox.connect(boost::bind(ThreadSafeMessageBox, this, _1, _2, _3));
    uiInterface.ThreadSafeQuestion.connect(boost::bind(ThreadSafeMessageBox, this, _1, _3, _4));
}

void BitcoinGUI::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    uiInterface.ThreadSafeMessageBox.disconnect(boost::bind(ThreadSafeMessageBox, this, _1, _2, _3));
    uiInterface.ThreadSafeQuestion.disconnect(boost::bind(ThreadSafeMessageBox, this, _1, _3, _4));
}

void BitcoinGUI::toggleNetworkActive()
{
    if (clientModel) {
        clientModel->setNetworkActive(!clientModel->getNetworkActive());
    }
}

/** Get restart command-line parameters and request restart */
void BitcoinGUI::handleRestart(QStringList args)
{
    if (!ShutdownRequested())
        Q_EMIT requestedRestart(args);
}

UnitDisplayStatusBarControl::UnitDisplayStatusBarControl(const PlatformStyle *platformStyle) :
    optionsModel(0),
    menu(0)
{
    createContextMenu();
    setToolTip(tr("Unit to show amounts in. Click to select another unit."));
    QList<BitcoinUnits::Unit> units = BitcoinUnits::availableUnits();
    int max_width = 0;
    const QFontMetrics fm(font());
    for (const BitcoinUnits::Unit unit : units)
    {
        max_width = qMax(max_width, fm.width(BitcoinUnits::name(unit)));
    }
    setMinimumSize(max_width, 0);
    setAlignment(Qt::AlignRight | Qt::AlignVCenter);
}

/** So that it responds to button clicks */
void UnitDisplayStatusBarControl::mousePressEvent(QMouseEvent *event)
{
    onDisplayUnitsClicked(event->pos());
}

/** Creates context menu, its actions, and wires up all the relevant signals for mouse events. */
void UnitDisplayStatusBarControl::createContextMenu()
{
    menu = new QMenu(this);
    for (BitcoinUnits::Unit u : BitcoinUnits::availableUnits())
    {
        QAction *menuAction = new QAction(QString(BitcoinUnits::name(u)), this);
        menuAction->setData(QVariant(u));
        menu->addAction(menuAction);
    }
    connect(menu,SIGNAL(triggered(QAction*)),this,SLOT(onMenuSelection(QAction*)));
}

/** Lets the control know about the Options Model (and its signals) */
void UnitDisplayStatusBarControl::setOptionsModel(OptionsModel *_optionsModel)
{
    if (_optionsModel)
    {
        this->optionsModel = _optionsModel;

        // be aware of a display unit change reported by the OptionsModel object.
        connect(_optionsModel,SIGNAL(displayUnitChanged(int)),this,SLOT(updateDisplayUnit(int)));

        // initialize the display units label with the current value in the model.
        updateDisplayUnit(_optionsModel->getDisplayUnit());
    }
}

/** When Display Units are changed on OptionsModel it will refresh the display text of the control on the status bar */
void UnitDisplayStatusBarControl::updateDisplayUnit(int newUnits)
{
    setText(BitcoinUnits::name(newUnits));
}

/** Shows context menu with Display Unit options by the mouse coordinates */
void UnitDisplayStatusBarControl::onDisplayUnitsClicked(const QPoint& point)
{
    QPoint globalPos = mapToGlobal(point);
    menu->exec(globalPos);
}

/** Tells underlying optionsModel to update its current display unit. */
void UnitDisplayStatusBarControl::onMenuSelection(QAction* action)
{
    if (action)
    {
        optionsModel->setDisplayUnit(action->data());
    }
}
