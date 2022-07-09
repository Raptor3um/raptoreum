// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2020 The Dash Core developers
// Copyright (c) 2020-2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/sendfuturesdialog.h>
#include <qt/forms/ui_sendfuturesdialog.h>

#include <qt/addresstablemodel.h>
#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/coincontroldialog.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/sendfuturesentry.h>

#include <key_io.h>
#include <wallet/coincontrol.h>
#include <validation.h> // mempool and minRelayTxFee
#include <ui_interface.h>
#include <txmempool.h>
#include <policy/fees.h>
#include <future/fee.h>
#include <wallet/wallet.h>

#include <QFontMetrics>
#include <QMessageBox>
#include <QScrollBar>
#include <QSettings>
#include <QTextDocument>
#include <QTimer>

#define FUTURES_CONFIRM_DELAY   3

static const std::array<int, 9> confTargets = { {2, 4, 6, 12, 24, 48, 144, 504, 1008} };

int getFTXConfTargetForIndex(int index) {
    if (index+1 > static_cast<int>(confTargets.size())) {
        return confTargets.back();
    }
    if (index < 0) {
        return confTargets[0];
    }
    return confTargets[index];
}

int getFTXIndexForConfTarget(int target) {
    for (unsigned int i = 0; i < confTargets.size(); i++) {
        if (confTargets[i] >= target) {
            return i;
        }
    }
    return confTargets.size() - 1;
}

SendFuturesDialog::SendFuturesDialog(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::SendFuturesDialog),
    clientModel(nullptr),
    model(nullptr),
    m_coin_control(new CCoinControl),
    fNewRecipientAllowed(true)
{
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
                      ui->fallbackFeeWarningLabel
                     }, GUIUtil::FontWeight::Bold);

    GUIUtil::setFont({ui->labelBalance
                     }, GUIUtil::FontWeight::Bold, 14);

    GUIUtil::setFont({ui->labelCoinControlFeatures
                     }, GUIUtil::FontWeight::Bold, 16);

    GUIUtil::setupAddressWidget(ui->lineEditCoinControlChange, this);

    addEntry();

    GUIUtil::updateFonts();
    /**
     * 
     * Hide unused UI elements for Future TX - functionality will remain intact for repurposing
     * 
     */
    //hide add recipient button
    //ui->addButton->hide();
    //hide private send checkbox
    //ui->checkUsePrivateSend->hide();
    //hide fee buttons
    //ui->buttonChooseFee->hide();
    //ui->fallbackFeeWarningLabel->hide();

    connect(ui->addButton, SIGNAL(clicked()), this, SLOT(addEntry()));
    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));

    // Coin Control
//    connect(ui->pushButtonCoinControl, SIGNAL(clicked()), this, SLOT(coinControlButtonClicked()));
//    connect(ui->checkBoxCoinControlChange, SIGNAL(stateChanged(int)), this, SLOT(coinControlChangeChecked(int)));
//    connect(ui->lineEditCoinControlChange, SIGNAL(textEdited(const QString &)), this, SLOT(coinControlChangeEdited(const QString &)));

    // Raptoreum specific
    QSettings settings;
/*    //TODO remove Darksend sometime after 0.14.1
    if (settings.contains("bUseDarkSend")) {
        settings.setValue("bUsePrivateSend", settings.value("bUseDarkSend").toBool());
        settings.remove("bUseDarkSend");
    }
    if (!settings.contains("bUsePrivateSend"))
        settings.setValue("bUsePrivateSend", false);

    //TODO remove InstantX sometime after 0.14.1
    if (settings.contains("bUseInstantX")) {
        settings.remove("bUseInstantX");
    }
    if (settings.contains("bUseInstantSend")) {
        settings.remove("bUseInstantSend");
    } */

    // Coin Control: clipboard actions
