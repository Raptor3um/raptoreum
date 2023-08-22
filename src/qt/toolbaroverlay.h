// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_ToolbarOverlay_H
#define BITCOIN_QT_ToolbarOverlay_H

#include <QDateTime>
#include <QWidget>
#include <QToolBar>
#include <QTimer>

namespace Ui {
    class ToolbarOverlay;
}

/** Modal overlay to display information about the chain-sync state */
class ToolbarOverlay : public QWidget {
    Q_OBJECT

public:
    explicit ToolbarOverlay(bool enable_wallet, QWidget *parent);

    ~ToolbarOverlay();

public
    Q_SLOTS:
    // will show or hide the modal layer
    void showHide(bool hide = false);

    void addtoolbar(QToolBar *toolbar);

    void setMaxWidth(int width);

    void cancelHide();

    bool isLayerVisible() const { return layerIsVisible; }

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

    bool event(QEvent *ev) override;

private:
    Ui::ToolbarOverlay *ui;
    bool layerIsVisible;
    QTimer *timer;

private
    Q_SLOTS:

    void timeout();
};

#endif // BITCOIN_QT_ToolbarOverlay_H