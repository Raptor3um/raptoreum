// Copyright (c) 2011-2015 The BitAsset Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/sendassetsdialog.h>
#include <qt/forms/ui_sendassetsdialog.h>

#include <qt/addresstablemodel.h>
#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/assetcontroldialog.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/sendassetsentry.h>

#include <interfaces/node.h>
#include <key_io.h>
#include <wallet/coincontrol.h>
#include <ui_interface.h>
#include <txmempool.h>
#include <policy/fees.h>
#include <wallet/fees.h>
#include <wallet/wallet.h>
#include <future/fee.h>
#include <validation.h>
#include <assets/assets.h>
#include <assets/assetstype.h>

#include <QFontMetrics>
#include <QScrollBar>
#include <QSettings>
#include <QTextDocument>

#define SEND_CONFIRM_DELAY   3

SendAssetsDialog::SendAssetsDialog(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::SendAssetsDialog),
    clientModel(nullptr),
    model(nullptr),
    m_coin_control(new CCoinControl),
    fNewRecipientAllowed(true),
    fFeeMinimized(true)
{
    ui->setupUi(this);

    GUIUtil::setFont({ui->labelAssetControlFeatures,
                      ui->labelAssetControlInsuffFunds,
                      ui->labelAssetControlQuantityText,
                      ui->labelAssetControlBytesText,
                      ui->labelAssetControlAmountText,
                      ui->labelAssetControlLowOutputText,
                      ui->labelAssetControlFeeText,
                      ui->labelAssetControlAfterFeeText,
                      ui->labelAssetControlChangeText,
                      ui->labelFeeHeadline,
                      ui->fallbackFeeWarningLabel
                     }, GUIUtil::FontWeight::Bold);

    GUIUtil::setFont({ui->labelBalance,
                      ui->labelBalanceText
                     }, GUIUtil::FontWeight::Bold, 14);

    GUIUtil::setFont({ui->labelAssetControlFeatures
                     }, GUIUtil::FontWeight::Bold, 16);

    GUIUtil::setupAddressWidget(ui->lineEditAssetControlChange, this);

    addEntry();

    GUIUtil::updateFonts();

    connect(ui->addButton, SIGNAL(clicked()), this, SLOT(addEntry()));
    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));

    // Asset Control
    connect(ui->pushButtonAssetControl, SIGNAL(clicked()), this, SLOT(AssetControlButtonClicked()));
    connect(ui->checkBoxAssetControlChange, SIGNAL(stateChanged(int)), this, SLOT(AssetControlChangeChecked(int)));
    connect(ui->lineEditAssetControlChange, SIGNAL(textEdited(const QString &)), this, SLOT(AssetControlChangeEdited(const QString &)));

    // Asset Control: clipboard actions
    QAction *clipboardQuantityAction = new QAction(tr("Copy quantity"), this);
    QAction *clipboardAmountAction = new QAction(tr("Copy amount"), this);
    QAction *clipboardFeeAction = new QAction(tr("Copy fee"), this);
    QAction *clipboardAfterFeeAction = new QAction(tr("Copy after fee"), this);
    QAction *clipboardBytesAction = new QAction(tr("Copy bytes"), this);
    QAction *clipboardLowOutputAction = new QAction(tr("Copy dust"), this);
    QAction *clipboardChangeAction = new QAction(tr("Copy change"), this);
    connect(clipboardQuantityAction, SIGNAL(triggered()), this, SLOT(AssetControlClipboardQuantity()));
    connect(clipboardAmountAction, SIGNAL(triggered()), this, SLOT(AssetControlClipboardAmount()));
    connect(clipboardFeeAction, SIGNAL(triggered()), this, SLOT(AssetControlClipboardFee()));
    connect(clipboardAfterFeeAction, SIGNAL(triggered()), this, SLOT(AssetControlClipboardAfterFee()));
    connect(clipboardBytesAction, SIGNAL(triggered()), this, SLOT(AssetControlClipboardBytes()));
    connect(clipboardLowOutputAction, SIGNAL(triggered()), this, SLOT(AssetControlClipboardLowOutput()));
    connect(clipboardChangeAction, SIGNAL(triggered()), this, SLOT(AssetControlClipboardChange()));
    ui->labelAssetControlQuantity->addAction(clipboardQuantityAction);
    ui->labelAssetControlAmount->addAction(clipboardAmountAction);
    ui->labelAssetControlFee->addAction(clipboardFeeAction);
    ui->labelAssetControlAfterFee->addAction(clipboardAfterFeeAction);
    ui->labelAssetControlBytes->addAction(clipboardBytesAction);
    ui->labelAssetControlLowOutput->addAction(clipboardLowOutputAction);
    ui->labelAssetControlChange->addAction(clipboardChangeAction);

    // init transaction fee section
    QSettings settings;
    if (!settings.contains("fFeeSectionMinimized"))
        settings.setValue("fFeeSectionMinimized", true);
    if (!settings.contains("nFeeRadio") && settings.contains("nTransactionFee") && settings.value("nTransactionFee").toLongLong() > 0) // compatibility
        settings.setValue("nFeeRadio", 1); // custom
    if (!settings.contains("nFeeRadio"))
        settings.setValue("nFeeRadio", 0); // recommended
    if (!settings.contains("nSmartFeeSliderPosition"))
        settings.setValue("nSmartFeeSliderPosition", 0);
    if (!settings.contains("nTransactionFee"))
        settings.setValue("nTransactionFee", (qint64)DEFAULT_TRANSACTION_FEE);
    if (!settings.contains("fPayOnlyMinFee"))
        settings.setValue("fPayOnlyMinFee", false);

    ui->groupFee->setId(ui->radioSmartFee, 0);
    ui->groupFee->setId(ui->radioCustomFee, 1);
    ui->groupFee->button((int)std::max(0, std::min(1, settings.value("nFeeRadio").toInt())))->setChecked(true);
    ui->customFee->setValue(settings.value("nTransactionFee").toLongLong());
    ui->checkBoxMinimumFee->setChecked(settings.value("fPayOnlyMinFee").toBool());
    minimizeFeeSection(settings.value("fFeeSectionMinimized").toBool());
    
    ui->sendButton->setText(tr("S&end"));
    ui->sendButton->setToolTip(tr("Confirm the send action"));

    m_coin_control->UseCoinJoin(false);       
}

