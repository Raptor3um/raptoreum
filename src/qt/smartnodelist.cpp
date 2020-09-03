#include "smartnodelist.h"
#include "ui_smartnodelist.h"

#include "smartnode/activesmartnode.h"
#include "clientmodel.h"
#include "clientversion.h"
#include "coins.h"
#include "guiutil.h"
#include "init.h"
#include "smartnode/smartnode-sync.h"
#include "netbase.h"
#include "sync.h"
#include "validation.h"
#include "wallet/wallet.h"
#include "walletmodel.h"

#include <univalue.h>

#include <QMessageBox>
#include <QTimer>
#include <QtGui/QClipboard>

int GetOffsetFromUtc()
{
#if QT_VERSION < 0x050200
    const QDateTime dateTime1 = QDateTime::currentDateTime();
    const QDateTime dateTime2 = QDateTime(dateTime1.date(), dateTime1.time(), Qt::UTC);
    return dateTime1.secsTo(dateTime2);
#else
    return QDateTime::currentDateTime().offsetFromUtc();
#endif
}

SmartnodeList::SmartnodeList(const PlatformStyle* platformStyle, QWidget* parent) :
    QWidget(parent),
    ui(new Ui::SmartnodeList),
    clientModel(0),
    walletModel(0),
    fFilterUpdatedDIP3(true),
    nTimeFilterUpdatedDIP3(0),
    nTimeUpdatedDIP3(0),
    mnListChanged(true)
{
    ui->setupUi(this);

    int columnAddressWidth = 200;
    int columnStatusWidth = 80;
    int columnPoSeScoreWidth = 80;
    int columnRegisteredWidth = 80;
    int columnLastPaidWidth = 80;
    int columnNextPaymentWidth = 100;
    int columnPayeeWidth = 130;
    int columnOperatorRewardWidth = 130;
    int columnCollateralWidth = 130;
    int columnCollateralAmountWidth = 130;
    int columnOwnerWidth = 130;
    int columnVotingWidth = 130;

    ui->tableWidgetSmartnodesDIP3->setColumnWidth(0, columnAddressWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(1, columnStatusWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(2, columnPoSeScoreWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(3, columnRegisteredWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(4, columnLastPaidWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(5, columnNextPaymentWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(6, columnPayeeWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(7, columnOperatorRewardWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(8, columnCollateralWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(9, columnCollateralAmountWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(10, columnOwnerWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(11, columnVotingWidth);

    // dummy column for proTxHash
    // TODO use a proper table model for the MN list
    ui->tableWidgetSmartnodesDIP3->insertColumn(12);
    ui->tableWidgetSmartnodesDIP3->setColumnHidden(12, true);

    ui->tableWidgetSmartnodesDIP3->setContextMenuPolicy(Qt::CustomContextMenu);

    QAction* copyProTxHashAction = new QAction(tr("Copy ProTx Hash"), this);
    QAction* copyCollateralOutpointAction = new QAction(tr("Copy Collateral Outpoint"), this);
    contextMenuDIP3 = new QMenu();
    contextMenuDIP3->addAction(copyProTxHashAction);
    contextMenuDIP3->addAction(copyCollateralOutpointAction);
    connect(ui->tableWidgetSmartnodesDIP3, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenuDIP3(const QPoint&)));
    connect(ui->tableWidgetSmartnodesDIP3, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(extraInfoDIP3_clicked()));
    connect(copyProTxHashAction, SIGNAL(triggered()), this, SLOT(copyProTxHash_clicked()));
    connect(copyCollateralOutpointAction, SIGNAL(triggered()), this, SLOT(copyCollateralOutpoint_clicked()));

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateDIP3ListScheduled()));
    timer->start(1000);
}

SmartnodeList::~SmartnodeList()
{
    delete ui;
}

void SmartnodeList::setClientModel(ClientModel* model)
{
    this->clientModel = model;
    if (model) {
        // try to update list when smartnode count changes
        connect(clientModel, SIGNAL(smartnodeListChanged()), this, SLOT(handleSmartnodeListChanged()));
    }
}

void SmartnodeList::setWalletModel(WalletModel* model)
{
    this->walletModel = model;
}

void SmartnodeList::showContextMenuDIP3(const QPoint& point)
{
    QTableWidgetItem* item = ui->tableWidgetSmartnodesDIP3->itemAt(point);
    if (item) contextMenuDIP3->exec(QCursor::pos());
}

void SmartnodeList::handleSmartnodeListChanged()
{
    LOCK(cs_dip3list);
    mnListChanged = true;
}

void SmartnodeList::updateDIP3ListScheduled()
{
    TRY_LOCK(cs_dip3list, fLockAcquired);
    if (!fLockAcquired) return;

    if (!clientModel || ShutdownRequested()) {
        return;
    }

    // To prevent high cpu usage update only once in SMARTNODELIST_FILTER_COOLDOWN_SECONDS seconds
    // after filter was last changed unless we want to force the update.
    if (fFilterUpdatedDIP3) {
        int64_t nSecondsToWait = nTimeFilterUpdatedDIP3 - GetTime() + SMARTNODELIST_FILTER_COOLDOWN_SECONDS;
        ui->countLabelDIP3->setText(QString::fromStdString(strprintf("Please wait... %d", nSecondsToWait)));

        if (nSecondsToWait <= 0) {
            updateDIP3List();
            fFilterUpdatedDIP3 = false;
        }
    } else if (mnListChanged) {
        int64_t nMnListUpdateSecods = smartnodeSync.IsBlockchainSynced() ? SMARTNODELIST_UPDATE_SECONDS : SMARTNODELIST_UPDATE_SECONDS*10;
        int64_t nSecondsToWait = nTimeUpdatedDIP3 - GetTime() + nMnListUpdateSecods;

        if (nSecondsToWait <= 0) {
            updateDIP3List();
            mnListChanged = false;
        }
    }
}

void SmartnodeList::updateDIP3List()
{
    if (!clientModel || ShutdownRequested()) {
        return;
    }

    auto mnList = clientModel->getSmartnodeList();
    std::map<uint256, CTxDestination> mapCollateralDests;

    {
        // Get all UTXOs for each MN collateral in one go so that we can reduce locking overhead for cs_main
        // We also do this outside of the below Qt list update loop to reduce cs_main locking time to a minimum
        LOCK(cs_main);
        mnList.ForEachMN(false, [&](const CDeterministicMNCPtr& dmn) {
            CTxDestination collateralDest;
            Coin coin;
            if (GetUTXOCoin(dmn->collateralOutpoint, coin) && ExtractDestination(coin.out.scriptPubKey, collateralDest)) {
                mapCollateralDests.emplace(dmn->proTxHash, collateralDest);
            }
        });
    }

    LOCK(cs_dip3list);

    QString strToFilter;
    ui->countLabelDIP3->setText("Updating...");
    ui->tableWidgetSmartnodesDIP3->setSortingEnabled(false);
    ui->tableWidgetSmartnodesDIP3->clearContents();
    ui->tableWidgetSmartnodesDIP3->setRowCount(0);

    nTimeUpdatedDIP3 = GetTime();

    auto projectedPayees = mnList.GetProjectedMNPayees(mnList.GetValidMNsCount());
    std::map<uint256, int> nextPayments;
    for (size_t i = 0; i < projectedPayees.size(); i++) {
        const auto& dmn = projectedPayees[i];
        nextPayments.emplace(dmn->proTxHash, mnList.GetHeight() + (int)i + 1);
    }

    std::set<COutPoint> setOutpts;
    if (walletModel && ui->checkBoxMySmartnodesOnly->isChecked()) {
        std::vector<COutPoint> vOutpts;
        walletModel->listProTxCoins(vOutpts);
        for (const auto& outpt : vOutpts) {
            setOutpts.emplace(outpt);
        }
    }

    mnList.ForEachMN(false, [&](const CDeterministicMNCPtr& dmn) {
        if (walletModel && ui->checkBoxMySmartnodesOnly->isChecked()) {
            bool fMySmartnode = setOutpts.count(dmn->collateralOutpoint) ||
                walletModel->IsSpendable(dmn->pdmnState->keyIDOwner) ||
                walletModel->IsSpendable(dmn->pdmnState->keyIDVoting) ||
                walletModel->IsSpendable(dmn->pdmnState->scriptPayout) ||
                walletModel->IsSpendable(dmn->pdmnState->scriptOperatorPayout);
            if (!fMySmartnode) return;
        }
        // populate list
        // Address, Protocol, Status, Active Seconds, Last Seen, Pub Key
        Coin coin;
		//should this be call directly or use pcoinsTip->GetCoin(outpoint, coin) without locking cs_main
		bool isValidUtxo = GetUTXOCoin(dmn->collateralOutpoint, coin);
		SmartnodeCollaterals collaterals = Params().GetConsensus().nCollaterals;
		int nHeight = chainActive.Tip() == nullptr ? 0 : chainActive.Tip()->nHeight;
	    QTableWidgetItem* collateralAmountItem = new QTableWidgetItem(!isValidUtxo ? tr("Invalid") : QString::number(coin.out.nValue / COIN));
        QTableWidgetItem* addressItem = new QTableWidgetItem(QString::fromStdString(dmn->pdmnState->addr.ToString()));
        QTableWidgetItem* statusItem = new QTableWidgetItem(mnList.IsMNValid(dmn) ? tr("ENABLED") :
        		(mnList.IsMNPoSeBanned(dmn) ? tr("POSE_BANNED") :
        				!collaterals.isPayableCollateral(nHeight, coin.out.nValue) ? tr("C_UPGRADE") : tr("UNKNOWN")));
        QTableWidgetItem* PoSeScoreItem = new QTableWidgetItem(QString::number(dmn->pdmnState->nPoSePenalty));
        QTableWidgetItem* registeredItem = new QTableWidgetItem(QString::number(dmn->pdmnState->nRegisteredHeight));
        QTableWidgetItem* lastPaidItem = new QTableWidgetItem(QString::number(dmn->pdmnState->nLastPaidHeight));
        QTableWidgetItem* nextPaymentItem = new QTableWidgetItem(nextPayments.count(dmn->proTxHash) ? QString::number(nextPayments[dmn->proTxHash]) : tr("UNKNOWN"));

        CTxDestination payeeDest;
        QString payeeStr = tr("UNKNOWN");
        if (ExtractDestination(dmn->pdmnState->scriptPayout, payeeDest)) {
            payeeStr = QString::fromStdString(CBitcoinAddress(payeeDest).ToString());
        }
        QTableWidgetItem* payeeItem = new QTableWidgetItem(payeeStr);

        QString operatorRewardStr = tr("NONE");
        if (dmn->nOperatorReward) {
            operatorRewardStr = QString::number(dmn->nOperatorReward / 100.0, 'f', 2) + "% ";

            if (dmn->pdmnState->scriptOperatorPayout != CScript()) {
                CTxDestination operatorDest;
                if (ExtractDestination(dmn->pdmnState->scriptOperatorPayout, operatorDest)) {
                    operatorRewardStr += tr("to %1").arg(QString::fromStdString(CBitcoinAddress(operatorDest).ToString()));
                } else {
                    operatorRewardStr += tr("to UNKNOWN");
                }
            } else {
                operatorRewardStr += tr("but not claimed");
            }
        }
        QTableWidgetItem* operatorRewardItem = new QTableWidgetItem(operatorRewardStr);

        QString collateralStr = tr("UNKNOWN");
        auto collateralDestIt = mapCollateralDests.find(dmn->proTxHash);
        if (collateralDestIt != mapCollateralDests.end()) {
            collateralStr = QString::fromStdString(CBitcoinAddress(collateralDestIt->second).ToString());
        }
        QTableWidgetItem* collateralItem = new QTableWidgetItem(collateralStr);

        QString ownerStr = QString::fromStdString(CBitcoinAddress(dmn->pdmnState->keyIDOwner).ToString());
        QTableWidgetItem* ownerItem = new QTableWidgetItem(ownerStr);

        QString votingStr = QString::fromStdString(CBitcoinAddress(dmn->pdmnState->keyIDVoting).ToString());
        QTableWidgetItem* votingItem = new QTableWidgetItem(votingStr);

        QTableWidgetItem* proTxHashItem = new QTableWidgetItem(QString::fromStdString(dmn->proTxHash.ToString()));

        if (strCurrentFilterDIP3 != "") {
            strToFilter = addressItem->text() + " " +
                          statusItem->text() + " " +
                          PoSeScoreItem->text() + " " +
                          registeredItem->text() + " " +
                          lastPaidItem->text() + " " +
                          nextPaymentItem->text() + " " +
                          payeeItem->text() + " " +
                          operatorRewardItem->text() + " " +
                          collateralItem->text() + " " +
						  collateralAmountItem->text() + " " +
                          ownerItem->text() + " " +
                          votingItem->text() + " " +
                          proTxHashItem->text();
            if (!strToFilter.contains(strCurrentFilterDIP3)) return;
        }

        ui->tableWidgetSmartnodesDIP3->insertRow(0);
        ui->tableWidgetSmartnodesDIP3->setItem(0, 0, addressItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, 1, statusItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, 2, PoSeScoreItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, 3, registeredItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, 4, lastPaidItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, 5, nextPaymentItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, 6, payeeItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, 7, operatorRewardItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, 8, collateralItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, 9, collateralAmountItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, 10, ownerItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, 11, votingItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, 12, proTxHashItem);
    });

    ui->countLabelDIP3->setText(QString::number(ui->tableWidgetSmartnodesDIP3->rowCount()));
    ui->tableWidgetSmartnodesDIP3->setSortingEnabled(true);
}

void SmartnodeList::on_filterLineEditDIP3_textChanged(const QString& strFilterIn)
{
    strCurrentFilterDIP3 = strFilterIn;
    nTimeFilterUpdatedDIP3 = GetTime();
    fFilterUpdatedDIP3 = true;
    ui->countLabelDIP3->setText(QString::fromStdString(strprintf("Please wait... %d", SMARTNODELIST_FILTER_COOLDOWN_SECONDS)));
}

void SmartnodeList::on_checkBoxMySmartnodesOnly_stateChanged(int state)
{
    // no cooldown
    nTimeFilterUpdatedDIP3 = GetTime() - SMARTNODELIST_FILTER_COOLDOWN_SECONDS;
    fFilterUpdatedDIP3 = true;
}

CDeterministicMNCPtr SmartnodeList::GetSelectedDIP3MN()
{
    if (!clientModel) {
        return nullptr;
    }

    std::string strProTxHash;
    {
        LOCK(cs_dip3list);

        QItemSelectionModel* selectionModel = ui->tableWidgetSmartnodesDIP3->selectionModel();
        QModelIndexList selected = selectionModel->selectedRows();
        if (selected.count() == 0) return nullptr;

        QModelIndex index = selected.at(0);
        int nSelectedRow = index.row();
        strProTxHash = ui->tableWidgetSmartnodesDIP3->item(nSelectedRow, 12)->text().toStdString();
    }

    uint256 proTxHash;
    proTxHash.SetHex(strProTxHash);

    auto mnList = clientModel->getSmartnodeList();
    return mnList.GetMN(proTxHash);
}

void SmartnodeList::extraInfoDIP3_clicked()
{
    auto dmn = GetSelectedDIP3MN();

    if (!dmn) {
        return;
    }
    UniValue json(UniValue::VOBJ);
    dmn->ToJson(json);
    // Title of popup window
    QString strWindowtitle = tr("Additional information for DIP3 Smartnode %1").arg(QString::fromStdString(dmn->proTxHash.ToString()));
    QString strText = QString::fromStdString(json.write(2));
    QMessageBox::information(this, strWindowtitle, strText);
}

void SmartnodeList::copyProTxHash_clicked()
{
    auto dmn = GetSelectedDIP3MN();
    if (!dmn) {
        return;
    }

    QApplication::clipboard()->setText(QString::fromStdString(dmn->proTxHash.ToString()));
}

void SmartnodeList::copyCollateralOutpoint_clicked()
{
    auto dmn = GetSelectedDIP3MN();
    if (!dmn) {
        return;
    }

    QApplication::clipboard()->setText(QString::fromStdString(dmn->collateralOutpoint.ToStringShort()));
}