/*    QAction *clipboardQuantityAction = new QAction(tr("Copy quantity"), this);
    QAction *clipboardAmountAction = new QAction(tr("Copy amount"), this);
    QAction *clipboardFeeAction = new QAction(tr("Copy fee"), this);
    QAction *clipboardAfterFeeAction = new QAction(tr("Copy after fee"), this);
    QAction *clipboardBytesAction = new QAction(tr("Copy bytes"), this);
    QAction *clipboardLowOutputAction = new QAction(tr("Copy dust"), this);
    QAction *clipboardChangeAction = new QAction(tr("Copy change"), this);
    connect(clipboardQuantityAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardQuantity()));
    connect(clipboardAmountAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardAmount()));
    connect(clipboardFeeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardFee()));
    connect(clipboardAfterFeeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardAfterFee()));
    connect(clipboardBytesAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardBytes()));
    connect(clipboardLowOutputAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardLowOutput()));
    connect(clipboardChangeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardChange()));
    ui->labelCoinControlQuantity->addAction(clipboardQuantityAction);
    ui->labelCoinControlAmount->addAction(clipboardAmountAction);
    ui->labelCoinControlFee->addAction(clipboardFeeAction);
    ui->labelCoinControlAfterFee->addAction(clipboardAfterFeeAction);
    ui->labelCoinControlBytes->addAction(clipboardBytesAction);
    ui->labelCoinControlLowOutput->addAction(clipboardLowOutputAction);
    ui->labelCoinControlChange->addAction(clipboardChangeAction);
*/
    // init transaction fee section
/*    if (!settings.contains("fFeeSectionMinimized"))
        settings.setValue("fFeeSectionMinimized", true);
    if (!settings.contains("nFeeRadio") && settings.contains("nTransactionFee") && settings.value("nTransactionFee").toLongLong() > 0) // compatibility
        settings.setValue("nFeeRadio", 1); // custom
    if (!settings.contains("nFeeRadio"))
        settings.setValue("nFeeRadio", 0); // recommended
    if (!settings.contains("nSmartFeeSliderPosition"))
        settings.setValue("nSmartFeeSliderPosition", 0);*/
    if (!settings.contains("nTransactionFee"))
        settings.setValue("nTransactionFee", (qint64)DEFAULT_TRANSACTION_FEE);
//    if (!settings.contains("fPayOnlyMinFee"))
//        settings.setValue("fPayOnlyMinFee", false);

//    ui->groupFee->setId(ui->radioSmartFee, 0);
//    ui->groupFee->setId(ui->radioCustomFee, 1);
//    ui->groupFee->button((int)std::max(0, std::min(1, settings.value("nFeeRadio").toInt())))->setChecked(true);
    ui->customFee->setValue(settings.value("nTransactionFee").toLongLong());
//    ui->checkBoxMinimumFee->setChecked(settings.value("fPayOnlyMinFee").toBool());
//    minimizeFeeSection(settings.value("fFeeSectionMinimized").toBool());

/*    if(fPrivateSend)
    {
      ui->sendButton->setText("PrivateS&end");
      ui->sendButton->setToolTip("Confirm the PrivateSend action");
    }
    else
    {
      ui->sendButton->setText("S&end");
      ui->sendButton->setToolTip("Confirm the send action");
    }*/
}

void SendFuturesDialog::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;
/*
    if (_clientModel) {
        connect(_clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(updateSmartFeeLabel()));
    }*/
}

