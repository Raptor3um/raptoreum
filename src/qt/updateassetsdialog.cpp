// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/updateassetsdialog.h>
#include <qt/forms/ui_updateassetsdialog.h>

#include <qt/addresstablemodel.h>
#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <txmempool.h>
#include <qt/coincontroldialog.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/sendassetsentry.h>

#include <chainparams.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <wallet/coincontrol.h>
#include <ui_interface.h>
#include <txmempool.h>
#include <policy/fees.h>
#include <wallet/fees.h>
#include <wallet/wallet.h>
#include <future/fee.h>
#include <spork.h>
#include <validation.h>
#include <assets/assets.h>
#include <assets/assetstype.h>

#include <QFontMetrics>
#include <QScrollBar>
#include <QSettings>
#include <QTextDocument>
#include <QStringListModel>
#include <QSortFilterProxyModel>
#include <QCompleter>

#define SEND_CONFIRM_DELAY   3

UpdateAssetsDialog::UpdateAssetsDialog(QWidget *parent) :
        QDialog(parent),
        ui(new Ui::UpdateAssetsDialog),
        clientModel(nullptr),
        model(nullptr),
        m_coin_control(new CCoinControl),
        fFeeMinimized(true) {
    ui->setupUi(this);

    GUIUtil::setFont({ui->labelFeeHeadline,
                      ui->fallbackFeeWarningLabel
                     }, GUIUtil::FontWeight::Bold);

    GUIUtil::setFont({ui->labelBalance,
                      ui->labelBalanceText
                     }, GUIUtil::FontWeight::Bold, 14);

    GUIUtil::setFont({ui->updateAssetLabel
                     }, GUIUtil::FontWeight::Bold, 16);

    GUIUtil::setFont({ui->assetNameLabel,
                      ui->addressLabel,
                      ui->typeLabel,
                      ui->quantityLabel,
                      ui->targetLabel,
                      ui->mintLabel,
                      ui->ipfsLabel,
                      ui->updatableBox}, GUIUtil::FontWeight::Normal, 15);

    GUIUtil::setupAddressWidget(ui->owneraddressText, this);
    GUIUtil::setupAddressWidget(ui->targetaddressText, this);

    GUIUtil::updateFonts();

    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));

    // init transaction fee section
    QSettings settings;
    if (!settings.contains("fFeeSectionMinimized"))
        settings.setValue("fFeeSectionMinimized", true);
    if (!settings.contains("nFeeRadio") && settings.contains("nTransactionFee") &&
        settings.value("nTransactionFee").toLongLong() > 0) // compatibility
        settings.setValue("nFeeRadio", 1); // custom
    if (!settings.contains("nFeeRadio"))
        settings.setValue("nFeeRadio", 0); // recommended
    if (!settings.contains("nSmartFeeSliderPosition"))
        settings.setValue("nSmartFeeSliderPosition", 0);
    if (!settings.contains("nTransactionFee"))
        settings.setValue("nTransactionFee", (qint64) DEFAULT_PAY_TX_FEE);
    if (!settings.contains("fPayOnlyMinFee"))
        settings.setValue("fPayOnlyMinFee", false);

    ui->groupFee->setId(ui->radioSmartFee, 0);
    ui->groupFee->setId(ui->radioCustomFee, 1);
    ui->groupFee->button((int) std::max(0, std::min(1, settings.value("nFeeRadio").toInt())))->setChecked(true);
    ui->customFee->setValue(settings.value("nTransactionFee").toLongLong());
    ui->checkBoxMinimumFee->setChecked(settings.value("fPayOnlyMinFee").toBool());
    minimizeFeeSection(settings.value("fFeeSectionMinimized").toBool());

    ui->updateAssetButton->setText(tr("U&pdate Asset"));
    ui->updateAssetButton->setToolTip(tr("Confirm update asset action"));

    ui->IssueFrequencyBox->setEnabled(false);
    ui->IssueFrequencyBox->setToolTip(tr("Disabled. Not in use"));
    ui->distributionBox->setEnabled(false);
    ui->distributionBox->setToolTip(tr("Manual only until other types are developed"));
    ui->openIpfsButton->hide();

    m_coin_control->UseCoinJoin(false);

    connect(ui->assetList, SIGNAL(currentIndexChanged(QString)), this, SLOT(onAssetSelected(QString)));

    /** Setup the asset list combobox */
    stringModel = new QStringListModel;

    proxy = new QSortFilterProxyModel;
    proxy->setSourceModel(stringModel);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    ui->assetList->setModel(proxy);
    ui->assetList->setEditable(true);
    ui->assetList->lineEdit()->setPlaceholderText("Select an asset");

    completer = new QCompleter(proxy, this);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    ui->assetList->setCompleter(completer);
}

