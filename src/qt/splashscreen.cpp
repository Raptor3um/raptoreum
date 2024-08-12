// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2020 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/raptoreum-config.h>
#endif

#include <qt/splashscreen.h>

#include <chainparams.h>
#include <clientversion.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <qt/guiutil.h>
#include <qt/networkstyle.h>
#include <qt/walletmodel.h>
#include <ui_interface.h>
#include <util/system.h>

#include <functional>

#include <QApplication>
#include <QCloseEvent>
#include <QPainter>
#include <QScreen>

namespace pl = std::placeholders;

SplashScreen::SplashScreen(interfaces::Node &node, Qt::WindowFlags f, const NetworkStyle *networkStyle) :
        QWidget(nullptr, f), curAlignment(0), m_node(node) {

    // transparent background
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet("background:transparent;");

    // no window decorations
    setWindowFlags(Qt::FramelessWindowHint);

    // Geometries of splashscreen
    int width = 480;
    int height = 540;
    int logoWidth = 480;
    int logoHeight = 540;

    // set reference point, paddings
    int paddingTop = 5;
    int titleVersionVSpace = 20;

    float fontFactor = 1.0;
    float scale = qApp->devicePixelRatio();

    // define text to place
    QString titleText = PACKAGE_NAME;
    QString versionText = QString::fromStdString(FormatFullVersion()).remove(0, 1);
    QString titleAddText = networkStyle->getTitleAddText();

    QFont fontNormal = GUIUtil::getFontNormal();
    QFont fontBold = GUIUtil::getFontBold();

    QPixmap pixmapLogo = networkStyle->getSplashImage();
    pixmapLogo.setDevicePixelRatio(scale);

    // TODO: Do we want to shade the splash screen for testnet/devnet?
    // Adjust logo color based on the current theme
    // QImage imgLogo = pixmapLogo.toImage().convertToFormat(QImage::Format_ARGB32);
    // QColor logoColor = GUIUtil::getThemedQColor(GUIUtil::ThemedColor::BLUE);
    // for (int x = 0; x < imgLogo.width(); ++x) {
    //     for (int y = 0; y < imgLogo.height(); ++y) {
    //         const QRgb rgb = imgLogo.pixel(x, y);
    //         imgLogo.setPixel(x, y, qRgba(logoColor.red(), logoColor.green(), logoColor.blue(), qAlpha(rgb)));
    //     }
    // }
    // pixmapLogo.convertFromImage(imgLogo);

    pixmap = QPixmap(width * scale, height * scale);
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(GUIUtil::getThemedQColor(GUIUtil::ThemedColor::BORDER_WIDGET));

    QPainter pixPaint(&pixmap);

    QRect rect = QRect(1, 1, width - 2, height - 2);
    pixPaint.fillRect(rect, GUIUtil::getThemedQColor(GUIUtil::ThemedColor::BACKGROUND_WIDGET));

    pixPaint.drawPixmap((width / 2) - (logoWidth / 2), (height / 2) - (logoHeight / 2) + 0,
                        pixmapLogo.scaled(logoWidth * scale, logoHeight * scale, Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation));
    pixPaint.setPen(GUIUtil::getThemedQColor(GUIUtil::ThemedColor::DEFAULT));

    // check font size and drawing with
    fontBold.setPointSize(40 * fontFactor);
    pixPaint.setFont(fontBold);
    QFontMetrics fm = pixPaint.fontMetrics();
    int titleTextWidth = GUIUtil::TextWidth(fm, titleText);
    if (titleTextWidth > width * 0.8) {
        fontFactor = 0.75;
    }

    fontNormal.setPointSize(16 * fontFactor);
    pixPaint.setFont(fontNormal);
    fm = pixPaint.fontMetrics();
    int versionTextWidth = GUIUtil::TextWidth(fm, versionText);
    pixPaint.drawText((width / 2) - (versionTextWidth / 2), titleVersionVSpace, versionText);

    // draw additional text if special network
    if (!titleAddText.isEmpty()) {
        fontBold.setPointSize(24 * fontFactor);
        pixPaint.setFont(fontBold);
        fm = pixPaint.fontMetrics();
        int titleAddTextWidth = GUIUtil::TextWidth(fm, titleAddText);
        // Draw the badge backround with the network-specific color
        QRect badgeRect = QRect(0, 0, titleAddTextWidth + 10, fm.height() + 10);
        QColor badgeColor = networkStyle->getBadgeColor();
        pixPaint.fillRect(badgeRect, badgeColor);
        // Draw the text itself using white color, regardless of the current theme
        pixPaint.setPen(Qt::white);
        pixPaint.drawText(5, fm.height(), titleAddText);
    }

    pixPaint.end();

    // Resize window and move to center of desktop, disallow resizing
    QRect r(QPoint(), QSize(width, height));
    resize(r.size());
    setFixedSize(r.size());
    move(QGuiApplication::primaryScreen()->geometry().center() - r.center());

    subscribeToCoreSignals();
    installEventFilter(this);
}

