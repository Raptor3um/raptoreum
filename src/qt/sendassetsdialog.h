// Copyright (c) 2011-2015 The BitAsset Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITAsset_QT_SENDAssetSDIALOG_H
#define BITAsset_QT_SENDAssetSDIALOG_H

#include <qt/walletmodel.h>

#include <QDialog>
#include <QMessageBox>
#include <QShowEvent>
#include <QString>
#include <QTimer>

static const int MAX_SEND_ASSET_POPUP_ENTRIES = 10;

class CCoinControl;

class ClientModel;

class SendAssetsEntry;

class SendCoinsRecipient;

namespace Ui {
    class SendAssetsDialog;
}

QT_BEGIN_NAMESPACE
class QUrl;
QT_END_NAMESPACE

/** Dialog for sending bitAssets */
class SendAssetsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SendAssetsDialog(QWidget *parent = 0);

    ~SendAssetsDialog();

    void setClientModel(ClientModel *clientModel);

    void setModel(WalletModel *model);

    /** Set up the tab chain manually, as Qt messes up the tab chain by default in some cases (issue https://bugreports.qt-project.org/browse/QTBUG-10907).
     */
    QWidget *setupTabChain(QWidget *prev);

    void setAddress(const QString &address);

    void pasteEntry(const SendCoinsRecipient &rv);

    bool handlePaymentRequest(const SendCoinsRecipient &recipient);

    void OnDisplay();

public
    Q_SLOTS:
            void clear();

    void reject();

    void accept();

    SendAssetsEntry *addEntry();

    void updateTabsAndLabels();

    void setBalance(const interfaces::WalletBalances &balances);

    void updateAssetList();

    Q_SIGNALS:
            void coinsSent(
    const uint256 &txid
    );

private:
    Ui::SendAssetsDialog *ui;
    ClientModel *clientModel;
    WalletModel *model;
    std::unique_ptr <CCoinControl> m_coin_control;
    bool fNewRecipientAllowed;

    void send(QList <SendCoinsRecipient> recipients);

    bool fFeeMinimized;

    // Process WalletModel::SendAssetsReturn and generate a pair consisting
    // of a message and message flags for use in Q_EMIT message().
    // Additional parameter msgArg can be used via .arg(msgArg).
    void
    processSendAssetsReturn(const WalletModel::SendAssetsReturn &sendAssetsReturn, const QString &msgArg = QString());

    void minimizeFeeSection(bool fMinimize);

    void updateFeeMinimizedLabel();

    // Update the passed in CCoinControl with state from the GUI
    void updateAssetControlState(CCoinControl &ctrl);

private
    Q_SLOTS:
            void on_sendButton_clicked();

    void on_buttonChooseFee_clicked();

    void on_buttonMinimizeFee_clicked();

    void removeEntry(SendAssetsEntry *entry);

    void useAvailableAssetsBalance(SendAssetsEntry *entry);

    void updateDisplayUnit();

    void AssetControlFeatureChanged(bool);

    void AssetControlButtonClicked();

    void AssetControlChangeChecked(int);

    void AssetControlChangeEdited(const QString &);

    void AssetControlUpdateLabels();

    void AssetControlClipboardQuantity();

    void AssetControlClipboardAmount();

    void AssetControlClipboardFee();

    void AssetControlClipboardAfterFee();

    void AssetControlClipboardBytes();

    void AssetControlClipboardLowOutput();

    void AssetControlClipboardChange();

    void setMinimumFee();

    void updateFeeSectionControls();

    void updateMinFeeLabel();

    void updateSmartFeeLabel();

    Q_SIGNALS:
            // Fired when a message should be reported to the user
            void message(
    const QString &title,
    const QString &message,
    unsigned int style
    );
};


class SendAssetConfirmationDialog : public QMessageBox {
    Q_OBJECT

public:
    SendAssetConfirmationDialog(const QString &title, const QString &text, int secDelay = 0, QWidget *parent = 0);

    int exec();

private
    Q_SLOTS:
            void countDown();

    void updateYesButton();

private:
    QAbstractButton *yesButton;
    QTimer countDownTimer;
    int secDelay;
};

#endif // BITAsset_QT_SENDAssetSDIALOG_H