void UpdateAssetsDialog::setClientModel(ClientModel *_clientModel) {
    this->clientModel = _clientModel;

    if (_clientModel) {
        connect(_clientModel, SIGNAL(numBlocksChanged(int, QDateTime, QString, double, bool)), this,
                SLOT(updateSmartFeeLabel()));
    }
}

void UpdateAssetsDialog::setModel(WalletModel *_model) {
    this->model = _model;

    if (_model && _model->getOptionsModel()) {
        interfaces::WalletBalances balances = _model->wallet().getBalances();
        setBalance(balances);
        connect(_model, SIGNAL(balanceChanged(interfaces::WalletBalances)), this,
                SLOT(setBalance(interfaces::WalletBalances)));
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        updateDisplayUnit();

        // fee section
        for (const int n: confTargets) {
            ui->confTargetSelector->addItem(tr("%1 (%2 blocks)").arg(
                    GUIUtil::formatNiceTimeOffset(n * Params().GetConsensus().nPowTargetSpacing)).arg(n));
        }
        connect(ui->confTargetSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(updateSmartFeeLabel()));
        connect(ui->groupFee, SIGNAL(buttonClicked(int)), this, SLOT(updateFeeSectionControls()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(setMinimumFee()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(updateFeeSectionControls()));

        updateFeeSectionControls();
        updateMinFeeLabel();
        updateSmartFeeLabel();

        // set the smartfee-sliders default value (wallets default conf.target or last stored value)
        QSettings settings;
        if (settings.value("nSmartFeeSliderPosition").toInt() != 0) {
            // migrate nSmartFeeSliderPosition to nConfTarget
            // nConfTarget is available since 0.15 (replaced nSmartFeeSliderPosition)
            int nConfirmTarget = 25 - settings.value("nSmartFeeSliderPosition").toInt(); // 25 == old slider range
            settings.setValue("nConfTarget", nConfirmTarget);
            settings.remove("nSmartFeeSliderPosition");
        }
        if (settings.value("nConfTarget").toInt() == 0)
            ui->confTargetSelector->setCurrentIndex(getIndexForConfTarget(model->wallet().getConfirmTarget()));
        else
            ui->confTargetSelector->setCurrentIndex(getIndexForConfTarget(settings.value("nConfTarget").toInt()));
    }
}

UpdateAssetsDialog::~UpdateAssetsDialog() {
    QSettings settings;
    settings.setValue("fFeeSectionMinimized", fFeeMinimized);
    settings.setValue("nFeeRadio", ui->groupFee->checkedId());
    settings.setValue("nConfTarget", getConfTargetForIndex(ui->confTargetSelector->currentIndex()));
    settings.setValue("nTransactionFee", (qint64) ui->customFee->value());
    settings.setValue("fPayOnlyMinFee", ui->checkBoxMinimumFee->isChecked());

    delete ui;
}

bool UpdateAssetsDialog::filladdress(QString address, CKeyID &field) {
    CTxDestination dest = DecodeDestination(address.toStdString());
    if (IsValidDestination(dest)) {
        const CKeyID *keyID = boost::get<CKeyID>(&dest);
        field = *keyID;
        return true;
    }
    return false;
}

static QString GetDistributionType(int t) {
    switch (t) {
        case 0:
            return "manual";
        case 1:
            return "coinbase";
        case 2:
            return "address";
        case 3:
            return "schedule";
    }
    return "invalid";
}

void UpdateAssetsDialog::on_updateAssetButton_clicked() {
    if (!model || !model->getOptionsModel() || !validateInputs())
        return;

    // request unlock only if was locked or unlocked for mixing:
    // this way we let users unlock by walletpassphrase or by menu
    // and make many transactions while unlocking through this dialog
    // will call relock
    WalletModel::EncryptionStatus encStatus = model->getEncryptionStatus();
    if (encStatus == model->Locked || encStatus == model->UnlockedForMixingOnly) {
        WalletModel::UnlockContext ctx(model->requestUnlock());
        if (!ctx.isValid()) {
            // Unlock wallet was cancelled
            return;
        }
        updateAsset();
        return;
    }
    // already unlocked or not encrypted at all
    updateAsset();
}

void UpdateAssetsDialog::updateAsset() {
    if (getAssetsFees() == 0) {
        QMessageBox msgBox;
        msgBox.setText(QString::fromStdString("Under maintenance, try again later."));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    std::string assetId;
    if (!passetsCache->GetAssetId(ui->assetList->currentText().toStdString(), assetId)) {
        QMessageBox msgBox;
        msgBox.setText(QString::fromStdString("Error: Asset not found"));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    CAssetMetaData assetData;
    if (!passetsCache->GetAssetMetaData(assetId, assetData)) {
        QMessageBox msgBox;
        msgBox.setText(QString::fromStdString("Error: Asset not found"));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    if (!assetData.updatable) {
        QMessageBox msgBox;
        msgBox.setText(QString::fromStdString("Error: This asset cannot be updated"));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
        return;  
    }

    //check on mempool if have a mint/update tx for this asset
    if (mempool.CheckForMintAssetConflict(assetData.assetId)) {
        QMessageBox msgBox;
        msgBox.setText("Error: Asset mint or update tx exist on mempool");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    CUpdateAssetTx assetTx;
    assetTx.assetId = assetId;
    if (ui->owneraddressText->text().toStdString() != EncodeDestination(assetData.ownerAddress)) {
        //only ownership change allowed?
        filladdress(ui->owneraddressText->text(), assetTx.ownerAddress);
        assetTx.referenceHash = assetData.referenceHash;
        assetTx.targetAddress = assetData.targetAddress;
        assetTx.amount = assetData.amount;
        assetTx.maxMintCount = assetData.maxMintCount;
        assetTx.issueFrequency = assetData.issueFrequency;
        assetTx.updatable = assetData.updatable;
        assetTx.type = assetData.type;
    } else {
        assetTx.ownerAddress = assetData.ownerAddress;

        if (ui->ipfsText->text() != "")
            assetTx.referenceHash = ui->ipfsText->text().toStdString();
        else
            assetTx.referenceHash = assetData.referenceHash;

        if (ui->targetaddressText->text().toStdString() != EncodeDestination(assetData.targetAddress))
            filladdress(ui->targetaddressText->text(), assetTx.targetAddress);
        else
            assetTx.targetAddress = assetData.targetAddress;

        if (ui->quantitySpinBox->value() > 0)
            assetTx.amount = ui->quantitySpinBox->value() * COIN;
        else
            assetTx.amount = assetData.amount;
        
        //maxMintCount need to be equal to or greater than current mintCount
        if (ui->quantitySpinBox->value() >= assetData.mintCount)
            assetTx.maxMintCount = ui->maxmintSpinBox->value();
        else
            assetTx.maxMintCount = assetData.maxMintCount;
        
        if (ui->IssueFrequencyBox->value() > 0)
            assetTx.issueFrequency = ui->IssueFrequencyBox->value();
        else
            assetTx.issueFrequency = assetData.issueFrequency; 

        assetTx.updatable = ui->updatableBox->isChecked();

        //Manual only until other types are developed
        assetTx.type = assetData.type;
    }
    bool haschanges = false;
    if (assetTx.ownerAddress != assetData.ownerAddress) {
        haschanges = true;
    } else {
        if (assetTx.updatable != assetData.updatable)
            haschanges = true;
        else if (assetTx.referenceHash != assetData.referenceHash)
            haschanges = true;
        else if (assetTx.targetAddress != assetData.targetAddress)
            haschanges = true;
        else if (assetTx.type != assetData.type)
            haschanges = true;
        else if (assetTx.amount != assetData.amount)
            haschanges = true;
        else if (assetTx.maxMintCount != assetData.maxMintCount)
            haschanges = true;
        else if (assetTx.issueFrequency != assetData.issueFrequency)
            haschanges = true;
    }

    if (!haschanges) {
        QMessageBox msgBox;
        msgBox.setText("Error: at least 1 parameter must be updated");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    CTxDestination ownerAddress = CTxDestination(assetData.ownerAddress);
    if (!IsValidDestination(ownerAddress)) {
        QMessageBox msgBox;
        msgBox.setText("ERROR: Invalid owner address");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    m_coin_control->destChange = ownerAddress;
    m_coin_control->fRequireAllInputs = false;

    std::vector <COutput> vecOutputs;
    //select only confirmed inputs, nMinDepth >= 1
    model->wallet().AvailableCoins(vecOutputs, true, nullptr, 1, MAX_MONEY , MAX_MONEY, 0, 1);

    for (const auto &out: vecOutputs) {
        CTxDestination txDest;
        if (ExtractDestination(out.tx->tx->vout[out.i].scriptPubKey, txDest) && txDest == ownerAddress) {
            m_coin_control->Select(COutPoint(out.tx->tx->GetHash(), out.i));
        }
    }

    if (!m_coin_control->HasSelected()) {
        QMessageBox msgBox;
        msgBox.setText(QString::fromStdString(strprintf("Error: No funds at specified address %s", EncodeDestination(ownerAddress))));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    CTransactionRef newTx;
    CAmount nFee;
    int nChangePos = -1;
    std::string strFailReason;
    std::vector <CRecipient> vecSend;
    assetTx.fee = getAssetsFees();
    int Payloadsize;

    if (!model->wallet().createTransaction(vecSend, newTx, *m_coin_control, true, nChangePos, nFee, strFailReason,
                                Payloadsize, nullptr, nullptr, &assetTx)) {
        //handle creation error
        QMessageBox msgBox;
        msgBox.setText(QString::fromStdString(strFailReason));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }
    
    m_coin_control->destChange = CNoDestination();
    m_coin_control->UnSelectAll();

    QString questionString = tr("Updating asset: %1<br>").arg(QString::fromStdString(assetData.name)); 
    questionString.append("<hr />");
    if (assetTx.ownerAddress != assetData.ownerAddress) {
        questionString.append(tr("transfer ownership from: %1 <br>").arg(QString::fromStdString(EncodeDestination(assetData.ownerAddress))));
        questionString.append(tr("To: %1 <hr />").arg(QString::fromStdString(EncodeDestination(assetTx.ownerAddress))));
    } else {
        if (assetTx.updatable != assetData.updatable)
            questionString.append(tr("Updatable: %1 <br>").arg(assetTx.updatable ? "true": "false"));
        
        if (assetTx.referenceHash != assetData.referenceHash)
            questionString.append(tr("ReferenceHash: %1 <br>").arg(QString::fromStdString(assetTx.referenceHash)));
        
        if (assetTx.targetAddress != assetData.targetAddress)
            questionString.append(tr("Target: %1 <br>").arg(QString::fromStdString(EncodeDestination(assetTx.targetAddress))));

        if (assetTx.type != assetData.type)
            questionString.append(tr("Distribution Type: %1 <br>").arg(GetDistributionType(assetTx.type)));
        
        if (assetTx.amount != assetData.amount)
            questionString.append(tr("Amount: %1 <br>").arg(QString::number(assetTx.amount / COIN)));
        
        if (assetTx.maxMintCount != assetData.maxMintCount)
            questionString.append(tr("MaxMintCount: %1 <br>").arg(QString::number(assetTx.maxMintCount)));

        if (assetTx.issueFrequency != assetData.issueFrequency)
            questionString.append(tr("IssueFrequency: %1 <br>").arg(QString::number(assetTx.issueFrequency)));
        
    }
    // Display message box
    nFee += assetTx.fee * COIN;
    if (nFee > 0) {
        // append fee string if a fee is required
        questionString.append(
                "<hr /><span style='" + GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR) + "'>");
        questionString.append(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), nFee));
        questionString.append("</span> ");
        questionString.append(tr("are added as transaction fee.<hr />"));
    }
    UpdateAssetConfirmationDialog confirmationDialog(tr("Confirm Asset Update"),
                                                   questionString, SEND_CONFIRM_DELAY, this);
    confirmationDialog.exec();
    QMessageBox::StandardButton retval = static_cast<QMessageBox::StandardButton>(confirmationDialog.result());
    if (retval != QMessageBox::Yes) {
        return;
    }
    model->wallet().commitTransaction(newTx, {}, {});
    clear();
}

bool UpdateAssetsDialog::validateInputs() {
    bool retval{true};

    std::string ipfshash = ui->ipfsText->text().toStdString();
    if (ipfshash.length() > 128) {
        retval = false;
        ui->ipfsText->setValid(false);
    }

    if (!model->validateAddress(ui->owneraddressText->text())) {
        ui->owneraddressText->setValid(false);
        retval = false;
    }

    if (!model->validateAddress(ui->targetaddressText->text())) {
        ui->targetaddressText->setValid(false);
        retval = false;
    }
    
    if (ui->maxmintSpinBox->value() < 0 || ui->maxmintSpinBox->value() > 65535){
        ui->maxmintSpinBox->setStyleSheet(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_INVALID));
        retval = false;
    }

    if (ui->quantitySpinBox->value() < 0 || ui->quantitySpinBox->value() > MAX_MONEY / COIN){
        ui->quantitySpinBox->setStyleSheet(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_INVALID));
        retval = false;
    }

    return retval;
}

void UpdateAssetsDialog::clear() {
    prevName = "";
    ui->assetList->setCurrentIndex(0);
    ui->owneraddressText->clear();
    ui->ipfsText->clear();
    ui->targetaddressText->clear();
    ui->quantitySpinBox->setValue(0);
    ui->maxmintSpinBox->setValue(1);
    ui->IssueFrequencyBox->setValue(0);
    ui->updatableBox->setChecked(false);
    
    updateTabsAndLabels();
}

void UpdateAssetsDialog::updateTabsAndLabels() {
    setupTabChain(0);
}

QWidget *UpdateAssetsDialog::setupTabChain(QWidget *prev) {
    QWidget::setTabOrder(prev, ui->updateAssetButton);
    QWidget::setTabOrder(ui->updateAssetButton, ui->clearButton);
    return ui->clearButton;
}

void UpdateAssetsDialog::setBalance(const interfaces::WalletBalances &balances) {
    if (model && model->getOptionsModel()) {
        CAmount bal = balances.balance;
        ui->labelBalance->setText(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), bal));
    }
}

void UpdateAssetsDialog::updateDisplayUnit() {
    setBalance(model->wallet().getBalances());
    ui->customFee->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    updateMinFeeLabel();
    updateSmartFeeLabel();
}

void UpdateAssetsDialog::minimizeFeeSection(bool fMinimize) {
    ui->labelFeeMinimized->setVisible(fMinimize);
    ui->buttonChooseFee->setVisible(fMinimize);
    ui->buttonMinimizeFee->setVisible(!fMinimize);
    ui->frameFeeSelection->setVisible(!fMinimize);
    ui->horizontalLayoutSmartFee->setContentsMargins(0, (fMinimize ? 0 : 6), 0, 0);
    fFeeMinimized = fMinimize;
}

void UpdateAssetsDialog::on_buttonChooseFee_clicked() {
    minimizeFeeSection(false);
}

void UpdateAssetsDialog::on_buttonMinimizeFee_clicked() {
    updateFeeMinimizedLabel();
    minimizeFeeSection(true);
}

void UpdateAssetsDialog::setMinimumFee() {
    ui->customFee->setValue(model->wallet().getRequiredFee(1000));
}

void UpdateAssetsDialog::updateFeeSectionControls() {
    ui->confTargetSelector->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee2->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee3->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelFeeEstimation->setEnabled(ui->radioSmartFee->isChecked());
    ui->checkBoxMinimumFee->setEnabled(ui->radioCustomFee->isChecked());
    ui->labelMinFeeWarning->setEnabled(ui->radioCustomFee->isChecked());
    ui->labelCustomPerKilobyte->setEnabled(ui->radioCustomFee->isChecked() && !ui->checkBoxMinimumFee->isChecked());
    ui->customFee->setEnabled(ui->radioCustomFee->isChecked() && !ui->checkBoxMinimumFee->isChecked());
}

void UpdateAssetsDialog::updateFeeMinimizedLabel() {
    if (!model || !model->getOptionsModel())
        return;

    if (ui->radioSmartFee->isChecked())
        ui->labelFeeMinimized->setText(ui->labelSmartFee->text());
    else {
        ui->labelFeeMinimized->setText(
                BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), ui->customFee->value()) +
                "/kB");
    }
}

