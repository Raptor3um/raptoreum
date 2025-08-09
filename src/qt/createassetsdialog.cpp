// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/createassetsdialog.h>
#include <qt/forms/ui_createassetsdialog.h>

#include <qt/addresstablemodel.h>
#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <txmempool.h>
#include <qt/coincontroldialog.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/sendassetsentry.h>
#include <qt/uploaddownload.h>

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
#include <QStringListModel>
#include <QSortFilterProxyModel>
#include <QTextDocument>
#include <QFileDialog>

#define SEND_CONFIRM_DELAY   3

CreateAssetsDialog::CreateAssetsDialog(QWidget *parent) :
        QDialog(parent),
        ui(new Ui::CreateAssetsDialog),
        clientModel(nullptr),
        model(nullptr),
        m_coin_control(new CCoinControl),
        fFeeMinimized(true) {
    ui->setupUi(this);

    GUIUtil::setFont({ui->labelCoinControlFeatures,
                      ui->labelCoinControlInsuffFunds,
                      ui->labelCoinControlQuantityText,
                      ui->labelCoinControlBytesText,
                      ui->labelCoinControlAmountText,
                      ui->labelCoinControlLowOutputText,
                      ui->labelCoinControlFeeText,
                      ui->labelCoinControlAfterFeeText,
                      ui->labelCoinControlChangeText,
                      ui->labelFeeHeadline,
                      ui->fallbackFeeWarningLabel,
                      ui->assetNameValidation,
                     }, GUIUtil::FontWeight::Bold);

    GUIUtil::setFont({ui->labelBalance,
                      ui->labelBalanceText,
                     }, GUIUtil::FontWeight::Bold, 14);

    GUIUtil::setFont({ui->labelCoinControlFeatures
                     }, GUIUtil::FontWeight::Bold, 16);

    GUIUtil::setFont({ui->assetNameLabel,
                      ui->addressLabel,
                      ui->unitsLabel,
                      ui->typeLabel,
                      ui->quantityLabel,
                      ui->targetLabel,
                      ui->mintLabel,
                      ui->ipfsLabel,
                      ui->uniqueBox,
                      ui->updatableBox,
                      ui->AssetTypeLabel,
                      ui->AssetTypeBox,
                      ui->RootAssetLabel,
                      ui->RootAssetBox,
                      ui->IssueFrequencyLabel}, GUIUtil::FontWeight::Normal, 15);

    GUIUtil::setupAddressWidget(ui->lineEditCoinControlChange, this);
    GUIUtil::setupAddressWidget(ui->owneraddressText, this);
    GUIUtil::setupAddressWidget(ui->targetaddressText, this);

    GUIUtil::updateFonts();

    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));
    connect(ui->RootAssetLabel, SIGNAL(textChanged(QString)), this, SLOT(onAssetNameChanged(QString)));
    connect(ui->assetnameText, SIGNAL(textChanged(QString)), this, SLOT(onAssetNameChanged(QString)));
    connect(ui->uniqueBox, SIGNAL(clicked()), this, SLOT(onUniqueChanged()));
    connect(ui->AssetTypeBox, SIGNAL(currentIndexChanged(QString)), this, SLOT(onAssetTypeSelected(QString)));
    connect(ui->AssetTypeBox, SIGNAL(currentIndexChanged(QString)), this, SLOT(onAssetNameChanged(QString)));
    connect(ui->RootAssetBox, SIGNAL(currentIndexChanged(QString)), this, SLOT(onAssetNameChanged(QString)));
    connect(ui->openIpfsButton, SIGNAL(clicked()), this, SLOT(openFilePicker()));

    // Coin Control
    connect(ui->pushButtonCoinControl, SIGNAL(clicked()), this, SLOT(CoinControlButtonClicked()));
    connect(ui->checkBoxCoinControlChange, SIGNAL(stateChanged(int)), this, SLOT(CoinControlChangeChecked(int)));
    connect(ui->lineEditCoinControlChange, SIGNAL(textEdited(
    const QString &)), this, SLOT(CoinControlChangeEdited(
    const QString &)));

    // Coin Control: clipboard actions
    QAction *clipboardQuantityAction = new QAction(tr("Copy quantity"), this);
    QAction *clipboardAmountAction = new QAction(tr("Copy amount"), this);
    QAction *clipboardFeeAction = new QAction(tr("Copy fee"), this);
    QAction *clipboardAfterFeeAction = new QAction(tr("Copy after fee"), this);
    QAction *clipboardBytesAction = new QAction(tr("Copy bytes"), this);
    QAction *clipboardLowOutputAction = new QAction(tr("Copy dust"), this);
    QAction *clipboardChangeAction = new QAction(tr("Copy change"), this);
    connect(clipboardQuantityAction, SIGNAL(triggered()), this, SLOT(CoinControlClipboardQuantity()));
    connect(clipboardAmountAction, SIGNAL(triggered()), this, SLOT(CoinControlClipboardAmount()));
    connect(clipboardFeeAction, SIGNAL(triggered()), this, SLOT(CoinControlClipboardFee()));
    connect(clipboardAfterFeeAction, SIGNAL(triggered()), this, SLOT(CoinControlClipboardAfterFee()));
    connect(clipboardBytesAction, SIGNAL(triggered()), this, SLOT(CoinControlClipboardBytes()));
    connect(clipboardLowOutputAction, SIGNAL(triggered()), this, SLOT(CoinControlClipboardLowOutput()));
    connect(clipboardChangeAction, SIGNAL(triggered()), this, SLOT(CoinControlClipboardChange()));
    ui->labelCoinControlQuantity->addAction(clipboardQuantityAction);
    ui->labelCoinControlAmount->addAction(clipboardAmountAction);
    ui->labelCoinControlFee->addAction(clipboardFeeAction);
    ui->labelCoinControlAfterFee->addAction(clipboardAfterFeeAction);
    ui->labelCoinControlBytes->addAction(clipboardBytesAction);
    ui->labelCoinControlLowOutput->addAction(clipboardLowOutputAction);
    ui->labelCoinControlChange->addAction(clipboardChangeAction);

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

    ui->createAssetButton->setText(tr("C&reate Asset"));
    ui->createAssetButton->setToolTip(tr("Confirm the create asset action"));

    ui->IssueFrequencyBox->setEnabled(false);
    ui->IssueFrequencyBox->setToolTip(tr("Disabled. Not in use"));
    ui->distributionBox->setEnabled(false);
    ui->distributionBox->setToolTip(tr("Manual only until other types are developed"));
    //ui->openIpfsButton->hide();

    ui->RootAssetLabel->setVisible(false);
    ui->RootAssetBox->setVisible(false);

    m_coin_control->UseCoinJoin(false);

    /** Setup the root asset list combobox */
    stringModel = new QStringListModel;

    proxy = new QSortFilterProxyModel;
    proxy->setSourceModel(stringModel);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    ui->RootAssetBox->setModel(proxy);
    ui->RootAssetBox->setEditable(true);
    ui->RootAssetBox->lineEdit()->setPlaceholderText(tr("Select Root asset"));
    bool isRoot = ui->AssetTypeBox->currentIndex() == 0;
    ui->assetnameText->setToolTip(isRoot ? tr("A-Z 0-9, no spaces") : tr("a-z A-Z 0-9 and space"));
}