void SendFuturesDialog::setModel(WalletModel *_model)
{
    this->model = _model;

    if(_model && _model->getOptionsModel())
    {
        for(int i = 0; i < ui->entries->count(); ++i)
        {
            SendFuturesEntry *entry = qobject_cast<SendFuturesEntry*>(ui->entries->itemAt(i)->widget());
            if(entry)
            {
                entry->setModel(_model);
            }
        }
        interfaces::WalletBalances balances = _model->wallet().getBalances();
        setBalance(balances);
        connect(_model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        updateDisplayUnit();

        // Coin Control
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(coinControlUpdateLabels()));
        connect(_model->getOptionsModel(), SIGNAL(coinControlFeaturesChanged(bool)), this, SLOT(coinControlFeatureChanged(bool)));
        /**************  DISABLE FOR FUTURES UNTIL READY FOR USE *****************/
        ui->frameCoinControl->setVisible(_model->getOptionsModel()->getCoinControlFeatures());
        //ui->frameCoinControl->setVisible(false);
        coinControlUpdateLabels();

        // fee section
        for (const int &n : confTargets) {
            ui->confTargetSelector->addItem(tr("%1 (%2 blocks)").arg(GUIUtil::formatNiceTimeOffset(n*Params().GetConsensus().nPowTargetSpacing)).arg(n));
        }
//        connect(ui->confTargetSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(updateSmartFeeLabel()));
//        connect(ui->confTargetSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(coinControlUpdateLabels()));
//        connect(ui->groupFee, SIGNAL(buttonClicked(int)), this, SLOT(updateFeeSectionControls()));
//        connect(ui->groupFee, SIGNAL(buttonClicked(int)), this, SLOT(coinControlUpdateLabels()));
        connect(ui->customFee, SIGNAL(valueChanged()), this, SLOT(coinControlUpdateLabels()));
//        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(setMinimumFee()));
//        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(updateFeeSectionControls()));
//        connect(ui->checkBoxMinimumFee, SIGNAL(stateChanged(int)), this, SLOT(coinControlUpdateLabels()));
//        ui->customFee->setSingleStep(CWallet::GetRequiredFee(1000));
//        updateFeeSectionControls();
//        updateMinFeeLabel();
//        updateSmartFeeLabel();
        //FTX specific labels
        updateFtxFeeLabel();

        // set the smartfee-sliders default value (wallets default conf.target or last stored value)
        QSettings settings;
/*        if (settings.value("nSmartFeeSliderPosition").toInt() != 0) {
            // migrate nSmartFeeSliderPosition to nConfTarget
            // nConfTarget is available since 0.15 (replaced nSmartFeeSliderPosition)
            int nConfirmTarget = 25 - settings.value("nSmartFeeSliderPosition").toInt(); // 25 == old slider range
            settings.setValue("nConfTarget", nConfirmTarget);
            settings.remove("nSmartFeeSliderPosition");
        }*/
        if (settings.value("nConfTarget").toInt() == 0)
            ui->confTargetSelector->setCurrentIndex(getFTXIndexForConfTarget(model->node().getTxConfirmTarget()));
        else
            ui->confTargetSelector->setCurrentIndex(getFTXIndexForConfTarget(settings.value("nConfTarget").toInt()));
    }
}

SendFuturesDialog::~SendFuturesDialog()
{
    QSettings settings;
//    settings.setValue("fFeeSectionMinimized", fFeeMinimized);
//    settings.setValue("nFeeRadio", ui->groupFee->checkedId());
    settings.setValue("nConfTarget", getFTXConfTargetForIndex(ui->confTargetSelector->currentIndex()));
    settings.setValue("nTransactionFee", (qint64)ui->customFee->value());
//    settings.setValue("fPayOnlyMinFee", ui->checkBoxMinimumFee->isChecked());

    delete ui;
}

void SendFuturesDialog::on_sendButton_clicked()
{
    if(!model || !model->getOptionsModel())
        return;

    QList<SendFuturesRecipient> recipients;
    bool valid = true;

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendFuturesEntry *entry = qobject_cast<SendFuturesEntry*>(ui->entries->itemAt(i)->widget());
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

void SendFuturesDialog::send(QList<SendFuturesRecipient> recipients)
{
    // prepare transaction for getting txFee earlier
    WalletModelFuturesTransaction currentTransaction(recipients);
    WalletModel::SendFuturesReturn prepareStatus;

    updateCoinControlState(*m_coin_control);

//    ctrl.UsePrivateSend(fPrivateSend);

    prepareStatus = model->prepareFuturesTransaction(currentTransaction, *m_coin_control);

    // process prepareStatus and on error generate message shown to user
    processSendFuturesReturn(prepareStatus,
        BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), currentTransaction.getTransactionFee()));

    if(prepareStatus.status != WalletModel::OK) {
        fNewRecipientAllowed = true;
        return;
    }

    // Format confirmation message
    QStringList formatted;
    for (const SendFuturesRecipient &rcp : currentTransaction.getRecipients())
    {
        // generate bold amount string
        QString amount = "<b>" + BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), rcp.amount);
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

        formatted.append(recipientElement);
    }

    // Limit number of displayed entries
    int messageEntries = formatted.size();
    int displayedEntries = 0;
    for(int i = 0; i < formatted.size(); i++){
        if(i >= MAX_FUTURES_POPUP_ENTRIES){
            formatted.removeLast();
            i--;
        }
        else{
            displayedEntries = i+1;
        }
    }


    QString questionString = tr("Are you sure you want to send as future?");
    questionString.append("<br /><br />");
    questionString.append(formatted.join("<br />"));
    questionString.append("<br />");
    questionString.append(tr("using") + " <b>" + recipients[0].amount + "</b>");


