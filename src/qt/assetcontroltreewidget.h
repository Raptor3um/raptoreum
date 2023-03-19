// Copyright (c) 2011-2014 The BitAsset Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITAsset_QT_AssetCONTROLTREEWIDGET_H
#define BITAsset_QT_AssetCONTROLTREEWIDGET_H

#include <QKeyEvent>
#include <QTreeWidget>

class AssetControlTreeWidget : public QTreeWidget
{
    Q_OBJECT

public:
    explicit AssetControlTreeWidget(QWidget *parent = 0);

protected:
    virtual void keyPressEvent(QKeyEvent *event) override;
};

#endif // BITAsset_QT_AssetCONTROLTREEWIDGET_H