void SendAssetsDialog::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;

    if (_clientModel) {
        connect(_clientModel, SIGNAL(numBlocksChanged(int,QDateTime,QString,double,bool)), this, SLOT(updateSmartFeeLabel()));
    }
}

void SendAssetsDialog::setModel(WalletModel *_model)
{
    this->model = _model;

    if(_model && _model->getOptionsModel())
    {
        for(int i = 0; i < ui->entries->count(); ++i)
        {
            SendAssetsEntry *entry = qobject_cast<SendAssetsEntry*>(ui->entries->itemAt(i)->widget());
            if(entry)
            {
                entry->setModel(_model);
                entry->setCoinControl(&*m_coin_control);
                entry->updateAssetList();
            }
        }

        interfaces::WalletBalances balances = _model->wallet().getBalances();
        setBalance(balances);
        connect(_model, SIGNAL(balanceChanged(interfaces::WalletBalances)), this, SLOT(setBalance(interfaces::WalletBalances)));
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        updateDisplayUnit();

        // Asset Control
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(AssetControlUpdateLabels()));
        connect(_model->getOptionsModel(), SIGNAL(coinControlFeaturesChanged(bool)), this, SLOT(AssetControlFeatureChanged(bool)));
        connect(_model, SIGNAL(assetListChanged()), this, SLOT(updateAssetList())); 
        ui->frameAssetControl->setVisible(_model->getOptionsModel()->getCoinControlFeatures());
        AssetControlUpdateLabels();

        // fee section
        for (const int n : confTargets) {
            ui->confTargetSelector->addItem(tr("%1 (%2 blocks)").arg(GUIUtil::formatNiceTimeOffset(n*Params().GetConsensus().nPowTargetSpacing)).arg(n));
        }
        connect(ui->confTargetSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(updateSmartFeeLabel()));
        connect(ui->confTargetSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(AssetControlUpdateLabels()));
        connect(ui->groupFee, SIGNAL(buttonClicked(int)), this, SLOT(updateFeeSectionControls()));
        connect(ui->groupFee, SIGNAL(buttonClicked(int)), this, SLOT(AssetControlUpdateLabels()));
        connect(ui->customFee, SIGNAL(valueChanged()), this, SLOT(AssetControlUpdateLabels()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(setMinimumFee()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(updateFeeSectionControls()));
        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(AssetControlUpdateLabels()));

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
            ui->confTargetSelector->setCurrentIndex(getIndexForConfTarget(model->node().getTxConfirmTarget()));
        else
            ui->confTargetSelector->setCurrentIndex(getIndexForConfTarget(settings.value("nConfTarget").toInt()));
    }
}

SendAssetsDialog::~SendAssetsDialog()
{
    QSettings settings;
    settings.setValue("fFeeSectionMinimized", fFeeMinimized);
    settings.setValue("nFeeRadio", ui->groupFee->checkedId());
    settings.setValue("nConfTarget", getConfTargetForIndex(ui->confTargetSelector->currentIndex()));
    settings.setValue("nTransactionFee", (qint64)ui->customFee->value());
    settings.setValue("fPayOnlyMinFee", ui->checkBoxMinimumFee->isChecked());

    delete ui;
}