/*    if(ctrl.IsUsingPrivateSend()) {
        questionString.append(tr("using") + " <b>" + tr("PrivateSend funds only") + "</b>");
    } else {
        //questionString.append(tr("using") + " <b>any available funds</b>");
        questionString.append(tr("using") + " <b>" + currentTransaction.getRecipients[0].amount + "</b>");
    }*/

    if (displayedEntries < messageEntries) {
        questionString.append("<br />");
        questionString.append("<span style='" + GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR) + "'>");
        questionString.append(tr("<b>(%1 of %2 entries displayed)</b>").arg(displayedEntries).arg(messageEntries));
        questionString.append("</span>");
    }

    CAmount txFee = currentTransaction.getTransactionFee() + getFutureFees();
//    CAmount futureFee = getFutureFees();

    if(txFee > 0)
    {
        // append fee string if a fee is required
        questionString.append("<hr /><span style='" + GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR) + "'>");
        questionString.append(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), txFee));
        questionString.append("</span> ");
        questionString.append(tr("are added as transaction fee"));

/*        if (ctrl.IsUsingPrivateSend()) {
            questionString.append(" " + tr("(PrivateSend transactions have higher fees usually due to no change output being allowed)"));
        }*/
    }

    // Show the future maturity information
    QDateTime currentDateTime = QDateTime::currentDateTime();

    questionString.append("<hr />");
    questionString.append("<span style='" + GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR) + "'><b>Future Maturity:</b></span><br />");

    if(recipients[0].maturity > 0) {
        questionString.append(tr("Confirmations: <b>%1</b><br />").arg(recipients[0].maturity));
    }
    if(recipients[0].locktime > 0) {
        questionString.append(tr("Time: <b>%1</b> - (<em>%2 seconds</em>)<br />")
        .arg( currentDateTime.addSecs(recipients[0].locktime).toString() )
        .arg(recipients[0].locktime));
    }
    if(recipients[0].maturity > 0 && recipients[0].locktime > 0) {
        int calcBlock = (recipients[0].maturity * 2 * 60);
        if(calcBlock < recipients[0].locktime) {
            questionString.append("This transaction will likely mature based on confirmations.");
        } else {
            questionString.append("This transaction will likely mature based on time.");
        }
    } else if(recipients[0].maturity <= 0 && recipients[0].locktime <= 0){
        questionString.append("<span style='" + GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR) + "'><b>No maturity is set. Transaction will mature as normal.</b></span>");
    }

    // Show some additioinal information
    questionString.append("<hr />");
    // append transaction size
    questionString.append(tr("Transaction size: %1").arg(QString::number((double)currentTransaction.getTransactionSize() / 1000)) + " kB");
    questionString.append("<br />");
    CFeeRate feeRate(txFee, currentTransaction.getTransactionSize());
    questionString.append(tr("Fee rate: %1").arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), feeRate.GetFeePerK())) + "/kB");

/*    if (ctrl.IsUsingPrivateSend()) {
        // append number of inputs
        questionString.append("<hr />");
        int nInputs = currentTransaction.getTransaction()->tx->vin.size();
        questionString.append(tr("This transaction will consume %n input(s)", "", nInputs));

        // warn about potential privacy issues when spending too many inputs at once
        if (nInputs >= 10 && ctrl.IsUsingPrivateSend()) {
            questionString.append("<br />");
            questionString.append("<span style='" + GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR) + "'>");
            questionString.append(tr("Warning: Using PrivateSend with %1 or more inputs can harm your privacy and is not recommended").arg(10));
            questionString.append("</span> ");
        }
    }*/

    // add total amount in all subdivision units
    questionString.append("<hr />");
    CAmount totalAmount = currentTransaction.getTotalTransactionAmount() + txFee;
    QStringList alternativeUnits;
    for (BitcoinUnits::Unit u : BitcoinUnits::availableUnits())
    {
        if(u != model->getOptionsModel()->getDisplayUnit())
            alternativeUnits.append(BitcoinUnits::formatHtmlWithUnit(u, totalAmount));
    }

    // Show total amount + all alternative units
    questionString.append(tr("Total Amount = <b>%1</b><br />= %2")
        .arg(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), totalAmount))
        .arg(alternativeUnits.join("<br />= ")));

    // Display message box
    FutureConfirmationDialog confirmationDialog(tr("Confirm send coins"),
        questionString, FUTURES_CONFIRM_DELAY, this);
    confirmationDialog.exec();
    QMessageBox::StandardButton retval = (QMessageBox::StandardButton)confirmationDialog.result();

    if(retval != QMessageBox::Yes)
    {
        fNewRecipientAllowed = true;
        return;
    }

    // now send the prepared transaction
    WalletModel::SendFuturesReturn sendStatus = model->sendFutures(currentTransaction);
    // process sendStatus and on error generate message shown to user
    processSendFuturesReturn(sendStatus);

    if (sendStatus.status == WalletModel::OK)
    {
        accept();
        m_coin_control->UnSelectAll();
        coinControlUpdateLabels();
    }
    fNewRecipientAllowed = true;
}