void CreateAssetsDialog::setClientModel(ClientModel *_clientModel) {
    this->clientModel = _clientModel;

    if (_clientModel) {
        connect(_clientModel, SIGNAL(numBlocksChanged(int, QDateTime, QString, double, bool)), this,
                SLOT(updateSmartFeeLabel()));
    }
}

void CreateAssetsDialog::setModel(WalletModel *_model) {
    this->model = _model;

    if (_model && _model->getOptionsModel()) {
        interfaces::WalletBalances balances = _model->wallet().getBalances();
        setBalance(balances);
        connect(_model, SIGNAL(balanceChanged(interfaces::WalletBalances)), this,
                SLOT(setBalance(interfaces::WalletBalances)));
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        updateDisplayUnit();

        // Coin Control
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(CoinControlUpdateLabels()));
        connect(_model->getOptionsModel(), SIGNAL(coinControlFeaturesChanged(bool)), this,
                SLOT(CoinControlFeatureChanged(bool)));
        ui->frameCoinControl->setVisible(_model->getOptionsModel()->getCoinControlFeatures());
        CoinControlUpdateLabels();

        // fee section
        for (const int n: confTargets) {
            ui->confTargetSelector->addItem(tr("%1 (%2 blocks)").arg(
                    GUIUtil::formatNiceTimeOffset(n * Params().GetConsensus().nPowTargetSpacing)).arg(n));
        }
        connect(ui->confTargetSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(updateSmartFeeLabel()));
        connect(ui->confTargetSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(CoinControlUpdateLabels()));
        connect(ui->groupFee, SIGNAL(buttonClicked(int)), this, SLOT(updateFeeSectionControls()));
        connect(ui->groupFee, SIGNAL(buttonClicked(int)), this, SLOT(CoinControlUpdateLabels()));
        connect(ui->customFee, SIGNAL(valueChanged()), this, SLOT(CoinControlUpdateLabels()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(setMinimumFee()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(updateFeeSectionControls()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(CoinControlUpdateLabels()));

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

CreateAssetsDialog::~CreateAssetsDialog() {
    QSettings settings;
    settings.setValue("fFeeSectionMinimized", fFeeMinimized);
    settings.setValue("nFeeRadio", ui->groupFee->checkedId());
    settings.setValue("nConfTarget", getConfTargetForIndex(ui->confTargetSelector->currentIndex()));
    settings.setValue("nTransactionFee", (qint64) ui->customFee->value());
    settings.setValue("fPayOnlyMinFee", ui->checkBoxMinimumFee->isChecked());

    delete ui;
}

bool CreateAssetsDialog::filladdress(QString address, CKeyID &field) {
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

void CreateAssetsDialog::keyPressEvent(QKeyEvent* evt)
{
    // Escape hides the dialog, which leaves us with an empty window
    if (evt->key() == Qt::Key_Escape)
        return;
    QDialog::keyPressEvent(evt);
}

void CreateAssetsDialog::on_createAssetButton_clicked() {
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
        createAsset();
        return;
    }
    // already unlocked or not encrypted at all
    createAsset();
}

std::string CreateAssetsDialog::getAssetName(bool isRoot) {
    std::string assetName = ui->assetnameText->text().toStdString();
    if(isRoot) {
        std::transform(assetName.begin(), assetName.end(), assetName.begin(), ::toupper);
    }
    return assetName;
}

void CreateAssetsDialog::createAsset() {
    if (getAssetsFees() == 0) {
        QMessageBox msgBox;
        msgBox.setText(QString::fromStdString("Under maintenance, try again later."));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    CNewAssetTx assetTx;
    bool isRoot = ui->AssetTypeBox->currentIndex() == 0;
    assetTx.name = this->getAssetName(isRoot);

    assetTx.referenceHash = ui->ipfsText->text().toStdString();

    if (!filladdress(ui->owneraddressText->text(), assetTx.ownerAddress) ||
        !filladdress(ui->targetaddressText->text(), assetTx.targetAddress)){
        return;
    }

    if (ui->uniqueBox->isChecked()) {
        assetTx.isUnique = true;
        assetTx.updatable = false;
        assetTx.decimalPoint = 0; //alway 0
        assetTx.type = 0;
    } else {
        assetTx.updatable = ui->updatableBox->isChecked();
        assetTx.isUnique = false;
        assetTx.type = 0; //change this later
        assetTx.decimalPoint = ui->unitBox->value();
    }
    assetTx.amount = ui->quantitySpinBox->value() * COIN;
    assetTx.maxMintCount = ui->maxmintSpinBox->value();
    assetTx.issueFrequency = ui->IssueFrequencyBox->value();

    assetTx.isRoot = ui->AssetTypeBox->currentIndex() == 0;
    if (!assetTx.isRoot) {//sub asset
        if (ui->RootAssetBox->currentIndex() > 0) {
            std::string assetId;
            if (passetsCache->GetAssetId(ui->RootAssetBox->currentText().toStdString(), assetId)) {
                assetTx.rootId = assetId;
            } else {
                //shold never hapen
                return;
            }
        }
    }

    CTransactionRef newTx;
    CAmount nFee;
    int nChangePos = -1;
    std::string strFailReason;
    std::vector <CRecipient> vecSend;
    assetTx.fee = getAssetsFees();
    int Payloadsize;

    if (!model->wallet().createTransaction(vecSend, newTx, *m_coin_control, true, nChangePos, nFee, strFailReason,
                                Payloadsize, &assetTx)) {
        //handle creation error
        QMessageBox msgBox;
        msgBox.setText(QString::fromStdString(strFailReason));
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
        return;
    }

    QString questionString = tr("Asset details:");
    questionString.append("<hr />");
    questionString.append(tr("Name: %1 <br>").arg(QString::fromStdString(assetTx.name)));
    questionString.append(tr("Isunique: %1 <br>").arg(assetTx.isUnique ? "true": "false"));
    questionString.append(tr("Updatable: %1 <br>").arg(assetTx.updatable ? "true": "false"));
    questionString.append(tr("Decimalpoint: %1 <br>").arg(QString::number(assetTx.decimalPoint)));
    questionString.append(tr("ReferenceHash: %1 <br>").arg(QString::fromStdString(assetTx.referenceHash)));
    questionString.append(tr("MaxMintCount: %1 <br>").arg(QString::number(assetTx.maxMintCount)));
    questionString.append(tr("Owner: %1 <hr />").arg(QString::fromStdString(EncodeDestination(assetTx.ownerAddress))));
    questionString.append(tr("Distribution:<hr />"));
    questionString.append(tr("Type: %1 <br>").arg(GetDistributionType(assetTx.type)));
    questionString.append(tr("Target: %1 <br>").arg(QString::fromStdString(EncodeDestination(assetTx.targetAddress))));
    questionString.append(tr("IssueFrequency: %1 <br>").arg(QString::number(assetTx.issueFrequency)));
    questionString.append(tr("Amount: %1").arg(QString::number(assetTx.amount / COIN)));
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
    CreateAssetConfirmationDialog confirmationDialog(tr("Confirm Asset Creation"),
                                                   questionString, SEND_CONFIRM_DELAY, this);
    confirmationDialog.exec();
    QMessageBox::StandardButton retval = static_cast<QMessageBox::StandardButton>(confirmationDialog.result());
    if (retval != QMessageBox::Yes) {
        return;
    }
    model->wallet().commitTransaction(newTx, {}, {});
    clear();
}

void CreateAssetsDialog::onUniqueChanged() {
    if (ui->uniqueBox->isChecked()) {
        ui->updatableBox->setEnabled(false);
        ui->updatableBox->setChecked(false);
        ui->updatableBox->setStyleSheet("");
        ui->unitBox->setValue(0);
        ui->unitBox->setEnabled(false);
        ui->unitBox->setStyleSheet("");
    } else {
        ui->updatableBox->setEnabled(true);
        ui->updatableBox->setStyleSheet("");
        ui->unitBox->setValue(0);
        ui->unitBox->setEnabled(true);
        ui->unitBox->setStyleSheet("");
    }
}

bool CreateAssetsDialog::validateInputs() {
    bool retval{true};

    bool isRoot = ui->AssetTypeBox->currentIndex() == 0;
    std::string assetname  = this->getAssetName(isRoot);
    //check if asset name is valid
    if (!IsAssetNameValid(assetname, isRoot)) {
        retval = false;
        ui->assetnameText->setValid(false);
    }

    if (!isRoot) {//sub asset
        if (ui->RootAssetBox->currentIndex() > 0) {
            assetname = ui->RootAssetBox->currentText().toStdString() + "|" + assetname;
        } else {
            //root asset not selected, set name as invalid
            ui->assetnameText->setValid(false);
            retval = false;
        }
    }

    // check if asset already exist
    std::string assetId;
    if (passetsCache->GetAssetId(assetname, assetId)) {
        CAssetMetaData tmpAsset;
        if (passetsCache->GetAssetMetaData(assetId, tmpAsset)) {
            retval = false;
            ui->assetnameText->setValid(false);
        }
    }

    //check on mempool if asset already exist
    if (mempool.CheckForNewAssetConflict(assetname)) {
        retval = false;
        ui->assetnameText->setValid(false);
    }

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

    //sanity checks, should never fail
    if (ui->uniqueBox->isChecked()) {
        if (ui->updatableBox->isChecked()) {
            ui->updatableBox->setStyleSheet(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_INVALID));
            retval = false;
        }

        if (ui->unitBox->value() > 0){
            ui->unitBox->setStyleSheet(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_INVALID));
            retval = false;
        }
    }

    if (ui->unitBox->value() < 0 || ui->unitBox->value() > 8){
        ui->unitBox->setStyleSheet(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_INVALID));
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

void CreateAssetsDialog::clear() {
    // Clear Coin Control settings
    m_coin_control->UnSelectAll();
    ui->checkBoxCoinControlChange->setChecked(false);
    ui->lineEditCoinControlChange->clear();
    ui->assetnameText->clear();
    ui->owneraddressText->clear();
    ui->ipfsText->clear();
    ui->targetaddressText->clear();
    ui->unitBox->setValue(0);
    ui->quantitySpinBox->setValue(0);
    ui->maxmintSpinBox->setValue(1);
    ui->IssueFrequencyBox->setValue(0);
    ui->uniqueBox->setChecked(false);
    ui->updatableBox->setChecked(false);

    onUniqueChanged();

    CoinControlUpdateLabels();

    updateTabsAndLabels();
}

void CreateAssetsDialog::updateTabsAndLabels() {
    setupTabChain(0);
    CoinControlUpdateLabels();
}

QWidget *CreateAssetsDialog::setupTabChain(QWidget *prev) {
    QWidget::setTabOrder(prev, ui->createAssetButton);
    QWidget::setTabOrder(ui->createAssetButton, ui->clearButton);
    return ui->clearButton;
}

void CreateAssetsDialog::setBalance(const interfaces::WalletBalances &balances) {
    if (model && model->getOptionsModel()) {
        CAmount bal = balances.balance;
        ui->labelBalance->setText(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), bal));
    }
}

void CreateAssetsDialog::updateDisplayUnit() {
    setBalance(model->wallet().getBalances());
    CoinControlUpdateLabels();
    ui->customFee->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    updateMinFeeLabel();
    updateSmartFeeLabel();
}

void CreateAssetsDialog::minimizeFeeSection(bool fMinimize) {
    ui->labelFeeMinimized->setVisible(fMinimize);
    ui->buttonChooseFee->setVisible(fMinimize);
    ui->buttonMinimizeFee->setVisible(!fMinimize);
    ui->frameFeeSelection->setVisible(!fMinimize);
    ui->horizontalLayoutSmartFee->setContentsMargins(0, (fMinimize ? 0 : 6), 0, 0);
    fFeeMinimized = fMinimize;
}

void CreateAssetsDialog::on_buttonChooseFee_clicked() {
    minimizeFeeSection(false);
}

void CreateAssetsDialog::on_buttonMinimizeFee_clicked() {
    updateFeeMinimizedLabel();
    minimizeFeeSection(true);
}

void CreateAssetsDialog::setMinimumFee() {
    ui->customFee->setValue(model->wallet().getRequiredFee(1000));
}

void CreateAssetsDialog::updateFeeSectionControls() {
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

void CreateAssetsDialog::updateFeeMinimizedLabel() {
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

void CreateAssetsDialog::updateMinFeeLabel() {
    if (model && model->getOptionsModel())
        ui->checkBoxMinimumFee->setText(tr("Pay only the required fee of %1").arg(
                BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(),
                                             model->wallet().getRequiredFee(1000)) + "/kB")
        );
}

void CreateAssetsDialog::updateCoinControlState(CCoinControl &ctrl) {
    if (ui->radioCustomFee->isChecked()) {
        ctrl.m_feerate = CFeeRate(ui->customFee->value());
    } else {
        ctrl.m_feerate.reset();
    }
    // Avoid using global defaults when sending money from the GUI
    // Either custom fee will be used or if not selected, the confirmation target from dropdown box
    ctrl.m_confirm_target = getConfTargetForIndex(ui->confTargetSelector->currentIndex());
}

void CreateAssetsDialog::updateSmartFeeLabel() {
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

// Coin Control: copy label "Quantity" to clipboard
void CreateAssetsDialog::CoinControlClipboardQuantity() {
    GUIUtil::setClipboard(ui->labelCoinControlQuantity->text());
}

// Coin Control: copy label "Amount" to clipboard
void CreateAssetsDialog::CoinControlClipboardAmount() {
    GUIUtil::setClipboard(ui->labelCoinControlAmount->text().left(ui->labelCoinControlAmount->text().indexOf(" ")));
}

// Coin Control: copy label "Fee" to clipboard
void CreateAssetsDialog::CoinControlClipboardFee() {
    GUIUtil::setClipboard(
            ui->labelCoinControlFee->text().left(ui->labelCoinControlFee->text().indexOf(" ")).replace(ASYMP_UTF8,
                                                                                                         ""));
}

// Coin Control: copy label "After fee" to clipboard
void CreateAssetsDialog::CoinControlClipboardAfterFee() {
    GUIUtil::setClipboard(
            ui->labelCoinControlAfterFee->text().left(ui->labelCoinControlAfterFee->text().indexOf(" ")).replace(
                    ASYMP_UTF8, ""));
}

// Coin Control: copy label "Bytes" to clipboard
void CreateAssetsDialog::CoinControlClipboardBytes() {
    GUIUtil::setClipboard(ui->labelCoinControlBytes->text().replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "Dust" to clipboard
void CreateAssetsDialog::CoinControlClipboardLowOutput() {
    GUIUtil::setClipboard(ui->labelCoinControlLowOutput->text());
}

// Coin Control: copy label "Change" to clipboard
void CreateAssetsDialog::CoinControlClipboardChange() {
    GUIUtil::setClipboard(
            ui->labelCoinControlChange->text().left(ui->labelCoinControlChange->text().indexOf(" ")).replace(
                    ASYMP_UTF8, ""));
}

// Coin Control: settings menu - Coin Control enabled/disabled by user
void CreateAssetsDialog::CoinControlFeatureChanged(bool checked) {
    ui->frameCoinControl->setVisible(checked);

    if (!checked && model) { // Coin Control features disabled
        m_coin_control->SetNull(false);
    }

    CoinControlUpdateLabels();
}

// Coin Control: button inputs -> show actual Coin Control dialog
void CreateAssetsDialog::CoinControlButtonClicked() {
    CoinControlDialog dlg(*m_coin_control, model, this);
    dlg.exec();
    CoinControlUpdateLabels();
}

// Coin Control: checkbox custom change address
void CreateAssetsDialog::CoinControlChangeChecked(int state) {
    if (state == Qt::Unchecked) {
        m_coin_control->destChange = CNoDestination();
        ui->labelCoinControlChangeLabel->clear();
    } else
        // use this to re-validate an already entered address
        CoinControlChangeEdited(ui->lineEditCoinControlChange->text());

    ui->lineEditCoinControlChange->setEnabled((state == Qt::Checked));
}

// Coin Control: custom change address changed
void CreateAssetsDialog::CoinControlChangeEdited(const QString &text) {
    if (model && model->getAddressTableModel()) {
        // Default to no change address until verified
        m_coin_control->destChange = CNoDestination();
        ui->labelCoinControlChangeLabel->setStyleSheet(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR));

        const CTxDestination dest = DecodeDestination(text.toStdString());

        if (text.isEmpty()) // Nothing entered
        {
            ui->labelCoinControlChangeLabel->setText("");
        } else if (!IsValidDestination(dest)) // Invalid address
        {
            ui->labelCoinControlChangeLabel->setText(tr("Warning: Invalid Raptoreum address"));
        } else // Valid address
        {
            if (!model->wallet().isSpendable(dest)) {
                ui->labelCoinControlChangeLabel->setText(tr("Warning: Unknown change address"));

                // confirmation dialog
                QMessageBox::StandardButton btnRetVal = QMessageBox::question(this, tr("Confirm custom change address"),
                                                                              tr("The address you selected for change is not part of this wallet. Any or all funds in your wallet may be sent to this address. Are you sure?"),
                                                                              QMessageBox::Yes | QMessageBox::Cancel,
                                                                              QMessageBox::Cancel);

                if (btnRetVal == QMessageBox::Yes)
                    m_coin_control->destChange = dest;
                else {
                    ui->lineEditCoinControlChange->setText("");
                    ui->labelCoinControlChangeLabel->setStyleSheet(
                            GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_PRIMARY));
                    ui->labelCoinControlChangeLabel->setText("");
                }
            } else // Known change address
            {
                ui->labelCoinControlChangeLabel->setStyleSheet(
                        GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_PRIMARY));

                // Query label
                QString associatedLabel = model->getAddressTableModel()->labelForAddress(text);
                if (!associatedLabel.isEmpty())
                    ui->labelCoinControlChangeLabel->setText(associatedLabel);
                else
                    ui->labelCoinControlChangeLabel->setText(tr("(no label)"));

                m_coin_control->destChange = dest;
            }
        }
    }
}

// Coin Control: update labels
void CreateAssetsDialog::CoinControlUpdateLabels() {
    if (!model || !model->getOptionsModel())
        return;

    updateCoinControlState(*m_coin_control);

    // set pay amounts
    CoinControlDialog::payAmounts.clear();
    CoinControlDialog::fSubtractFeeFromAmount = false;

    if (m_coin_control->HasSelected()) {
        // actual Coin Control calculation
        CoinControlDialog::updateLabels(*m_coin_control, model, this);

        // show Coin Control stats
        ui->labelCoinControlAutomaticallySelected->hide();
        ui->widgetCoinControl->show();
    } else {
        // hide Coin Control stats
        ui->labelCoinControlAutomaticallySelected->show();
        ui->widgetCoinControl->hide();
        ui->labelCoinControlInsuffFunds->hide();
    }
}

void CreateAssetsDialog::openFilePicker() {
    std::string cid;
    pickAndUploadFileForIpfs(this, cid);
    ui->ipfsText->setText(QString::fromStdString(cid));
}

void CreateAssetsDialog::onAssetNameChanged(QString name)
{
    bool isRoot = ui->AssetTypeBox->currentIndex() == 0;
    std::string assetname = this->getAssetName(isRoot);

    // Check asset name (root or sub)
    if (!IsAssetNameValid(assetname, isRoot)) {
        ui->assetnameText->setValid(false);
        ui->assetNameValidation->setText(tr("Asset name invalid"));
        return;
    }

    if (!isRoot) {
        if (ui->RootAssetBox->currentIndex() > 0) {
            assetname = ui->RootAssetBox->currentText().toStdString() + "|" + assetname;
        } else {
            // Root asset not selected, set name as invalid
            ui->assetnameText->setValid(false);
            ui->assetNameValidation->setText(tr("Root asset not selected"));
            return;
        }
    }

    // Check if asset already exists
    std::string assetId;
    if (passetsCache->GetAssetId(assetname, assetId)) {
        CAssetMetaData tmpAsset;
        if (passetsCache->GetAssetMetaData(assetId, tmpAsset)) {
            ui->assetnameText->setValid(false);
            ui->assetNameValidation->setText(tr("Asset already exists"));
            return;
        }
    }

    // Check if asset already exists on mempool
    if (mempool.CheckForNewAssetConflict(assetname)) {
        ui->assetnameText->setValid(false);
        ui->assetNameValidation->setText(tr("Asset already exists in mempool"));
        return;
    }
    ui->assetNameValidation->setText(tr("Asset name is valid and available"));
}

void CreateAssetsDialog::onAssetTypeSelected(QString name) {
    if (name == "Root") {
        ui->RootAssetLabel->setVisible(false);
        ui->RootAssetBox->setVisible(false);
        ui->assetnameText->setToolTip(tr("A-Z 0-9, no spaces"));
    } else if (name == "Sub") {
        ui->RootAssetLabel->setVisible(true);
        ui->RootAssetBox->setVisible(true);
        ui->assetnameText->setToolTip(tr("a-z A-Z 0-9 and space"));

        // Get available assets list
        std::map <uint256, std::pair<std::string, CKeyID>> assets = model->wallet().getMyAssets();

        QStringList list;
        list << "Select an asset";
        for (auto asset: assets) {
            CAssetMetaData assetData;
            std::string assetId = asset.first.ToString();
            if (passetsCache->GetAssetMetaData(assetId, assetData) && assetData.isRoot) {
                if (model->wallet().isSpendable(assetData.ownerAddress)){
                    list << QString::fromStdString(assetData.name);
                }
            }
        }
        //update the list
        stringModel->setStringList(list);
    }
}

CreateAssetConfirmationDialog::CreateAssetConfirmationDialog(const QString &title, const QString &text, int _secDelay,
                                                         QWidget *parent) :
        QMessageBox(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::Cancel, parent),
        secDelay(_secDelay) {
    GUIUtil::updateFonts();
    setDefaultButton(QMessageBox::Cancel);
    yesButton = button(QMessageBox::Yes);
    updateYesButton();
    connect(&countDownTimer, SIGNAL(timeout()), this, SLOT(countDown()));
}

int CreateAssetConfirmationDialog::exec() {
    updateYesButton();
    countDownTimer.start(1000);
    return QMessageBox::exec();
}

void CreateAssetConfirmationDialog::countDown() {
    secDelay--;
    updateYesButton();

    if (secDelay <= 0) {
        countDownTimer.stop();
    }
}

void CreateAssetConfirmationDialog::updateYesButton() {
    if (secDelay > 0) {
        yesButton->setEnabled(false);
        yesButton->setText(tr("Yes") + " (" + QString::number(secDelay) + ")");
    } else {
        yesButton->setEnabled(true);
        yesButton->setText(tr("Yes"));
    }
}