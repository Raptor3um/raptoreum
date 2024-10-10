// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qt/assetsdialog.h>
#include <qt/forms/ui_assetsdialog.h>
#include <qt/uploaddownload.h>

#include <chainparams.h>
#include <qt/clientmodel.h>
#include <clientversion.h>
#include <coins.h>
#include <txmempool.h>
#include <qt/guiutil.h>
#include <qt/walletmodel.h>
#include <qt/optionsmodel.h>
#include <wallet/wallet.h>
#include <validation.h>
#include <assets/assets.h>
#include <assets/assetstype.h>
#include <qt/bitcoinunits.h>

#include <univalue.h>

#include <QMessageBox>
#include <QPushButton>
#include <QTableWidgetItem>
#include <QtGui/QClipboard>
#include <QByteArray>
#include <QBuffer>
#include <QPixmap>
#include <QLabel>
#include <QScrollArea>
#include <QApplication>
#include <QDesktopWidget>
#include <QScreen>
#include <QKeyEvent>

template<typename T>
class CAssetListWidgetItem : public QTableWidgetItem {
    T itemData;

public:
    explicit CAssetListWidgetItem(const QString &text, const T &data, int type = Type) :
            QTableWidgetItem(text, type),
            itemData(data) {}

    bool operator<(const QTableWidgetItem &other) const override {
        return itemData < ((CAssetListWidgetItem *) &other)->itemData;
    }
};

AssetsDialog::AssetsDialog(QWidget *parent) :
        QDialog(parent),
        cachedNumBlocks(-1),
        ui(new Ui::AssetsDialog) {
    ui->setupUi(this);

    GUIUtil::setFont({ui->label_filter_2, ui->assetinfolabel}, GUIUtil::FontWeight::Bold, 16);
    GUIUtil::setFont({ui->label_6, ui->label_4, ui->label_3, ui->label_4, ui->label_5, ui->referenceLabel,
                        ui->errorLabel}, GUIUtil::FontWeight::Bold, 14);
    GUIUtil::setFont({ui->idTextLablel, ui->nameTextLabel, ui->typeLabel,
                        ui->supplyTextLabel}, GUIUtil::FontWeight::Normal, 14);

    int columnNameWidth = 300;
    int columnIdWidth = 80;
    int columnBalanceWidth = 120;

    ui->tableWidgetAssets->verticalHeader()->hide();
    ui->tableWidgetAssets->setColumnWidth(COLUMN_NAME, columnNameWidth);
    ui->tableWidgetAssets->setColumnWidth(COLUMN_ID, columnIdWidth);
    ui->tableWidgetAssets->setColumnHidden(COLUMN_ID, true);
    ui->tableWidgetAssets->setColumnWidth(COLUMN_CONFIRMED, columnBalanceWidth);
    ui->tableWidgetAssets->setColumnWidth(COLUMN_PENDING, columnBalanceWidth);

    ui->tableWidgetAssets->setContextMenuPolicy(Qt::CustomContextMenu);

    QAction *sendAssetAction = new QAction(tr("Send asset"), this);
    QAction *copyAssetNameAction = new QAction(tr("Copy asset name"), this);
    QAction *detailsAction = new QAction(tr("View details"), this);

    contextMenuAsset = new QMenu(this);
    contextMenuAsset->addAction(sendAssetAction);
    contextMenuAsset->addAction(detailsAction);

    connect(ui->tableWidgetAssets, &QTableWidget::customContextMenuRequested, this,
            &AssetsDialog::showContextMenuAsset);
    connect(ui->tableWidgetAssets, &QTableWidget::cellClicked, this, &AssetsDialog::Asset_clicked);
    connect(ui->tableWidgetAssets, &QTableWidget::doubleClicked, this, &AssetsDialog::Asset_details_clicked);
    connect(sendAssetAction, &QAction::triggered, this, &AssetsDialog::SendAsset_clicked);
    connect(detailsAction, &QAction::triggered, this, &AssetsDialog::Asset_details_clicked);
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &AssetsDialog::updateAssetBalanceScheduled);
    timer->start(1000);

    ui->updateButton->setVisible(false);
    ui->mintButton->setVisible(false);
    ui->errorLabel->setVisible(false);
    ui->errorTextLabel->setVisible(false);
    ui->errorTextLabel->setStyleSheet(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_INVALID));
    ui->referenceDisplay->setCursor(Qt::PointingHandCursor);

    GUIUtil::updateFonts();
}

