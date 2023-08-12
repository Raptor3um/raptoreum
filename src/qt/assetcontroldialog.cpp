// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/assetcontroldialog.h>
#include <qt/forms/ui_assetcontroldialog.h>

#include <qt/addresstablemodel.h>
#include <qt/bitcoinunits.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <txmempool.h>
#include <qt/walletmodel.h>

#include <wallet/coincontrol.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <policy/fees.h>
#include <policy/policy.h>
#include <validation.h> // For mempool
#include <wallet/fees.h>
#include <wallet/wallet.h>
#include <assets/assets.h>
#include <assets/assetstype.h>

#include <QApplication>
#include <QCheckBox>
#include <QCursor>
#include <QDialogButtonBox>
#include <QFlags>
#include <QIcon>
#include <QSettings>
#include <QString>
#include <QTreeWidget>
#include <QStringListModel>
#include <QSortFilterProxyModel>
#include <QCompleter>
#include <QLineEdit>

QList <CAmount> AssetControlDialog::payAmounts;
bool AssetControlDialog::fSubtractFeeFromAmount = false;

bool CAssetControlWidgetItem::operator<(const QTreeWidgetItem &other) const {
    int column = treeWidget()->sortColumn();
    if (column == AssetControlDialog::COLUMN_AMOUNT || column == AssetControlDialog::COLUMN_DATE ||
        column == AssetControlDialog::COLUMN_CONFIRMATIONS)
        return data(column, Qt::UserRole).toLongLong() < other.data(column, Qt::UserRole).toLongLong();
    return QTreeWidgetItem::operator<(other);
}