void UpdateAssetsDialog::updateMinFeeLabel() {
    if (model && model->getOptionsModel())
        ui->checkBoxMinimumFee->setText(tr("Pay only the required fee of %1").arg(
                BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(),
                                             model->wallet().getRequiredFee(1000)) + "/kB")
        );
}

void UpdateAssetsDialog::updateCoinControlState(CCoinControl &ctrl) {
    if (ui->radioCustomFee->isChecked()) {
        ctrl.m_feerate = CFeeRate(ui->customFee->value());
    } else {
        ctrl.m_feerate.reset();
    }
    // Avoid using global defaults when sending money from the GUI
    // Either custom fee will be used or if not selected, the confirmation target from dropdown box
    ctrl.m_confirm_target = getConfTargetForIndex(ui->confTargetSelector->currentIndex());
}

void UpdateAssetsDialog::updateSmartFeeLabel() {
    if (!model || !model->getOptionsModel())
        return;
    updateCoinControlState(*m_coin_control);
    m_coin_control->m_feerate.reset(); // Explicitly use only fee estimation rate for smart fee labels
    int returned_target;
    FeeReason reason;
    CFeeRate feeRate = CFeeRate(model->wallet().getMinimumFee(1000, *m_coin_control, &returned_target, &reason));

    ui->labelSmartFee->setText(
            BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), feeRate.GetFeePerK()) + "/kB");

    if (reason == FeeReason::FALLBACK) {
        ui->labelSmartFee2->show(); // (Smart fee not initialized yet. This usually takes a few blocks...)
        ui->labelFeeEstimation->setText("");
        ui->fallbackFeeWarningLabel->setVisible(true);
    } else {
        ui->labelSmartFee2->hide();
        ui->labelFeeEstimation->setText(tr("Estimated to begin confirmation within %n block(s).", "", returned_target));
        ui->fallbackFeeWarningLabel->setVisible(false);
    }

    updateFeeMinimizedLabel();
}