AssetsDialog::~AssetsDialog() {
    delete ui;
}

void AssetsDialog::setClientModel(ClientModel *model) {
    this->clientModel = model;
}

void AssetsDialog::setModel(WalletModel *model) {
    this->walletModel = model;
    balanceChanged = true;
}

void AssetsDialog::keyPressEvent(QKeyEvent* evt)
{
    // Escape hides the dialog, which leaves us with an empty window
    if (evt->key() == Qt::Key_Escape)
        return;
    QDialog::keyPressEvent(evt);
}

void AssetsDialog::showContextMenuAsset(const QPoint &point) {
    QTableWidgetItem *item = ui->tableWidgetAssets->itemAt(point);
    if (item) contextMenuAsset->exec(QCursor::pos());
}

void AssetsDialog::updateAssetBalance(){
    balanceChanged = true;
}

void AssetsDialog::updateAssetBalanceScheduled() {
    if (!walletModel || !clientModel || clientModel->node().shutdownRequested()) {
        return;
    }
    // Fix crash if the node is shutting down
    int num_block = clientModel->node().getNumBlocks();
    if (!balanceChanged && num_block == cachedNumBlocks) {
        return;
    }

    cachedNumBlocks = num_block;
    balanceChanged = false;

    ui->tableWidgetAssets->setSortingEnabled(false);
    ui->tableWidgetAssets->clearContents();
    ui->tableWidgetAssets->setRowCount(0);

    std::map<std::string, std::pair<CAmount, CAmount>> assetsbalance = walletModel->wallet().getAssetsBalanceAll();
    for (auto it : assetsbalance){
        CAssetMetaData asset;
        if (!passetsCache->GetAssetMetaData(it.first, asset))
            continue; //this should never happen

        QTableWidgetItem *name = new CAssetListWidgetItem<QString>(
                QString::fromStdString(asset.name), QString::fromStdString(asset.name));
        QTableWidgetItem *asset_Id = new CAssetListWidgetItem<QString>(
                QString::fromStdString(it.first), QString::fromStdString(it.first));
        QTableWidgetItem *confirmed = new CAssetListWidgetItem<CAmount>(
                BitcoinUnits::format(8, it.second.first / BitcoinUnits::factorAsset(MAX_ASSET_UNITS -  asset.decimalPoint) , false, BitcoinUnits::separatorAlways, asset.decimalPoint), it.second.first);
        QTableWidgetItem *pending = new CAssetListWidgetItem<CAmount>(
                BitcoinUnits::format(8, it.second.second / BitcoinUnits::factorAsset(MAX_ASSET_UNITS -  asset.decimalPoint) , false, BitcoinUnits::separatorAlways, asset.decimalPoint), it.second.second);


        ui->tableWidgetAssets->insertRow(0);
        ui->tableWidgetAssets->setItem(0, COLUMN_NAME, name);
        ui->tableWidgetAssets->setItem(0, COLUMN_ID, asset_Id);
        ui->tableWidgetAssets->setItem(0, COLUMN_CONFIRMED, confirmed);
        ui->tableWidgetAssets->setItem(0, COLUMN_PENDING, pending);
    }
    //map of assets that the wallet own, this is need for display assets with 0 balance
    std::map <uint256, std::pair<std::string, CKeyID>> myassets = walletModel->wallet().getMyAssets();
    for (auto it : myassets){
        std::string assetId = it.first.ToString();

        auto tmp = assetsbalance.find(assetId);
        if (tmp != assetsbalance.end() || !walletModel->wallet().isSpendable(it.second.second))
            continue;

        QTableWidgetItem *name = new CAssetListWidgetItem<QString>(
                QString::fromStdString(it.second.first), QString::fromStdString(it.second.first));
        QTableWidgetItem *asset_Id = new CAssetListWidgetItem<QString>(
                QString::fromStdString(assetId), QString::fromStdString(assetId));
        QTableWidgetItem *confirmed = new CAssetListWidgetItem<CAmount>(
                QString::number(0), 0);
        QTableWidgetItem *pending = new CAssetListWidgetItem<CAmount>(
                QString::number(0), 0);

        ui->tableWidgetAssets->insertRow(0);
        ui->tableWidgetAssets->setItem(0, COLUMN_NAME, name);
        ui->tableWidgetAssets->setItem(0, COLUMN_ID, asset_Id);
        ui->tableWidgetAssets->setItem(0, COLUMN_CONFIRMED, confirmed);
        ui->tableWidgetAssets->setItem(0, COLUMN_PENDING, pending);
    }

    ui->tableWidgetAssets->setSortingEnabled(true);
}

