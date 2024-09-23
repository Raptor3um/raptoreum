#include <qt/test/wallettests.h>

#include <coinjoin/coinjoin-client.h>
#include <init.h>
#include <interfaces/chain.h>
#include <interfaces/node.h>
#include <qt/bitcoinamountfield.h>
#include <qt/clientmodel.h>
#include <qt/optionsmodel.h>
#include <qt/qvalidatedlineedit.h>
#include <qt/sendcoinsdialog.h>
#include <qt/sendcoinsentry.h>
#include <qt/transactiontablemodel.h>
#include <qt/transactionview.h>
#include <qt/walletmodel.h>
#include <key_io.h>
#include <test/test_raptoreum.h>
#include <validation.h>
#include <wallet/wallet.h>
#include <qt/overviewpage.h>
#include <qt/receivecoinsdialog.h>
#include <qt/recentrequeststablemodel.h>
#include <qt/receiverequestdialog.h>

#include <memory>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QListView>
#include <QDialogButtonBox>

namespace {
//! Press "Ok" button in message box dialog.
    void ConfirmMessage(QString *text = nullptr) {
        QTimer::singleShot(0, [text]() {
            for (QWidget *widget: QApplication::topLevelWidgets()) {
                if (widget->inherits("QMessageBox")) {
                    QMessageBox *messageBox = qobject_cast<QMessageBox *>(widget);
                    if (text) *text = messageBox->text();
                    messageBox->defaultButton()->click();
                }
            }
        });
    }

//! Press "Yes" or "Cancel" buttons in modal send confirmation dialog.
    void ConfirmSend(QString *text = nullptr, bool cancel = false) {
        QTimer::singleShot(0, [text, cancel]() {
            for (QWidget *widget: QApplication::topLevelWidgets()) {
                if (widget->inherits("SendConfirmationDialog")) {
                    SendConfirmationDialog *dialog = qobject_cast<SendConfirmationDialog *>(widget);
                    if (text) *text = dialog->text();
                    QAbstractButton *button = dialog->button(cancel ? QMessageBox::Cancel : QMessageBox::Yes);
                    button->setEnabled(true);
                    button->click();
                }
            }
        });
    }

//! Send coins to address and return txid.
    uint256
    SendCoins(CWallet &wallet, SendCoinsDialog &sendCoinsDialog, const CTxDestination &address, CAmount amount) {
        QVBoxLayout *entries = sendCoinsDialog.findChild<QVBoxLayout *>("entries");
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry *>(entries->itemAt(0)->widget());
        entry->findChild<QValidatedLineEdit *>("payTo")->setText(QString::fromStdString(EncodeDestination(address)));
        entry->findChild<BitcoinAmountField *>("payAmount")->setValue(amount);
        uint256 txid;
        boost::signals2::scoped_connection c(
                wallet.NotifyTransactionChanged.connect([&txid](CWallet *, const uint256 &hash, ChangeType status) {
                    if (status == CT_NEW) txid = hash;
                }));
        ConfirmSend();
        bool invoked = QMetaObject::invokeMethod(&sendCoinsDialog, "on_sendButton_clicked");
        assert(invoked);
        return txid;
    }

//! Find index of txid in transaction list.
    QModelIndex FindTx(const QAbstractItemModel &model, const uint256 &txid) {
        QString hash = QString::fromStdString(txid.ToString());
        int rows = model.rowCount({});
        for (int row = 0; row < rows; ++row) {
            QModelIndex index = model.index(row, 0, {});
            if (model.data(index, TransactionTableModel::TxHashRole) == hash) {
                return index;
            }
        }
        return {};
    }

//! Simple qt wallet tests.
//
// Test widgets can be debugged interactively calling show() on them and
// manually running the event loop, e.g.:
//
//     sendCoinsDialog.show();
//     QEventLoop().exec();
//
// This also requires overriding the default minimal Qt platform:
//
//     QT_QPA_PLATFORM=xcb     src/qt/test/test_raptoreum-qt  # Linux
//     QT_QPA_PLATFORM=windows src/qt/test/test_raptoreum-qt  # Windows
//     QT_QPA_PLATFORM=cocoa   src/qt/test/test_raptoreum-qt  # macOS
    void TestGUI(interfaces::Node &node) {
        // Set up wallet and chain with 105 blocks (5 mature blocks for spending).
        TestChain100Setup test;
        for (int i = 0; i < 5; ++i) {
            test.CreateAndProcessBlock({}, GetScriptForRawPubKey(test.coinbaseKey.GetPubKey()));
        }
        node.context()->connman = std::move(test.m_node.connman);
        node.context()->mempool = std::move(test.m_node.mempool);
        std::shared_ptr <CWallet> wallet = std::make_shared<CWallet>(node.context()->chain.get(), WalletLocation(),
                                                                     CreateMockWalletDatabase());
        AddWallet(wallet);
        bool firstRun;
        wallet->LoadWallet(firstRun);
        {
            LOCK(wallet->cs_wallet);
            wallet->SetAddressBook(test.coinbaseKey.GetPubKey().GetID(), "", "receive");
            wallet->AddKeyPubKey(test.coinbaseKey, test.coinbaseKey.GetPubKey());
            wallet->SetLastBlockProcessed(105, ::ChainActive().Tip()->GetBlockHash());
        }
        {
            WalletRescanReserver reserver(wallet.get());
            reserver.reserve();
            CWallet::ScanResult result = wallet->ScanForWalletTransactions(wallet->chain().getBlockHash(0),
                                                                           {} /* stop_block */, reserver,
                                                                           true /* fUpdate */);
            QCOMPARE(result.status, CWallet::ScanResult::SUCCESS);
            QCOMPARE(result.last_scanned_block, ::ChainActive().Tip()->GetBlockHash());
            QVERIFY(result.last_failed_block.IsNull());
        }
        wallet->SetBroadcastTransactions(true);

        // Create widgets for sending coins and listing transactions.
        SendCoinsDialog sendCoinsDialog;
        TransactionView transactionView;
        OptionsModel optionsModel(node);
        ClientModel clientModel(node, &optionsModel);
        WalletModel walletModel(interfaces::MakeWallet(wallet), node, &optionsModel);;
        sendCoinsDialog.setModel(&walletModel);
        transactionView.setModel(&walletModel);

        // Send two transactions, and verify they are added to transaction list.
        TransactionTableModel *transactionTableModel = walletModel.getTransactionTableModel();
        QCOMPARE(transactionTableModel->rowCount({}), 105);
        uint256 txid1 = SendCoins(*wallet.get(), sendCoinsDialog, CKeyID(), 5 * COIN);
        uint256 txid2 = SendCoins(*wallet.get(), sendCoinsDialog, CKeyID(), 10 * COIN);
        QCOMPARE(transactionTableModel->rowCount({}), 107);
        QVERIFY(FindTx(*transactionTableModel, txid1).isValid());
        QVERIFY(FindTx(*transactionTableModel, txid2).isValid());

        // Check current balance on OverviewPage
        OverviewPage overviewPage;
        overviewPage.setClientModel(&clientModel);
        overviewPage.setWalletModel(&walletModel);
        QLabel *balanceLabel = overviewPage.findChild<QLabel *>("labelBalance");
        QString balanceText = balanceLabel->text();
        int unit = walletModel.getOptionsModel()->getDisplayUnit();
        CAmount balance = walletModel.wallet().getBalance();
        QString balanceComparison = BitcoinUnits::floorHtmlWithUnit(unit, balance, false,
                                                                    BitcoinUnits::separatorAlways);
        QCOMPARE(balanceText, balanceComparison);

        // Check Request Payment button
        ReceiveCoinsDialog receiveCoinsDialog;
        receiveCoinsDialog.setModel(&walletModel);
        RecentRequestsTableModel *requestTableModel = walletModel.getRecentRequestsTableModel();

        // Label input
        QLineEdit *labelInput = receiveCoinsDialog.findChild<QLineEdit *>("reqLabel");
        labelInput->setText("TEST_LABEL_1");

        // Amount input
        BitcoinAmountField *amountInput = receiveCoinsDialog.findChild<BitcoinAmountField *>("reqAmount");
        amountInput->setValue(1);

        // Message input
        QLineEdit *messageInput = receiveCoinsDialog.findChild<QLineEdit *>("reqMessage");
        messageInput->setText("TEST_MESSAGE_1");
        int initialRowCount = requestTableModel->rowCount({});
        QPushButton *requestPaymentButton = receiveCoinsDialog.findChild<QPushButton *>("receiveButton");
        requestPaymentButton->click();
        for (QWidget *widget: QApplication::topLevelWidgets()) {
            if (widget->inherits("ReceiveRequestDialog")) {
                ReceiveRequestDialog *receiveRequestDialog = qobject_cast<ReceiveRequestDialog *>(widget);
                QTextEdit *rlist = receiveRequestDialog->QObject::findChild<QTextEdit *>("outUri");
                QString paymentText = rlist->toPlainText();
                QStringList paymentTextList = paymentText.split('\n');
                QCOMPARE(paymentTextList.at(0), QString("Payment information"));
                QVERIFY(paymentTextList.at(2).indexOf(QString("URI: raptoreum:")) != -1);
                QVERIFY(paymentTextList.at(3).indexOf(QString("Address:")) != -1);
                QCOMPARE(paymentTextList.at(4), QString("Amount: 0.00000001 ") + BitcoinUnits::name(unit));
                QCOMPARE(paymentTextList.at(5), QString("Label: TEST_LABEL_1"));
                QCOMPARE(paymentTextList.at(6), QString("Message: TEST_MESSAGE_1"));
            }
        }

        // Clear button
        QPushButton *clearButton = receiveCoinsDialog.findChild<QPushButton *>("clearButton");
        clearButton->click();
        QCOMPARE(labelInput->text(), QString(""));
        QCOMPARE(amountInput->value(), CAmount(0));
        QCOMPARE(messageInput->text(), QString(""));

        // Check addition to history
        int currentRowCount = requestTableModel->rowCount({});
        QCOMPARE(currentRowCount, initialRowCount + 1);

        // Check Remove button
        QTableView *table = receiveCoinsDialog.findChild<QTableView *>("recentRequestsView");
        table->selectRow(currentRowCount - 1);
        QPushButton *removeRequestButton = receiveCoinsDialog.findChild<QPushButton *>("removeRequestButton");
        removeRequestButton->click();
        QCOMPARE(requestTableModel->rowCount({}), currentRowCount - 1);
        RemoveWallet(wallet);
    }

}

void WalletTests::walletTests() {
#ifdef Q_OS_MAC
    if (QApplication::platformName() == "minimal") {
      QWARN("Skipping WalletTests on mac build with 'minimal' platform set due to Qt bugs. To run AppTests, invoke with 'test_raptoreum-qt -platform cocoa' on mac or use a linux or windows build.");
      return;
    }
#endif
    TestGUI(m_node);
}