SplashScreen::~SplashScreen() {
    unsubscribeFromCoreSignals();
}

bool SplashScreen::eventFilter(QObject *obj, QEvent *ev) {
    if (ev->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(ev);
        if (keyEvent->text()[0] == 'q') {
            m_node.startShutdown();
        }
    }
    return QObject::eventFilter(obj, ev);
}

void SplashScreen::finish() {
    /* If the window is minimized, hide() will be ignored. */
    /* Make sure we de-minimize the splashscreen window before hiding */
    if (isMinimized())
        showNormal();
    hide();
    deleteLater(); // No more need for this
}

static void InitMessage(SplashScreen *splash, const std::string &message) {
    bool invoked = QMetaObject::invokeMethod(splash, "showMessage",
                                             Qt::QueuedConnection,
                                             Q_ARG(QString, QString::fromStdString(message)),
                                             Q_ARG(int, Qt::AlignBottom | Qt::AlignHCenter),
                                             Q_ARG(QColor, GUIUtil::getThemedQColor(GUIUtil::ThemedColor::DEFAULT)));
    assert(invoked);
}

static void ShowProgress(SplashScreen *splash, const std::string &title, int nProgress, bool resume_possible) {
    InitMessage(splash, title + std::string("\n") +
                        (resume_possible ? _("(press q to shutdown and continue later)")
                                         : _("press q to shutdown")) +
                        strprintf("\n%d", nProgress) + "%");
}

void SplashScreen::subscribeToCoreSignals() {
    // Connect signals to client
    m_handler_init_message = m_node.handleInitMessage(std::bind(InitMessage, this, pl::_1));
    m_handler_show_progress = m_node.handleShowProgress(std::bind(ShowProgress, this, pl::_1, pl::_2, pl::_3));
}

void SplashScreen::handleLoadWallet() {
#ifdef ENABLE_WALLET
    if (!WalletModel::isWalletEnabled()) return;
    m_handler_load_wallet = m_node.walletClient().handleLoadWallet([this](std::unique_ptr<interfaces::Wallet> wallet) {
        m_connected_wallet_handlers.emplace_back(wallet->handleShowProgress(std::bind(ShowProgress, this, pl::_1, pl::_2, false)));
        m_connected_wallets.emplace_back(std::move(wallet));
    });
#endif
}

void SplashScreen::unsubscribeFromCoreSignals() {
    // Disconnect signals from client
    m_handler_init_message->disconnect();
    m_handler_show_progress->disconnect();
#ifdef ENABLE_WALLET
    if (m_handler_load_wallet != nullptr) {
        m_handler_load_wallet->disconnect();
    }
#endif // ENABLE_WALLET
    for (const auto &handler: m_connected_wallet_handlers) {
        handler->disconnect();
    }
    m_connected_wallet_handlers.clear();
    m_connected_wallets.clear();
}

void SplashScreen::showMessage(const QString &message, int alignment, const QColor &color) {
    curMessage = message;
    curAlignment = alignment;
    curColor = color;
    update();
}

void SplashScreen::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    QFont messageFont = GUIUtil::getFontNormal();
    messageFont.setPointSize(14);
    painter.setFont(messageFont);
    painter.drawPixmap(0, 0, pixmap);
    QRect r = rect().adjusted(5, 5, -5, -15);
    painter.setPen(curColor);
    painter.drawText(r, curAlignment, curMessage);
}

void SplashScreen::closeEvent(QCloseEvent *event) {
    m_node.startShutdown(); // allows an "emergency" shutdown during startup
    event->ignore();
}