std::string AssetsDialog::GetSelectedAsset() {
    if (!clientModel) {
        return "";
    }

    QItemSelectionModel *selectionModel = ui->tableWidgetAssets->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return "";

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    return ui->tableWidgetAssets->item(nSelectedRow, COLUMN_ID)->text().toStdString();
}

void AssetsDialog::SendAsset_clicked() {
    std::string assetId = GetSelectedAsset();

    if(assetId == "") return;
    CAssetMetaData asset;
    if (passetsCache->GetAssetMetaData(assetId, asset))
        Q_EMIT assetSendClicked(asset.name);
}

void AssetsDialog::Asset_clicked() {
    std::string assetId = GetSelectedAsset();

    if(assetId == "") return;

    ui->idTextLablel->setText(QString::fromStdString(assetId));
    CAssetMetaData asset;
    if (!passetsCache->GetAssetMetaData(assetId, asset)){
        ui->errorLabel->setVisible(true);
        ui->errorTextLabel->setVisible(true);
        ui->errorTextLabel->setText(tr("Asset metadata not found.\n(Not mined on a block)"));
        ui->nameTextLabel->clear();
        ui->supplyTextLabel->clear();
        ui->typeLabel->clear();
        ui->mintButton->setEnabled(false);
        ui->updateButton->setEnabled(false);
        return;
    }
    //hide error labels
    ui->errorLabel->setVisible(false);
    ui->errorTextLabel->setVisible(false);

    ui->nameTextLabel->setText(QString::fromStdString(asset.name));
    if (asset.isRoot) {
      ui->typeLabel->setText(asset.isUnique ? tr("Unique/NFT") : tr("Root"));
    } else {
      ui->typeLabel->setText(asset.isUnique ? tr("Unique/NFT") : tr("Sub"));
    }
    if (asset.mintCount > 0) {
        QString decimal = asset.decimalPoint > 0 ? "." + QString::number(0).rightJustified(asset.decimalPoint, '0') : "";
        ui->supplyTextLabel->setText(BitcoinUnits::format(8, asset.circulatingSupply, false,
                        BitcoinUnits::separatorAlways, 0) + decimal);
    } else {
        ui->supplyTextLabel->setText("0");
    }

    if (walletModel->wallet().isSpendable(asset.ownerAddress)){
        ui->updateButton->setVisible(true);
        ui->mintButton->setVisible(true);
        ui->mintButton->setEnabled(asset.mintCount < asset.maxMintCount);
        ui->updateButton->setEnabled(asset.updatable);
    } else {
        ui->updateButton->setVisible(false);
        ui->mintButton->setVisible(false);
        ui->mintButton->setEnabled(false);
        ui->updateButton->setEnabled(false);
    }
    displayImage(asset.referenceHash);
}

void AssetsDialog::Asset_details_clicked() {
    std::string assetId = GetSelectedAsset();

    if(assetId == "") return;

    CAssetMetaData asset;
    if (passetsCache->GetAssetMetaData(assetId, asset)) {
        UniValue json(UniValue::VOBJ);
        asset.ToJson(json);

        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Information);
        msgBox.setWindowTitle(tr("Details for asset: %1").arg(QString::fromStdString(asset.name)));
        msgBox.setText(QString::fromStdString(json.write(2)));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setTextInteractionFlags(Qt::TextSelectableByMouse);
        msgBox.exec();
    }
}

void AssetsDialog::on_mintButton_clicked() {
    if (!walletModel || !walletModel->getOptionsModel())
        return;
    // request unlock only if was locked or unlocked for mixing:
    // this way we let users unlock by walletpassphrase or by menu
    // and make many transactions while unlocking through this dialog
    // will call relock
    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();
    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForMixingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());
        if (!ctx.isValid()) {
            // Unlock wallet was cancelled
            return;
        }
        mintAsset();
        return;
    }
    // already unlocked or not encrypted at all
    mintAsset();
}

