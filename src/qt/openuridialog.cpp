// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2014-2020 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/openuridialog.h>
#include <qt/forms/ui_openuridialog.h>

#include <qt/guiutil.h>
#include <qt/walletmodel.h>

#include <QUrl>

OpenURIDialog::OpenURIDialog(QWidget *parent) :
        QDialog(parent),
        ui(new Ui::OpenURIDialog) {
    ui->setupUi(this);
    GUIUtil::updateFonts();
    GUIUtil::disableMacFocusRect(this);
}

OpenURIDialog::~OpenURIDialog() {
    delete ui;
}

QString OpenURIDialog::getURI() {
    return ui->uriEdit->text();
}

void OpenURIDialog::accept() {
    SendCoinsRecipient rcp;
    if (GUIUtil::parseBitcoinURI(getURI(), &rcp)) {
        /* Only accept value URIs */
        QDialog::accept();
    } else {
        ui->uriEdit->setValid(false);
    }
}