void SendAssetsDialog::OnDisplay() {
    for (int i = 0; i < ui->entries->count(); ++i) {
        SendAssetsEntry *entry = qobject_cast<SendAssetsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry) {
            entry->SetFutureVisible(sporkManager.IsSporkActive(SPORK_22_SPECIAL_TX_FEE) && Params().IsFutureActive(chainActive.Tip()) && i == 0);
        }
    }
}

void SendAssetsDialog::on_sendButton_clicked()
{
    if(!model || !model->getOptionsModel())
        return;

    QList<SendCoinsRecipient> recipients;
    bool valid = true;

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendAssetsEntry *entry = qobject_cast<SendAssetsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            if(entry->validate(model->node()))
            {
                recipients.append(entry->getValue());
            }
            else
            {
                valid = false;
            }
        }
    }

    if(!valid || recipients.isEmpty())
    {
        return;
    }

    fNewRecipientAllowed = false;
    // request unlock only if was locked or unlocked for mixing:
    // this way we let users unlock by walletpassphrase or by menu
    // and make many transactions while unlocking through this dialog
    // will call relock
    WalletModel::EncryptionStatus encStatus = model->getEncryptionStatus();
    if(encStatus == model->Locked || encStatus == model->UnlockedForMixingOnly)
    {
        WalletModel::UnlockContext ctx(model->requestUnlock());
        if(!ctx.isValid())
        {
            // Unlock wallet was cancelled
            fNewRecipientAllowed = true;
            return;
        }
        send(recipients);
        return;
    }
    // already unlocked or not encrypted at all
    send(recipients);
}