void SendFuturesDialog::clear()
{
    // Remove entries until only one left
    while(ui->entries->count())
    {
        ui->entries->takeAt(0)->widget()->deleteLater();
    }
    m_coin_control->UnSelectAll();
    addEntry();

    updateTabsAndLabels();
}

void SendFuturesDialog::reject()
{
    clear();
}

void SendFuturesDialog::accept()
{
    clear();
}

SendFuturesEntry *SendFuturesDialog::addEntry()
{
    SendFuturesEntry* entry = new SendFuturesEntry(this);
    entry->setModel(model);
    ui->entries->addWidget(entry);
    //connect(entry, SIGNAL(removeEntry(SendFuturesEntry*)), this, SLOT(removeEntry(SendFuturesEntry*)));
    connect(entry, SIGNAL(payAmountChanged()), this, SLOT(coinControlUpdateLabels()));
    //connect(entry, SIGNAL(subtractFeeFromAmountChanged()), this, SLOT(coinControlUpdateLabels()));

    connect(entry, SIGNAL(payFromChanged(const QString &)), this, SLOT(updateFTXpayFromLabels()));

    // Focus the field, so that entry can start immediately
    entry->clear();
    entry->setFocus();
    ui->scrollAreaWidgetContents->resize(ui->scrollAreaWidgetContents->sizeHint());
    qApp->processEvents();
    QScrollBar* bar = ui->scrollArea->verticalScrollBar();
    if(bar)
        bar->setSliderPosition(bar->maximum());

    updateTabsAndLabels();
    return entry;
}

void SendFuturesDialog::updateTabsAndLabels()
{
    setupTabChain(0);
    coinControlUpdateLabels();
}

void SendFuturesDialog::removeEntry(SendFuturesEntry* entry)
{
    entry->hide();

    // If the last entry is about to be removed add an empty one
    if (ui->entries->count() == 1)
        addEntry();

    entry->deleteLater();

    updateTabsAndLabels();
}

QWidget *SendFuturesDialog::setupTabChain(QWidget *prev)
{
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendFuturesEntry *entry = qobject_cast<SendFuturesEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            prev = entry->setupTabChain(prev);
        }
    }
    QWidget::setTabOrder(prev, ui->sendButton);
    QWidget::setTabOrder(ui->sendButton, ui->clearButton);
    QWidget::setTabOrder(ui->clearButton, ui->addButton);
    //Add button hidden for Futures
    return ui->addButton;
    //return ui->clearButton;
}

void SendFuturesDialog::setAddress(const QString &address)
{
    SendFuturesEntry *entry = 0;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendFuturesEntry *first = qobject_cast<SendFuturesEntry*>(ui->entries->itemAt(0)->widget());
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

void SendFuturesDialog::pasteEntry(const SendFuturesRecipient &rv)
{
    if(!fNewRecipientAllowed)
        return;

    SendFuturesEntry *entry = 0;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendFuturesEntry *first = qobject_cast<SendFuturesEntry*>(ui->entries->itemAt(0)->widget());
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

bool SendFuturesDialog::handlePaymentRequest(const SendFuturesRecipient &rv)
{
    // Just paste the entry, all pre-checks
    // are done in paymentserver.cpp.
    pasteEntry(rv);
    return true;
}

void SendFuturesDialog::setBalance(const interfaces::WalletBalances& balances)
{
    if(model && model->getOptionsModel())
    {
        CAmount bal = 0;
        if (m_coin_control->IsUsingCoinJoin()) {
            bal = balances.anonymized_balance;
        } else {
            bal = balances.balance;
        }

        ui->labelBalance->setText(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), bal));
    }
}