void AssetsDialog::on_updateButton_clicked() {
        std::string assetId = GetSelectedAsset();

    if(assetId == "") return;
    CAssetMetaData asset;
    if (passetsCache->GetAssetMetaData(assetId, asset))
        Q_EMIT assetUpdateClicked(asset.name);
}

void AssetsDialog::displayImage(const std::string& cid) {
    ui->referenceDisplay->clear();
    if(cid.empty() || cid.length() < 46 || cid.compare(0, 2, "Qm")) {
        ui->referenceLabel->hide();
        ui->referenceDisplay->hide();
    } else {
        ui->referenceLabel->show();
        ui->referenceDisplay->show();
        std::string response_data;
        download(cid, response_data);
        QByteArray imageData = QByteArray::fromRawData(response_data.data(), response_data.size());
        QBuffer buffer(&imageData);
        buffer.open(QIODevice::ReadOnly);
        bool isLoaded = currentRefImage.load(&buffer, nullptr);

        if(isLoaded) {
            int displayWidth = ui->referenceDisplay->width() < currentRefImage.width() ? ui->referenceDisplay->width() : currentRefImage.width();
            int displayHeight = currentRefImage.height() > 260 ? 260 : currentRefImage.height();
            QPixmap pixmap = QPixmap::fromImage(currentRefImage.scaled(displayWidth, displayHeight, Qt::KeepAspectRatio));
            ui->referenceDisplay->setPixmap(pixmap);
            ui->referenceDisplay->setToolTip(QString::fromStdString(cid));
            ui->referenceDisplay->setOpenExternalLinks(false);
        } else {
            std::string ipfsGatewayUrl = gArgs.GetArg("-ipfsgateway", DEFAULT_IPFS_GATEWAY_URL);
            std::string displayUrl = "<a href=\"" + ipfsGatewayUrl + cid + "\">" + cid + "</a>";
            ui->referenceDisplay->setText(QString::fromStdString(displayUrl));
            ui->referenceDisplay->setToolTip("");
            ui->referenceDisplay->setOpenExternalLinks(true);

        }

    }
}
//TODO: not yet working.
void AssetsDialog::showFullRefImage() {
    printf("full image showing\n");
    QDialog dialog(this);
    dialog.setWindowTitle(ui->referenceDisplay->toolTip());

    QVBoxLayout *dialogLayout = new QVBoxLayout(&dialog);

    QLabel *fullImageLabel = new QLabel(&dialog);
    fullImageLabel->setPixmap(QPixmap::fromImage(currentRefImage));

    QScrollArea *scrollArea = new QScrollArea(&dialog);
    scrollArea->setWidget(fullImageLabel);
    scrollArea->setWidgetResizable(true);

    dialogLayout->addWidget(scrollArea);

    QScreen *currentScreen = this->screen();
    QRect screenGeometry = currentScreen->geometry();
    int displayWidth = currentRefImage.width() >= screenGeometry.width() ? screenGeometry.width() - 20 : screenGeometry.width();
    int displayHeight = currentRefImage.height() >= screenGeometry.height() ? screenGeometry.height() - 20 : screenGeometry.height();

    dialog.setLayout(dialogLayout);
    dialog.resize(displayWidth, displayHeight); // Adjust the size of the dialog as needed
    dialog.exec();
}

