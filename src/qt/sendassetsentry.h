// Copyright (c) 2011-2015 The BitAsset Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITAsset_QT_SENDAssetSENTRY_H
#define BITAsset_QT_SENDAssetSENTRY_H

#include <qt/walletmodel.h>

#include <QStackedWidget>

class WalletModel;

class QStringListModel;

class QSortFilterProxyModel;

class QCompleter;

namespace Ui {
    class SendAssetsEntry;
}

/**
 * A single entry in the dialog for sending bitAssets.
 * Stacked widget, with different UIs for payment requests
 * with a strong payee identity.
 */
class SendAssetsEntry : public QStackedWidget {
    Q_OBJECT

public:
    explicit SendAssetsEntry(QWidget *parent = 0, bool hideFuture = false);

    ~SendAssetsEntry();

    void setModel(WalletModel *model);

    void setCoinControl(CCoinControl *coin_control);

    bool validate(interfaces::Node &node);

    SendCoinsRecipient getValue();

    /** Return whether the entry is still empty and unedited */
    bool isClear();

    void setValue(const SendCoinsRecipient &value);

    void setAddress(const QString &address);

    void setAmount(const CAmount &amount);

    void SetFutureVisible(const bool visible);

    //update the list of assets
    void updateAssetList();

    /** Set up the tab chain manually, as Qt messes up the tab chain by default in some cases
     *  (issue https://bugreports.qt-project.org/browse/QTBUG-10907).
     */
    QWidget *setupTabChain(QWidget *prev);

    void setFocus();

public
    Q_SLOTS:
            void clear();

    Q_SIGNALS:
            void removeEntry(SendAssetsEntry * entry);

    void useAvailableAssetsBalance(SendAssetsEntry *entry);

    void payAmountChanged();

private
    Q_SLOTS:
            void deleteClicked();

    void useAvailableAssetsBalanceClicked();

    void on_payTo_textChanged(const QString &address);

    void on_addressBookButton_clicked();

    void on_pasteButton_clicked();

    void updateDisplayUnit();

    void futureToggleChanged();

    void onAssetSelected(QString name);

protected:
    void changeEvent(QEvent *e);

private:
    SendCoinsRecipient recipient;
    Ui::SendAssetsEntry *ui;
    WalletModel *model;
    CCoinControl *m_coin_control;
    bool uniqueAssetSelected;

    QStringListModel *stringModel;
    QSortFilterProxyModel *proxy;
    QCompleter *completer;

    QStringListModel *stringModelId;
    QSortFilterProxyModel *proxyId;

    /** Set required icons for buttons inside the dialog */
    void setButtonIcons();

    bool updateLabel(const QString &address);
};

#endif // BITAsset_QT_SENDAssetSENTRY_H
