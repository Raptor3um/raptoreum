// Copyright (c) 2011-2015 The BitAsset Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_ASSETCONTROLDIALOG_H
#define BITCOIN_QT_ASSETCONTROLDIALOG_H

#include <amount.h>

#include <QAbstractButton>
#include <QAction>
#include <QDialog>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QString>
#include <QTreeWidgetItem>

class WalletModel;

class CCoinControl;

class QStringListModel;

class QSortFilterProxyModel;

class QCompleter;

namespace Ui {
    class AssetControlDialog;
}

#define ASYMP_UTF8 "\xE2\x89\x88"

class CAssetControlWidgetItem : public QTreeWidgetItem {
public:
    explicit CAssetControlWidgetItem(QTreeWidget *parent, int type = Type) : QTreeWidgetItem(parent, type) {}

    explicit CAssetControlWidgetItem(int type = Type) : QTreeWidgetItem(type) {}

    explicit CAssetControlWidgetItem(QTreeWidgetItem *parent, int type = Type) : QTreeWidgetItem(parent, type) {}

    bool operator<(const QTreeWidgetItem &other) const;
};


class AssetControlDialog : public QDialog {
    Q_OBJECT

public:
    explicit AssetControlDialog(CCoinControl &Asset_control, WalletModel *model, QWidget *parent = nullptr);

    ~AssetControlDialog();

    // static because also called from sendAssetsdialog
    static void updateLabels(CCoinControl &m_coin_control, WalletModel *, QDialog *);

    //update the list of assets
    void updateAssetList();

    static QList <CAmount> payAmounts;
    static bool fSubtractFeeFromAmount;

    QStringListModel *stringModel;
    QSortFilterProxyModel *proxy;
    QCompleter *completer;

private:
    Ui::AssetControlDialog *ui;
    CCoinControl &m_coin_control;
    WalletModel *model;
    int sortColumn;
    Qt::SortOrder sortOrder;

    QMenu *contextMenu;
    QTreeWidgetItem *contextMenuItem;
    QAction *copyTransactionHashAction;
    QAction *lockAction;
    QAction *unlockAction;

    bool fHideAdditional{true};

    void sortView(int, Qt::SortOrder);

    void updateView();

    enum {
        COLUMN_CHECKBOX = 0,
        COLUMN_NAME,
        COLUMN_AMOUNT,
        COLUMN_LABEL,
        COLUMN_ADDRESS,
        COLUMN_DATE,
        COLUMN_CONFIRMATIONS,
    };

    enum {
        TxHashRole = Qt::UserRole,
        VOutRole
    };

    friend class CAssetControlWidgetItem;

private
    Q_SLOTS:
            void showMenu(
    const QPoint &);

    void copyAmount();

    void copyLabel();

    void copyAddress();

    void copyTransactionHash();

    void lockAsset();

    void unlockAsset();

    void clipboardQuantity();

    void clipboardAmount();

    void clipboardFee();

    void clipboardAfterFee();

    void clipboardBytes();

    void clipboardLowOutput();

    void clipboardChange();

    void radioTreeMode(bool);

    void radioListMode(bool);

    void viewItemChanged(QTreeWidgetItem *, int);

    void headerSectionClicked(int);

    void buttonBoxClicked(QAbstractButton *);

    void buttonSelectAllClicked();

    void buttonToggleLockClicked();

    void updateLabelLocked();

    void onAssetSelected(QString name);

};

#endif // BITCOIN_QT_ASSETCONTROLDIALOG_H