void SendAssetsDialog::send(QList<SendCoinsRecipient> recipients)
{
    // prepare transaction for getting txFee earlier
    WalletModelTransaction currentTransaction(recipients);
    WalletModel::SendAssetsReturn prepareStatus;

    updateAssetControlState(*m_coin_control);

    prepareStatus = model->prepareAssetTransaction(currentTransaction, *m_coin_control);

    // process prepareStatus and on error generate message shown to user
    processSendAssetsReturn(prepareStatus,
        BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), currentTransaction.getTransactionFee()));

    if(prepareStatus.status != WalletModel::OK) {
        fNewRecipientAllowed = true;
        return;
    }

    // Format confirmation message
    QStringList formatted;
    bool hasFuture = false;
    std::map<std::string, std::pair<CAmount, uint16_t>> mapAmounts;
    for (const SendCoinsRecipient &rcp : currentTransaction.getRecipients())
    {
        // generate bold amount string with wallet name in case of multiwallet
        CAssetMetaData assetdata;
        int decimalPoint = 0;
        std::string assetId;
        passetsCache->GetAssetId(rcp.assetId.toStdString(), assetId);
        if (passetsCache->GetAssetMetaData(assetId , assetdata))
            decimalPoint = assetdata.Decimalpoint;
        if (!mapAmounts.count(assetId))
            mapAmounts[rcp.assetId.toStdString()] = std::make_pair(rcp.assetAmount, decimalPoint);
        else
        mapAmounts[rcp.assetId.toStdString()].first += rcp.assetAmount;

        std::string uniqueId = "";
        if (rcp.uniqueId < MAX_UNIQUE_ID)
            uniqueId += " ["+to_string(rcp.uniqueId)+"]";
        QString amount = "<b>" + BitcoinUnits::formatHtmlWithCustomName(QString::fromStdString(assetdata.Name), QString::fromStdString(uniqueId), decimalPoint, rcp.assetAmount);
        
        if (model->isMultiwallet()) {
            amount.append(" <u>"+tr("from wallet %1").arg(GUIUtil::HtmlEscape(model->getWalletName()))+"</u> ");
        }
        amount.append("</b> ");
        // generate monospace address string
        QString address = "<span style='font-family: monospace;'>" + rcp.address;
        address.append("</span>");

        QString recipientElement;

        if (!rcp.paymentRequest.IsInitialized()) // normal payment
        {
            if(rcp.label.length() > 0) // label with address
            {
                recipientElement = tr("%1 to %2").arg(amount, GUIUtil::HtmlEscape(rcp.label));
                recipientElement.append(QString(" (%1)").arg(address));
            }
            else // just address
            {
                recipientElement = tr("%1 to %2").arg(amount, address);
            }
        }
        else if(!rcp.authenticatedMerchant.isEmpty()) // authenticated payment request
        {
            recipientElement = tr("%1 to %2").arg(amount, GUIUtil::HtmlEscape(rcp.authenticatedMerchant));
        }
        else // unauthenticated payment request
        {
            recipientElement = tr("%1 to %2").arg(amount, address);
        }
        //std::cout << rcp.amount << " is future output " << rcp.isFutureOutput << "\n";
        if(rcp.isFutureOutput) {
            hasFuture = true;
            if(recipients[0].maturity >= 0) {
                recipientElement.append(tr("<br>Confirmations in: <b>%1 blocks</b><br />").arg(recipients[0].maturity));
            }
            if(recipients[0].locktime >= 0) {
                recipientElement.append(tr("Time in: <b>%1 seconds from first confirmed</b><br />")
                                                .arg(recipients[0].locktime));
            }
            if(recipients[0].maturity >= 0 && recipients[0].locktime >= 0) {
                int calcBlock = (recipients[0].maturity * 2 * 60);
                if(calcBlock < recipients[0].locktime) {
                    recipientElement.append("This transaction will likely mature based on confirmations.");
                } else {
                    recipientElement.append("This transaction will likely mature based on time.");
                }
            } else if(recipients[0].maturity < 0 && recipients[0].locktime < 0){
                recipientElement.append("<span style='" + GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR) + "'><b>No maturity is set. Transaction will mature as normal.</b></span>");
            }
        }

        formatted.append(recipientElement);
    }

    // Limit number of displayed entries
    int messageEntries = formatted.size();
    int displayedEntries = 0;
    for(int i = 0; i < formatted.size(); i++){
        if(i >= MAX_SEND_ASSET_POPUP_ENTRIES){
            formatted.removeLast();
            i--;
        }
        else{
            displayedEntries = i+1;
        }
    }

    QString questionString = tr("Are you sure you want to send?");
    questionString.append("<br /><br />");
    questionString.append(formatted.join("<br />"));
    questionString.append("<br />");

    questionString.append(tr("using") + " <b>" + tr("any available funds") + "</b>");

    if (displayedEntries < messageEntries) {
        questionString.append("<br />");
        questionString.append("<span style='" + GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR) + "'>");
        questionString.append(tr("<b>(%1 of %2 entries displayed)</b>").arg(displayedEntries).arg(messageEntries));
        questionString.append("</span>");
    }

    CAmount txFee = currentTransaction.getTransactionFee();
    txFee += hasFuture ? getFutureFeesCoin() : 0;
    if(txFee > 0)
    {
        // append fee string if a fee is required
        questionString.append("<hr /><span style='" + GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR) + "'>");
        questionString.append(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), txFee));
        questionString.append("</span> ");
        questionString.append(tr("are added as transaction fee"));
    }

    // Show some additioinal information
    questionString.append("<hr />");
    // append transaction size
    questionString.append(tr("Transaction size: %1").arg(QString::number((double)currentTransaction.getTransactionSize() / 1000)) + " kB");
    questionString.append("<br />");
    CFeeRate feeRate(txFee, currentTransaction.getTransactionSize());
    questionString.append(tr("Fee rate: %1").arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), feeRate.GetFeePerK())) + "/kB");

    // add total amount in all subdivision units
    questionString.append("<hr />");
    CAmount totalAmount = currentTransaction.getTotalTransactionAmount() + txFee;

    // Show total amount
    questionString.append(tr("Total Amount:<br>"));
    for (auto entry : mapAmounts){
        questionString.append(tr("<b>%1</b><br>")
        .arg(BitcoinUnits::formatHtmlWithCustomName(QString::fromStdString(entry.first), "", entry.second.second, entry.second.first)));
    }
    questionString.append(tr("<b>%1</b>")
        .arg(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), totalAmount)));

    // Display message box
    SendAssetConfirmationDialog confirmationDialog(tr("Confirm send Assets"),
        questionString, SEND_CONFIRM_DELAY, this);
    confirmationDialog.exec();
    QMessageBox::StandardButton retval = static_cast<QMessageBox::StandardButton>(confirmationDialog.result());

    if(retval != QMessageBox::Yes)
    {
        fNewRecipientAllowed = true;
        return;
    }

    // now send the prepared transaction
    WalletModel::SendAssetsReturn sendStatus = model->sendAssets(currentTransaction, m_coin_control->IsUsingCoinJoin());
    // process sendStatus and on error generate message shown to user
    processSendAssetsReturn(sendStatus);

    if (sendStatus.status == WalletModel::OK)
    {
        accept();
        m_coin_control->UnSelectAll();
        AssetControlUpdateLabels();
        Q_EMIT coinsSent(currentTransaction.getWtx()->get().GetHash());
    }
    fNewRecipientAllowed = true;
}

void SendAssetsDialog::clear()
{
    // Clear Asset control settings
    m_coin_control->UnSelectAll();
    ui->checkBoxAssetControlChange->setChecked(false);
    ui->lineEditAssetControlChange->clear();
    AssetControlUpdateLabels();

    // Remove entries until only one left
    while(ui->entries->count())
    {
        ui->entries->takeAt(0)->widget()->deleteLater();
    }
    addEntry();

    updateTabsAndLabels();
}