AssetControlDialog::AssetControlDialog(CCoinControl &coin_control, WalletModel *_model, QWidget *parent) :
        QDialog(parent),
        ui(new Ui::AssetControlDialog),
        m_coin_control(coin_control),
        model(_model) {
    ui->setupUi(this);

    GUIUtil::setFont({ui->labelAssetControlQuantityText,
                      ui->labelAssetControlBytesText,
                      ui->labelAssetControlAmountText,
                      ui->labelAssetControlLowOutputText,
                      ui->labelAssetControlFeeText,
                      ui->labelAssetControlAfterFeeText,
                      ui->labelAssetControlChangeText
                     }, GUIUtil::FontWeight::Bold);

    GUIUtil::updateFonts();

    GUIUtil::disableMacFocusRect(this);

    // context menu actions
    QAction *copyAddressAction = new QAction(tr("Copy address"), this);
    QAction *copyLabelAction = new QAction(tr("Copy label"), this);
    QAction *copyAmountAction = new QAction(tr("Copy amount"), this);
    copyTransactionHashAction = new QAction(tr("Copy transaction ID"), this);  // we need to enable/disable this
    lockAction = new QAction(tr("Lock unspent"), this);                        // we need to enable/disable this
    unlockAction = new QAction(tr("Unlock unspent"), this);                    // we need to enable/disable this

    // context menu
    contextMenu = new QMenu(this);
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(copyAmountAction);
    contextMenu->addAction(copyTransactionHashAction);
    contextMenu->addSeparator();
    contextMenu->addAction(lockAction);
    contextMenu->addAction(unlockAction);

    // context menu signals
    connect(ui->treeWidget, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showMenu(QPoint)));
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(copyAddress()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(copyLabel()));
    connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(copyAmount()));
    connect(copyTransactionHashAction, SIGNAL(triggered()), this, SLOT(copyTransactionHash()));
    connect(lockAction, SIGNAL(triggered()), this, SLOT(lockAsset()));
    connect(unlockAction, SIGNAL(triggered()), this, SLOT(unlockAsset()));

    // clipboard actions
    QAction *clipboardQuantityAction = new QAction(tr("Copy quantity"), this);
    QAction *clipboardAmountAction = new QAction(tr("Copy amount"), this);
    QAction *clipboardFeeAction = new QAction(tr("Copy fee"), this);
    QAction *clipboardAfterFeeAction = new QAction(tr("Copy after fee"), this);
    QAction *clipboardBytesAction = new QAction(tr("Copy bytes"), this);
    QAction *clipboardLowOutputAction = new QAction(tr("Copy dust"), this);
    QAction *clipboardChangeAction = new QAction(tr("Copy change"), this);

    connect(clipboardQuantityAction, SIGNAL(triggered()), this, SLOT(clipboardQuantity()));
    connect(clipboardAmountAction, SIGNAL(triggered()), this, SLOT(clipboardAmount()));
    connect(clipboardFeeAction, SIGNAL(triggered()), this, SLOT(clipboardFee()));
    connect(clipboardAfterFeeAction, SIGNAL(triggered()), this, SLOT(clipboardAfterFee()));
    connect(clipboardBytesAction, SIGNAL(triggered()), this, SLOT(clipboardBytes()));
    connect(clipboardLowOutputAction, SIGNAL(triggered()), this, SLOT(clipboardLowOutput()));
    connect(clipboardChangeAction, SIGNAL(triggered()), this, SLOT(clipboardChange()));

    ui->labelAssetControlQuantity->addAction(clipboardQuantityAction);
    ui->labelAssetControlAmount->addAction(clipboardAmountAction);
    ui->labelAssetControlFee->addAction(clipboardFeeAction);
    ui->labelAssetControlAfterFee->addAction(clipboardAfterFeeAction);
    ui->labelAssetControlBytes->addAction(clipboardBytesAction);
    ui->labelAssetControlLowOutput->addAction(clipboardLowOutputAction);
    ui->labelAssetControlChange->addAction(clipboardChangeAction);

    // toggle tree/list mode
    connect(ui->radioTreeMode, SIGNAL(toggled(bool)), this, SLOT(radioTreeMode(bool)));
    connect(ui->radioListMode, SIGNAL(toggled(bool)), this, SLOT(radioListMode(bool)));

    // click on checkbox
    connect(ui->treeWidget, SIGNAL(itemChanged(QTreeWidgetItem * , int)), this,
            SLOT(viewItemChanged(QTreeWidgetItem * , int)));

    // click on header
    ui->treeWidget->header()->setSectionsClickable(true);
    connect(ui->treeWidget->header(), SIGNAL(sectionClicked(int)), this, SLOT(headerSectionClicked(int)));

    // ok button
    connect(ui->buttonBox, SIGNAL(clicked(QAbstractButton * )), this, SLOT(buttonBoxClicked(QAbstractButton * )));

    // (un)select all
    connect(ui->pushButtonSelectAll, SIGNAL(clicked()), this, SLOT(buttonSelectAllClicked()));

    ui->treeWidget->setColumnWidth(COLUMN_CHECKBOX, 94);
    ui->treeWidget->setColumnWidth(COLUMN_NAME, 250);
    ui->treeWidget->setColumnWidth(COLUMN_AMOUNT, 100);
    ui->treeWidget->setColumnWidth(COLUMN_LABEL, 170);
    ui->treeWidget->setColumnWidth(COLUMN_DATE, 120);
    ui->treeWidget->setColumnWidth(COLUMN_CONFIRMATIONS, 110);

    ui->treeWidget->header()->setStretchLastSection(false);
    ui->treeWidget->header()->setSectionResizeMode(COLUMN_ADDRESS, QHeaderView::Stretch);

    // default view is sorted by amount desc
    sortView(COLUMN_AMOUNT, Qt::DescendingOrder);

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

    // restore list mode and sortorder as a convenience feature
    QSettings settings;
    if (settings.contains("nAssetControlMode") && !settings.value("nAssetControlMode").toBool())
        ui->radioTreeMode->click();
    if (settings.contains("nAssetControlSortColumn") && settings.contains("nAssetControlSortOrder"))
        sortView(settings.value("nAssetControlSortColumn").toInt(),
                 (static_cast<Qt::SortOrder>(settings.value("nAssetControlSortOrder").toInt())));

    if (_model->getOptionsModel() && _model->getAddressTableModel()) {
        updateView();
        updateAssetList();
        updateLabelLocked();
        AssetControlDialog::updateLabels(m_coin_control, _model, this);
    }
}

AssetControlDialog::~AssetControlDialog() {
    QSettings settings;
    settings.setValue("nAssetControlMode", ui->radioListMode->isChecked());
    settings.setValue("nAssetControlSortColumn", sortColumn);
    settings.setValue("nAssetControlSortOrder", (int) sortOrder);

    delete ui;
}

// ok button
void AssetControlDialog::buttonBoxClicked(QAbstractButton *button) {
    if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole)
        Q_EMIT
    model->assetListChanged();//emit signal to refresh list of asset
    done(QDialog::Accepted); // closes the dialog
}