void SendFuturesDialog::updateDisplayUnit()
{
    setBalance(model->wallet().getBalances());
    coinControlUpdateLabels();
    ui->customFee->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
//    updateMinFeeLabel();
//    updateSmartFeeLabel();
    //FTX specific labels
    updateFtxFeeLabel();
    updateFTXpayFromLabels();
}

void SendFuturesDialog::processSendFuturesReturn(const WalletModel::SendFuturesReturn &sendFuturesReturn, const QString &msgArg)
{
    QPair<QString, CClientUIInterface::MessageBoxFlags> msgParams;
    // Default to a warning message, override if error message is needed
    msgParams.second = CClientUIInterface::MSG_WARNING;

    // This comment is specific to SendFuturesDialog usage of WalletModel::SendFuturesReturn.
    // WalletModel::TransactionCommitFailed is used only in WalletModel::sendFutures()
    // all others are used only in WalletModel::prepareTransaction()
    switch(sendFuturesReturn.status)
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
        msgParams.first = tr("The transaction was rejected with the following reason: %1").arg(sendFuturesReturn.reasonCommitFailed);
        msgParams.second = CClientUIInterface::MSG_ERROR;
        break;
    case WalletModel::AbsurdFee:
        msgParams.first = tr("A fee higher than %1 is considered an absurdly high fee.").arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), maxTxFee));
        break;
    case WalletModel::PaymentRequestExpired:
        msgParams.first = tr("Payment request expired.");
        msgParams.second = CClientUIInterface::MSG_ERROR;
        break;
    // included to prevent a compiler warning.
    case WalletModel::OK:
    default:
        return;
    }

    Q_EMIT message(tr("Send Futures"), msgParams.first, msgParams.second);
}

void SendFuturesDialog::minimizeFeeSection(bool fMinimize)
{
    //ui->labelFeeMinimized->setVisible(fMinimize);
    //hide for future tx 
    //ui->buttonChooseFee  ->setVisible(fMinimize);
    //ui->buttonMinimizeFee->setVisible(!fMinimize);
    //ui->frameFeeSelection->setVisible(!fMinimize);
    //ui->horizontalLayoutSmartFee->setContentsMargins(0, (fMinimize ? 0 : 6), 0, 0);
    //fFeeMinimized = fMinimize;
}

void SendFuturesDialog::on_buttonChooseFee_clicked()
{
    //minimizeFeeSection(false);
}

void SendFuturesDialog::on_buttonMinimizeFee_clicked()
{
    //updateFeeMinimizedLabel();
    //minimizeFeeSection(true);
}

void SendFuturesDialog::setMinimumFee()
{
    ui->customFee->setValue(model->node().getRequiredFee(1000));
}

void SendFuturesDialog::updateFtxFeeLabel()
{
    CAmount futureFee = getFutureFees();
    ui->ftxFee->setText(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), futureFee));
}

void SendFuturesDialog::updateFeeSectionControls()
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

void SendFuturesDialog::updateFeeMinimizedLabel()
{
    if(!model || !model->getOptionsModel())
        return;

    if (ui->radioSmartFee->isChecked())
        ui->labelFeeMinimized->setText(ui->labelSmartFee->text());
    else {
        ui->labelFeeMinimized->setText(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), ui->customFee->value()) + "/kB");
    }
}

void SendFuturesDialog::updateMinFeeLabel()
{
    if (model && model->getOptionsModel())
        ui->checkBoxMinimumFee->setText(tr("Pay only the required fee of %1").arg(
            BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), model->node().getRequiredFee(1000)) + "/kB")
        );
}

void SendFuturesDialog::updateCoinControlState(CCoinControl& ctrl)
{
//    if (ui->radioCustomFee->isChecked()) {
        ctrl.m_feerate = CFeeRate(ui->customFee->value());
//    } else {
//        ctrl.m_feerate.reset();
//    }
    // Avoid using global defaults when sending money from the GUI
    // Either custom fee will be used or if not selected, the confirmation target from dropdown box
    ctrl.m_confirm_target = getFTXConfTargetForIndex(ui->confTargetSelector->currentIndex());
}

