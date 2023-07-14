// Copyright (c) 2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/sendassetsentry.h>
#include <qt/forms/ui_sendassetsentry.h>

#include <qt/addressbookpage.h>
#include <qt/addresstablemodel.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <future/fee.h>
#include <qt/bitcoinunits.h>
#include <assets/assets.h>
#include <assets/assetstype.h>
#include <validation.h>

#include <QApplication>
#include <QClipboard>
#include <QString>
#include <QStringListModel>
#include <QSortFilterProxyModel>
#include <QCompleter>
#include <QLineEdit>

SendAssetsEntry::SendAssetsEntry(QWidget *parent, bool hideFuture) :
        QStackedWidget(parent),
        ui(new Ui::SendAssetsEntry),
        model(0) {
    ui->setupUi(this);

    GUIUtil::disableMacFocusRect(this);

    setCurrentWidget(ui->SendAssets);

    ui->addAsLabel->setPlaceholderText(tr("Enter a label for this address to add it to your address book"));

    setButtonIcons();

    // normal raptoreum address field
    GUIUtil::setupAddressWidget(ui->payTo, this, true);

    GUIUtil::setFont({ui->payToLabel,
                      ui->Assetlabel,
                      ui->labellLabel,
                      ui->amountLabel,
                      ui->maturityLb,
                      ui->locktimeLb}, GUIUtil::FontWeight::Normal, 15);

    GUIUtil::updateFonts();
    this->futureToggleChanged();
    ui->futureCb->setVisible(hideFuture);
    ui->maturity->setValidator(new QIntValidator(-1, INT_MAX, this));
    ui->locktime->setValidator(new QIntValidator(-1, INT_MAX, this));
    // Connect signals
    connect(ui->payAmount, SIGNAL(valueChanged()), this, SIGNAL(payAmountChanged()));
    connect(ui->deleteButton, SIGNAL(clicked()), this, SLOT(deleteClicked()));
    connect(ui->useAvailableBalanceButton, SIGNAL(clicked()), this, SLOT(useAvailableAssetsBalanceClicked()));
    connect(ui->futureCb, SIGNAL(toggled(bool)), this, SLOT(futureToggleChanged()));

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

    //uniqueid selection
    stringModelId = new QStringListModel;

    proxyId = new QSortFilterProxyModel;
    proxyId->setSourceModel(stringModelId);

    ui->uniqueIdList->setModel(proxyId);
    ui->uniqueIdList->setEditable(false);
}

SendAssetsEntry::~SendAssetsEntry() {
    delete ui;
}

void SendAssetsEntry::on_pasteButton_clicked() {
    // Paste text from clipboard into recipient field
    ui->payTo->setText(QApplication::clipboard()->text());
}

void SendAssetsEntry::on_addressBookButton_clicked() {
    if (!model)
        return;
    AddressBookPage dlg(AddressBookPage::ForSelection, AddressBookPage::SendingTab, this);
    dlg.setModel(model->getAddressTableModel());
    if (dlg.exec()) {
        ui->payTo->setText(dlg.getReturnValue());
        ui->payAmount->setFocus();
    }
}

void SendAssetsEntry::on_payTo_textChanged(const QString &address) {
    SendCoinsRecipient rcp;
    if (GUIUtil::parseBitcoinURI(address, &rcp)) {
        ui->payTo->blockSignals(true);
        setValue(rcp);
        ui->payTo->blockSignals(false);
    } else {
        updateLabel(address);
    }
}

void SendAssetsEntry::setModel(WalletModel *_model) {
    this->model = _model;

    if (_model && _model->getOptionsModel())
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

    clear();
}

void SendAssetsEntry::setCoinControl(CCoinControl *coin_control) {
    this->m_coin_control = coin_control;
}