// (un)select all
void AssetControlDialog::buttonSelectAllClicked() {
    Qt::CheckState state = Qt::Checked;
    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++) {
        if (ui->treeWidget->topLevelItem(i)->checkState(COLUMN_CHECKBOX) != Qt::Unchecked) {
            state = Qt::Unchecked;
            break;
        }
    }
    ui->treeWidget->setEnabled(false);
    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++)
        if (ui->treeWidget->topLevelItem(i)->checkState(COLUMN_CHECKBOX) != state)
            ui->treeWidget->topLevelItem(i)->setCheckState(COLUMN_CHECKBOX, state);
    ui->treeWidget->setEnabled(true);
    if (state == Qt::Unchecked)
        m_coin_control.UnSelectAll(); // just to be sure
    AssetControlDialog::updateLabels(m_coin_control, model, this);
}

// Toggle lock state
void AssetControlDialog::buttonToggleLockClicked() {
    QTreeWidgetItem *item;
    // Works in list-mode only
    if (ui->radioListMode->isChecked()) {
        ui->treeWidget->setEnabled(false);
        for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++) {
            item = ui->treeWidget->topLevelItem(i);
            COutPoint outpt(uint256S(item->data(COLUMN_ADDRESS, TxHashRole).toString().toStdString()),
                            item->data(COLUMN_ADDRESS, VOutRole).toUInt());
            if (model->wallet().isLockedCoin(outpt)) {
                model->wallet().unlockCoin(outpt);
                item->setDisabled(false);
                item->setIcon(COLUMN_CHECKBOX, QIcon());
            } else {
                model->wallet().lockCoin(outpt);
                item->setDisabled(true);
                item->setIcon(COLUMN_CHECKBOX, GUIUtil::getIcon("lock_closed", GUIUtil::ThemedColor::RED));
            }
            updateLabelLocked();
        }
        ui->treeWidget->setEnabled(true);
        AssetControlDialog::updateLabels(m_coin_control, model, this);
    } else {
        QMessageBox msgBox(this);
        msgBox.setObjectName("lockMessageBox");
        msgBox.setText(tr("Please switch to \"List mode\" to use this function."));
        msgBox.exec();
    }
}

// context menu
void AssetControlDialog::showMenu(const QPoint &point) {
    QTreeWidgetItem *item = ui->treeWidget->itemAt(point);
    if (item) {
        contextMenuItem = item;

        // disable some items (like Copy Transaction ID, lock, unlock) for tree roots in context menu
        if (item->data(COLUMN_ADDRESS, TxHashRole).toString().length() ==
            64) // transaction hash is 64 characters (this means it is a child node, so it is not a parent node in tree mode)
        {
            copyTransactionHashAction->setEnabled(true);
            if (model->wallet().isLockedCoin(
                    COutPoint(uint256S(item->data(COLUMN_ADDRESS, TxHashRole).toString().toStdString()),
                              item->data(COLUMN_ADDRESS, VOutRole).toUInt()))) {
                lockAction->setEnabled(false);
                unlockAction->setEnabled(true);
            } else {
                lockAction->setEnabled(true);
                unlockAction->setEnabled(false);
            }
        } else // this means click on parent node in tree mode -> disable all
        {
            copyTransactionHashAction->setEnabled(false);
            lockAction->setEnabled(false);
            unlockAction->setEnabled(false);
        }

        // show context menu
        contextMenu->exec(QCursor::pos());
    }
}

// context menu action: copy amount
void AssetControlDialog::copyAmount() {
    GUIUtil::setClipboard(BitcoinUnits::removeSpaces(contextMenuItem->text(COLUMN_AMOUNT)));
}

// context menu action: copy label
void AssetControlDialog::copyLabel() {
    if (ui->radioTreeMode->isChecked() && contextMenuItem->text(COLUMN_LABEL).length() == 0 &&
        contextMenuItem->parent())
        GUIUtil::setClipboard(contextMenuItem->parent()->text(COLUMN_LABEL));
    else
        GUIUtil::setClipboard(contextMenuItem->text(COLUMN_LABEL));
}

// context menu action: copy address
void AssetControlDialog::copyAddress() {
    if (ui->radioTreeMode->isChecked() && contextMenuItem->text(COLUMN_ADDRESS).length() == 0 &&
        contextMenuItem->parent())
        GUIUtil::setClipboard(contextMenuItem->parent()->text(COLUMN_ADDRESS));
    else
        GUIUtil::setClipboard(contextMenuItem->text(COLUMN_ADDRESS));
}

// context menu action: copy transaction id
void AssetControlDialog::copyTransactionHash() {
    GUIUtil::setClipboard(contextMenuItem->data(COLUMN_ADDRESS, TxHashRole).toString());
}