void SendAssetsDialog::reject()
{
    clear();
}

void SendAssetsDialog::accept()
{
    clear();
}

SendAssetsEntry *SendAssetsDialog::addEntry()
{

    SendAssetsEntry* entry = new SendAssetsEntry(this, sporkManager.IsSporkActive(SPORK_22_SPECIAL_TX_FEE)
                                                            && Params().IsFutureActive(chainActive.Tip())
                                                            && ui->entries->count() == 0);
    entry->setModel(model);
    entry->setCoinControl(&*m_coin_control);
    ui->entries->addWidget(entry);
    connect(entry, SIGNAL(removeEntry(SendAssetsEntry*)), this, SLOT(removeEntry(SendAssetsEntry*)));
    connect(entry, SIGNAL(useAvailableAssetsBalance(SendAssetsEntry*)), this, SLOT(useAvailableAssetsBalance(SendAssetsEntry*)));
    connect(entry, SIGNAL(payAmountChanged()), this, SLOT(AssetControlUpdateLabels()));
    
    // Focus the field, so that entry can start immediately
    entry->clear();
    entry->setFocus();
    ui->scrollAreaWidgetContents->resize(ui->scrollAreaWidgetContents->sizeHint());
    qApp->processEvents();
    QScrollBar* bar = ui->scrollArea->verticalScrollBar();
    if(bar)
        bar->setSliderPosition(bar->maximum());

    updateTabsAndLabels();
    if(model)
    entry->updateAssetList();
    return entry;
}

void SendAssetsDialog::updateTabsAndLabels()
{
    setupTabChain(0);
    AssetControlUpdateLabels();
}

void SendAssetsDialog::removeEntry(SendAssetsEntry* entry)
{
    entry->hide();

    // If the last entry is about to be removed add an empty one
    if (ui->entries->count() == 1)
        addEntry();

    entry->deleteLater();

    updateTabsAndLabels();
}

QWidget *SendAssetsDialog::setupTabChain(QWidget *prev)
{
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendAssetsEntry *entry = qobject_cast<SendAssetsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            prev = entry->setupTabChain(prev);
        }
    }
    QWidget::setTabOrder(prev, ui->sendButton);
    QWidget::setTabOrder(ui->sendButton, ui->clearButton);
    QWidget::setTabOrder(ui->clearButton, ui->addButton);
    return ui->addButton;
}

void SendAssetsDialog::setAddress(const QString &address)
{
    SendAssetsEntry *entry = 0;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendAssetsEntry *first = qobject_cast<SendAssetsEntry*>(ui->entries->itemAt(0)->widget());
        if(first->isClear())
        {
            entry = first;
        }
    }
    if(!entry)
    {
        entry = addEntry();
    }

    entry->setAddress(address);
}

void SendAssetsDialog::pasteEntry(const SendCoinsRecipient &rv)
{
    if(!fNewRecipientAllowed)
        return;

    SendAssetsEntry *entry = 0;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendAssetsEntry *first = qobject_cast<SendAssetsEntry*>(ui->entries->itemAt(0)->widget());
        if(first->isClear())
        {
            entry = first;
        }
    }
    if(!entry)
    {
        entry = addEntry();
    }

    entry->setValue(rv);
    updateTabsAndLabels();
}

bool SendAssetsDialog::handlePaymentRequest(const SendCoinsRecipient &rv)
{
    // Just paste the entry, all pre-checks
    // are done in paymentserver.cpp.
    pasteEntry(rv);
    return true;
}

void SendAssetsDialog::setBalance(const interfaces::WalletBalances& balances)
{
    if(model && model->getOptionsModel())
    {
        CAmount bal = balances.balance;
        ui->labelBalance->setText(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), bal));
    }
}

void SendAssetsDialog::updateDisplayUnit()
{
    setBalance(model->wallet().getBalances());
    AssetControlUpdateLabels();
    ui->customFee->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    updateMinFeeLabel();
    updateSmartFeeLabel();
}

