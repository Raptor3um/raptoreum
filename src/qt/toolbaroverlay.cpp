// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/toolbaroverlay.h>
#include <qt/forms/ui_toolbaroverlay.h>

#include <qt/guiutil.h>

#include <chainparams.h>

#include <QResizeEvent>
#include <QPropertyAnimation>

ToolbarOverlay::ToolbarOverlay(bool enable_wallet, QWidget *parent) :
        QWidget(parent),
        ui(new Ui::ToolbarOverlay),
        layerIsVisible(false) {
    ui->setupUi(this);

    if (parent) {
        parent->installEventFilter(this);
        raise();
    }
    timer = new QTimer();
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, &ToolbarOverlay::timeout);

    setVisible(false);
}

ToolbarOverlay::~ToolbarOverlay() {
    delete ui;
}

void ToolbarOverlay::timeout() {
    if (!layerIsVisible)
        return;

    setGeometry( width(), 0, width(), height());

    QPropertyAnimation *animation = new QPropertyAnimation(this, "pos");
    animation->setDuration(300);
    animation->setStartValue(QPoint(0, 0));
    animation->setEndValue(QPoint(-this->width(), 0));
    animation->setEasingCurve(QEasingCurve::OutQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    layerIsVisible = false;
}

void ToolbarOverlay::addtoolbar(QToolBar *toolbar) {
    ui->verticalLayout_2->addWidget(toolbar);
}

void ToolbarOverlay::setMaxWidth(int width) {
    resize(width, height());
    setMaximumWidth(width);
}

bool ToolbarOverlay::eventFilter(QObject *obj, QEvent *ev) {
    if (obj == parent()) {
        if (ev->type() == QEvent::Resize) {
            QResizeEvent *rev = static_cast<QResizeEvent *>(ev);
            resize(rev->size());
            if (!layerIsVisible)
                setGeometry(0, height(), width(), height());

        } else if (ev->type() == QEvent::ChildAdded) {
            raise();
        } 
    }
    return QWidget::eventFilter(obj, ev);
}

//! Tracks parent widget changes
bool ToolbarOverlay::event(QEvent *ev) {
    if (ev->type() == QEvent::ParentAboutToChange) {
        if (parent()) parent()->removeEventFilter(this);
    } else if (ev->type() == QEvent::ParentChange) {
        if (parent()) {
            parent()->installEventFilter(this);
            raise();
        }
    }
    return QWidget::event(ev);
}
void ToolbarOverlay::cancelHide() {
    if (layerIsVisible)
        timer->stop();
}
void ToolbarOverlay::showHide(bool hide) {
    if ((layerIsVisible && !hide) || (!layerIsVisible && hide))
        return;

    if (!isVisible() && !hide)
        setVisible(true);

    if(!hide){
        setGeometry( hide ? 0 : width(), 0, width(), height());

        QPropertyAnimation *animation = new QPropertyAnimation(this, "pos");
        animation->setDuration(300);
        animation->setStartValue(QPoint(-this->width(), 0));
        animation->setEndValue(QPoint(0, 0));
        animation->setEasingCurve(QEasingCurve::OutQuad);
        animation->start(QAbstractAnimation::DeleteWhenStopped);
        layerIsVisible = !hide;
    } else {
        timer->start(500);
    }
}