void SendFuturesDialog::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);
  if(!event->spontaneous())
  {
//    CoinControlDialog::usePrivateSend(fPrivateSend);
  }
}

void SendFuturesDialog::updateSmartFeeLabel()
{
    if(!model || !model->getOptionsModel())
        return;
    CCoinControl coin_control;
    updateCoinControlState(coin_control);
    coin_control.m_feerate.reset(); // Explicitly use only fee estimation rate for smart fee labels
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

// Coin Control: copy label "Quantity" to clipboard
void SendFuturesDialog::coinControlClipboardQuantity()
{
    GUIUtil::setClipboard(ui->labelCoinControlQuantity->text());
}

// Coin Control: copy label "Amount" to clipboard
void SendFuturesDialog::coinControlClipboardAmount()
{
    GUIUtil::setClipboard(ui->labelCoinControlAmount->text().left(ui->labelCoinControlAmount->text().indexOf(" ")));
}

// Coin Control: copy label "Fee" to clipboard
void SendFuturesDialog::coinControlClipboardFee()
{
    GUIUtil::setClipboard(ui->labelCoinControlFee->text().left(ui->labelCoinControlFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "After fee" to clipboard
void SendFuturesDialog::coinControlClipboardAfterFee()
{
    GUIUtil::setClipboard(ui->labelCoinControlAfterFee->text().left(ui->labelCoinControlAfterFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "Bytes" to clipboard
void SendFuturesDialog::coinControlClipboardBytes()
{
    GUIUtil::setClipboard(ui->labelCoinControlBytes->text().replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "Dust" to clipboard
void SendFuturesDialog::coinControlClipboardLowOutput()
{
    GUIUtil::setClipboard(ui->labelCoinControlLowOutput->text());
}

// Coin Control: copy label "Change" to clipboard
void SendFuturesDialog::coinControlClipboardChange()
{
    GUIUtil::setClipboard(ui->labelCoinControlChange->text().left(ui->labelCoinControlChange->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: settings menu - coin control enabled/disabled by user
void SendFuturesDialog::coinControlFeatureChanged(bool checked)
{
    //hide for futures until ready for implementation
    //ui->frameCoinControl->setVisible(checked);
    ui->frameCoinControl->setVisible(false);
    
    //Disable check on control dialog for Future TX
    //if (!checked && model) // coin control features disabled
    m_coin_control->SetNull(false);

    coinControlUpdateLabels();
}

// Coin Control: button inputs -> show actual coin control dialog
void SendFuturesDialog::coinControlButtonClicked()
{
    CoinControlDialog dlg(*m_coin_control, model, this);
    dlg.exec();
    coinControlUpdateLabels();
}

// Coin Control: checkbox custom change address
void SendFuturesDialog::coinControlChangeChecked(int state)
{
    if (state == Qt::Unchecked)
    {
        m_coin_control->destChange = CNoDestination();
        ui->labelCoinControlChangeLabel->clear();
    }
    else
        // use this to re-validate an already entered address
        coinControlChangeEdited(ui->lineEditCoinControlChange->text());

    ui->lineEditCoinControlChange->setEnabled((state == Qt::Checked));
}

// Coin Control: custom change address changed
void SendFuturesDialog::coinControlChangeEdited(const QString& text)
{
    if (model && model->getAddressTableModel())
    {
        // Default to no change address until verified
        m_coin_control->destChange = CNoDestination();
        ui->labelCoinControlChangeLabel->setStyleSheet(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR));

        const CTxDestination dest = DecodeDestination(text.toStdString());

        if (text.isEmpty()) // Nothing entered
        {
            ui->labelCoinControlChangeLabel->setText("");
        }
        else if (!IsValidDestination(dest)) // Invalid address
        {
            ui->labelCoinControlChangeLabel->setText(tr("Warning: Invalid Raptoreum address"));
        }
        else // Valid address
        {
            if (!model->wallet().isSpendable(dest)) {
                ui->labelCoinControlChangeLabel->setText(tr("Warning: Unknown change address"));

                // confirmation dialog
                QMessageBox::StandardButton btnRetVal = QMessageBox::question(this, tr("Confirm custom change address"), tr("The address you selected for change is not part of this wallet. Any or all funds in your wallet may be sent to this address. Are you sure?"),
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);

                if(btnRetVal == QMessageBox::Yes)
                    m_coin_control->destChange = dest;
                else
                {
                    ui->lineEditCoinControlChange->setText("");
                    ui->labelCoinControlChangeLabel->setStyleSheet(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_PRIMARY));
                    ui->labelCoinControlChangeLabel->setText("");
                }
            }
            else // Known change address
            {
                ui->labelCoinControlChangeLabel->setStyleSheet(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_PRIMARY));

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
void SendFuturesDialog::coinControlUpdateLabels()
{
    if (!model || !model->getOptionsModel())
        return;

    updateCoinControlState(*m_coin_control);

    //Update FTX specific labels
    updateFTXpayFromLabels();

    // set pay amounts
    CoinControlDialog::payAmounts.clear();
    CoinControlDialog::fSubtractFeeFromAmount = false;

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendFuturesEntry *entry = qobject_cast<SendFuturesEntry*>(ui->entries->itemAt(i)->widget());
        if(entry && !entry->isHidden())
        {
            SendFuturesRecipient rcp = entry->getValue();
            CoinControlDialog::payAmounts.append(rcp.amount);
//            if (rcp.fSubtractFeeFromAmount)
//                CoinControlDialog::fSubtractFeeFromAmount = true;
        }
    }

    if (m_coin_control->HasSelected())
    {
        // actual coin control calculation
        CoinControlDialog::updateLabels(*m_coin_control, model, this);

        // show coin control stats
        ui->labelCoinControlAutomaticallySelected->hide();
        ui->widgetCoinControl->show();
    }
    else
    {
        // hide coin control stats
        ui->labelCoinControlAutomaticallySelected->show();
        ui->widgetCoinControl->hide();
        ui->labelCoinControlInsuffFunds->hide();
    }
}

//Future pay from: update labels
void SendFuturesDialog::updateFTXpayFromLabels()
{
    if (!model || !model->getOptionsModel())
        return;
    //disable send button until form is valid
    ui->sendButton->setEnabled(false);

    std::map<CTxDestination, CAmount> balances = model->getAddressBalances();
    CAmount addressBalance = 0;
    CAmount futureFee = getFutureFees();
    CAmount sendAmount = 0;

    //future tx entries
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendFuturesEntry *entry = qobject_cast<SendFuturesEntry*>(ui->entries->itemAt(i)->widget());
        if(entry && !entry->isHidden())
        {
            SendFuturesRecipient rcp = entry->getValue();
            sendAmount += rcp.amount;
            //find balance for matched address in payfrom field
            for (auto& balance : balances) {
                if (EncodeDestination(balance.first) == rcp.payFrom.toStdString()) {
                    addressBalance += balance.second;
                }
            }

        }
    }
    //update balance label with selected
    ui->labelBalance->setText(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), addressBalance));
    //enable send button when amounts look good
    if(addressBalance > sendAmount + futureFee && sendAmount > 0) {
        ui->sendButton->setEnabled(true);
    }

    if(addressBalance > sendAmount + futureFee) {
        ui->labelBalance->setStyleSheet(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_PRIMARY));
    } else {
        ui->labelBalance->setStyleSheet(GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR));
    }

}

FutureConfirmationDialog::FutureConfirmationDialog(const QString &title, const QString &text, int _secDelay,
    QWidget *parent) :
    QMessageBox(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::Cancel, parent), secDelay(_secDelay)
{
    setDefaultButton(QMessageBox::Cancel);
    yesButton = button(QMessageBox::Yes);
    updateYesButton();
    connect(&countDownTimer, SIGNAL(timeout()), this, SLOT(countDown()));
}

int FutureConfirmationDialog::exec()
{
    updateYesButton();
    countDownTimer.start(1000);
    return QMessageBox::exec();
}

void FutureConfirmationDialog::countDown()
{
    secDelay--;
    updateYesButton();

    if(secDelay <= 0)
    {
        countDownTimer.stop();
    }
}

void FutureConfirmationDialog::updateYesButton()
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
