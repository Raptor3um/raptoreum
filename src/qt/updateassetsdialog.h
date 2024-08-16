// Copyright (c) 2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_QT_UPDATEASSETSDIALOG_H
#define RAPTOREUM_QT_UPDATEASSETSDIALOG_H

#include <qt/walletmodel.h>

#include <QDialog>
#include <QMessageBox>
#include <QShowEvent>
#include <QString>
#include <QTimer>

class CCoinControl;

class ClientModel;

class QStringListModel;

class QSortFilterProxyModel;

class QCompleter;

namespace Ui {
    class UpdateAssetsDialog;
}

QT_BEGIN_NAMESPACE
class QUrl;
QT_END_NAMESPACE

/** Dialog for creating assets */
class UpdateAssetsDialog : public QDialog {
    Q_OBJECT

public:
    explicit UpdateAssetsDialog(QWidget *parent = 0);

    ~UpdateAssetsDialog();

    void setClientModel(ClientModel *clientModel);

    void setModel(WalletModel *model);

    /** Set up the tab chain manually, as Qt messes up the tab chain by default in some cases (issue https://bugreports.qt-project.org/browse/QTBUG-10907).
     */
    QWidget *setupTabChain(QWidget *prev);

    //update the list of assets
    void updateAssetList();

    QStringListModel *stringModel;
    QSortFilterProxyModel *proxy;
    QCompleter *completer;
    QString prevName = "";

public
    Q_SLOTS:
    void clear();

    void updateTabsAndLabels();

    void setBalance(const interfaces::WalletBalances &balances);

    Q_SIGNALS:
            void coinsSent(
    const uint256 &txid
    );

private:
    Ui::UpdateAssetsDialog *ui;
    ClientModel *clientModel;
    WalletModel *model;
    std::unique_ptr <CCoinControl> m_coin_control;

    bool validateInputs();

    bool filladdress(QString address, CKeyID &field);

    bool fFeeMinimized;

    void minimizeFeeSection(bool fMinimize);

    void updateFeeMinimizedLabel();

    void updateAsset();

    // Update the passed in CCoinControl with state from the GUI
    void updateCoinControlState(CCoinControl &ctrl);

private
    Q_SLOTS:
    void on_updateAssetButton_clicked();

    void on_buttonChooseFee_clicked();

    void on_buttonMinimizeFee_clicked();

    void updateDisplayUnit();

    void setMinimumFee();

    void updateFeeSectionControls();

    void updateMinFeeLabel();

    void updateSmartFeeLabel();

    void onAssetSelected(QString name);

    void focusAsset(const std::string assetId);
    void openFilePicker();

    Q_SIGNALS:
            // Fired when a message should be reported to the user
            void message(
    const QString &title,
    const QString &message,
    unsigned int style
    );
};


class UpdateAssetConfirmationDialog : public QMessageBox {
    Q_OBJECT

public:
    UpdateAssetConfirmationDialog(const QString &title, const QString &text, int secDelay = 0, QWidget *parent = 0);

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

#endif // RAPTOREUM_QT_UPDATEASSETSDIALOG_H
