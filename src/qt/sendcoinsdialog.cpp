// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/raptoreum-config.h>
#endif

#include <qt/sendcoinsdialog.h>
#include <qt/forms/ui_sendcoinsdialog.h>

#include <qt/addresstablemodel.h>
#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/coincontroldialog.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/sendcoinsentry.h>

#include <chainparams.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <wallet/coincontrol.h>
#include <ui_interface.h>
#include <txmempool.h>
#include <validation.h>
#include <policy/fees.h>
#include <wallet/fees.h>
#include <wallet/wallet.h>
#include <future/fee.h>
#include <spork.h>
#include <validation.h>

#include <QFontMetrics>
#include <QScrollBar>
#include <QSettings>
#include <QTextDocument>

#define SEND_CONFIRM_DELAY   3

SendCoinsDialog::SendCoinsDialog(bool _fCoinJoin, QWidget *parent) :
        QDialog(parent),
        ui(new Ui::SendCoinsDialog),
        clientModel(nullptr),
        model(nullptr),
        m_coin_control(new CCoinControl),
        fNewRecipientAllowed(true),
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
                      ui->fallbackFeeWarningLabel
                     }, GUIUtil::FontWeight::Bold);

    GUIUtil::setFont({ui->labelBalance,
                      ui->labelBalanceText
                     }, GUIUtil::FontWeight::Bold, 14);

    GUIUtil::setFont({ui->labelCoinControlFeatures
                     }, GUIUtil::FontWeight::Bold, 16);

    GUIUtil::setupAddressWidget(ui->lineEditCoinControlChange, this);

    addEntry();

    GUIUtil::updateFonts();

    connect(ui->addButton, &QPushButton::clicked, this, &SendCoinsDialog::addEntry);
    connect(ui->clearButton, &QPushButton::clicked, this, &SendCoinsDialog::clear);

    // Coin Control
    connect(ui->pushButtonCoinControl, &QPushButton::clicked, this, &SendCoinsDialog::coinControlButtonClicked);
    connect(ui->checkBoxCoinControlChange, &QCheckBox::stateChanged, this, &SendCoinsDialog::coinControlChangeChecked);
    connect(ui->lineEditCoinControlChange, &QValidatedLineEdit::textEdited, this,
            &SendCoinsDialog::coinControlChangeEdited);

    // Coin Control: clipboard actions
    QAction *clipboardQuantityAction = new QAction(tr("Copy quantity"), this);
    QAction *clipboardAmountAction = new QAction(tr("Copy amount"), this);
    QAction *clipboardFeeAction = new QAction(tr("Copy fee"), this);
    QAction *clipboardAfterFeeAction = new QAction(tr("Copy after fee"), this);
    QAction *clipboardBytesAction = new QAction(tr("Copy bytes"), this);
    QAction *clipboardLowOutputAction = new QAction(tr("Copy dust"), this);
    QAction *clipboardChangeAction = new QAction(tr("Copy change"), this);
    connect(clipboardQuantityAction, &QAction::triggered, this, &SendCoinsDialog::coinControlClipboardQuantity);
    connect(clipboardAmountAction, &QAction::triggered, this, &SendCoinsDialog::coinControlClipboardAmount);
    connect(clipboardFeeAction, &QAction::triggered, this, &SendCoinsDialog::coinControlClipboardFee);
    connect(clipboardAfterFeeAction, &QAction::triggered, this, &SendCoinsDialog::coinControlClipboardAfterFee);
    connect(clipboardBytesAction, &QAction::triggered, this, &SendCoinsDialog::coinControlClipboardBytes);
    connect(clipboardLowOutputAction, &QAction::triggered, this, &SendCoinsDialog::coinControlClipboardLowOutput);
    connect(clipboardChangeAction, &QAction::triggered, this, &SendCoinsDialog::coinControlClipboardChange);
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

    if (_fCoinJoin) {
        ui->sendButton->setText(tr("S&end mixed funds"));
        ui->sendButton->setToolTip(tr("Confirm the %1 send action").arg(QString::fromStdString(gCoinJoinName)));
    } else {
        ui->sendButton->setText(tr("S&end"));
        ui->sendButton->setToolTip(tr("Confirm the send action"));
    }

    m_coin_control->UseCoinJoin(_fCoinJoin);
}