void UpdateAssetsDialog::onAssetSelected(QString name) {
    if (name == "" || ui->assetList->currentIndex() <= 0)
        return;

    if (name == prevName)
        return;

    std::string assetId;
    if (passetsCache->GetAssetId(name.toStdString(), assetId)) {
        CAssetMetaData assetData;
        if (passetsCache->GetAssetMetaData(assetId, assetData)) {
            prevName = name;
            ui->owneraddressText->setText(QString::fromStdString(EncodeDestination(assetData.ownerAddress)));
            ui->targetaddressText->setText(QString::fromStdString(EncodeDestination(assetData.targetAddress)));
            ui->ipfsText->setText(QString::fromStdString(assetData.referenceHash));
            ui->updatableBox->setChecked(assetData.updatable);
            ui->quantitySpinBox->setValue(assetData.amount / COIN);
            ui->IssueFrequencyBox->setValue(assetData.issueFrequency);
            ui->maxmintSpinBox->setValue(assetData.maxMintCount);
        }
    }
}

void UpdateAssetsDialog::updateAssetList() {

    //make a copy of current selected asset to restore selection after
    //updating the list of assets
    int index = ui->assetList->currentIndex();
    QString assetName = ui->assetList->currentText();

    // Get the assets list
    std::map <uint256, std::pair<std::string, CKeyID>> assets = model->wallet().getMyAssets();

    QStringList list;
    list << "Select an asset";
    for (auto asset: assets) {
        CAssetMetaData assetData;
        std::string assetId = asset.first.ToString();
        if (passetsCache->GetAssetMetaData(assetId, assetData)) {
            if (assetData.updatable && model->wallet().isSpendable(assetData.ownerAddress)) {
                list << QString::fromStdString(assetData.name);
            }
        }
    }

    stringModel->setStringList(list);
    //restore selected asset
    if (index >= 1) {
        index = ui->assetList->findText(assetName);
        ui->assetList->setCurrentIndex(index);
    } else {
        ui->assetList->setCurrentIndex(0);
        ui->assetList->activated(0);
    }
}

UpdateAssetConfirmationDialog::UpdateAssetConfirmationDialog(const QString &title, const QString &text, int _secDelay,
                                                         QWidget *parent) :
        QMessageBox(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::Cancel, parent),
        secDelay(_secDelay) {
    GUIUtil::updateFonts();
    setDefaultButton(QMessageBox::Cancel);
    yesButton = button(QMessageBox::Yes);
    updateYesButton();
    connect(&countDownTimer, SIGNAL(timeout()), this, SLOT(countDown()));
}

int UpdateAssetConfirmationDialog::exec() {
    updateYesButton();
    countDownTimer.start(1000);
    return QMessageBox::exec();
}

void UpdateAssetConfirmationDialog::countDown() {
    secDelay--;
    updateYesButton();

    if (secDelay <= 0) {
        countDownTimer.stop();
    }
}

void UpdateAssetConfirmationDialog::updateYesButton() {
    if (secDelay > 0) {
        yesButton->setEnabled(false);
        yesButton->setText(tr("Yes") + " (" + QString::number(secDelay) + ")");
    } else {
        yesButton->setEnabled(true);
        yesButton->setText(tr("Yes"));
    }
}