void SendAssetsDialog::processSendAssetsReturn(const WalletModel::SendAssetsReturn &sendAssetsReturn, const QString &msgArg)
{
    QPair<QString, CClientUIInterface::MessageBoxFlags> msgParams;
    // Default to a warning message, override if error message is needed
    msgParams.second = CClientUIInterface::MSG_WARNING;

    // This comment is specific to SendAssetsDialog usage of WalletModel::SendAssetsReturn.
    // WalletModel::TransactionCommitFailed is used only in WalletModel::sendAssets()
    // all others are used only in WalletModel::prepareTransaction()
    switch(sendAssetsReturn.status)
    {
    case WalletModel::InvalidAddress:
        msgParams.first = tr("The recipient address is not valid. Please recheck.");
        break;
    case WalletModel::InvalidAmount:
        msgParams.first = tr("The amount to pay must be larger than 0.");
        break;
    case WalletModel::AmountExceedsBalance:
        msgParams.first = tr("The amount exceeds your balance.");
        break;
    case WalletModel::AmountWithFeeExceedsBalance:
        msgParams.first = tr("The total exceeds your balance when the %1 transaction fee is included.").arg(msgArg);
        break;
    case WalletModel::DuplicateAddress:
        msgParams.first = tr("Duplicate address found: addresses should only be used once each.");
        break;
    case WalletModel::TransactionCreationFailed:
        msgParams.first = tr("Transaction creation failed!");
        msgParams.second = CClientUIInterface::MSG_ERROR;
        break;
    case WalletModel::TransactionCommitFailed:
        msgParams.first = tr("The transaction was rejected with the following reason: %1").arg(sendAssetsReturn.reasonCommitFailed);
        msgParams.second = CClientUIInterface::MSG_ERROR;
        break;
    case WalletModel::AbsurdFee:
        msgParams.first = tr("A fee higher than %1 is considered an absurdly high fee.").arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), model->node().getMaxTxFee()));
        break;
    case WalletModel::PaymentRequestExpired:
        msgParams.first = tr("Payment request expired.");
        msgParams.second = CClientUIInterface::MSG_ERROR;
        break;
    case WalletModel::AmountExceedsmaxmoney:
        msgParams.first = tr("The amount to pay exceeds the limit of 21 million per transaction.");
        break;    
    // included to prevent a compiler warning.
    case WalletModel::OK:
    default:
        return;
    }

    Q_EMIT message(tr("Send Assets"), msgParams.first, msgParams.second);
}

void SendAssetsDialog::minimizeFeeSection(bool fMinimize)
{
    ui->labelFeeMinimized->setVisible(fMinimize);
    ui->buttonChooseFee  ->setVisible(fMinimize);
    ui->buttonMinimizeFee->setVisible(!fMinimize);
    ui->frameFeeSelection->setVisible(!fMinimize);
    ui->horizontalLayoutSmartFee->setContentsMargins(0, (fMinimize ? 0 : 6), 0, 0);
    fFeeMinimized = fMinimize;
}

void SendAssetsDialog::on_buttonChooseFee_clicked()
{
    minimizeFeeSection(false);
}

void SendAssetsDialog::on_buttonMinimizeFee_clicked()
{
    updateFeeMinimizedLabel();
    minimizeFeeSection(true);
}

void SendAssetsDialog::useAvailableAssetsBalance(SendAssetsEntry* entry)
{
    // Calculate available amount to send.
    CAmount amount = 0;//model->wallet().getAvailableBalance(*m_coin_control);
    std::map<std::string, CAmount> assetsbalance = model->wallet().getAssetsBalance( &*m_coin_control, true);
    QString assetName = entry->getValue().assetId;
    std::string assetId;
    if (passetsCache->GetAssetId(assetName.toStdString(), assetId)){
    if (assetsbalance.count(assetId))
        amount = assetsbalance[assetId];
    }
    for (int i = 0; i < ui->entries->count(); ++i) {
        SendAssetsEntry* e = qobject_cast<SendAssetsEntry*>(ui->entries->itemAt(i)->widget());
        if (e && !e->isHidden() && e != entry) {
            if (e->getValue().assetId == assetName)
            amount -= e->getValue().amount;
        }
    }

    if (amount > 0) {
      entry->setAmount(amount);
    } else {
      entry->setAmount(0);
    }
}


void SendAssetsDialog::setMinimumFee()
{
    ui->customFee->setValue(model->node().getRequiredFee(1000));
}

void SendAssetsDialog::updateFeeSectionControls()
{
    ui->confTargetSelector      ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee           ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee2          ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelSmartFee3          ->setEnabled(ui->radioSmartFee->isChecked());
    ui->labelFeeEstimation      ->setEnabled(ui->radioSmartFee->isChecked());
    ui->checkBoxMinimumFee      ->setEnabled(ui->radioCustomFee->isChecked());
    ui->labelMinFeeWarning      ->setEnabled(ui->radioCustomFee->isChecked());
    ui->labelCustomPerKilobyte  ->setEnabled(ui->radioCustomFee->isChecked() && !ui->checkBoxMinimumFee->isChecked());
    ui->customFee               ->setEnabled(ui->radioCustomFee->isChecked() && !ui->checkBoxMinimumFee->isChecked());
}