// context menu action: lock Asset
void AssetControlDialog::lockAsset() {
    if (contextMenuItem->checkState(COLUMN_CHECKBOX) == Qt::Checked)
        contextMenuItem->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);

    COutPoint outpt(uint256S(contextMenuItem->data(COLUMN_ADDRESS, TxHashRole).toString().toStdString()),
                    contextMenuItem->data(COLUMN_ADDRESS, VOutRole).toUInt());
    model->wallet().lockCoin(outpt);
    contextMenuItem->setDisabled(true);
    contextMenuItem->setIcon(COLUMN_CHECKBOX, GUIUtil::getIcon("lock_closed", GUIUtil::ThemedColor::RED));
    updateLabelLocked();
}

// context menu action: unlock Asset
void AssetControlDialog::unlockAsset() {
    COutPoint outpt(uint256S(contextMenuItem->data(COLUMN_ADDRESS, TxHashRole).toString().toStdString()),
                    contextMenuItem->data(COLUMN_ADDRESS, VOutRole).toUInt());
    model->wallet().unlockCoin(outpt);
    contextMenuItem->setDisabled(false);
    contextMenuItem->setIcon(COLUMN_CHECKBOX, QIcon());
    updateLabelLocked();
}

// copy label "Quantity" to clipboard
void AssetControlDialog::clipboardQuantity() {
    GUIUtil::setClipboard(ui->labelAssetControlQuantity->text());
}

// copy label "Amount" to clipboard
void AssetControlDialog::clipboardAmount() {
    GUIUtil::setClipboard(ui->labelAssetControlAmount->text().left(ui->labelAssetControlAmount->text().indexOf(" ")));
}

// copy label "Fee" to clipboard
void AssetControlDialog::clipboardFee() {
    GUIUtil::setClipboard(
            ui->labelAssetControlFee->text().left(ui->labelAssetControlFee->text().indexOf(" ")).replace(ASYMP_UTF8,
                                                                                                         ""));
}

// copy label "After fee" to clipboard
void AssetControlDialog::clipboardAfterFee() {
    GUIUtil::setClipboard(
            ui->labelAssetControlAfterFee->text().left(ui->labelAssetControlAfterFee->text().indexOf(" ")).replace(
                    ASYMP_UTF8, ""));
}

// copy label "Bytes" to clipboard
void AssetControlDialog::clipboardBytes() {
    GUIUtil::setClipboard(ui->labelAssetControlBytes->text().replace(ASYMP_UTF8, ""));
}

// copy label "Dust" to clipboard
void AssetControlDialog::clipboardLowOutput() {
    GUIUtil::setClipboard(ui->labelAssetControlLowOutput->text());
}

// copy label "Change" to clipboard
void AssetControlDialog::clipboardChange() {
    GUIUtil::setClipboard(
            ui->labelAssetControlChange->text().left(ui->labelAssetControlChange->text().indexOf(" ")).replace(
                    ASYMP_UTF8, ""));
}

// treeview: sort
void AssetControlDialog::sortView(int column, Qt::SortOrder order) {
    sortColumn = column;
    sortOrder = order;
    ui->treeWidget->sortItems(column, order);
    ui->treeWidget->header()->setSortIndicator(sortColumn, sortOrder);
}