void SendCoinsDialog::setClientModel(ClientModel *_clientModel) {
    this->clientModel = _clientModel;

    if (_clientModel) {
        connect(_clientModel, &ClientModel::numBlocksChanged, this, &SendCoinsDialog::updateSmartFeeLabel);
    }
}

void SendCoinsDialog::setModel(WalletModel *_model) {
    this->model = _model;

    if (_model && _model->getOptionsModel()) {
        for (int i = 0; i < ui->entries->count(); ++i) {
            SendCoinsEntry *entry = qobject_cast<SendCoinsEntry *>(ui->entries->itemAt(i)->widget());
            if (entry) {
                entry->setModel(_model);
            }
        }

        interfaces::WalletBalances balances = _model->wallet().getBalances();
        setBalance(balances);
        connect(_model, &WalletModel::balanceChanged, this, &SendCoinsDialog::setBalance);
        connect(_model->getOptionsModel(), &OptionsModel::displayUnitChanged, this,
                &SendCoinsDialog::updateDisplayUnit);
        updateDisplayUnit();

        // Coin Control
        connect(_model->getOptionsModel(), &OptionsModel::displayUnitChanged, this,
                &SendCoinsDialog::coinControlUpdateLabels);
        connect(_model->getOptionsModel(), &OptionsModel::coinControlFeaturesChanged, this,
                &SendCoinsDialog::coinControlFeatureChanged);
        ui->frameCoinControl->setVisible(_model->getOptionsModel()->getCoinControlFeatures());
        coinControlUpdateLabels();

        // fee section
        for (const int n: confTargets) {
            ui->confTargetSelector->addItem(tr("%1 (%2 blocks)").arg(
                    GUIUtil::formatNiceTimeOffset(n * Params().GetConsensus().nPowTargetSpacing)).arg(n));
        }
        connect(ui->confTargetSelector, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
                &SendCoinsDialog::updateSmartFeeLabel);
        connect(ui->confTargetSelector, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
                &SendCoinsDialog::coinControlUpdateLabels);
        connect(ui->groupFee, static_cast<void (QButtonGroup::*)(int)>(&QButtonGroup::idClicked), this,
                &SendCoinsDialog::updateFeeSectionControls);
        connect(ui->groupFee, static_cast<void (QButtonGroup::*)(int)>(&QButtonGroup::idClicked), this,
                &SendCoinsDialog::coinControlUpdateLabels);
        connect(ui->customFee, &BitcoinAmountField::valueChanged, this, &SendCoinsDialog::coinControlUpdateLabels);
        connect(ui->checkBoxMinimumFee, &QCheckBox::stateChanged, this, &SendCoinsDialog::setMinimumFee);
        connect(ui->checkBoxMinimumFee, &QCheckBox::stateChanged, this, &SendCoinsDialog::updateFeeSectionControls);
        connect(ui->checkBoxMinimumFee, &QCheckBox::stateChanged, this, &SendCoinsDialog::coinControlUpdateLabels);

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

SendCoinsDialog::~SendCoinsDialog() {
    QSettings settings;
    settings.setValue("fFeeSectionMinimized", fFeeMinimized);
    settings.setValue("nFeeRadio", ui->groupFee->checkedId());
    settings.setValue("nConfTarget", getConfTargetForIndex(ui->confTargetSelector->currentIndex()));
    settings.setValue("nTransactionFee", (qint64) ui->customFee->value());
    settings.setValue("fPayOnlyMinFee", ui->checkBoxMinimumFee->isChecked());

    delete ui;
}

void SendCoinsDialog::OnDisplay() {
    for (int i = 0; i < ui->entries->count(); ++i) {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry *>(ui->entries->itemAt(i)->widget());
        if (entry) {
            entry->SetFutureVisible(sporkManager.IsSporkActive(SPORK_22_SPECIAL_TX_FEE) &&
                                    Params().IsFutureActive(::ChainActive().Tip()) && i == 0);
        }
    }
}

void SendCoinsDialog::on_sendButton_clicked() {
    if (!model || !model->getOptionsModel())
        return;

    QList <SendCoinsRecipient> recipients;
    bool valid = true;

    for (int i = 0; i < ui->entries->count(); ++i) {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry *>(ui->entries->itemAt(i)->widget());
        if (entry) {
            if (entry->validate(model->node())) {
                recipients.append(entry->getValue());
            } else {
                valid = false;
            }
        }
    }

    if (!valid || recipients.isEmpty()) {
        return;
    }

    fNewRecipientAllowed = false;
    // request unlock only if was locked or unlocked for mixing:
    // this way we let users unlock by walletpassphrase or by menu
    // and make many transactions while unlocking through this dialog
    // will call relock
    WalletModel::EncryptionStatus encStatus = model->getEncryptionStatus();
    if (encStatus == model->Locked || encStatus == model->UnlockedForMixingOnly) {
        WalletModel::UnlockContext ctx(model->requestUnlock());
        if (!ctx.isValid()) {
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

void SendCoinsDialog::send(QList <SendCoinsRecipient> recipients) {
    // prepare transaction for getting txFee earlier
    WalletModelTransaction currentTransaction(recipients);
    WalletModel::SendCoinsReturn prepareStatus;

    updateCoinControlState(*m_coin_control);

    prepareStatus = model->prepareTransaction(currentTransaction, *m_coin_control);

    // process prepareStatus and on error generate message shown to user
    processSendCoinsReturn(prepareStatus,
                           BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(),
                                                        currentTransaction.getTransactionFee()));

    if (prepareStatus.status != WalletModel::OK) {
        fNewRecipientAllowed = true;
        return;
    }

    // Format confirmation message
    QStringList formatted;
    bool hasFuture = false;
    for (const SendCoinsRecipient &rcp: currentTransaction.getRecipients()) {
        // generate bold amount string with wallet name in case of multiwallet
        QString amount =
                "<b>" + BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), rcp.amount);
        if (model->isMultiwallet()) {
            amount.append(" <u>" + tr("from wallet %1").arg(GUIUtil::HtmlEscape(model->getWalletName())) + "</u> ");
        }
        amount.append("</b> ");
        // generate monospace address string
        QString address = "<span style='font-family: monospace;'>" + rcp.address;
        address.append("</span>");

        QString recipientElement;

        {
            if (rcp.label.length() > 0) // label with address
            {
                recipientElement = tr("%1 to %2").arg(amount, GUIUtil::HtmlEscape(rcp.label));
                recipientElement.append(QString(" (%1)").arg(address));
            } else // just address
            {
                recipientElement = tr("%1 to %2").arg(amount, address);
            }
        }
        //std::cout << rcp.amount << " is future output " << rcp.isFutureOutput << "\n";
        if (rcp.isFutureOutput) {
            hasFuture = true;
            if (recipients[0].maturity >= 0) {
                recipientElement.append(tr("<br>Confirmations in: <b>%1 blocks</b><br />").arg(recipients[0].maturity));
            }
            if (recipients[0].locktime >= 0) {
                recipientElement.append(
                        tr("Time in: <b>%1 seconds from first confirmed</b><br />").arg(recipients[0].locktime));
            }
            if (recipients[0].maturity >= 0 && recipients[0].locktime >= 0) {
                int calcBlock = (recipients[0].maturity * 2 * 60);
                if (calcBlock < recipients[0].locktime) {
                    recipientElement.append("This transaction will likely mature based on confirmations.");
                } else {
                    recipientElement.append("This transaction will likely mature based on time.");
                }
            } else if (recipients[0].maturity < 0 && recipients[0].locktime < 0) {
                recipientElement.append(
                        "<span style='" + GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR) +
                        "'><b>No maturity is set. Transaction will mature as normal.</b></span>");
            }
        }

        formatted.append(recipientElement);
    }

    // Limit number of displayed entries
    int messageEntries = formatted.size();
    int displayedEntries = 0;
    for (int i = 0; i < formatted.size(); i++) {
        if (i >= MAX_SEND_POPUP_ENTRIES) {
            formatted.removeLast();
            i--;
        } else {
            displayedEntries = i + 1;
        }
    }

    QString questionString = tr("Are you sure you want to send?");
    questionString.append("<br /><br />");
    questionString.append(formatted.join("<br />"));
    questionString.append("<br />");

    QString strCoinJoinName = QString::fromStdString(gCoinJoinName);

    if (m_coin_control->IsUsingCoinJoin()) {
        questionString.append(tr("using") + " <b>" + tr("%1 funds only").arg(strCoinJoinName) + "</b>");
    } else {
        questionString.append(tr("using") + " <b>" + tr("any available funds") + "</b>");
    }

    if (displayedEntries < messageEntries) {
        questionString.append("<br />");
        questionString.append("<span style='" + GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR) + "'>");
        questionString.append(tr("<b>(%1 of %2 entries displayed)</b>").arg(displayedEntries).arg(messageEntries));
        questionString.append("</span>");
    }

    CAmount txFee = currentTransaction.getTransactionFee();
    txFee += hasFuture ? getFutureFeesCoin() : 0;
    if (txFee > 0) {
        // append fee string if a fee is required
        questionString.append(
                "<hr /><span style='" + GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR) + "'>");
        questionString.append(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), txFee));
        questionString.append("</span> ");
        questionString.append(tr("are added as transaction fee"));

        if (m_coin_control->IsUsingCoinJoin()) {
            questionString.append(" " +
                                  tr("(%1 transactions have higher fees usually due to no change output being allowed)").arg(
                                          strCoinJoinName));
        }
    }

    // Show some additioinal information
    questionString.append("<hr />");
    // append transaction size
    questionString.append(
            tr("Transaction size: %1").arg(QString::number((double) currentTransaction.getTransactionSize() / 1000)) +
            " kB");
    questionString.append("<br />");
    CFeeRate feeRate(txFee, currentTransaction.getTransactionSize());
    questionString.append(tr("Fee rate: %1").arg(
            BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), feeRate.GetFeePerK())) + "/kB");

    if (m_coin_control->IsUsingCoinJoin()) {
        // append number of inputs
        questionString.append("<hr />");
        int nInputs = currentTransaction.getWtx()->vin.size();
        questionString.append(tr("This transaction will consume %n input(s)", "", nInputs));

        // warn about potential privacy issues when spending too many inputs at once
        if (nInputs >= 10 && m_coin_control->IsUsingCoinJoin()) {
            questionString.append("<br />");
            questionString.append(
                    "<span style='" + GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR) + "'>");
            questionString.append(
                    tr("Warning: Using %1 with %2 or more inputs can harm your privacy and is not recommended").arg(
                            strCoinJoinName).arg(10));
            questionString.append("</span> ");
        }
    }

    // add total amount in all subdivision units
    questionString.append("<hr />");
    CAmount totalAmount = currentTransaction.getTotalTransactionAmount() + txFee;
    QStringList alternativeUnits;
    for (const BitcoinUnits::Unit u: BitcoinUnits::availableUnits()) {
        if (u != model->getOptionsModel()->getDisplayUnit())
            alternativeUnits.append(BitcoinUnits::formatHtmlWithUnit(u, totalAmount));
    }

    // Show total amount + all alternative units
    questionString.append(tr("Total Amount = <b>%1</b><br />= %2")
                                  .arg(BitcoinUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(),
                                                                        totalAmount))
                                  .arg(alternativeUnits.join("<br />= ")));

    // Display message box
    SendConfirmationDialog confirmationDialog(tr("Confirm send coins"),
                                              questionString, SEND_CONFIRM_DELAY, this);
    confirmationDialog.exec();
    QMessageBox::StandardButton retval = static_cast<QMessageBox::StandardButton>(confirmationDialog.result());

    if (retval != QMessageBox::Yes) {
        fNewRecipientAllowed = true;
        return;
    }

    // now send the prepared transaction
    WalletModel::SendCoinsReturn sendStatus = model->sendCoins(currentTransaction, m_coin_control->IsUsingCoinJoin());
    // process sendStatus and on error generate message shown to user
    processSendCoinsReturn(sendStatus);

    if (sendStatus.status == WalletModel::OK) {
        accept();
        m_coin_control->UnSelectAll();
        coinControlUpdateLabels();
        Q_EMIT coinsSent(currentTransaction.getWtx()->GetHash());
    }
    fNewRecipientAllowed = true;
}