void SendAssetsDialog::updateFeeMinimizedLabel()
{
    if(!model || !model->getOptionsModel())
        return;

    if (ui->radioSmartFee->isChecked())
        ui->labelFeeMinimized->setText(ui->labelSmartFee->text());
    else {
        ui->labelFeeMinimized->setText(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), ui->customFee->value()) + "/kB");
    }
}

void SendAssetsDialog::updateMinFeeLabel()
{
    if (model && model->getOptionsModel())
        ui->checkBoxMinimumFee->setText(tr("Pay only the required fee of %1").arg(
            BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), model->node().getRequiredFee(1000)) + "/kB")
        );
}

void SendAssetsDialog::updateAssetControlState(CCoinControl& ctrl)
{
    if (ui->radioCustomFee->isChecked()) {
        ctrl.m_feerate = CFeeRate(ui->customFee->value());
    } else {
        ctrl.m_feerate.reset();
    }
    // Avoid using global defaults when sending money from the GUI
    // Either custom fee will be used or if not selected, the confirmation target from dropdown box
    ctrl.m_confirm_target = getConfTargetForIndex(ui->confTargetSelector->currentIndex());
}

void SendAssetsDialog::updateSmartFeeLabel()
{
    if(!model || !model->getOptionsModel())
        return;
    updateAssetControlState(*m_coin_control);
    m_coin_control->m_feerate.reset(); // Explicitly use only fee estimation rate for smart fee labels
    int returned_target;
    FeeReason reason;
    CFeeRate feeRate = CFeeRate(model->node().getMinimumFee(1000, *m_coin_control, &returned_target, &reason));

    ui->labelSmartFee->setText(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), feeRate.GetFeePerK()) + "/kB");

    if (reason == FeeReason::FALLBACK) {
        ui->labelSmartFee2->show(); // (Smart fee not initialized yet. This usually takes a few blocks...)
        ui->labelFeeEstimation->setText("");
        ui->fallbackFeeWarningLabel->setVisible(true);
    }
    else
    {
        ui->labelSmartFee2->hide();
        ui->labelFeeEstimation->setText(tr("Estimated to begin confirmation within %n block(s).", "", returned_target));
        ui->fallbackFeeWarningLabel->setVisible(false);
    }

    updateFeeMinimizedLabel();
}

// Asset Control: copy label "Quantity" to clipboard
void SendAssetsDialog::AssetControlClipboardQuantity()
{
    GUIUtil::setClipboard(ui->labelAssetControlQuantity->text());
}

// Asset Control: copy label "Amount" to clipboard
void SendAssetsDialog::AssetControlClipboardAmount()
{
    GUIUtil::setClipboard(ui->labelAssetControlAmount->text().left(ui->labelAssetControlAmount->text().indexOf(" ")));
}

