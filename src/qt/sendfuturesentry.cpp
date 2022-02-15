// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2019 The Dash Core developers
// Copyright (c) 2020-2021 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/sendfuturesentry.h>
#include <qt/forms/ui_sendfuturesentry.h>

#include <qt/addressbookpage.h>
#include <qt/addresstablemodel.h>
#include <qt/bitcoinunits.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/walletmodel.h>
#include <key_io.h>

#include <future/fee.h> // future fee

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QStandardItemModel>
#include <QTableView>

SendFuturesEntry::SendFuturesEntry(QWidget* parent) :
    QStackedWidget(parent),
    ui(new Ui::SendFuturesEntry),
    model(0)
{
    ui->setupUi(this);

    GUIUtil::disableMacFocusRect(this);

    setCurrentWidget(ui->SendFutures);

#if QT_VERSION >= 0x040700
    ui->addAsLabel->setPlaceholderText(tr("Enter a label for this address to add it to your address book"));
#endif

    setButtonIcons();

    // normal raptoreum address field
    GUIUtil::setupAddressWidget(ui->payTo, this, true);

    GUIUtil::setFont({ui->payToLabel,
                     ui->labellLabel,
                     ui->amountLabel,
                     ui->messageLabel}, GUIUtil::FontWeight::Normal, 15);

    GUIUtil::updateFonts();

    //hide unused UI elements for futures
    ui->deleteButton->hide();
    ui->deleteButton_is->hide();
    ui->deleteButton_s->hide();
    ui->checkboxSubtractFeeFromAmount->hide();
    QDateTime defaultDate = QDateTime::currentDateTime();
    defaultDate = defaultDate.addDays(1);
    //FTX Specific form fields
    //Setup maturity locktime datetime field
    ui->ftxLockTime->setDateTime( defaultDate );
    ui->ftxLockTime->setMinimumDate( QDate::currentDate() );

    // Connect signals
    connect(ui->payAmount, SIGNAL(valueChanged()), this, SIGNAL(payAmountChanged()));
    //connect(ui->checkboxSubtractFeeFromAmount, SIGNAL(toggled(bool)), this, SIGNAL(subtractFeeFromAmountChanged()));
    //connect(ui->deleteButton, SIGNAL(clicked()), this, SLOT(deleteClicked()));
    //connect(ui->deleteButton_is, SIGNAL(clicked()), this, SLOT(deleteClicked()));
    //connect(ui->deleteButton_s, SIGNAL(clicked()), this, SLOT(deleteClicked()));

    //Connect signals for future tx pay from field
    connect(ui->payFrom, SIGNAL(currentTextChanged(const QString &)), this, SIGNAL(payFromChanged(const QString &)));
    //Connect signals for FTX maturity fields
    connect(ui->ftxLockTime, SIGNAL(dateTimeChanged (QDateTime)), this, SLOT(updateLockTimeField (QDateTime)));
}

SendFuturesEntry::~SendFuturesEntry()
{
    delete ui;
}

void SendFuturesEntry::on_pasteButton_clicked()
{
    // Paste text from clipboard into recipient field
    ui->payTo->setText(QApplication::clipboard()->text());
}

void SendFuturesEntry::on_addressBookButton_clicked()
{
    if(!model)
        return;
    AddressBookPage dlg(AddressBookPage::ForSelection, AddressBookPage::SendingTab, this);
    dlg.setModel(model->getAddressTableModel());
    if(dlg.exec())
    {
        ui->payTo->setText(dlg.getReturnValue());
        ui->payAmount->setFocus();
    }
}

void SendFuturesEntry::on_payTo_textChanged(const QString &address)
{
    updateLabel(address);
}

void SendFuturesEntry::setModel(WalletModel *_model)
{
    this->model = _model;

    if (_model && _model->getOptionsModel())
    {
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        connect(_model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setupPayFrom()));
    }
    clear();
}