void SendCoinsDialog::clear() {
    // Clear coin control settings
    m_coin_control->UnSelectAll();
    ui->checkBoxCoinControlChange->setChecked(false);
    ui->lineEditCoinControlChange->clear();
    coinControlUpdateLabels();

    // Remove entries until only one left
    while (ui->entries->count()) {
        ui->entries->takeAt(0)->widget()->deleteLater();
    }
    addEntry();

    updateTabsAndLabels();
}

void SendCoinsDialog::reject() {
    clear();
}

void SendCoinsDialog::accept() {
    clear();
}

SendCoinsEntry *SendCoinsDialog::addEntry() {

    SendCoinsEntry *entry = new SendCoinsEntry(this, sporkManager.IsSporkActive(SPORK_22_SPECIAL_TX_FEE)
                                                     && Params().IsFutureActive(::ChainActive().Tip())
                                                     && ui->entries->count() == 0);
    entry->setModel(model);
    ui->entries->addWidget(entry);
    connect(entry, &SendCoinsEntry::removeEntry, this, &SendCoinsDialog::removeEntry);
    connect(entry, &SendCoinsEntry::useAvailableBalance, this, &SendCoinsDialog::useAvailableBalance);
    connect(entry, &SendCoinsEntry::payAmountChanged, this, &SendCoinsDialog::coinControlUpdateLabels);
    connect(entry, &SendCoinsEntry::subtractFeeFromAmountChanged, this, &SendCoinsDialog::coinControlUpdateLabels);

    // Focus the field, so that entry can start immediately
    entry->clear();
    entry->setFocus();
    ui->scrollAreaWidgetContents->resize(ui->scrollAreaWidgetContents->sizeHint());
    qApp->processEvents();
    QScrollBar *bar = ui->scrollArea->verticalScrollBar();
    if (bar)
        bar->setSliderPosition(bar->maximum());

    updateTabsAndLabels();
    return entry;
}