// Asset Control: copy label "Fee" to clipboard
void SendAssetsDialog::AssetControlClipboardFee()
{
    GUIUtil::setClipboard(ui->labelAssetControlFee->text().left(ui->labelAssetControlFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Asset Control: copy label "After fee" to clipboard
void SendAssetsDialog::AssetControlClipboardAfterFee()
{
    GUIUtil::setClipboard(ui->labelAssetControlAfterFee->text().left(ui->labelAssetControlAfterFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Asset Control: copy label "Bytes" to clipboard
void SendAssetsDialog::AssetControlClipboardBytes()
{
    GUIUtil::setClipboard(ui->labelAssetControlBytes->text().replace(ASYMP_UTF8, ""));
}

// Asset Control: copy label "Dust" to clipboard
void SendAssetsDialog::AssetControlClipboardLowOutput()
{
    GUIUtil::setClipboard(ui->labelAssetControlLowOutput->text());
}

// Asset Control: copy label "Change" to clipboard
void SendAssetsDialog::AssetControlClipboardChange()
{
    GUIUtil::setClipboard(ui->labelAssetControlChange->text().left(ui->labelAssetControlChange->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Asset Control: settings menu - Asset control enabled/disabled by user
void SendAssetsDialog::AssetControlFeatureChanged(bool checked)
{
    ui->frameAssetControl->setVisible(checked);

    if (!checked && model) { // Asset control features disabled
        m_coin_control->SetNull(false);
    }

    AssetControlUpdateLabels();
}

// Asset Control: button inputs -> show actual Asset control dialog
void SendAssetsDialog::AssetControlButtonClicked()
{
    AssetControlDialog dlg(*m_coin_control, model, this);
    dlg.exec();
    AssetControlUpdateLabels();
}

// Asset Control: checkbox custom change address
void SendAssetsDialog::AssetControlChangeChecked(int state)
{
    if (state == Qt::Unchecked)
    {
        m_coin_control->destChange = CNoDestination();
        ui->labelAssetControlChangeLabel->clear();
    }
    else
        // use this to re-validate an already entered address
        AssetControlChangeEdited(ui->lineEditAssetControlChange->text());

    ui->lineEditAssetControlChange->setEnabled((state == Qt::Checked));
}

// Asset Control: custom change address changed
void SendAssetsDialog::AssetControlChangeEdited(const QString& text)
{
    if (model && model->getAddressTableModel())
    {
        // Default to no change address until verified
        m_coin_control->destChange = CNoDestination();
        ui->labelAssetControlChangeLabel->setStyleSheet(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR));

        const CTxDestination dest = DecodeDestination(text.toStdString());

        if (text.isEmpty()) // Nothing entered
        {
            ui->labelAssetControlChangeLabel->setText("");
        }
        else if (!IsValidDestination(dest)) // Invalid address
        {
            ui->labelAssetControlChangeLabel->setText(tr("Warning: Invalid Raptoreum address"));
        }
        else // Valid address
        {
            if (!model->wallet().isSpendable(dest)) {
                ui->labelAssetControlChangeLabel->setText(tr("Warning: Unknown change address"));

                // confirmation dialog
                QMessageBox::StandardButton btnRetVal = QMessageBox::question(this, tr("Confirm custom change address"), tr("The address you selected for change is not part of this wallet. Any or all funds in your wallet may be sent to this address. Are you sure?"),
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);

                if(btnRetVal == QMessageBox::Yes)
                    m_coin_control->destChange = dest;
                else
                {
                    ui->lineEditAssetControlChange->setText("");
                    ui->labelAssetControlChangeLabel->setStyleSheet(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_PRIMARY));
                    ui->labelAssetControlChangeLabel->setText("");
                }
            }
            else // Known change address
            {
                ui->labelAssetControlChangeLabel->setStyleSheet(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_PRIMARY));

                // Query label
                QString associatedLabel = model->getAddressTableModel()->labelForAddress(text);
                if (!associatedLabel.isEmpty())
                    ui->labelAssetControlChangeLabel->setText(associatedLabel);
                else
                    ui->labelAssetControlChangeLabel->setText(tr("(no label)"));

                m_coin_control->destChange = dest;
            }
        }
    }
}

// Asset Control: update labels
void SendAssetsDialog::AssetControlUpdateLabels()
{
    if (!model || !model->getOptionsModel())
        return;

    updateAssetControlState(*m_coin_control);

    // set pay amounts
    AssetControlDialog::payAmounts.clear();
    AssetControlDialog::fSubtractFeeFromAmount = false;

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendAssetsEntry *entry = qobject_cast<SendAssetsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry && !entry->isHidden())
        {
            SendCoinsRecipient rcp = entry->getValue();
            AssetControlDialog::payAmounts.append(rcp.assetAmount);
            if (rcp.fSubtractFeeFromAmount)
                AssetControlDialog::fSubtractFeeFromAmount = true;
        }
    }

    if (m_coin_control->HasAssetSelected())
    {
        // actual Asset control calculation
        AssetControlDialog::updateLabels(*m_coin_control, model, this);

        // show Asset control stats
        ui->labelAssetControlAutomaticallySelected->hide();
        ui->widgetAssetControl->show();
    }
    else
    {
        // hide Asset control stats
        ui->labelAssetControlAutomaticallySelected->show();
        ui->widgetAssetControl->hide();
        ui->labelAssetControlInsuffFunds->hide();
    }
}

void SendAssetsDialog::updateAssetList()
{
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendAssetsEntry *entry = qobject_cast<SendAssetsEntry*>(ui->entries->itemAt(i)->widget());
        if (entry) {
            entry->updateAssetList();
        }
    }
}

SendAssetConfirmationDialog::SendAssetConfirmationDialog(const QString &title, const QString &text, int _secDelay,
    QWidget *parent) :
    QMessageBox(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::Cancel, parent), secDelay(_secDelay)
{
    GUIUtil::updateFonts();
    setDefaultButton(QMessageBox::Cancel);
    yesButton = button(QMessageBox::Yes);
    updateYesButton();
    connect(&countDownTimer, SIGNAL(timeout()), this, SLOT(countDown()));
}

int SendAssetConfirmationDialog::exec()
{
    updateYesButton();
    countDownTimer.start(1000);
    return QMessageBox::exec();
}

void SendAssetConfirmationDialog::countDown()
{
    secDelay--;
    updateYesButton();

    if(secDelay <= 0)
    {
        countDownTimer.stop();
    }
}

void SendAssetConfirmationDialog::updateYesButton()
{
    if(secDelay > 0)
    {
        yesButton->setEnabled(false);
        yesButton->setText(tr("Yes") + " (" + QString::number(secDelay) + ")");
    }
    else
    {
        yesButton->setEnabled(true);
        yesButton->setText(tr("Yes"));
    }
}
