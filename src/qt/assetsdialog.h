// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QT_ASSETSDIALOG_H
#define BITCOIN_QT_ASSETSDIALOG_H

#include <primitives/transaction.h>
#include <sync.h>
#include <util/system.h>

#include <QMenu>
#include <QTimer>
#include <QMessageBox>
#include <QDialog>

#define ASSETSDIALOG_UPDATE_SECONDS 3
#define ASSETSDIALOG_FILTER_COOLDOWN_SECONDS 3

namespace Ui {
    class AssetsDialog;
}

class ClientModel;

class WalletModel;

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Smartnode Manager page widget */
class AssetsDialog : public QDialog {
    Q_OBJECT

public:
    explicit AssetsDialog(QWidget *parent = 0);

    ~AssetsDialog();

    enum {
        COLUMN_NAME,
        COLUMN_ID,
        COLUMN_CONFIRMED,
        COLUMN_PENDING,
    };

    void setClientModel(ClientModel *clientModel);

    void setModel(WalletModel *walletModel);

    void updateAssetBalance();

    Q_SIGNALS:
    void assetSendClicked(const std::string &assetId);

private:
    QMenu *contextMenuAsset;

    QTimer *timer;
    Ui::AssetsDialog *ui;
    ClientModel *clientModel{nullptr};
    WalletModel *walletModel{nullptr};

    bool balanceChanged{true};

    std::string GetSelectedAsset();

    Q_SIGNALS:
        void doubleClicked(const QModelIndex&);

private
    Q_SLOTS:

        void on_mintButton_clicked();

        void showContextMenuAsset(const QPoint&);

        void updateAssetBalanceScheduled();

        void Asset_clicked();

        void Asset_details_clicked();

        void SendAsset_clicked();

};

class MintAssetConfirmationDialog : public QMessageBox {
    Q_OBJECT

public:
    MintAssetConfirmationDialog(const QString &title, const QString &text, int secDelay = 0, QWidget *parent = 0);

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

#endif // BITCOIN_QT_ASSETSDIALOG_H