void SendCoinsDialog::updateTabsAndLabels() {
    setupTabChain(nullptr);
    coinControlUpdateLabels();
}

void SendCoinsDialog::removeEntry(SendCoinsEntry *entry) {
    entry->hide();

    // If the last entry is about to be removed add an empty one
    if (ui->entries->count() == 1)
        addEntry();

    entry->deleteLater();

    updateTabsAndLabels();
}

QWidget *SendCoinsDialog::setupTabChain(QWidget *prev) {
    for (int i = 0; i < ui->entries->count(); ++i) {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry *>(ui->entries->itemAt(i)->widget());
        if (entry) {
            prev = entry->setupTabChain(prev);
        }
    }
    QWidget::setTabOrder(prev, ui->sendButton);
    QWidget::setTabOrder(ui->sendButton, ui->clearButton);
    QWidget::setTabOrder(ui->clearButton, ui->addButton);
    return ui->addButton;
}

void SendCoinsDialog::setAddress(const QString &address) {
    SendCoinsEntry *entry = nullptr;
    // Replace the first entry if it is still unused
    if (ui->entries->count() == 1) {
        SendCoinsEntry *first = qobject_cast<SendCoinsEntry *>(ui->entries->itemAt(0)->widget());
        if (first->isClear()) {
            entry = first;
        }
    }
    if (!entry) {
        entry = addEntry();
    }

    entry->setAddress(address);
}