// treeview: clicked on header
void AssetControlDialog::headerSectionClicked(int logicalIndex) {
    if (logicalIndex == COLUMN_CHECKBOX) // click on most left column -> do nothing
    {
        ui->treeWidget->header()->setSortIndicator(sortColumn, sortOrder);
    } else {
        if (sortColumn == logicalIndex)
            sortOrder = ((sortOrder == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder);
        else {
            sortColumn = logicalIndex;
            sortOrder = ((sortColumn == COLUMN_NAME || sortColumn == COLUMN_LABEL || sortColumn == COLUMN_ADDRESS)
                         ? Qt::AscendingOrder
                         : Qt::DescendingOrder); // if label or address then default => asc, else default => desc
        }

        sortView(sortColumn, sortOrder);
    }
}

// toggle tree mode
void AssetControlDialog::radioTreeMode(bool checked) {
    if (checked && model)
        updateView();
}

// toggle list mode
void AssetControlDialog::radioListMode(bool checked) {
    if (checked && model)
        updateView();
}

// checkbox clicked by user
void AssetControlDialog::viewItemChanged(QTreeWidgetItem *item, int column) {
    if (column == COLUMN_CHECKBOX && item->data(COLUMN_ADDRESS, TxHashRole).toString().length() ==
                                     64) // transaction hash is 64 characters (this means it is a child node, so it is not a parent node in tree mode)
    {
        COutPoint outpt(uint256S(item->data(COLUMN_ADDRESS, TxHashRole).toString().toStdString()),
                        item->data(COLUMN_ADDRESS, VOutRole).toUInt());

        if (item->checkState(COLUMN_CHECKBOX) == Qt::Unchecked)
            m_coin_control.UnSelectAsset(outpt);
        else if (item->isDisabled()) // locked (this happens if "check all" through parent node)
            item->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
        else {
            m_coin_control.SelectAsset(outpt);
        }

        // selection changed -> update labels
        if (ui->treeWidget->isEnabled()) // do not update on every click for (un)select all
            AssetControlDialog::updateLabels(m_coin_control, model, this);
    }
}

// shows count of locked unspent outputs
void AssetControlDialog::updateLabelLocked() {
    std::vector <COutPoint> vOutpts;
    model->wallet().listLockedCoins(vOutpts);
    if (vOutpts.size() > 0) {
        ui->labelLocked->setText(tr("(%1 locked)").arg(vOutpts.size()));
        ui->labelLocked->setVisible(true);
    } else ui->labelLocked->setVisible(false);
}

void AssetControlDialog::updateLabels(CCoinControl &m_coin_control, WalletModel *model, QDialog *dialog) {
    if (!model)
        return;

    // nPayAmount
    CAmount nPayAmount = 0;
    bool fDust = false;
    CMutableTransaction txDummy;
    for (const CAmount &amount: AssetControlDialog::payAmounts) {
        nPayAmount += amount;

        if (amount > 0) {
            CTxOut txout(amount, CScript() << std::vector<unsigned char>(24, 0));
            txDummy.vout.push_back(txout);
            fDust |= IsDust(txout, model->node().getDustRelayFee());
        }
    }

    std::string AssetId = "";
    CAmount nAmount = 0;
    CAmount nAssetAmount = 0;
    CAmount nPayFee = 0;
    CAmount nAfterFee = 0;
    CAmount nChange = 0;
    unsigned int nBytes = 0;
    unsigned int nBytesInputs = 0;
    unsigned int nQuantity = 0;
    int nQuantityUncompressed = 0;
    bool fUnselectedSpent{false};
    bool fUnselectedNonMixed{false};
    bool isasset{false};

    std::vector <COutPoint> vAssetControl;
    m_coin_control.ListSelectedAssets(vAssetControl);

    size_t i = 0;
    for (const auto &out: model->wallet().getCoins(vAssetControl)) {
        if (out.depth_in_main_chain < 0) continue;

        // unselect already spent, very unlikely scenario, this could happen
        // when selected are spent elsewhere, like rpc or another computer
        const COutPoint &outpt = vAssetControl[i++];
        if (out.is_spent) {
            m_coin_control.UnSelectAsset(outpt);
            fUnselectedSpent = true;
            continue;
        }

        // Quantity
        nQuantity++;

        // Amount
        nAmount += out.txout.nValue;

        CAssetTransfer assetTransfer;
        if (GetTransferAsset(out.txout.scriptPubKey, assetTransfer)) {
            nAssetAmount += assetTransfer.nAmount;
            AssetId = assetTransfer.assetId;
        }

        // Bytes
        CTxDestination address;
        if (ExtractDestination(out.txout.scriptPubKey, address)) {
            CPubKey pubkey;
            CKeyID *keyid = boost::get<CKeyID>(&address);
            if (keyid && model->wallet().getPubKey(*keyid, pubkey)) {
                nBytesInputs += (pubkey.IsCompressed() ? 148 : 180);
            } else
                nBytesInputs += 148; // in all error cases, simply assume 148 here
        } else nBytesInputs += 148;
    }

    // calculation
    if (nQuantity > 0) {
        // Bytes
        nBytes = nBytesInputs +
                 ((AssetControlDialog::payAmounts.size() > 0 ? AssetControlDialog::payAmounts.size() + 1 : 2) * 34) +
                 10; // always assume +1 output for change here

        // in the subtract fee from amount case, we can tell if zero change already and subtract the bytes, so that fee calculation afterwards is accurate
        if (AssetControlDialog::fSubtractFeeFromAmount)
            if (nAmount - nPayAmount == 0)
                nBytes -= 34;

        // Fee
        nPayFee = model->wallet().getMinimumFee(nBytes, m_coin_control, nullptr /* returned_target */,
                                                nullptr /* reason */);

        if (nPayAmount > 0) {
            nChange = nAssetAmount - nPayAmount;

            if (nChange == 0)
                nBytes -= 34;
        }

        // after fee
        nAfterFee = std::max<CAmount>(nAmount - nPayFee, 0);
    }

    // actually update labels
    int nDisplayUnit = BitcoinUnits::RTM;
    if (model && model->getOptionsModel())
        nDisplayUnit = model->getOptionsModel()->getDisplayUnit();

    QLabel *l1 = dialog->findChild<QLabel *>("labelAssetControlQuantity");
    QLabel *l2 = dialog->findChild<QLabel *>("labelAssetControlAmount");
    QLabel *l3 = dialog->findChild<QLabel *>("labelAssetControlFee");
    QLabel *l4 = dialog->findChild<QLabel *>("labelAssetControlAfterFee");
    QLabel *l5 = dialog->findChild<QLabel *>("labelAssetControlBytes");
    QLabel *l7 = dialog->findChild<QLabel *>("labelAssetControlLowOutput");
    QLabel *l8 = dialog->findChild<QLabel *>("labelAssetControlChange");

    // enable/disable "dust" and "change"
    dialog->findChild<QLabel *>("labelAssetControlLowOutputText")->setEnabled(nPayAmount > 0);
    dialog->findChild<QLabel *>("labelAssetControlLowOutput")->setEnabled(nPayAmount > 0);
    dialog->findChild<QLabel *>("labelAssetControlChangeText")->setEnabled(nPayAmount > 0);
    dialog->findChild<QLabel *>("labelAssetControlChange")->setEnabled(nPayAmount > 0);

    // stats
    l1->setText(QString::number(nQuantity));
    // Quantity
    CAssetMetaData assetData;
    if (passetsCache->GetAssetMetaData(AssetId, assetData))
        l2->setText(BitcoinUnits::formatWithCustomName(QString::fromStdString(assetData.name),
                                                       nAssetAmount));        // Amount
    l3->setText(BitcoinUnits::formatWithUnit(nDisplayUnit, nPayFee));        // Fee
    l4->setText(BitcoinUnits::formatWithUnit(nDisplayUnit, nAfterFee));      // After Fee
    l5->setText(((nBytes > 0) ? ASYMP_UTF8 : "") + QString::number(nBytes));        // Bytes
    l7->setText(fDust ? tr("yes") : tr("no"));
    l8->setText(BitcoinUnits::formatWithCustomName(QString::fromStdString(assetData.name), nChange));        // Amount
    if (nPayFee > 0) {
        l3->setText(ASYMP_UTF8 + l3->text());
        l4->setText(ASYMP_UTF8 + l4->text());
        if (nChange > 0 && !AssetControlDialog::fSubtractFeeFromAmount)
            l8->setText(ASYMP_UTF8 + l8->text());
    }

    // turn label red when dust
    l7->setStyleSheet((fDust) ? GUIUtil::getThemedStyleQString(GUIUtil::ThemedStyle::TS_ERROR) : "");

    // tool tips
    QString toolTipDust = tr(
            "This label turns red if any recipient receives an amount smaller than the current dust threshold.");

    // how many satoshis the estimated fee can vary per byte we guess wrong
    double dFeeVary = (nBytes != 0) ? (double) nPayFee / nBytes : 0;

    QString toolTip4 = tr("Can vary +/- %1 ruff(s) per input.").arg(dFeeVary);

    l3->setToolTip(toolTip4);
    l4->setToolTip(toolTip4);
    l7->setToolTip(toolTipDust);
    l8->setToolTip(toolTip4);
    dialog->findChild<QLabel *>("labelAssetControlFeeText")->setToolTip(l3->toolTip());
    dialog->findChild<QLabel *>("labelAssetControlAfterFeeText")->setToolTip(l4->toolTip());
    dialog->findChild<QLabel *>("labelAssetControlBytesText")->setToolTip(l5->toolTip());
    dialog->findChild<QLabel *>("labelAssetControlLowOutputText")->setToolTip(l7->toolTip());
    dialog->findChild<QLabel *>("labelAssetControlChangeText")->setToolTip(l8->toolTip());

    // Insufficient funds
    QLabel *label = dialog->findChild<QLabel *>("labelAssetControlInsuffFunds");
    if (label)
        label->setVisible(nChange < 0);

    // Warn about unselected Assets
    if (fUnselectedSpent) {
        QMessageBox::warning(dialog, "AssetControl",
                             tr("Some Assets were unselected because they were spent."),
                             QMessageBox::Ok, QMessageBox::Ok);
    } else if (fUnselectedNonMixed) {
        QMessageBox::warning(dialog, "AssetControl",
                             tr("Some Assets were unselected because they do not have enough mixing rounds."),
                             QMessageBox::Ok, QMessageBox::Ok);
    }
}

void AssetControlDialog::updateView() {
    if (!model || !model->getOptionsModel() || !model->getAddressTableModel())
        return;

    bool fNormalMode = !m_coin_control.IsUsingCoinJoin();
    ui->treeWidget->setColumnHidden(COLUMN_LABEL, !fNormalMode);
    ui->radioTreeMode->setVisible(fNormalMode);
    ui->radioListMode->setVisible(fNormalMode);


    bool treeMode = ui->radioTreeMode->isChecked();
    ui->treeWidget->clear();
    ui->treeWidget->setEnabled(false); // performance, otherwise updateLabels would be called for every checked checkbox
    ui->treeWidget->setAlternatingRowColors(!treeMode);
    QFlags <Qt::ItemFlag> flgCheckbox = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
    QFlags <Qt::ItemFlag> flgTristate =
            Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsTristate;

    int nDisplayUnit = model->getOptionsModel()->getDisplayUnit();

    QString assetToDisplay = ui->assetList->currentText();
    std::map < CTxDestination, std::vector < std::tuple < COutPoint, interfaces::WalletTxOut>>> mapassets;

    mapassets = model->wallet().listAssets();

    for (const auto &Assets: mapassets) {
        CAssetControlWidgetItem *itemWalletAddress = new CAssetControlWidgetItem();
        itemWalletAddress->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
        QString sWalletAddress = QString::fromStdString(EncodeDestination(Assets.first));
        QString sWalletLabel = model->getAddressTableModel()->labelForAddress(sWalletAddress);
        QString assetname = "";
        if (sWalletLabel.isEmpty())
            sWalletLabel = tr("(no label)");

        bool hastree = false;

        CAmount nSum = 0;
        int nChildren = 0;
        for (const auto &outpair: Assets.second) {
            const COutPoint &output = std::get<0>(outpair);
            const interfaces::WalletTxOut &out = std::get<1>(outpair);
            bool fFullyMixed{false};
            CAmount nAmount = out.txout.nValue;

            CAssetTransfer assetTransfer;
            std::string uniqueId = "";
            if (GetTransferAsset(out.txout.scriptPubKey, assetTransfer)) {
                nAmount = assetTransfer.nAmount;
                CAssetMetaData assetData;
                if (passetsCache->GetAssetMetaData(assetTransfer.assetId, assetData)) {
                    if (assetData.isUnique)
                        uniqueId += " [" + std::to_string(assetTransfer.uniqueId) + "]";
                    assetname = QString::fromStdString(assetData.name);
                }
            }

            if (assetname != assetToDisplay)
                continue;

            assetname += QString::fromStdString(uniqueId);

            if (treeMode && !hastree) {
                hastree = true;
                // wallet address
                ui->treeWidget->addTopLevelItem(itemWalletAddress);

                itemWalletAddress->setText(COLUMN_NAME, assetname);
                itemWalletAddress->setToolTip(COLUMN_NAME, assetname);

                itemWalletAddress->setFlags(flgTristate);
                itemWalletAddress->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);

                // label
                itemWalletAddress->setText(COLUMN_LABEL, sWalletLabel);
                itemWalletAddress->setToolTip(COLUMN_LABEL, sWalletLabel);

                // address
                itemWalletAddress->setText(COLUMN_ADDRESS, sWalletAddress);
                itemWalletAddress->setToolTip(COLUMN_ADDRESS, sWalletAddress);
            }

            /* if (fHideAdditional) {
                 m_coin_control.UnSelectAsset(output);
                 continue;
             }*/

            nSum += nAmount;
            nChildren++;

            CAssetControlWidgetItem *itemOutput;
            if (treeMode) {
                itemOutput = new CAssetControlWidgetItem(itemWalletAddress);
            } else {
                itemOutput = new CAssetControlWidgetItem(ui->treeWidget);
            }
            itemOutput->setFlags(flgCheckbox);
            itemOutput->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);

            // address
            CTxDestination outputAddress;
            QString sAddress = "";
            if (ExtractDestination(out.txout.scriptPubKey, outputAddress)) {
                sAddress = QString::fromStdString(EncodeDestination(outputAddress));

                // if listMode or change => show raptoreum address. In tree mode, address is not shown again for direct wallet address outputs
                if (!treeMode || (!(sAddress == sWalletAddress))) {
                    itemOutput->setText(COLUMN_ADDRESS, sAddress);
                }

                itemOutput->setToolTip(COLUMN_ADDRESS, sAddress);
            }

            // label
            if (!(sAddress == sWalletAddress)) { //change
                // tooltip from where the change comes from
                itemOutput->setToolTip(COLUMN_LABEL, tr("change from %1 (%2)").arg(sWalletLabel).arg(sWalletAddress));
                itemOutput->setText(COLUMN_LABEL, tr("(change)"));
            } else if (!treeMode) {
                QString sLabel = model->getAddressTableModel()->labelForAddress(sAddress);
                if (sLabel.isEmpty()) {
                    sLabel = tr("(no label)");
                }
                itemOutput->setText(COLUMN_LABEL, sLabel);
            }
            //asset id
            itemOutput->setText(COLUMN_NAME, assetname);
            itemOutput->setToolTip(COLUMN_NAME, assetname);
            //itemOutput->setData(COLUMN_NAME, TxHashRole, BitcoinUnits::name(model->getOptionsModel()->getDisplayUnit()));
            // amount
            itemOutput->setText(COLUMN_AMOUNT, BitcoinUnits::format(nDisplayUnit, nAmount));
            itemOutput->setToolTip(COLUMN_AMOUNT, BitcoinUnits::format(nDisplayUnit, nAmount));
            itemOutput->setData(COLUMN_AMOUNT, Qt::UserRole,
                                QVariant((qlonglong) nAmount)); // padding so that sorting works correctly

            // date
            itemOutput->setText(COLUMN_DATE, GUIUtil::dateTimeStr(out.time));
            itemOutput->setToolTip(COLUMN_DATE, GUIUtil::dateTimeStr(out.time));
            itemOutput->setData(COLUMN_DATE, Qt::UserRole, QVariant((qlonglong) out.time));

            // confirmations
            itemOutput->setText(COLUMN_CONFIRMATIONS, QString::number(out.depth_in_main_chain));
            itemOutput->setData(COLUMN_CONFIRMATIONS, Qt::UserRole, QVariant((qlonglong) out.depth_in_main_chain));

            // transaction hash
            itemOutput->setData(COLUMN_ADDRESS, TxHashRole, QString::fromStdString(output.hash.GetHex()));

            // vout index
            itemOutput->setData(COLUMN_ADDRESS, VOutRole, output.n);

            // disable locked Assets
            if (model->wallet().isLockedCoin(output)) {
                m_coin_control.UnSelectAsset(output); // just to be sure
                itemOutput->setDisabled(true);
                itemOutput->setIcon(COLUMN_CHECKBOX, GUIUtil::getIcon("lock_closed", GUIUtil::ThemedColor::RED));
            }

            // set checkbox
            if (m_coin_control.IsAssetSelected(output)) {
                itemOutput->setCheckState(COLUMN_CHECKBOX, Qt::Checked);
            }
        }

        // amount
        if (treeMode) {
            itemWalletAddress->setText(COLUMN_CHECKBOX, "(" + QString::number(nChildren) + ")");
            itemWalletAddress->setText(COLUMN_AMOUNT, BitcoinUnits::format(nDisplayUnit, nSum));
            itemWalletAddress->setToolTip(COLUMN_AMOUNT, BitcoinUnits::format(nDisplayUnit, nSum));
            itemWalletAddress->setData(COLUMN_AMOUNT, Qt::UserRole, QVariant((qlonglong) nSum));
        }
    }

    // expand all partially selected and hide the empty
    if (treeMode) {
        for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++) {
            QTreeWidgetItem *topLevelItem = ui->treeWidget->topLevelItem(i);
            topLevelItem->setHidden(topLevelItem->childCount() == 0);
            if (topLevelItem->checkState(COLUMN_CHECKBOX) == Qt::PartiallyChecked)
                topLevelItem->setExpanded(true);
        }
    }

    // sort view
    sortView(sortColumn, sortOrder);
    ui->treeWidget->setEnabled(true);
}

void AssetControlDialog::onAssetSelected(QString name) {
    AssetControlDialog::updateLabels(m_coin_control, model, this);
    updateView();
}

void AssetControlDialog::updateAssetList() {
    // Get the assets list
    std::vector <std::string> assets = model->wallet().listMyAssets();

    QStringList list;
    //list << BitcoinUnits::name(model->getOptionsModel()->getDisplayUnit());
    for (auto assetId: assets) {
        CAssetMetaData assetData;
        if (passetsCache->GetAssetMetaData(assetId, assetData)) {
            list << QString::fromStdString(assetData.name);
        }
    }

    stringModel->setStringList(list);
    ui->assetList->setCurrentIndex(0);
    ui->assetList->activated(0);
}
