#ifndef BITCOIN_QT_SMARTNODELIST_H
#define BITCOIN_QT_SMARTNODELIST_H

#include <primitives/transaction.h>
#include <sync.h>
#include <util.h>

#include <evo/deterministicmns.h>

#include <QMenu>
#include <QTimer>
#include <QWidget>

#define SMARTNODELIST_UPDATE_SECONDS 3
#define SMARTNODELIST_FILTER_COOLDOWN_SECONDS 3

namespace Ui
{
class SmartnodeList;
}

class ClientModel;
class WalletModel;

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Smartnode Manager page widget */
class SmartnodeList : public QWidget
{
    Q_OBJECT

public:
    explicit SmartnodeList(QWidget* parent = 0);
    ~SmartnodeList();

    enum {
        COLUMN_SERVICE,
        COLUMN_STATUS,
        COLUMN_POSE,
        COLUMN_REGISTERED,
        COLUMN_LAST_PAYMENT,
        COLUMN_NEXT_PAYMENT,
        COLUMN_PAYOUT_ADDRESS,
        COLUMN_OPERATOR_REWARD,
        COLUMN_COLLATERAL_ADDRESS,
        COLUMN_COLLATERAL_AMOUNT,
        COLUMN_OWNER_ADDRESS,
        COLUMN_VOTING_ADDRESS,
        COLUMN_PROTX_HASH,
    };

    void setClientModel(ClientModel* clientModel);
    void setWalletModel(WalletModel* walletModel);

private:
    QMenu* contextMenuDIP3;
    int64_t nTimeFilterUpdatedDIP3;
    int64_t nTimeUpdatedDIP3;
    bool fFilterUpdatedDIP3;

    QTimer* timer;
    Ui::SmartnodeList* ui;
    ClientModel* clientModel;
    WalletModel* walletModel;

    // Protects tableWidgetSmartnodesDIP3
    CCriticalSection cs_dip3list;

    QString strCurrentFilterDIP3;

    bool mnListChanged;

    CDeterministicMNCPtr GetSelectedDIP3MN();

    void updateDIP3List();

Q_SIGNALS:
    void doubleClicked(const QModelIndex&);

private Q_SLOTS:
    void showContextMenuDIP3(const QPoint&);
    void on_filterLineEditDIP3_textChanged(const QString& strFilterIn);
    void on_checkBoxMySmartnodesOnly_stateChanged(int state);

    void extraInfoDIP3_clicked();
    void copyProTxHash_clicked();
    void copyCollateralOutpoint_clicked();

    void handleSmartnodeListChanged();
    void updateDIP3ListScheduled();
};
#endif // BITCOIN_QT_SMARTNODELIST_H