void SendCoinsDialog::pasteEntry(const SendCoinsRecipient &rv) {
    if (!fNewRecipientAllowed)
        return;

    SendCoinsEntry *entry = nullptr;
    // Replace the first entry if it is still unused
    if (ui->entries->count() == 1) {
        SendCoinsEntry *first = qobject_cast<SendCoinsEntry *>(ui->entries->itemAt(0)->widget());
        if (first->isClear()) {
            entry = first;
        }
    }
    if (!entry) {
        entry = addEntry();
    }

    entry->setValue(rv);
    updateTabsAndLabels();
}

bool SendCoinsDialog::handlePaymentRequest(const SendCoinsRecipient &rv) {
    // Just paste the entry, all pre-checks
    // are done in paymentserver.cpp.
    pasteEntry(rv);
    return true;
}

void SendCoinsDialog::setBalance(const interfaces::WalletBalances &balances) {
    if (model && model->getOptionsModel()) {
        CAmount bal = 0;
        if (m_coin_control->IsUsingCoinJoin()) {
            bal = balances.anonymized_balance;
        } else {
            bal = balances.balance;
        }

        ui->labelBalance->setText(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), bal));
    }
}

void SendCoinsDialog::updateDisplayUnit() {
    setBalance(model->wallet().getBalances());
    coinControlUpdateLabels();
    ui->customFee->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    updateMinFeeLabel();
    updateSmartFeeLabel();
}