void SendFuturesEntry::clear()
{
    // clear UI elements for normal payment
    ui->payTo->clear();
    ui->addAsLabel->clear();
    ui->payAmount->clear();
    ui->checkboxSubtractFeeFromAmount->setCheckState(Qt::Unchecked);
    ui->messageTextLabel->clear();
    ui->messageTextLabel->hide();
    ui->messageLabel->hide();
    // clear and reset FTX UI elements
    ui->ftxMaturity->setValue(100);
    QDateTime defaultDate = QDateTime::currentDateTime();
    defaultDate = defaultDate.addDays(1);
    //FTX Specific form fields
    //Setup maturity locktime datetime field
    ui->ftxLockTime->setDateTime( defaultDate );
    // clear UI elements for unauthenticated payment request
    ui->payTo_is->clear();
    ui->memoTextLabel_is->clear();
    ui->payAmount_is->clear();
    // clear UI elements for authenticated payment request
    ui->payTo_s->clear();
    ui->memoTextLabel_s->clear();
    ui->payAmount_s->clear();

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void SendFuturesEntry::deleteClicked()
{
    Q_EMIT removeEntry(this);
}

bool SendFuturesEntry::validate(interfaces::Node& node)
{
    if (!model)
        return false;

    // Check input validity
    bool retval = true;

    // Skip checks for payment request
    if (recipient.paymentRequest.IsInitialized())
        return retval;

    if (!model->validateAddress(ui->payFrom->currentText()))
    {
        retval = false;
    }

    if (!model->validateAddress(ui->payTo->text()))
    {
        ui->payTo->setValid(false);
        retval = false;
    }

    if (!ui->payAmount->validate())
    {
        retval = false;
    }

    // Sending a zero amount is invalid
    if (ui->payAmount->value(0) <= 0)
    {
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

SendFuturesRecipient SendFuturesEntry::getValue()
{
    // Payment request
    if (recipient.paymentRequest.IsInitialized())
        return recipient;

    // Normal payment
    recipient.address = ui->payTo->text();
    recipient.label = ui->addAsLabel->text();
    recipient.amount = ui->payAmount->value();
    recipient.message = ui->messageTextLabel->text();
    //recipient.fSubtractFeeFromAmount = (ui->checkboxSubtractFeeFromAmount->checkState() == Qt::Checked);

    //Future TX
    recipient.payFrom = ui->payFrom->currentText();
    recipient.maturity = ui->ftxMaturity->value();
    recipient.locktime = ui->ftxLockTimeField->text().toInt();

    return recipient;
}

QWidget *SendFuturesEntry::setupTabChain(QWidget *prev)
{
    QWidget::setTabOrder(prev, ui->payTo);
    QWidget::setTabOrder(ui->payTo, ui->addAsLabel);
    QWidget *w = ui->payAmount->setupTabChain(ui->addAsLabel);
    QWidget::setTabOrder(w, ui->checkboxSubtractFeeFromAmount);
    QWidget::setTabOrder(ui->checkboxSubtractFeeFromAmount, ui->addressBookButton);
    QWidget::setTabOrder(ui->addressBookButton, ui->pasteButton);
    // Disable delete button for future tx
    //QWidget::setTabOrder(ui->pasteButton, ui->deleteButton);
    //return ui->deleteButton;
    return ui->pasteButton;
}

void SendFuturesEntry::setValue(const SendFuturesRecipient &value)
{
    recipient = value;

    if (recipient.paymentRequest.IsInitialized()) // payment request
    {
        if (recipient.authenticatedMerchant.isEmpty()) // unauthenticated
        {
            ui->payTo_is->setText(recipient.address);
            ui->memoTextLabel_is->setText(recipient.message);
            ui->payAmount_is->setValue(recipient.amount);
            ui->payAmount_is->setReadOnly(true);
            setCurrentWidget(ui->SendCoins_UnauthenticatedPaymentRequest);
        }
        else // authenticated
        {
            ui->payTo_s->setText(recipient.authenticatedMerchant);
            ui->memoTextLabel_s->setText(recipient.message);
            ui->payAmount_s->setValue(recipient.amount);
            ui->payAmount_s->setReadOnly(true);
            setCurrentWidget(ui->SendCoins_AuthenticatedPaymentRequest);
        }
    }
    else // normal payment
    {
        // message
        ui->messageTextLabel->setText(recipient.message);
        ui->messageTextLabel->setVisible(!recipient.message.isEmpty());
        ui->messageLabel->setVisible(!recipient.message.isEmpty());

        ui->payTo->setText(recipient.address);
        ui->addAsLabel->setText(recipient.label);
        ui->payAmount->setValue(recipient.amount);
    }

    updateLabel(recipient.address);
}

void SendFuturesEntry::setAddress(const QString &address)
{
    ui->payTo->setText(address);
    ui->payAmount->setFocus();
}

bool SendFuturesEntry::isClear()
{
    return ui->payTo->text().isEmpty() && ui->payTo_is->text().isEmpty() && ui->payTo_s->text().isEmpty();
}

void SendFuturesEntry::setFocus()
{
    ui->payTo->setFocus();
}

void SendFuturesEntry::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        // Update payAmount with the current unit
        ui->payAmount->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
        ui->payAmount_is->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
        ui->payAmount_s->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
        setupPayFrom();
    }
}

void SendFuturesEntry::changeEvent(QEvent* e)
{
  QStackedWidget::changeEvent(e);
  if(e->type() == QEvent::StyleChange)
  {
    setButtonIcons();
  }
}

void SendFuturesEntry::setButtonIcons()
{
  GUIUtil::setIcon(ui->addressBookButton, "address-book");
  GUIUtil::setIcon(ui->pasteButton, "editpaste");
  GUIUtil::setIcon(ui->deleteButton, "remove", GUIUtil::ThemedColor::RED);
  GUIUtil::setIcon(ui->deleteButton_is, "remove", GUIUtil::ThemedColor::RED);
  GUIUtil::setIcon(ui->deleteButton_s, "remove", GUIUtil::ThemedColor::RED);
}

bool SendFuturesEntry::updateLabel(const QString &address)
{
    if(!model)
        return false;

    // Fill in label from address book, if address has an associated label
    QString associatedLabel = model->getAddressTableModel()->labelForAddress(address);
    if(!associatedLabel.isEmpty())
    {
        ui->addAsLabel->setText(associatedLabel);
        return true;
    }

    return false;
}

//Calculate Future tx locktime from QDateTime field
void SendFuturesEntry::updateLockTimeField(const QDateTime & dateTime)
{
    QDateTime currentDateTime = QDateTime::currentDateTime();
    //Calculate seconds from now to the chosen datetime value
    qint64 futureTime = currentDateTime.secsTo(dateTime) > 0 ? currentDateTime.secsTo(dateTime) : -1;
    QString int_string = QString::number(futureTime);
    
    //set the seconds in this field for handling
    ui->ftxLockTimeField->setText(int_string);
}

//Future coin control: update combobox
void SendFuturesEntry::setupPayFrom()
{
    if (!model || !model->getOptionsModel())
        return;

    CAmount futureFee = getFutureFees();
    CAmount nMinAmount = futureFee;

    std::map<CTxDestination, CAmount> balances = model->getAddressBalances();

    if(balances.empty())
    {
        ui->payFrom->setDisabled(true);
        return;
    }

    int selected = ui->payFrom->currentIndex() > 0 ? ui->payFrom->currentIndex() : 0;

    //Build table for dropdown
    QStandardItemModel *itemModel = new QStandardItemModel( this );
    QStringList horzHeaders;
    horzHeaders << "Address" << "Label" << BitcoinUnits::getAmountColumnTitle(model->getOptionsModel()->getDisplayUnit());

    QList<QStandardItem *> placeholder;
    placeholder.append(new QStandardItem( "Select a Raptoreum address" ) );
    itemModel->appendRow(placeholder);

    #define SORT_ROLE Qt::UserRole + 1

    for (auto& balance : balances) {
        const CTxDestination dest = balance.first;
        QString walletAddr = QString::fromStdString(EncodeDestination(dest));
        //QString walletBalance = BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), balance.second);
        QString walletLabel = model->getAddressTableModel()->labelForAddress(walletAddr);
        if (balance.second >= nMinAmount) {
            QList<QStandardItem *> items;

            QStandardItem *walletAddress = new QStandardItem();
            walletAddress->setText(GUIUtil::HtmlEscape(walletAddr));
            walletAddress->setData(GUIUtil::HtmlEscape(walletAddr), SORT_ROLE);

            QStandardItem *walletAddressLabel = new QStandardItem();
            walletAddressLabel->setText(walletLabel);
            walletAddressLabel->setData(walletLabel, SORT_ROLE);

            QStandardItem *balanceAmount = new QStandardItem();
            balanceAmount->setText(BitcoinUnits::format(model->getOptionsModel()->getDisplayUnit(), balance.second, false, BitcoinUnits::separatorAlways));
            balanceAmount->setData(qlonglong(balance.second), SORT_ROLE);

            items.append(walletAddress);
            items.append(walletAddressLabel);
            items.append(balanceAmount);
            itemModel->appendRow(items);
        }
    }

    itemModel->setHorizontalHeaderLabels( horzHeaders );
    itemModel->setSortRole(SORT_ROLE);
    //Table settings
    QTableView* tableView = new QTableView( this );
    tableView->setObjectName("payFromTable");
    tableView->setModel( itemModel );
    tableView->resizeColumnsToContents();
    tableView->setColumnWidth(1,160);
    tableView->horizontalHeader()->setStretchLastSection(true);
    tableView->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    tableView->setSortingEnabled(true);
    tableView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    tableView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView->setAutoScroll(true);
    tableView->hideRow(0);

    ui->payFrom->setModel( itemModel );
    ui->payFrom->setView( tableView );

    ui->payFrom->setCurrentIndex(selected);

}