void AssetsDialog::mintAsset() {
    if (getAssetsFees() == 0) {
        QMessageBox msgBox;
        msgBox.setText(QString::fromStdString("Under maintenance, try again later."));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    std::string assetId = ui->idTextLablel->text().toStdString();

    // get asset metadadta
    CAssetMetaData tmpAsset;
    if (!passetsCache->GetAssetMetaData(assetId, tmpAsset)) {
        QMessageBox msgBox;
        msgBox.setText(tr("ERROR: Asset metadata not found"));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    //check on mempool if have a mint tx for this asset
    if (mempool.CheckForMintAssetConflict(tmpAsset.assetId)) {
        QMessageBox msgBox;
        msgBox.setText(tr("Error: Asset mint or update tx exists on mempool"));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    CMintAssetTx mintAsset;
    mintAsset.assetId = tmpAsset.assetId;
    mintAsset.fee = getAssetsFees();

    CCoinControl coinControl;

    CTransactionRef wtx;
    CAmount nFee;
    int nChangePos = -1;
    std::string strFailReason;
    std::vector <CRecipient> vecSend;

    if (tmpAsset.isUnique) {
        //build unique output using current supply as start unique id
        uint64_t id = tmpAsset.circulatingSupply;
        // Get the script for the target address
        CScript scriptPubKey = GetScriptForDestination(tmpAsset.targetAddress);
        // Update the scriptPubKey with the transfer asset information
        CAssetTransfer assetTransfer(tmpAsset.assetId, tmpAsset.amount, id);
        assetTransfer.BuildAssetTransaction(scriptPubKey);

        CRecipient recipient = {scriptPubKey, 0, false};
        vecSend.push_back(recipient);
    } else {
        // Get the script for the target address
        CScript scriptPubKey = GetScriptForDestination(DecodeDestination(EncodeDestination(tmpAsset.targetAddress)));
        // Update the scriptPubKey with the transfer asset information
        CAssetTransfer assetTransfer(tmpAsset.assetId, tmpAsset.amount);
        assetTransfer.BuildAssetTransaction(scriptPubKey);

        CRecipient recipient = {scriptPubKey, 0, false};
        vecSend.push_back(recipient);
    }
    int Payloadsize;

    if (!walletModel->wallet().createTransaction(vecSend, wtx, coinControl, true, nChangePos, nFee, strFailReason,
                                Payloadsize, nullptr, &mintAsset)) {
        //handle creation error
        QMessageBox msgBox;
        msgBox.setText(QString::fromStdString(strFailReason));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    QString questionString = tr("Mint details:");
    questionString.append("<hr />");
    questionString.append(tr("Name: %1 <br>").arg(QString::fromStdString(tmpAsset.name)));
    questionString.append(tr("Amount: %1").arg(QString::number(tmpAsset.amount / COIN)));
    // Display message box
    nFee += tmpAsset.fee * COIN;
    if (nFee > 0) {
        // append fee string if a fee is required
        questionString.append(
                "<hr /><span style='" + GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR) + "'>");
        questionString.append(BitcoinUnits::formatHtmlWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), nFee));
        questionString.append("</span> ");
        questionString.append(tr("are added as transaction fee.<hr />"));
    }
    MintAssetConfirmationDialog confirmationDialog(tr("Confirm Asset Mint"),
                                                   questionString, 3, this);
    confirmationDialog.exec();
    QMessageBox::StandardButton retval = static_cast<QMessageBox::StandardButton>(confirmationDialog.result());
    if (retval != QMessageBox::Yes) {
        return;
    }
    walletModel->wallet().commitTransaction(wtx, {}, {});

    //disable mint button
    if (tmpAsset.mintCount + 1 >= tmpAsset.maxMintCount)
            ui->mintButton->setEnabled(false);
}

MintAssetConfirmationDialog::MintAssetConfirmationDialog(const QString &title, const QString &text, int _secDelay,
                                                         QWidget *parent) :
        QMessageBox(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::Cancel, parent),
        secDelay(_secDelay) {
    GUIUtil::updateFonts();
    setDefaultButton(QMessageBox::Cancel);
    yesButton = button(QMessageBox::Yes);
    updateYesButton();
    connect(&countDownTimer, SIGNAL(timeout()), this, SLOT(countDown()));
}

int MintAssetConfirmationDialog::exec() {
    updateYesButton();
    countDownTimer.start(1000);
    return QMessageBox::exec();
}

void MintAssetConfirmationDialog::countDown() {
    secDelay--;
    updateYesButton();

    if (secDelay <= 0) {
        countDownTimer.stop();
    }
}

void MintAssetConfirmationDialog::updateYesButton() {
    if (secDelay > 0) {
        yesButton->setEnabled(false);
        yesButton->setText(tr("Yes") + " (" + QString::number(secDelay) + ")");
    } else {
        yesButton->setEnabled(true);
        yesButton->setText(tr("Yes"));
    }
}