void
SendCoinsDialog::processSendCoinsReturn(const WalletModel::SendCoinsReturn &sendCoinsReturn, const QString &msgArg) {
    QPair <QString, CClientUIInterface::MessageBoxFlags> msgParams;
    // Default to a warning message, override if error message is needed
    msgParams.second = CClientUIInterface::MSG_WARNING;

    // All status values are used only in WalletModel::prepareTransaction()
    switch (sendCoinsReturn.status) {
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
        case WalletModel::AbsurdFee:
            msgParams.first = tr("A fee higher than %1 is considered an absurdly high fee.").arg(
                    BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(),
                                                 model->wallet().getDefaultMaxTxFee()));
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

    Q_EMIT message(tr("Send Coins"), msgParams.first, msgParams.second);
}

void SendCoinsDialog::minimizeFeeSection(bool fMinimize) {
    ui->labelFeeMinimized->setVisible(fMinimize);
    ui->buttonChooseFee->setVisible(fMinimize);
    ui->buttonMinimizeFee->setVisible(!fMinimize);
    ui->frameFeeSelection->setVisible(!fMinimize);
    ui->horizontalLayoutSmartFee->setContentsMargins(0, (fMinimize ? 0 : 6), 0, 0);
    fFeeMinimized = fMinimize;
}

void SendCoinsDialog::on_buttonChooseFee_clicked() {
    minimizeFeeSection(false);
}

void SendCoinsDialog::on_buttonMinimizeFee_clicked() {
    updateFeeMinimizedLabel();
    minimizeFeeSection(true);
}

void SendCoinsDialog::useAvailableBalance(SendCoinsEntry *entry) {
    // Calculate available amount to send.
    CAmount amount = model->wallet().getAvailableBalance(*m_coin_control);
    for (int i = 0; i < ui->entries->count(); ++i) {
        SendCoinsEntry *e = qobject_cast<SendCoinsEntry *>(ui->entries->itemAt(i)->widget());
        if (e && !e->isHidden() && e != entry) {
            amount -= e->getValue().amount;
        }
    }

    if (amount > 0) {
        entry->checkSubtractFeeFromAmount();
        entry->setAmount(amount);
    } else {
        entry->setAmount(0);
    }
}

void SendCoinsDialog::setMinimumFee() {
    ui->customFee->setValue(model->wallet().getRequiredFee(1000));
}

