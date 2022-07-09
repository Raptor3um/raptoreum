#include <qt/smartnodelist.h>
#include <qt/forms/ui_smartnodelist.h>

#include <qt/clientmodel.h>
#include <clientversion.h>
#include <coins.h>
#include <qt/guiutil.h>
#include <netbase.h>
#include <qt/walletmodel.h>
#include <validation.h>

#include <univalue.h>

#include <QMessageBox>
#include <QTableWidgetItem>
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

template <typename T>
class CSmartnodeListWidgetItem : public QTableWidgetItem
{
    T itemData;

public:
    explicit CSmartnodeListWidgetItem(const QString& text, const T& data, int type = Type) :
        QTableWidgetItem(text, type),
        itemData(data) {}

    bool operator<(const QTableWidgetItem& other) const
    {
        return itemData < ((CSmartnodeListWidgetItem*)&other)->itemData;
    }
};

SmartnodeList::SmartnodeList(QWidget* parent) :
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

    GUIUtil::setFont({ui->label_count_2,
                      ui->countLabelDIP3
                     }, GUIUtil::FontWeight::Bold, 14);
    GUIUtil::setFont({ui->label_filter_2}, GUIUtil::FontWeight::Normal, 15);

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

    ui->tableWidgetSmartnodesDIP3->setColumnWidth(COLUMN_SERVICE, columnAddressWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(COLUMN_STATUS, columnStatusWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(COLUMN_POSE, columnPoSeScoreWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(COLUMN_REGISTERED, columnRegisteredWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(COLUMN_LAST_PAYMENT, columnLastPaidWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(COLUMN_NEXT_PAYMENT, columnNextPaymentWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(COLUMN_PAYOUT_ADDRESS, columnPayeeWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(COLUMN_OPERATOR_REWARD, columnOperatorRewardWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(COLUMN_COLLATERAL_ADDRESS, columnCollateralWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(COLUMN_COLLATERAL_AMOUNT, columnCollateralAmountWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(COLUMN_OWNER_ADDRESS, columnOwnerWidth);
    ui->tableWidgetSmartnodesDIP3->setColumnWidth(COLUMN_VOTING_ADDRESS, columnVotingWidth);

    // dummy column for proTxHash
    // TODO use a proper table model for the MN list
    ui->tableWidgetSmartnodesDIP3->insertColumn(COLUMN_PROTX_HASH);
    ui->tableWidgetSmartnodesDIP3->setColumnHidden(COLUMN_PROTX_HASH, true);

    ui->tableWidgetSmartnodesDIP3->setContextMenuPolicy(Qt::CustomContextMenu);

    ui->filterLineEditDIP3->setPlaceholderText(tr("Filter by any property (e.g. address or protx hash)"));

    QAction* copyProTxHashAction = new QAction(tr("Copy ProTx Hash"), this);
    QAction* copyCollateralOutpointAction = new QAction(tr("Copy Collateral Outpoint"), this);
    contextMenuDIP3 = new QMenu(this);
    contextMenuDIP3->addAction(copyProTxHashAction);
    contextMenuDIP3->addAction(copyCollateralOutpointAction);
    connect(ui->tableWidgetSmartnodesDIP3, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenuDIP3(const QPoint&)));
    connect(ui->tableWidgetSmartnodesDIP3, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(extraInfoDIP3_clicked()));
    connect(copyProTxHashAction, SIGNAL(triggered()), this, SLOT(copyProTxHash_clicked()));
    connect(copyCollateralOutpointAction, SIGNAL(triggered()), this, SLOT(copyCollateralOutpoint_clicked()));

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateDIP3ListScheduled()));
    timer->start(1000);

    GUIUtil::updateFonts();
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

    if (!clientModel || clientModel->node().shutdownRequested()) {
        return;
    }

    // To prevent high cpu usage update only once in SMARTNODELIST_FILTER_COOLDOWN_SECONDS seconds
    // after filter was last changed unless we want to force the update.
    if (fFilterUpdatedDIP3) {
        int64_t nSecondsToWait = nTimeFilterUpdatedDIP3 - GetTime() + SMARTNODELIST_FILTER_COOLDOWN_SECONDS;
        ui->countLabelDIP3->setText(tr("Please wait...") + " " + QString::number(nSecondsToWait));

        if (nSecondsToWait <= 0) {
            updateDIP3List();
            fFilterUpdatedDIP3 = false;
        }
    } else if (mnListChanged) {
        int64_t nMnListUpdateSecods = clientModel->smartnodeSync().isBlockchainSynced() ? SMARTNODELIST_UPDATE_SECONDS : SMARTNODELIST_UPDATE_SECONDS * 10;
        int64_t nSecondsToWait = nTimeUpdatedDIP3 - GetTime() + nMnListUpdateSecods;

        if (nSecondsToWait <= 0) {
            updateDIP3List();
            mnListChanged = false;
        }
    }
}

void SmartnodeList::updateDIP3List()
{
    if (!clientModel || clientModel->node().shutdownRequested()) {
        return;
    }

    auto mnList = clientModel->getSmartnodeList();
    std::map<uint256, CTxDestination> mapCollateralDests;

    {
        // Get all UTXOs for each MN collateral in one go so that we can reduce locking overhead for cs_main
        // We also do this outside of the below Qt list update loop to reduce cs_main locking time to a minimum
        mnList.ForEachMN(false, [&](const CDeterministicMNCPtr& dmn) {
            CTxDestination collateralDest;
            Coin coin;
            if (clientModel->node().getUnspentOutput(dmn->collateralOutpoint, coin) && ExtractDestination(coin.out.scriptPubKey, collateralDest)) {
                mapCollateralDests.emplace(dmn->proTxHash, collateralDest);
            }
        });
    }

    LOCK(cs_dip3list);

    QString strToFilter;
    ui->countLabelDIP3->setText(tr("Updating..."));
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
        walletModel->wallet().listProTxCoins(vOutpts);
        for (const auto& outpt : vOutpts) {
            setOutpts.emplace(outpt);
        }
    }

    mnList.ForEachMN(false, [&](const CDeterministicMNCPtr& dmn) {
        if (walletModel && ui->checkBoxMySmartnodesOnly->isChecked()) {
            bool fMySmartnode = setOutpts.count(dmn->collateralOutpoint) ||
                walletModel->wallet().isSpendable(dmn->pdmnState->keyIDOwner) ||
                walletModel->wallet().isSpendable(dmn->pdmnState->keyIDVoting) ||
                walletModel->wallet().isSpendable(dmn->pdmnState->scriptPayout) ||
                walletModel->wallet().isSpendable(dmn->pdmnState->scriptOperatorPayout);
            if (!fMySmartnode) return;
        }
        // populate list
        // Address, Protocol, Status, Active Seconds, Last Seen, Pub Key
        auto addr_key = dmn->pdmnState->addr.GetKey();
        QByteArray addr_ba(reinterpret_cast<const char*>(addr_key.data()), addr_key.size());
        Coin coin;
        //should this be call directly or use pcoinsTip->GetCoin(outpoint, coin) without locking cs_main
        bool isValidUtxo = GetUTXOCoin(dmn->collateralOutpoint, coin);
        SmartnodeCollaterals collaterals = Params().GetConsensus().nCollaterals;
        int nHeight = chainActive.Tip() == nullptr ? 0 : chainActive.Tip()->nHeight;
        QTableWidgetItem* collateralAmountItem = new QTableWidgetItem(!isValidUtxo ? tr("Invalid") : QString::number(coin.out.nValue / COIN));
        QTableWidgetItem* addressItem = new CSmartnodeListWidgetItem<QByteArray>(QString::fromStdString(dmn->pdmnState->addr.ToString()), addr_ba);
        QTableWidgetItem* statusItem = new QTableWidgetItem(mnList.IsMNValid(dmn) ? tr("ENABLED") : (mnList.IsMNPoSeBanned(dmn) ? tr("POSE_BANNED") : tr("UNKNOWN")));
        QTableWidgetItem* PoSeScoreItem = new CSmartnodeListWidgetItem<int>(QString::number(dmn->pdmnState->nPoSePenalty), dmn->pdmnState->nPoSePenalty);
        QTableWidgetItem* registeredItem = new CSmartnodeListWidgetItem<int>(QString::number(dmn->pdmnState->nRegisteredHeight), dmn->pdmnState->nRegisteredHeight);
        QTableWidgetItem* lastPaidItem = new CSmartnodeListWidgetItem<int>(QString::number(dmn->pdmnState->nLastPaidHeight), dmn->pdmnState->nLastPaidHeight);

        QString strNextPayment = "UNKNOWN";
        int nNextPayment = 0;
        if (nextPayments.count(dmn->proTxHash)) {
            nNextPayment = nextPayments[dmn->proTxHash];
            strNextPayment = QString::number(nNextPayment);
        }
        QTableWidgetItem* nextPaymentItem = new CSmartnodeListWidgetItem<int>(strNextPayment, nNextPayment);

        CTxDestination payeeDest;
        QString payeeStr = tr("UNKNOWN");
        if (ExtractDestination(dmn->pdmnState->scriptPayout, payeeDest)) {
            payeeStr = QString::fromStdString(EncodeDestination(payeeDest));
        }
        QTableWidgetItem* payeeItem = new QTableWidgetItem(payeeStr);

        QString operatorRewardStr = tr("NONE");
        if (dmn->nOperatorReward) {
            operatorRewardStr = QString::number(dmn->nOperatorReward / 100.0, 'f', 2) + "% ";

            if (dmn->pdmnState->scriptOperatorPayout != CScript()) {
                CTxDestination operatorDest;
                if (ExtractDestination(dmn->pdmnState->scriptOperatorPayout, operatorDest)) {
                    operatorRewardStr += tr("to %1").arg(QString::fromStdString(EncodeDestination(operatorDest)));
                } else {
                    operatorRewardStr += tr("to UNKNOWN");
                }
            } else {
                operatorRewardStr += tr("but not claimed");
            }
        }
        QTableWidgetItem* operatorRewardItem = new CSmartnodeListWidgetItem<uint16_t>(operatorRewardStr, dmn->nOperatorReward);

        QString collateralStr = tr("UNKNOWN");
        auto collateralDestIt = mapCollateralDests.find(dmn->proTxHash);
        if (collateralDestIt != mapCollateralDests.end()) {
            collateralStr = QString::fromStdString(EncodeDestination(collateralDestIt->second));
        }
        QTableWidgetItem* collateralItem = new QTableWidgetItem(collateralStr);

        QString ownerStr = QString::fromStdString(EncodeDestination(dmn->pdmnState->keyIDOwner));
        QTableWidgetItem* ownerItem = new QTableWidgetItem(ownerStr);

        QString votingStr = QString::fromStdString(EncodeDestination(dmn->pdmnState->keyIDVoting));
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
        ui->tableWidgetSmartnodesDIP3->setItem(0, COLUMN_SERVICE, addressItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, COLUMN_STATUS, statusItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, COLUMN_POSE, PoSeScoreItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, COLUMN_REGISTERED, registeredItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, COLUMN_LAST_PAYMENT, lastPaidItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, COLUMN_NEXT_PAYMENT, nextPaymentItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, COLUMN_PAYOUT_ADDRESS, payeeItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, COLUMN_OPERATOR_REWARD, operatorRewardItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, COLUMN_COLLATERAL_ADDRESS, collateralItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, COLUMN_COLLATERAL_AMOUNT, collateralAmountItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, COLUMN_OWNER_ADDRESS, ownerItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, COLUMN_VOTING_ADDRESS, votingItem);
        ui->tableWidgetSmartnodesDIP3->setItem(0, COLUMN_PROTX_HASH, proTxHashItem);
    });

    ui->countLabelDIP3->setText(QString::number(ui->tableWidgetSmartnodesDIP3->rowCount()));
    ui->tableWidgetSmartnodesDIP3->setSortingEnabled(true);
}

void SmartnodeList::on_filterLineEditDIP3_textChanged(const QString& strFilterIn)
{
    strCurrentFilterDIP3 = strFilterIn;
    nTimeFilterUpdatedDIP3 = GetTime();
    fFilterUpdatedDIP3 = true;
    ui->countLabelDIP3->setText(tr("Please wait...") + " " + QString::number(SMARTNODELIST_FILTER_COOLDOWN_SECONDS));
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
        strProTxHash = ui->tableWidgetSmartnodesDIP3->item(nSelectedRow, COLUMN_PROTX_HASH)->text().toStdString();
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