void SendAssetsEntry::clear() {
    // clear UI elements for normal payment
    ui->payTo->clear();
    ui->addAsLabel->clear();
    ui->payAmount->clear();
    ui->maturity->setText("100");
    ui->locktime->setText("100");
    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void SendAssetsEntry::futureToggleChanged() {
    bool isFuture = ui->futureCb->isChecked();
    if (isFuture) {
        char feeDisplay[18];
        sprintf(feeDisplay, "%d RTM", getFutureFees());
        ui->feeDisplay->setText(feeDisplay);
    }
    ui->maturityLb->setVisible(isFuture);
    ui->maturity->setVisible(isFuture);
    ui->locktime->setVisible(isFuture);
    ui->locktimeLb->setVisible(isFuture);
    ui->feeDisplay->setVisible(isFuture);
    ui->feeLb->setVisible(isFuture);
}

void SendAssetsEntry::deleteClicked() {
    Q_EMIT removeEntry(this);
}

void SendAssetsEntry::useAvailableAssetsBalanceClicked() {
    Q_EMIT useAvailableAssetsBalance(this);
}

bool SendAssetsEntry::validate(interfaces::Node &node) {
    if (!model)
        return false;

    // Check input validity
    bool retval = true;

    if (!model->validateAddress(ui->payTo->text())) {
        ui->payTo->setValid(false);
        retval = false;
    }

    if (ui->assetList->currentIndex() <= 0) {
        retval = false;
    }

    if (uniqueAssetSelected && ui->assetList->currentIndex() <= 0) {
        retval = false;
    }

    if (!ui->payAmount->validate()) {
        retval = false;
    }

    // Sending a zero amount is invalid
    if (ui->payAmount->value(0) <= 0) {
        ui->payAmount->setValid(false);
        retval = false;
    }

    std::string assetId;
    passetsCache->GetAssetId(ui->assetList->currentText().toStdString(), assetId);
    if (!validateAmount(assetId, ui->payAmount->value())) {
        ui->payAmount->setValid(false);
        retval = false;
    }

    // Reject dust outputs:
    if (retval && GUIUtil::isDust(node, ui->payTo->text(), ui->payAmount->value())) {
        ui->payAmount->setValid(false);
        retval = false;
    }

    return retval;
}

SendCoinsRecipient SendAssetsEntry::getValue() {
    // Normal payment
    recipient.assetId = ui->assetList->currentText();
    recipient.address = ui->payTo->text();
    recipient.label = ui->addAsLabel->text();
    CAmount amount = ui->payAmount->value();
    recipient.amount = 0;
    recipient.assetAmount = amount;
    recipient.fSubtractFeeFromAmount = false;
    if (uniqueAssetSelected)
        recipient.uniqueId = ui->uniqueIdList->currentText().toInt();
    else
        recipient.uniqueId = MAX_UNIQUE_ID;
    //std::cout << " ui->futureCb->isChecked() " << ui->futureCb->isChecked() << "\n";
    if (ui->futureCb->isChecked()) {
        recipient.isFutureOutput = true;
        recipient.maturity = ui->maturity->text().isEmpty() ? -1 : std::stoi(ui->maturity->text().toStdString());
        recipient.locktime = ui->locktime->text().isEmpty() ? -1 : std::stol(ui->locktime->text().toStdString());
    } else {
        recipient.isFutureOutput = false;
        recipient.maturity = -1;
        recipient.locktime = -1;
    }
    return recipient;
}

QWidget *SendAssetsEntry::setupTabChain(QWidget *prev) {
    QWidget::setTabOrder(prev, ui->payTo);
    QWidget::setTabOrder(ui->payTo, ui->addAsLabel);
    QWidget::setTabOrder(ui->addressBookButton, ui->pasteButton);
    QWidget::setTabOrder(ui->pasteButton, ui->deleteButton);
    return ui->deleteButton;
}

void SendAssetsEntry::setValue(const SendCoinsRecipient &value) {
    recipient = value;

    // normal payment
    ui->payTo->setText(recipient.address);
    ui->addAsLabel->setText(recipient.label);
    ui->payAmount->setValue(recipient.amount);

    updateLabel(recipient.address);
}

void SendAssetsEntry::setAddress(const QString &address) {
    ui->payTo->setText(address);
    ui->payAmount->setFocus();
}

void SendAssetsEntry::setAmount(const CAmount &amount) {
    ui->payAmount->setValue(amount);
}

bool SendAssetsEntry::isClear() {
    return ui->payTo->text().isEmpty();
}

void SendAssetsEntry::setFocus() {
    ui->payTo->setFocus();
}

void SendAssetsEntry::updateDisplayUnit() {
    if (model && model->getOptionsModel()) {
        // Update payAmount with the current unit
        ui->payAmount->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    }
}

void SendAssetsEntry::changeEvent(QEvent *e) {
    QStackedWidget::changeEvent(e);
    if (e->type() == QEvent::StyleChange) {
        // Adjust button icon colors on theme changes
        setButtonIcons();
    }
}

void SendAssetsEntry::setButtonIcons() {
    GUIUtil::setIcon(ui->addressBookButton, "address-book");
    GUIUtil::setIcon(ui->pasteButton, "editpaste");
    GUIUtil::setIcon(ui->deleteButton, "remove", GUIUtil::ThemedColor::RED);
}

bool SendAssetsEntry::updateLabel(const QString &address) {
    if (!model)
        return false;

    // Fill in label from address book, if address has an associated label
    QString associatedLabel = model->getAddressTableModel()->labelForAddress(address);
    if (!associatedLabel.isEmpty()) {
        ui->addAsLabel->setText(associatedLabel);
        return true;
    }

    return false;
}

void SendAssetsEntry::SetFutureVisible(bool visible) {
    if (!visible) {
        ui->futureCb->setChecked(false);
    }
    futureToggleChanged();
    ui->futureCb->setVisible(visible);
}

void SendAssetsEntry::ClearAssetOptions() {
    ui->AssetBalance->setText(tr("Select an asset to see the balance"));
    ui->payAmount->setVisible(true);
    ui->payAmount->clear();
    ui->payAmount->setAssetsUnit(MAX_ASSET_UNITS);
    //hide unique list
    ui->uniqueIdList->setVisible(false);
    ui->assetList->setFocus();
}

void SendAssetsEntry::onAssetSelected(QString name) {
    static QString prevname = "";

    //when updating asset list it triggers onAssetSelected which needs to be ignored if assetName is not empty
    if (name == "" || assetName.length() > 0)
        return;

    if (ui->assetList->currentIndex() <= 0) {
        ClearAssetOptions();
        prevname.clear();
        return;
    }

    std::map <std::string, CAmount> assetsbalance = model->wallet().getAssetsBalance();
    CAmount bal = 0;
    std::string assetId;
    if (passetsCache->GetAssetId(name.toStdString(), assetId)) {
        if (assetsbalance.count(assetId))
            bal = assetsbalance[assetId];
    }

    //update balance
    ui->AssetBalance->setText(BitcoinUnits::formatWithCustomName(name, bal));

    CAssetMetaData assetdata;
    if (!passetsCache->GetAssetMetaData(assetId, assetdata))
        return;

    if (assetdata.isUnique) {

        uniqueAssetSelected = true;
        ui->payAmount->setAssetsUnit(0);
        ui->payAmount->setValue(1);
        ui->payAmount->setVisible(false);
        ui->useAvailableBalanceButton->setEnabled(false);
        ui->uniqueIdList->setVisible(true);
        ui->amountLabel->setText("U&niqueId:");

        //make a copy of current selected uniqueId to restore after
        //updating the list of uniqueId
        int index = ui->uniqueIdList->currentIndex();
        QString uniqueId = ui->uniqueIdList->currentText();

        //update the unique list
        std::vector <uint16_t> assetsIds = model->wallet().listAssetUniqueId(assetId, m_coin_control);
        QStringList list;
        for (auto uniqueid: assetsIds) {
            list << QString::number(uniqueid);
        }

        stringModelId->setStringList(list);

        //restore selected assetId
        if (index >= 0 && name == prevname) {
            index = ui->uniqueIdList->findText(uniqueId);
            ui->uniqueIdList->setCurrentIndex(index);
        } else {
            ui->uniqueIdList->setCurrentIndex(0);
            ui->uniqueIdList->activated(0);
        }
    } else {

        uniqueAssetSelected = false;
        ui->amountLabel->setText("A&mount:");
        ui->payAmount->setVisible(true);
        ui->useAvailableBalanceButton->setEnabled(true);
        ui->uniqueIdList->setVisible(false);

        if (name != prevname) {
            ui->payAmount->setAssetsUnit(assetdata.decimalPoint);
            ui->payAmount->setValue(0);
        }
    }
    prevname = name;
}

void SendAssetsEntry::updateAssetList() {
    //make a copy of current selected asset to restore selection after
    //updating the list of available assets
    int index = ui->assetList->currentIndex();
    assetName = ui->assetList->currentText();

    // Get available assets list
    std::vector <std::string> assets = model->wallet().listMyAssets(m_coin_control);

    QStringList list;
    list << "Select an asset";
    for (auto assetId: assets) {
        CAssetMetaData assetData;
        if (passetsCache->GetAssetMetaData(assetId, assetData)) {
            list << QString::fromStdString(assetData.name);
        }
    }

    //update the list
    stringModel->setStringList(list);

    //restore selected asset
    if (index >= 1) {
        index = ui->assetList->findText(assetName);
        assetName.clear();
        ui->assetList->setCurrentIndex(index);
    } else {
        ClearAssetOptions();
        assetName.clear();
    }   
}