void SendCoinsDialog::updateFeeSectionControls() {
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

void SendCoinsDialog::updateFeeMinimizedLabel() {
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

void SendCoinsDialog::updateMinFeeLabel() {
    if (model && model->getOptionsModel())
        ui->checkBoxMinimumFee->setText(tr("Pay only the required fee of %1").arg(
                BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(),
                                             model->wallet().getRequiredFee(1000)) + "/kB")
        );
}

void SendCoinsDialog::updateCoinControlState(CCoinControl &ctrl) {
    if (ui->radioCustomFee->isChecked()) {
        ctrl.m_feerate = CFeeRate(ui->customFee->value());
    } else {
        ctrl.m_feerate.reset();
    }
    // Avoid using global defaults when sending money from the GUI
    // Either custom fee will be used or if not selected, the confirmation target from dropdown box
    ctrl.m_confirm_target = getConfTargetForIndex(ui->confTargetSelector->currentIndex());
}

void SendCoinsDialog::updateSmartFeeLabel() {
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
void SendCoinsDialog::coinControlClipboardQuantity() {
    GUIUtil::setClipboard(ui->labelCoinControlQuantity->text());
}

// Coin Control: copy label "Amount" to clipboard
void SendCoinsDialog::coinControlClipboardAmount() {
    GUIUtil::setClipboard(ui->labelCoinControlAmount->text().left(ui->labelCoinControlAmount->text().indexOf(" ")));
}

// Coin Control: copy label "Fee" to clipboard
void SendCoinsDialog::coinControlClipboardFee() {
    GUIUtil::setClipboard(
            ui->labelCoinControlFee->text().left(ui->labelCoinControlFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "After fee" to clipboard
void SendCoinsDialog::coinControlClipboardAfterFee() {
    GUIUtil::setClipboard(
            ui->labelCoinControlAfterFee->text().left(ui->labelCoinControlAfterFee->text().indexOf(" ")).replace(
                    ASYMP_UTF8, ""));
}

// Coin Control: copy label "Bytes" to clipboard
void SendCoinsDialog::coinControlClipboardBytes() {
    GUIUtil::setClipboard(ui->labelCoinControlBytes->text().replace(ASYMP_UTF8, ""));
}

// Coin Control: copy label "Dust" to clipboard
void SendCoinsDialog::coinControlClipboardLowOutput() {
    GUIUtil::setClipboard(ui->labelCoinControlLowOutput->text());
}

// Coin Control: copy label "Change" to clipboard
void SendCoinsDialog::coinControlClipboardChange() {
    GUIUtil::setClipboard(
            ui->labelCoinControlChange->text().left(ui->labelCoinControlChange->text().indexOf(" ")).replace(ASYMP_UTF8,
                                                                                                             ""));
}

// Coin Control: settings menu - coin control enabled/disabled by user
void SendCoinsDialog::coinControlFeatureChanged(bool checked) {
    ui->frameCoinControl->setVisible(checked);

    if (!checked && model) { // coin control features disabled
        m_coin_control->SetNull(false);
    }

    coinControlUpdateLabels();
}

// Coin Control: button inputs -> show actual coin control dialog
void SendCoinsDialog::coinControlButtonClicked() {
    CoinControlDialog dlg(*m_coin_control, model, this);
    dlg.exec();
    coinControlUpdateLabels();
}

// Coin Control: checkbox custom change address
void SendCoinsDialog::coinControlChangeChecked(int state) {
    if (state == Qt::Unchecked) {
        m_coin_control->destChange = CNoDestination();
        ui->labelCoinControlChangeLabel->clear();
    } else
        // use this to re-validate an already entered address
        coinControlChangeEdited(ui->lineEditCoinControlChange->text());

    ui->lineEditCoinControlChange->setEnabled((state == Qt::Checked));
}

// Coin Control: custom change address changed
void SendCoinsDialog::coinControlChangeEdited(const QString &text) {
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
void SendCoinsDialog::coinControlUpdateLabels() {
    if (!model || !model->getOptionsModel())
        return;

    updateCoinControlState(*m_coin_control);

    // set pay amounts
    CoinControlDialog::payAmounts.clear();
    CoinControlDialog::fSubtractFeeFromAmount = false;

    for (int i = 0; i < ui->entries->count(); ++i) {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry *>(ui->entries->itemAt(i)->widget());
        if (entry && !entry->isHidden()) {
            SendCoinsRecipient rcp = entry->getValue();
            CoinControlDialog::payAmounts.append(rcp.amount);
            if (rcp.fSubtractFeeFromAmount)
                CoinControlDialog::fSubtractFeeFromAmount = true;
        }
    }

    if (m_coin_control->HasSelected()) {
        // actual coin control calculation
        CoinControlDialog::updateLabels(*m_coin_control, model, this);

        // show coin control stats
        ui->labelCoinControlAutomaticallySelected->hide();
        ui->widgetCoinControl->show();
    } else {
        // hide coin control stats
        ui->labelCoinControlAutomaticallySelected->show();
        ui->widgetCoinControl->hide();
        ui->labelCoinControlInsuffFunds->hide();
    }
}

SendConfirmationDialog::SendConfirmationDialog(const QString &title, const QString &text, int _secDelay,
                                               QWidget *parent) :
        QMessageBox(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::Cancel, parent),
        secDelay(_secDelay) {
    GUIUtil::updateFonts();
    setDefaultButton(QMessageBox::Cancel);
    yesButton = button(QMessageBox::Yes);
    updateYesButton();
    connect(&countDownTimer, &QTimer::timeout, this, &SendConfirmationDialog::countDown);
}

int SendConfirmationDialog::exec() {
    updateYesButton();
    countDownTimer.start(1000);
    return QMessageBox::exec();
}

void SendConfirmationDialog::countDown() {
    secDelay--;
    updateYesButton();

    if (secDelay <= 0) {
        countDownTimer.stop();
    }
}

void SendConfirmationDialog::updateYesButton() {
    if (secDelay > 0) {
        yesButton->setEnabled(false);
        yesButton->setText(tr("Yes") + " (" + QString::number(secDelay) + ")");
    } else {
        yesButton->setEnabled(true);
        yesButton->setText(tr("Yes"));
    }
}
