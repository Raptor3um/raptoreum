// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2020-2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_SENDFUTURESDIALOG_H
#define BITCOIN_QT_SENDFUTURESDIALOG_H

#include <qt/walletmodel.h>

#include <QDialog>
#include <QMessageBox>
#include <QShowEvent>
#include <QString>
#include <QTimer>

static const int MAX_FUTURES_POPUP_ENTRIES = 10;

class CCoinControl;
class ClientModel;
class SendFuturesEntry;
class SendFuturesRecipient;

namespace Ui {
    class SendFuturesDialog;
}

QT_BEGIN_NAMESPACE
class QUrl;
QT_END_NAMESPACE

/** Dialog for sending bitcoins */
class SendFuturesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SendFuturesDialog(QWidget* parent = 0);
    ~SendFuturesDialog();

    void setClientModel(ClientModel *clientModel);
    void setModel(WalletModel *model);

    /** Set up the tab chain manually, as Qt messes up the tab chain by default in some cases (issue https://bugreports.qt-project.org/browse/QTBUG-10907).
     */
    QWidget *setupTabChain(QWidget *prev);

    void setAddress(const QString &address);
    void pasteEntry(const SendFuturesRecipient &rv);
    bool handlePaymentRequest(const SendFuturesRecipient &recipient);

public Q_SLOTS:
    void clear();
    void reject();
    void accept();
    SendFuturesEntry *addEntry();
    void updateTabsAndLabels();
    void setBalance(const interfaces::WalletBalances& balances);

private:
    Ui::SendFuturesDialog *ui;
    ClientModel *clientModel;
    WalletModel *model;
    std::unique_ptr<CCoinControl> m_coin_control;
    bool fNewRecipientAllowed;
    void send(QList<SendFuturesRecipient> recipients);
    bool fFeeMinimized;

    // Process WalletModel::SendFuturesReturn and generate a pair consisting
    // of a message and message flags for use in Q_EMIT message().
    // Additional parameter msgArg can be used via .arg(msgArg).
    void processSendFuturesReturn(const WalletModel::SendFuturesReturn &sendFuturesReturn, const QString &msgArg = QString());
    void minimizeFeeSection(bool fMinimize);
    void updateFeeMinimizedLabel();
    // Update the passed in CCoinControl with state from the GUI
    void updateCoinControlState(CCoinControl& ctrl);

    void showEvent(QShowEvent* event);

private Q_SLOTS:
    void on_sendButton_clicked();
    void on_buttonChooseFee_clicked();
    void on_buttonMinimizeFee_clicked();
    void removeEntry(SendFuturesEntry* entry);
    void updateDisplayUnit();
    void coinControlFeatureChanged(bool);
    void coinControlButtonClicked();
    void coinControlChangeChecked(int);
    void coinControlChangeEdited(const QString &);
    void coinControlUpdateLabels();
    void coinControlClipboardQuantity();
    void coinControlClipboardAmount();
    void coinControlClipboardFee();
    void coinControlClipboardAfterFee();
    void coinControlClipboardBytes();
    void coinControlClipboardLowOutput();
    void coinControlClipboardChange();
    void setMinimumFee();
    void updateFeeSectionControls();
    void updateMinFeeLabel();
    void updateSmartFeeLabel();
    void updateFtxFeeLabel();
    void updateFTXpayFromLabels();

Q_SIGNALS:
    // Fired when a message should be reported to the user
    void message(const QString &title, const QString &message, unsigned int style);
};



class FutureConfirmationDialog : public QMessageBox
{
    Q_OBJECT

public:
    FutureConfirmationDialog(const QString &title, const QString &text, int secDelay = 0, QWidget *parent = 0);
    int exec();

private Q_SLOTS:
    void countDown();
    void updateYesButton();

private:
    QAbstractButton *yesButton;
    QTimer countDownTimer;
    int secDelay;
};

#endif // BITCOIN_QT_SENDFUTURESDIALOG_H
