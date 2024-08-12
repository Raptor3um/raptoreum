// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/guiutil.h>

#include <qt/appearancewidget.h>
#include <qt/bitcoinaddressvalidator.h>
#include <qt/bitcoingui.h>
#include <qt/bitcoinunits.h>
#include <qt/optionsdialog.h>
#include <qt/qvalidatedlineedit.h>
#include <qt/walletmodel.h>

#include <base58.h>
#include <chainparams.h>
#include <primitives/transaction.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <policy/policy.h>
#include <protocol.h>
#include <script/script.h>
#include <script/standard.h>
#include <ui_interface.h>
#include <util/system.h>

#ifdef WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#endif

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QList>
#include <QMouseEvent>
#include <QPointer>
#include <QProgressDialog>
#include <QScreen>
#include <QSettings>
#include <QSize>
#include <QString>
#include <QTextDocument> // for Qt::mightBeRichText
#include <QThread>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QtGlobal>

#if defined(Q_OS_MAC)

#include <QProcess>

void ForceActivation();
#endif

namespace GUIUtil {

    static RecursiveMutex cs_css;
// The default stylesheet directory
    static const QString defaultStylesheetDirectory = ":css";
// The actual stylesheet directory
    static QString stylesheetDirectory = defaultStylesheetDirectory;
// The name of the traditional theme
    static const QString traditionalTheme = "Traditional";
// The theme to set by default if settings are missing or incorrect
    static const QString defaultTheme = "Dark";
// The prefix a theme name should have if we want to apply dark colors and styles to it
    static const QString darkThemePrefix = "Dark";
// The prefix a theme name should have if we want to apply light colors and styles to it
    static const QString lightThemePrefix = "Light";
// The theme to set as a base one for non-traditional themes
    static const QString generalTheme = "general";
// Mapping theme => css file
    static const std::map <QString, QString> mapThemeToStyle{
            {generalTheme,  "general.css"},
            {"Dark",        "dark.css"},
            {"Light",       "light.css"},
            {"Traditional", "traditional.css"},
    };

/** loadFonts stores the SystemDefault font in osDefaultFont to be able to reference it later again */
    static std::unique_ptr <QFont> osDefaultFont;
/** Font related default values. */
    static const FontFamily defaultFontFamily = FontFamily::SystemDefault;
    static const int defaultFontSize = 12;
    static const double fontScaleSteps = 0.01;
#ifdef Q_OS_MAC
    static const QFont::Weight defaultFontWeightNormal = QFont::Normal;
    static const QFont::Weight defaultFontWeightBold = QFont::DemiBold;
    static const int defaultFontScale = 0;
#else
    static const QFont::Weight defaultFontWeightNormal = QFont::Normal;
    static const QFont::Weight defaultFontWeightBold = QFont::DemiBold;
    static const int defaultFontScale = 0;
#endif

/** Font related variables. */
// Application font family. May be overwritten by -font-family.
    static FontFamily fontFamily = defaultFontFamily;
// Application font scale value. May be overwritten by -font-scale.
    static int fontScale = defaultFontScale;
// Contains the weight settings separated for all available fonts
    static std::map <FontFamily, std::pair<QFont::Weight, QFont::Weight>> mapDefaultWeights;
    static std::map <FontFamily, std::pair<QFont::Weight, QFont::Weight>> mapWeights;
// Contains all widgets and its font attributes (weight, italic, size) with font changes due to GUIUtil::setFont
    static std::map <QPointer<QWidget>, std::tuple<FontWeight, bool, int>> mapFontUpdates;
// Contains a list of supported font weights for all members of GUIUtil::FontFamily
    static std::map <FontFamily, std::vector<QFont::Weight>> mapSupportedWeights;

#ifdef Q_OS_MAC
    // Contains all widgets where the macOS focus rect has been disabled.
    static std::set<QWidget*> setRectsDisabled;
#endif

    static const std::map <ThemedColor, QColor> themedColors = {
            {ThemedColor::UNCONFIRMED,             QColor(128, 128, 128)},
            {ThemedColor::TX_STATUS_OPENUNTILDATE, QColor(64, 64, 255)},
            {ThemedColor::QR_PIXEL,                QColor(17, 21, 34, 1)},
            {ThemedColor::BACKGROUND_WIDGET,       QColor(247, 235, 232)},
            {ThemedColor::BORDER_WIDGET,           QColor(194, 192, 192)},
            {ThemedColor::BACKGROUND_NETSTATS,     QColor(239, 189, 177)},
            {ThemedColor::BORDER_NETSTATS,         QColor(32, 36, 42)},
            {ThemedColor::DEFAULT,                 QColor(17, 21, 34)},
            {ThemedColor::ICON_ALTERNATIVE_COLOR,  QColor(194, 192, 192)},
            {ThemedColor::BLUE,                    QColor(97, 123, 209)},
            {ThemedColor::GREEN,                   QColor(10, 173, 6)}, 
            {ThemedColor::ORANGE,                  QColor(235, 127, 0)},
            {ThemedColor::PRIMARY,                 QColor(183, 68, 38)},
            {ThemedColor::RED,                     QColor(243, 35, 73)},
            {ThemedColor::SECONDARY,               QColor(194, 192, 192)},
    };

    static const std::map <ThemedColor, QColor> themedDarkColors = {
            {ThemedColor::UNCONFIRMED,             QColor(85, 91, 112)},
            {ThemedColor::TX_STATUS_OPENUNTILDATE, QColor(64, 64, 255)},
            {ThemedColor::QR_PIXEL,                QColor(194, 192, 192)},
            {ThemedColor::BACKGROUND_WIDGET,       QColor(17, 21, 34)},
            {ThemedColor::BORDER_WIDGET,           QColor(85, 91, 112)},
            {ThemedColor::BACKGROUND_NETSTATS,     QColor(27, 31, 44)},
            {ThemedColor::BORDER_NETSTATS,         QColor(194, 192, 192)},
            {ThemedColor::DEFAULT,                 QColor(194, 192, 192)},
            {ThemedColor::ICON_ALTERNATIVE_COLOR,  QColor(128, 134, 154)},
            {ThemedColor::BLUE,                    QColor(171, 186, 237)},
            {ThemedColor::GREEN,                   QColor(10, 173, 6)},
            {ThemedColor::ORANGE,                  QColor(235, 127, 0)},
            {ThemedColor::PRIMARY,                 QColor(180, 90, 70)},
            {ThemedColor::RED,                     QColor(243, 35, 72)},
            {ThemedColor::SECONDARY,               QColor(128, 134, 154)},
    };

    static const std::map <ThemedStyle, QString> themedStyles = {
            {ThemedStyle::TS_INVALID,   "border: 1px solid #F90000; background: rgb(255, 20, 20, 0.2);"},
            {ThemedStyle::TS_ERROR,     "color:#F32349;"},
            {ThemedStyle::TS_SUCCESS,   "color:#56AA01;"},
            {ThemedStyle::TS_COMMAND,   "color:#617BD1;"},
            {ThemedStyle::TS_PRIMARY,   "color:#555B70;"}, 
            {ThemedStyle::TS_SECONDARY, "color:#c2c0c0;"}, 
    };

    static const std::map <ThemedStyle, QString> themedDarkStyles = {
            {ThemedStyle::TS_INVALID,   "border: 1px solid #FF1414; background: rgb(255, 20, 20, 0.4);"},
            {ThemedStyle::TS_ERROR,     "color:#F32349;"},
            {ThemedStyle::TS_SUCCESS,   "color:#0AAD06;"},
            {ThemedStyle::TS_COMMAND,   "color:#ABBAED;"},
            {ThemedStyle::TS_PRIMARY,   "color:#d2d0d0;"},
            {ThemedStyle::TS_SECONDARY, "color:#80869A;"},
    };

    QColor getThemedQColor(ThemedColor color) {
        QString theme = QSettings().value("theme", "").toString();
        return theme.startsWith(darkThemePrefix) ? themedDarkColors.at(color) : themedColors.at(color);
    }

    QString getThemedStyleQString(ThemedStyle style) {
        QString theme = QSettings().value("theme", "").toString();
        return theme.startsWith(darkThemePrefix) ? themedDarkStyles.at(style) : themedStyles.at(style);
    }

    QIcon getIcon(const QString &strIcon, const ThemedColor color, const ThemedColor colorAlternative,
                  const QString &strIconPath) {
        QColor qcolor = getThemedQColor(color);
        QColor qcolorAlternative = getThemedQColor(colorAlternative);
        QIcon icon(strIconPath + strIcon);
        QIcon themedIcon;
        for (const QSize& size : icon.availableSizes()) {
            QImage image(icon.pixmap(size).toImage());
            image = image.convertToFormat(QImage::Format_ARGB32);
            for (int x = 0; x < image.width(); ++x) {
                for (int y = 0; y < image.height(); ++y) {
                    const QRgb rgb = image.pixel(x, y);
                    QColor* pColor;
                    if ((rgb & RGB_MASK) < RGB_HALF) {
                        pColor = &qcolor;
                    } else {
                        pColor = &qcolorAlternative;
                    }
                    image.setPixel(x, y, qRgba(pColor->red(), pColor->green(), pColor->blue(), qAlpha(rgb)));
                }
            }
            themedIcon.addPixmap(QPixmap::fromImage(image));
        }
        return themedIcon;
    }

    QIcon getIcon(const QString &strIcon, const ThemedColor color, const QString &strIconPath) {
        return getIcon(strIcon, color, ThemedColor::ICON_ALTERNATIVE_COLOR, strIconPath);
    }

    void setIcon(QAbstractButton *button, const QString &strIcon, const ThemedColor color,
                 const ThemedColor colorAlternative, const QSize &size) {
        button->setIcon(getIcon(strIcon, color, colorAlternative));
        button->setIconSize(size);
    }

    void setIcon(QAbstractButton *button, const QString &strIcon, const ThemedColor color, const QSize &size) {
        setIcon(button, strIcon, color, ThemedColor::ICON_ALTERNATIVE_COLOR, size);
    }

    QString dateTimeStr(const QDateTime &date) {
        return QLocale::system().toString(date.date(), QLocale::ShortFormat) + QString(" ") + date.toString("hh:mm");
    }

    QString dateTimeStr(qint64 nTime) {
        return dateTimeStr(QDateTime::fromTime_t((qint32) nTime));
    }

// Just some dummy data to generate a convincing random-looking (but consistent) address
    static const uint8_t dummydata[] = {0xeb, 0x15, 0x23, 0x1d, 0xfc, 0xeb, 0x60, 0x92, 0x58, 0x86, 0xb6, 0x7d, 0x06,
                                        0x52, 0x99, 0x92, 0x59, 0x15, 0xae, 0xb1, 0x72, 0xc0, 0x66, 0x47};

// Generate a dummy address with invalid CRC, starting with the network prefix.
    static std::string DummyAddress(const CChainParams &params) {
        std::vector<unsigned char> sourcedata = params.Base58Prefix(CChainParams::PUBKEY_ADDRESS);
        sourcedata.insert(sourcedata.end(), dummydata, dummydata + sizeof(dummydata));
        for (int i = 0; i < 256; ++i) { // Try every trailing byte
            std::string s = EncodeBase58(sourcedata.data(), sourcedata.data() + sourcedata.size());
            if (!IsValidDestinationString(s)) {
                return s;
            }
            sourcedata[sourcedata.size() - 1] += 1;
        }
        return "";
    }

    void setupAddressWidget(QValidatedLineEdit *widget, QWidget *parent, bool fAllowURI) {
        parent->setFocusProxy(widget);

        // We don't want translators to use own addresses in translations
        // and this is the only place, where this address is supplied.
        widget->setPlaceholderText(QObject::tr("Enter a Raptoreum address (e.g. %1)").arg(
                QString::fromStdString(DummyAddress(Params()))));
        widget->setValidator(new BitcoinAddressEntryValidator(parent, fAllowURI));
        widget->setCheckValidator(new BitcoinAddressCheckValidator(parent));
    }


    void setupAppearance(QWidget *parent, OptionsModel *model) {
        if (!QSettings().value("fAppearanceSetupDone", false).toBool()) {
            // Create the dialog
            QDialog dlg(parent);
            dlg.setObjectName("AppearanceSetup");
            dlg.setWindowTitle(QObject::tr("Appearance Setup"));
            dlg.setWindowIcon(QIcon(":icons/raptoreum"));
            // And the widgets we add to it
            QLabel lblHeading(
                    QObject::tr("Please choose your preferred settings for the appearance of %1").arg(PACKAGE_NAME),
                    &dlg);
            lblHeading.setObjectName("lblHeading");
            lblHeading.setWordWrap(true);
            QLabel lblSubHeading(
                    QObject::tr("This can also be adjusted later in the \"Appearance\" tab of the preferences."), &dlg);
            lblSubHeading.setObjectName("lblSubHeading");
            lblSubHeading.setWordWrap(true);
            AppearanceWidget appearance(&dlg);
            appearance.setModel(model);
            QFrame line(&dlg);
            line.setFrameShape(QFrame::HLine);
            QDialogButtonBox buttonBox(QDialogButtonBox::Save);
            // Put them into a vbox and add the vbox to the dialog
            QVBoxLayout layout;
            layout.addWidget(&lblHeading);
            layout.addWidget(&lblSubHeading);
            layout.addWidget(&line);
            layout.addWidget(&appearance);
            layout.addWidget(&buttonBox);
            dlg.setLayout(&layout);
            // Adjust the headings
            setFont({&lblHeading}, FontWeight::Bold, 16);
            setFont({&lblSubHeading}, FontWeight::Normal, 14, true);
            // Make sure the dialog closes and accepts the settings if save has been pressed
            QObject::connect(&buttonBox, &QDialogButtonBox::accepted, [&]() {
                QSettings().setValue("fAppearanceSetupDone", true);
                appearance.accept();
                dlg.accept();
            });
            // And fire it!
            dlg.exec();
        }
    }

    bool parseBitcoinURI(const QUrl &uri, SendCoinsRecipient *out) {
        // return if URI is not valid or is no raptoreum: URI
        if (!uri.isValid() || uri.scheme() != QString("raptoreum"))
            return false;

        SendCoinsRecipient rv;
        rv.address = uri.path();
        // Trim any following forward slash which may have been added by the OS
        if (rv.address.endsWith("/")) {
            rv.address.truncate(rv.address.length() - 1);
        }
        rv.amount = 0;

        QUrlQuery uriQuery(uri);
        QList <QPair<QString, QString>> items = uriQuery.queryItems();

        for (QList < QPair < QString, QString > > ::iterator i = items.begin(); i != items.end();
        i++)
        {
            bool fShouldReturnFalse = false;
            if (i->first.startsWith("req-")) {
                i->first.remove(0, 4);
                fShouldReturnFalse = true;
            }

            if (i->first == "label") {
                rv.label = i->second;
                fShouldReturnFalse = false;
            }
            if (i->first == "IS") {
                // we simply ignore IS
                fShouldReturnFalse = false;
            }
            if (i->first == "message") {
                rv.message = i->second;
                fShouldReturnFalse = false;
            } else if (i->first == "amount") {
                if (!i->second.isEmpty()) {
                    if (!BitcoinUnits::parse(BitcoinUnits::RTM, i->second, &rv.amount)) {
                        return false;
                    }
                }
                fShouldReturnFalse = false;
            }

            if (fShouldReturnFalse)
                return false;
        }
        if (out) {
            *out = rv;
        }
        return true;
    }

    bool parseBitcoinURI(QString uri, SendCoinsRecipient *out) {
        QUrl uriInstance(uri);
        return parseBitcoinURI(uriInstance, out);
    }

    bool validateBitcoinURI(const QString &uri) {
        SendCoinsRecipient rcp;
        return parseBitcoinURI(uri, &rcp);
    }

    QString formatBitcoinURI(const SendCoinsRecipient &info) {
        QString ret = QString("raptoreum:%1").arg(info.address);
        int paramCount = 0;

        if (info.amount) {
            ret += QString("?amount=%1").arg(
                    BitcoinUnits::format(BitcoinUnits::RTM, info.amount, false, BitcoinUnits::separatorNever));
            paramCount++;
        }

        if (!info.label.isEmpty()) {
            QString lbl(QUrl::toPercentEncoding(info.label));
            ret += QString("%1label=%2").arg(paramCount == 0 ? "?" : "&").arg(lbl);
            paramCount++;
        }

        if (!info.message.isEmpty()) {
            QString msg(QUrl::toPercentEncoding(info.message));
            ret += QString("%1message=%2").arg(paramCount == 0 ? "?" : "&").arg(msg);
            paramCount++;
        }

        return ret;
    }

    bool isDust(interfaces::Node &node, const QString &address, const CAmount &amount) {
        CTxDestination dest = DecodeDestination(address.toStdString());
        CScript script = GetScriptForDestination(dest);
        CTxOut txOut(amount, script);
        return IsDust(txOut, node.getDustRelayFee());
    }

    QString HtmlEscape(const QString &str, bool fMultiLine) {
        QString escaped = str.toHtmlEscaped();
        if (fMultiLine) {
            escaped = escaped.replace("\n", "<br>\n");
        }
        return escaped;
    }

    QString HtmlEscape(const std::string &str, bool fMultiLine) {
        return HtmlEscape(QString::fromStdString(str), fMultiLine);
    }

    void copyEntryData(QAbstractItemView *view, int column, int role) {
        if (!view || !view->selectionModel())
            return;
        QModelIndexList selection = view->selectionModel()->selectedRows(column);

        if (!selection.isEmpty()) {
            // Copy first item
            setClipboard(selection.at(0).data(role).toString());
        }
    }

    QList <QModelIndex> getEntryData(QAbstractItemView *view, int column) {
        if (!view || !view->selectionModel())
            return QList<QModelIndex>();
        return view->selectionModel()->selectedRows(column);
    }

    QString getDefaultDataDirectory() {
        return boostPathToQString(GetDefaultDataDir());
    }

    QString getSaveFileName(QWidget *parent, const QString &caption, const QString &dir,
                            const QString &filter,
                            QString *selectedSuffixOut) {
        QString selectedFilter;
        QString myDir;
        if (dir.isEmpty()) // Default to user documents location
        {
            myDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        } else {
            myDir = dir;
        }
        /* Directly convert path to native OS path separators */
        QString result = QDir::toNativeSeparators(
                QFileDialog::getSaveFileName(parent, caption, myDir, filter, &selectedFilter));

        /* Extract first suffix from filter pattern "Description (*.foo)" or "Description (*.foo *.bar ...) */
        QRegExp filter_re(".* \\(\\*\\.(.*)[ \\)]");
        QString selectedSuffix;
        if (filter_re.exactMatch(selectedFilter)) {
            selectedSuffix = filter_re.cap(1);
        }

        /* Add suffix if needed */
        QFileInfo info(result);
        if (!result.isEmpty()) {
            if (info.suffix().isEmpty() && !selectedSuffix.isEmpty()) {
                /* No suffix specified, add selected suffix */
                if (!result.endsWith("."))
                    result.append(".");
                result.append(selectedSuffix);
            }
        }

        /* Return selected suffix if asked to */
        if (selectedSuffixOut) {
            *selectedSuffixOut = selectedSuffix;
        }
        return result;
    }

    QString getOpenFileName(QWidget *parent, const QString &caption, const QString &dir,
                            const QString &filter,
                            QString *selectedSuffixOut) {
        QString selectedFilter;
        QString myDir;
        if (dir.isEmpty()) // Default to user documents location
        {
            myDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        } else {
            myDir = dir;
        }
        /* Directly convert path to native OS path separators */
        QString result = QDir::toNativeSeparators(
                QFileDialog::getOpenFileName(parent, caption, myDir, filter, &selectedFilter));

        if (selectedSuffixOut) {
            /* Extract first suffix from filter pattern "Description (*.foo)" or "Description (*.foo *.bar ...) */
            QRegExp filter_re(".* \\(\\*\\.(.*)[ \\)]");
            QString selectedSuffix;
            if (filter_re.exactMatch(selectedFilter)) {
                selectedSuffix = filter_re.cap(1);
            }
            *selectedSuffixOut = selectedSuffix;
        }
        return result;
    }

    Qt::ConnectionType blockingGUIThreadConnection() {
        if (QThread::currentThread() != qApp->thread()) {
            return Qt::BlockingQueuedConnection;
        } else {
            return Qt::DirectConnection;
        }
    }

    bool checkPoint(const QPoint &p, const QWidget *w) {
        QWidget *atW = QApplication::widgetAt(w->mapToGlobal(p));
        if (!atW) return false;
        return atW->window() == w;
    }

    bool isObscured(QWidget *w) {
        return !(checkPoint(QPoint(0, 0), w)
                 && checkPoint(QPoint(w->width() - 1, 0), w)
                 && checkPoint(QPoint(0, w->height() - 1), w)
                 && checkPoint(QPoint(w->width() - 1, w->height() - 1), w)
                 && checkPoint(QPoint(w->width() / 2, w->height() / 2), w));
    }

    void bringToFront(QWidget *w) {
#ifdef Q_OS_MAC
        ForceActivation();
#endif

        if (w) {
            // activateWindow() (sometimes) helps with keyboard focus on Windows
            if (w->isMinimized()) {
                w->showNormal();
            } else {
                w->show();
            }
            w->activateWindow();
            w->raise();
        }
    }

    void openDebugLogfile() {
        fs::path pathDebug = GetDataDir() / "debug.log";

        /* Open debug.log with the associated application */
        if (fs::exists(pathDebug))
            QDesktopServices::openUrl(QUrl::fromLocalFile(boostPathToQString(pathDebug)));
    }

    void openConfigfile() {
        fs::path pathConfig = GetConfigFile(gArgs.GetArg("-conf", BITCOIN_CONF_FILENAME));

        /* Open raptoreum.conf with the associated application */
        if (fs::exists(pathConfig))
            QDesktopServices::openUrl(QUrl::fromLocalFile(boostPathToQString(pathConfig)));
    }

    void showBackups() {
        fs::path backupsDir = GetBackupsDir();

        /* Open folder with default browser */
        if (fs::exists(backupsDir))
            QDesktopServices::openUrl(QUrl::fromLocalFile(boostPathToQString(backupsDir)));
    }

    ToolTipToRichTextFilter::ToolTipToRichTextFilter(int _size_threshold, QObject *parent) :
            QObject(parent),
            size_threshold(_size_threshold) {

    }

    bool ToolTipToRichTextFilter::eventFilter(QObject *obj, QEvent *evt) {
        if (evt->type() == QEvent::ToolTipChange) {
            QWidget *widget = static_cast<QWidget *>(obj);
            QString tooltip = widget->toolTip();
            if (tooltip.size() > size_threshold && !tooltip.startsWith("<qt")) {
                // Escape the current message as HTML and replace \n by <br> if it's not rich text
                if (!Qt::mightBeRichText(tooltip))
                    tooltip = HtmlEscape(tooltip, true);
                // Envelop with <qt></qt> to make sure Qt detects every tooltip as rich text
                // and style='white-space:pre' to preserve line composition
                tooltip = "<qt style='white-space:pre'>" + tooltip + "</qt>";
                widget->setToolTip(tooltip);
                return true;
            }
        }
        return QObject::eventFilter(obj, evt);
    }

    LabelOutOfFocusEventFilter::LabelOutOfFocusEventFilter(QObject *parent)
            : QObject(parent) {
    }

    bool LabelOutOfFocusEventFilter::eventFilter(QObject *watched, QEvent *event) {
        if (event->type() == QEvent::FocusOut) {
            auto focus_out = static_cast<QFocusEvent *>(event);
            if (focus_out->reason() != Qt::PopupFocusReason) {
                auto label = qobject_cast<QLabel *>(watched);
                if (label) {
                    auto flags = label->textInteractionFlags();
                    label->setTextInteractionFlags(Qt::NoTextInteraction);
                    label->setTextInteractionFlags(flags);
                }
            }
        }

        return QObject::eventFilter(watched, event);
    }

    void TableViewLastColumnResizingFixer::connectViewHeadersSignals() {
        connect(tableView->horizontalHeader(), &QHeaderView::sectionResized, this,
                &TableViewLastColumnResizingFixer::on_sectionResized);
        connect(tableView->horizontalHeader(), &QHeaderView::geometriesChanged, this,
                &TableViewLastColumnResizingFixer::on_geometriesChanged);
    }

// We need to disconnect these while handling the resize events, otherwise we can enter infinite loops.
    void TableViewLastColumnResizingFixer::disconnectViewHeadersSignals() {
        disconnect(tableView->horizontalHeader(), &QHeaderView::sectionResized, this,
                   &TableViewLastColumnResizingFixer::on_sectionResized);
        disconnect(tableView->horizontalHeader(), &QHeaderView::geometriesChanged, this,
                   &TableViewLastColumnResizingFixer::on_geometriesChanged);
    }

// Setup the resize mode, handles compatibility for Qt5 and below as the method signatures changed.
// Refactored here for readability.
    void
    TableViewLastColumnResizingFixer::setViewHeaderResizeMode(int logicalIndex, QHeaderView::ResizeMode resizeMode) {
        tableView->horizontalHeader()->setSectionResizeMode(logicalIndex, resizeMode);
    }

    void TableViewLastColumnResizingFixer::resizeColumn(int nColumnIndex, int width) {
        tableView->setColumnWidth(nColumnIndex, width);
        tableView->horizontalHeader()->resizeSection(nColumnIndex, width);
    }

    int TableViewLastColumnResizingFixer::getColumnsWidth() {
        int nColumnsWidthSum = 0;
        for (int i = 0; i < columnCount; i++) {
            nColumnsWidthSum += tableView->horizontalHeader()->sectionSize(i);
        }
        return nColumnsWidthSum;
    }

    int TableViewLastColumnResizingFixer::getAvailableWidthForColumn(int column) {
        int nResult = lastColumnMinimumWidth;
        int nTableWidth = tableView->horizontalHeader()->width();

        if (nTableWidth > 0) {
            int nOtherColsWidth = getColumnsWidth() - tableView->horizontalHeader()->sectionSize(column);
            nResult = std::max(nResult, nTableWidth - nOtherColsWidth);
        }

        return nResult;
    }

// Make sure we don't make the columns wider than the table's viewport width.
    void TableViewLastColumnResizingFixer::adjustTableColumnsWidth() {
        disconnectViewHeadersSignals();
        resizeColumn(lastColumnIndex, getAvailableWidthForColumn(lastColumnIndex));
        connectViewHeadersSignals();

        int nTableWidth = tableView->horizontalHeader()->width();
        int nColsWidth = getColumnsWidth();
        if (nColsWidth > nTableWidth) {
            resizeColumn(secondToLastColumnIndex, getAvailableWidthForColumn(secondToLastColumnIndex));
        }
    }

// Make column use all the space available, useful during window resizing.
    void TableViewLastColumnResizingFixer::stretchColumnWidth(int column) {
        disconnectViewHeadersSignals();
        resizeColumn(column, getAvailableWidthForColumn(column));
        connectViewHeadersSignals();
    }

// When a section is resized this is a slot-proxy for ajustAmountColumnWidth().
    void TableViewLastColumnResizingFixer::on_sectionResized(int logicalIndex, int oldSize, int newSize) {
        adjustTableColumnsWidth();
        int remainingWidth = getAvailableWidthForColumn(logicalIndex);
        if (newSize > remainingWidth) {
            resizeColumn(logicalIndex, remainingWidth);
        }
    }

// When the table's geometry is ready, we manually perform the stretch of the "Message" column,
// as the "Stretch" resize mode does not allow for interactive resizing.
    void TableViewLastColumnResizingFixer::on_geometriesChanged() {
        if ((getColumnsWidth() - this->tableView->horizontalHeader()->width()) != 0) {
            disconnectViewHeadersSignals();
            resizeColumn(secondToLastColumnIndex, getAvailableWidthForColumn(secondToLastColumnIndex));
            connectViewHeadersSignals();
        }
    }

/**
 * Initializes all internal variables and prepares the
 * the resize modes of the last 2 columns of the table and
 */
    TableViewLastColumnResizingFixer::TableViewLastColumnResizingFixer(QTableView *table, int lastColMinimumWidth,
                                                                       int allColsMinimumWidth, QObject *parent) :
            QObject(parent),
            tableView(table),
            lastColumnMinimumWidth(lastColMinimumWidth),
            allColumnsMinimumWidth(allColsMinimumWidth) {
        columnCount = tableView->horizontalHeader()->count();
        lastColumnIndex = columnCount - 1;
        secondToLastColumnIndex = columnCount - 2;
        tableView->horizontalHeader()->setMinimumSectionSize(allColumnsMinimumWidth);
        setViewHeaderResizeMode(secondToLastColumnIndex, QHeaderView::Interactive);
        setViewHeaderResizeMode(lastColumnIndex, QHeaderView::Interactive);
    }

#ifdef WIN32
    fs::path static StartupShortcutPath()
    {
        std::string chain = gArgs.GetChainName();
        if (chain == CBaseChainParams::MAIN)
            return GetSpecialFolderPath(CSIDL_STARTUP) / "Raptoreum Core.lnk";
        if (chain == CBaseChainParams::TESTNET) // Remove this special case when CBaseChainParams::TESTNET = "testnet4"
            return GetSpecialFolderPath(CSIDL_STARTUP) / "Raptoreum Core (testnet).lnk";
        return GetSpecialFolderPath(CSIDL_STARTUP) / strprintf("Raptoreum Core (%s).lnk", chain);
    }

    bool GetStartOnSystemStartup()
    {
        // check for "Raptoreum Core*.lnk"
        return fs::exists(StartupShortcutPath());
    }

    bool SetStartOnSystemStartup(bool fAutoStart)
    {
        // If the shortcut exists already, remove it for updating
        fs::remove(StartupShortcutPath());

        if (fAutoStart)
        {
            CoInitialize(nullptr);

            // Get a pointer to the IShellLink interface.
            IShellLinkW* psl = nullptr;
            HRESULT hres = CoCreateInstance(CLSID_ShellLink, nullptr,
                CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                reinterpret_cast<void**>(&psl));

            if (SUCCEEDED(hres))
            {
                // Get the current executable path
                WCHAR pszExePath[MAX_PATH];
                GetModuleFileNameW(nullptr, pszExePath, ARRAYSIZE(pszExePath));

                // Start client minimized
                QString strArgs = "-min";
                // Set -testnet /-regtest options
                strArgs += QString::fromStdString(strprintf(" -testnet=%d -regtest=%d", gArgs.GetBoolArg("-testnet", false), gArgs.GetBoolArg("-regtest", false)));

                // Set the path to the shortcut target
                psl->SetPath(pszExePath);
                PathRemoveFileSpecW(pszExePath);
                psl->SetWorkingDirectory(pszExePath);
                psl->SetShowCmd(SW_SHOWMINNOACTIVE);
                psl->SetArguments(strArgs.toStdWString().c_str());

                // Query IShellLink for the IPersistFile interface for
                // saving the shortcut in persistent storage.
                IPersistFile* ppf = nullptr;
                hres = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
                if (SUCCEEDED(hres))
                {
                    // Save the link by calling IPersistFile::Save.
                    hres = ppf->Save(StartupShortcutPath().wstring().c_str(), TRUE);
                    ppf->Release();
                    psl->Release();
                    CoUninitialize();
                    return true;
                }
                psl->Release();
            }
            CoUninitialize();
            return false;
        }
        return true;
    }
#elif defined(Q_OS_LINUX)

    // Follow the Desktop Application Autostart Spec:
    // http://standards.freedesktop.org/autostart-spec/autostart-spec-latest.html

    fs::path static GetAutostartDir()
    {
        char* pszConfigHome = getenv("XDG_CONFIG_HOME");
        if (pszConfigHome) return fs::path(pszConfigHome) / "autostart";
        char* pszHome = getenv("HOME");
        if (pszHome) return fs::path(pszHome) / ".config" / "autostart";
        return fs::path();
    }

    fs::path static GetAutostartFilePath()
    {
        std::string chain = gArgs.GetChainName();
        if (chain == CBaseChainParams::MAIN)
            return GetAutostartDir() / "raptoreumcore.desktop";
        return GetAutostartDir() / strprintf("raptoreumcore-%s.desktop", chain);
    }

    bool GetStartOnSystemStartup()
    {
        fsbridge::ifstream optionFile(GetAutostartFilePath());
        if (!optionFile.good())
            return false;
        // Scan through file for "Hidden=true":
        std::string line;
        while (!optionFile.eof())
        {
            getline(optionFile, line);
            if (line.find("Hidden") != std::string::npos &&
                line.find("true") != std::string::npos)
                return false;
        }
        optionFile.close();

        return true;
    }

    bool SetStartOnSystemStartup(bool fAutoStart)
    {
        if (!fAutoStart)
            fs::remove(GetAutostartFilePath());
        else
        {
            char pszExePath[MAX_PATH+1];
            ssize_t r = readlink("/proc/self/exe", pszExePath, sizeof(pszExePath) - 1);
            if (r == -1)
                return false;
            pszExePath[r] = '\0';

            fs::create_directories(GetAutostartDir());

            fsbridge::ofstream optionFile(GetAutostartFilePath(), std::ios_base::out | std::ios_base::trunc);
            if (!optionFile.good())
                return false;
            std::string chain = gArgs.GetChainName();
            // Write a raptoreumcore.desktop file to the autostart directory:
            optionFile << "[Desktop Entry]\n";
            optionFile << "Type=Application\n";
            if (chain == CBaseChainParams::MAIN)
                optionFile << "Name=Raptoreum Core\n";
            else
                optionFile << strprintf("Name=Raptoreum Core (%s)\n", chain);
            optionFile << "Exec=" << pszExePath << strprintf(" -min -testnet=%d -regtest=%d\n", gArgs.GetBoolArg("-testnet", false), gArgs.GetBoolArg("-regtest", false));
            optionFile << "Terminal=false\n";
            optionFile << "Hidden=false\n";
            optionFile.close();
        }
        return true;
    }

#else

    bool GetStartOnSystemStartup() { return false; }

    bool SetStartOnSystemStartup(bool fAutoStart) { return false; }

#endif

    void setStyleSheetDirectory(const QString &path) {
        stylesheetDirectory = path;
    }

    bool isStyleSheetDirectoryCustom() {
        return stylesheetDirectory != defaultStylesheetDirectory;
    }

    const std::vector <QString> listStyleSheets() {
        std::vector <QString> vecStylesheets;
        for (const auto &it: mapThemeToStyle) {
            vecStylesheets.push_back(it.second);
        }
        return vecStylesheets;
    }

    const std::vector <QString> listThemes() {
        std::vector <QString> vecThemes;
        for (const auto &it: mapThemeToStyle) {
            if (it.first == generalTheme) continue;
            vecThemes.push_back(it.first);
        }
        return vecThemes;
    }

    const QString getDefaultTheme() {
        return defaultTheme;
    }

    bool isValidTheme(const QString &strTheme) {
        // return strTheme == defaultTheme || strTheme == darkThemePrefix || strTheme == traditionalTheme;
        return strTheme == lightThemePrefix || strTheme == darkThemePrefix || strTheme == traditionalTheme;
    }

    void loadStyleSheet(bool fForceUpdate) {
        AssertLockNotHeld(cs_css);
        LOCK(cs_css);

        static std::unique_ptr <QString> stylesheet;

        bool fDebugCustomStyleSheets = gArgs.GetBoolArg("-debug-ui", false) && isStyleSheetDirectoryCustom();
        bool fStyleSheetChanged = false;

        if (stylesheet == nullptr || fForceUpdate || fDebugCustomStyleSheets) {
            auto hasModified = [](const std::vector <QString> &vecFiles) -> bool {
                static std::map<const QString, QDateTime> mapLastModified;

                bool fModified = false;
                for (auto file = vecFiles.begin(); file != vecFiles.end() && !fModified; ++file) {
                    QFileInfo info(*file);
                    QDateTime lastModified = info.lastModified(), prevLastModified;
                    auto it = mapLastModified.emplace(std::make_pair(*file, lastModified));
                    prevLastModified = it.second ? QDateTime() : it.first->second;
                    it.first->second = lastModified;
                    fModified = prevLastModified != lastModified;
                }
                return fModified;
            };

            auto loadFiles = [&](const std::vector <QString> &vecFiles) -> bool {
                if (!fForceUpdate && fDebugCustomStyleSheets && !hasModified(vecFiles)) {
                    return false;
                }

                std::string platformName = gArgs.GetArg("-uiplatform", BitcoinGUI::DEFAULT_UIPLATFORM);
                stylesheet = std::make_unique<QString>();

                for (const auto &file: vecFiles) {
                    QFile qFile(file);
                    if (!qFile.open(QFile::ReadOnly)) {
                        throw std::runtime_error(
                                strprintf("%s: Failed to open file: %s", __func__, file.toStdString()));
                    }

                    QString strStyle = QLatin1String(qFile.readAll());
                    // Process all <os=...></os> groups in the stylesheet first
                    QRegularExpressionMatch osStyleMatch;
                    QRegularExpression osStyleExp(
                            "^"
                            "(<os=(?:'|\").+(?:'|\")>)" // group 1
                            "((?:.|\n)+?)"              // group 2
                            "(</os>?)"                  // group 3
                            "$");
                    osStyleExp.setPatternOptions(QRegularExpression::MultilineOption);
                    QRegularExpressionMatchIterator it = osStyleExp.globalMatch(strStyle);

                    // For all <os=...></os> sections
                    while (it.hasNext() && (osStyleMatch = it.next()).isValid()) {
                        QStringList listMatches = osStyleMatch.capturedTexts();

                        // Full match + 3 group matches
                        if (listMatches.size() % 4) {
                            throw std::runtime_error(strprintf("%s: Invalid <os=...></os> section in file %s", __func__,
                                                               file.toStdString()));
                        }

                        for (int i = 0; i < listMatches.size(); i += 4) {
                            if (!listMatches[i + 1].contains(QString::fromStdString(platformName))) {
                                // If os is not supported for this styles
                                // just remove the full match
                                strStyle.replace(listMatches[i], "");
                            } else {
                                // If its supported remove the <os=...></os> tags
                                strStyle.replace(listMatches[i + 1], "");
                                strStyle.replace(listMatches[i + 3], "");
                            }
                        }
                    }
                    stylesheet->append(strStyle);
                }
                return true;
            };

            auto pathToFile = [&](const QString &theme) -> QString {
                return stylesheetDirectory + "/" + (isStyleSheetDirectoryCustom() ? mapThemeToStyle.at(theme) : theme);
            };

            std::vector <QString> vecFiles;
            // If light/dark theme is used load general styles first
            if (raptoreumThemeActive()) {
                vecFiles.push_back(pathToFile(generalTheme));
            }
            vecFiles.push_back(pathToFile(getActiveTheme()));

            fStyleSheetChanged = loadFiles(vecFiles);
        }

        bool fUpdateStyleSheet = fForceUpdate || (fDebugCustomStyleSheets && fStyleSheetChanged);

        if (fUpdateStyleSheet && stylesheet != nullptr) {
            qApp->setStyleSheet(*stylesheet);
        }
    }

    FontFamily fontFamilyFromString(const QString &strFamily) {
        if (strFamily == "SystemDefault") {
            return FontFamily::SystemDefault;
        }
        if (strFamily == "Montserrat") {
            return FontFamily::Montserrat;
        }
        if (strFamily == "Manrope") {
            return FontFamily::Manrope;
        }
        throw std::invalid_argument(strprintf("Invalid font-family: %s", strFamily.toStdString()));
    }

    QString fontFamilyToString(FontFamily family) {
        switch (family) {
            case FontFamily::SystemDefault:
                return "SystemDefault";
            case FontFamily::Montserrat:
                return "Montserrat";
            case FontFamily::Manrope:
                return "Manrope";
            default:
                assert(false);
        }
    }

    void setFontFamily(FontFamily family) {
        fontFamily = family;
        setApplicationFont();
        updateFonts();
    }

    FontFamily getFontFamilyDefault() {
        return defaultFontFamily;
    }

    FontFamily getFontFamily() {
        return fontFamily;
    }

    bool weightFromArg(int nArg, QFont::Weight &weight) {
        const std::map<int, QFont::Weight> mapWeight{
                {0, QFont::Thin},
                {1, QFont::ExtraLight},
                {2, QFont::Light},
                {3, QFont::Normal},
                {4, QFont::Medium},
                {5, QFont::DemiBold},
                {6, QFont::Bold},
                {7, QFont::ExtraBold},
                {8, QFont::Black}
        };
        auto it = mapWeight.find(nArg);
        if (it == mapWeight.end()) {
            return false;
        }
        weight = it->second;
        return true;
    }

    int weightToArg(const QFont::Weight weight) {
        const std::map<QFont::Weight, int> mapWeight{
                {QFont::Thin,       0},
                {QFont::ExtraLight, 1},
                {QFont::Light,      2},
                {QFont::Normal,     3},
                {QFont::Medium,     4},
                {QFont::DemiBold,   5},
                {QFont::Bold,       6},
                {QFont::ExtraBold,  7},
                {QFont::Black,      8}
        };
        assert(mapWeight.count(weight));
        return mapWeight.find(weight)->second;
    }

    QFont::Weight getFontWeightNormalDefault() {
        return defaultFontWeightNormal;
    }

    QFont::Weight toQFontWeight(FontWeight weight) {
        return weight == FontWeight::Bold ? getFontWeightBold() : getFontWeightNormal();
    }

    QFont::Weight getFontWeightNormal() {
        if (!mapWeights.count(fontFamily)) {
            return defaultFontWeightNormal;
        }
        return mapWeights[fontFamily].first;
    }

    void setFontWeightNormal(QFont::Weight weight) {
        if (!mapWeights.count(fontFamily)) {
            throw std::runtime_error(strprintf("%s: Font family not loaded: %s", __func__,
                                               fontFamilyToString(fontFamily).toStdString()));
        }
        mapWeights[fontFamily].first = weight;
        updateFonts();
    }

    QFont::Weight getFontWeightBoldDefault() {
        return defaultFontWeightBold;
    }

    QFont::Weight getFontWeightBold() {
        if (!mapWeights.count(fontFamily)) {
            return defaultFontWeightBold;
        }
        return mapWeights[fontFamily].second;
    }

    void setFontWeightBold(QFont::Weight weight) {
        if (!mapWeights.count(fontFamily)) {
            throw std::runtime_error(strprintf("%s: Font family not loaded: %s", __func__,
                                               fontFamilyToString(fontFamily).toStdString()));
        }
        mapWeights[fontFamily].second = weight;
        updateFonts();
    }

    int getFontScaleDefault() {
        return defaultFontScale;
    }

    int getFontScale() {
        return fontScale;
    }

    void setFontScale(int nScale) {
        fontScale = nScale;
        updateFonts();
    }

    double getScaledFontSize(int nSize) {
        return std::round(nSize * (1 + (fontScale * fontScaleSteps)) * 4) / 4.0;
    }

    bool loadFonts() {
        // Before any font changes store the applications default font to use it as SystemDefault.
        osDefaultFont = std::make_unique<QFont>(QApplication::font());

        QString family = fontFamilyToString(FontFamily::Montserrat);
        QString italic = "Italic";

        std::map<QString, bool> mapStyles{
                {"Thin",       true},
                {"ExtraLight", true},
                {"Light",      true},
                {"Italic",     false},
                {"Regular",    false},
                {"Medium",     true},
                {"SemiBold",   true},
                {"Bold",       true},
                {"ExtraBold",  true},
                {"Black",      true},
        };

        QFontDatabase database;
        std::vector<int> vecFontIds;

        for (const auto &it: mapStyles) {
            QString font = ":fonts/" + family + "-" + it.first;
            vecFontIds.push_back(QFontDatabase::addApplicationFont(font));
            qDebug() << __func__ << ": " << font << " loaded with id " << vecFontIds.back();
            if (it.second) {
                vecFontIds.push_back(QFontDatabase::addApplicationFont(font + italic));
                qDebug() << __func__ << ": " << font + italic << " loaded with id " << vecFontIds.back();
            }
        }

        // Fail if an added id is -1 which means QFontDatabase::addApplicationFont failed.
        if (std::find(vecFontIds.begin(), vecFontIds.end(), -1) != vecFontIds.end()) {
            osDefaultFont = nullptr;
            return false;
        }

        // Print debug logs for added fonts fetched by the added ids
        for (const auto &i: vecFontIds) {
            auto families = QFontDatabase::applicationFontFamilies(i);
            for (const QString &f: families) {
                qDebug() << __func__ << ": - Font id " << i << " is family: " << f;
                const QStringList fontStyles = database.styles(f);
                for (const QString &style: fontStyles) {
                    qDebug() << __func__ << ": Style for family " << f << " with id: " << i << ": " << style;
                }
            }
        }
        // Print debug logs for added fonts fetched by the family name
        const QStringList fontFamilies = database.families();
        for (const QString &f: fontFamilies) {
            if (f.contains(family)) {
                const QStringList fontStyles = database.styles(f);
                for (const QString &style: fontStyles) {
                    qDebug() << __func__ << ": Family: " << f << ", Style: " << style;
                }
            }
        }

        setApplicationFont();

        // Initialize supported font weights for all available fonts
        // Generate a vector with supported font weights by comparing the width of a certain test text for all font weights
        auto supportedWeights = [](FontFamily family) -> std::vector <QFont::Weight> {
            auto getTestWidth = [&](QFont::Weight weight) -> int {
                QFont font = getFont(family, weight, false, defaultFontSize);
                return QFontMetrics(font).horizontalAdvance(
                        "Check the width of this text to see if the weight change has an impact!");
            };
            std::vector <QFont::Weight> vecWeights{QFont::Thin, QFont::ExtraLight, QFont::Light,
                                                   QFont::Normal, QFont::Medium, QFont::DemiBold,
                                                   QFont::Bold, QFont::ExtraBold, QFont::Black};
            std::vector <QFont::Weight> vecSupported;
            QFont::Weight prevWeight = vecWeights.front();
            for (auto weight = vecWeights.begin() + 1; weight != vecWeights.end(); ++weight) {
                if (getTestWidth(prevWeight) != getTestWidth(*weight)) {
                    if (vecSupported.empty()) {
                        vecSupported.push_back(prevWeight);
                    }
                    vecSupported.push_back(*weight);
                }
                prevWeight = *weight;
            }
            if (vecSupported.empty()) {
                vecSupported.push_back(QFont::Normal);
            }
            return vecSupported;
        };

        mapSupportedWeights.insert(
                std::make_pair(FontFamily::SystemDefault, supportedWeights(FontFamily::SystemDefault)));
        mapSupportedWeights.insert(std::make_pair(FontFamily::Montserrat, supportedWeights(FontFamily::Montserrat)));
        mapSupportedWeights.insert(std::make_pair(FontFamily::Manrope, supportedWeights(FontFamily::Manrope)));

        auto getBestMatch = [&](FontFamily fontFamily, QFont::Weight targetWeight) {
            auto &vecSupported = mapSupportedWeights[fontFamily];
            auto it = vecSupported.begin();
            QFont::Weight bestWeight = *it;
            int nBestDiff = abs(*it - targetWeight);
            while (++it != vecSupported.end()) {
                int nDiff = abs(*it - targetWeight);
                if (nDiff < nBestDiff) {
                    bestWeight = *it;
                    nBestDiff = nDiff;
                }
            }
            return bestWeight;
        };

        auto addBestDefaults = [&](FontFamily family) -> auto {
            QFont::Weight normalWeight = getBestMatch(family, defaultFontWeightNormal);
            QFont::Weight boldWeight = getBestMatch(family, defaultFontWeightBold);
            if (normalWeight == boldWeight) {
                // If the results are the same use the next possible weight for bold font
                auto &vecSupported = mapSupportedWeights[fontFamily];
                auto it = std::find(vecSupported.begin(), vecSupported.end(), normalWeight);
                if (++it != vecSupported.end()) {
                    boldWeight = *it;
                }
            }
            mapDefaultWeights.emplace(family, std::make_pair(normalWeight, boldWeight));
        };

        addBestDefaults(FontFamily::SystemDefault);
        addBestDefaults(FontFamily::Montserrat);
        addBestDefaults(FontFamily::Manrope);

        // Load supported defaults. May become overwritten later.
        mapWeights = mapDefaultWeights;

        return true;
    }

    bool fontsLoaded() {
        return osDefaultFont != nullptr;
    }

    void setApplicationFont() {
        if (!fontsLoaded()) {
            return;
        }

        std::unique_ptr <QFont> font;

        if (fontFamily == FontFamily::Montserrat) {
            QString family = fontFamilyToString(FontFamily::Montserrat);
#ifdef Q_OS_MAC
            if (getFontWeightNormal() != getFontWeightNormalDefault()) {
                font = std::make_unique<QFont>(getFontNormal());
            } else {
                font = std::make_unique<QFont>(family);
                font->setWeight(getFontWeightNormalDefault());
            }
#else
            font = std::make_unique<QFont>(family);
            font->setWeight(getFontWeightNormal());
#endif
        } else if (fontFamily == FontFamily::Manrope) {
            QString family = fontFamilyToString(FontFamily::Manrope);
#ifdef Q_OS_MAC
            if (getFontWeightNormal() != getFontWeightNormalDefault()) {
                font = std::make_unique<QFont>(getFontNormal());
            } else {
                font = std::make_unique<QFont>(family);
                font->setWeight(getFontWeightNormalDefault());
            }
#else
            font = std::make_unique<QFont>(family);
            font->setWeight(getFontWeightNormal());
#endif
        } else {
            font = std::make_unique<QFont>(*osDefaultFont);
        }

        font->setPointSizeF(defaultFontSize);
        qApp->setFont(*font);

        qDebug() << __func__ << ": " << qApp->font().toString() <<
                 " family: " << qApp->font().family() <<
                 ", style: " << qApp->font().styleName() <<
                 " match: " << qApp->font().exactMatch();
    }

    void setFont(const std::vector<QWidget *> &vecWidgets, FontWeight weight, int nPointSize, bool fItalic) {
        for (auto it: vecWidgets) {
            auto fontAttributes = std::make_tuple(weight, fItalic, nPointSize);
            auto itFontUpdate = mapFontUpdates.emplace(std::make_pair(it, fontAttributes));
            if (!itFontUpdate.second) {
                itFontUpdate.first->second = fontAttributes;
            }
        }
    }

    void updateFonts() {
        // Fonts need to be loaded by GUIIUtil::loadFonts(), if not just return.
        if (!osDefaultFont) {
            return;
        }

        static std::map<QPointer<QWidget>, int> mapWidgetDefaultFontSizes;

        // QPointer becomes nullptr for objects that were deleted.
        // Remove them from mapDefaultFontSize and mapFontUpdates
        // before proceeding any further.
        size_t nRemovedDefaultFonts{0};
        auto itd = mapWidgetDefaultFontSizes.begin();
        while (itd != mapWidgetDefaultFontSizes.end()) {
            if (itd->first.isNull()) {
                itd = mapWidgetDefaultFontSizes.erase(itd);
                ++nRemovedDefaultFonts;
            } else {
                ++itd;
            }
        }

        size_t nRemovedFontUpdates{0};
        auto itn = mapFontUpdates.begin();
        while (itn != mapFontUpdates.end()) {
            if (itn->first.isNull()) {
                itn = mapFontUpdates.erase(itn);
                ++nRemovedFontUpdates;
            } else {
                ++itn;
            }
        }

        size_t nUpdatable{0}, nUpdated{0};
        std::map < QWidget * , QFont > mapWidgetFonts;
        // Loop through all widgets
        for (QWidget *w: qApp->allWidgets()) {
            std::vector <QString> vecIgnoreClasses{
                    "QWidget", "QDialog", "QFrame", "QStackedWidget", "QDesktopWidget", "QDesktopScreenWidget",
                    "QTipLabel", "QMessageBox", "QMenu", "QComboBoxPrivateScroller", "QComboBoxPrivateContainer",
                    "QScrollBar", "QListView", "BitcoinGUI", "WalletView", "WalletFrame"
            };
            std::vector <QString> vecIgnoreObjects{
                    "messagesWidget"
            };
            if (std::find(vecIgnoreClasses.begin(), vecIgnoreClasses.end(), w->metaObject()->className()) !=
                vecIgnoreClasses.end() ||
                std::find(vecIgnoreObjects.begin(), vecIgnoreObjects.end(), w->objectName()) !=
                vecIgnoreObjects.end()) {
                continue;
            }
            ++nUpdatable;

            QFont font = w->font();
            assert(font.pointSize() > 0);
            font.setFamily(qApp->font().family());
            font.setWeight(getFontWeightNormal());
            font.setStyleName(qApp->font().styleName());
            font.setStyle(qApp->font().style());

            // Insert/Get the default font size of the widget
            auto itDefault = mapWidgetDefaultFontSizes.emplace(w, font.pointSize());

            auto it = mapFontUpdates.find(w);
            if (it != mapFontUpdates.end()) {
                int nSize = std::get<2>(it->second);
                if (nSize == -1) {
                    nSize = itDefault.first->second;
                }
                font = getFont(std::get<0>(it->second), std::get<1>(it->second), nSize);
            } else {
                font.setPointSizeF(getScaledFontSize(itDefault.first->second));
            }

            if (w->font() != font) {
                auto itWidgetFont = mapWidgetFonts.emplace(w, font);
                assert(itWidgetFont.second);
                ++nUpdated;
            }
        }
        qDebug().nospace() << __func__ << " - widget counts: updated/updatable/total(" << nUpdated << "/" << nUpdatable
                           << "/" << qApp->allWidgets().size() << ")"
                           << ", removed items: mapWidgetDefaultFontSizes/mapFontUpdates(" << nRemovedDefaultFonts
                           << "/" << nRemovedFontUpdates << ")";

        // Perform the required font updates
        // NOTE: This is done as seperate step to avoid scaling issues due to font inheritance
        //       hence all fonts are calculated and stored in mapWidgetFonts above.
        for (auto it: mapWidgetFonts) {
            it.first->setFont(it.second);
        }

        // Scale the global font size for the classes in the map below
        static std::map<std::string, int> mapClassFontUpdates{
                {"QTipLabel",   -1},
                {"QMenu",       -1},
                {"QMessageBox", -1}
        };
        for (auto &it: mapClassFontUpdates) {
            QFont fontClass = qApp->font(it.first.c_str());
            if (it.second == -1) {
                it.second = fontClass.pointSize();
            }
            double dSize = getScaledFontSize(it.second);
            if (fontClass.pointSizeF() != dSize) {
                fontClass.setPointSizeF(dSize);
                qApp->setFont(fontClass, it.first.c_str());
            }
        }
    }

    QFont getFont(FontFamily family, QFont::Weight qWeight, bool fItalic, int nPointSize) {
        QFont font;
        if (!fontsLoaded()) {
            return font;
        }

        if (family == FontFamily::Montserrat) {
            static std::map <QFont::Weight, QString> mapMontserratMapping{
                    {QFont::Thin, "Thin"},
                    {QFont::ExtraLight, "ExtraLight"},
                    {QFont::Light, "Light"},
                    {QFont::Medium, "Medium"},
                    {QFont::DemiBold, "SemiBold"},
                    {QFont::ExtraBold, "ExtraBold"},
                    {QFont::Black, "Black"},
#ifdef Q_OS_MAC
                    {QFont::Normal, "Regular"},
                    {QFont::Bold, "Bold"},
#else
                    {QFont::Normal, "Regular"},
                    {QFont::Bold, "Bold"},
#endif
            };

            assert(mapMontserratMapping.count(qWeight));

#ifdef Q_OS_MAC

            QString styleName = mapMontserratMapping[qWeight];

            if (fItalic) {
                if (styleName == "Regular") {
                    styleName = "Italic";
                } else {
                    styleName += " Italic";
                }
            }

            font.setFamily(fontFamilyToString(FontFamily::Montserrat));
            font.setStyleName(styleName);
#else
            font.setFamily(fontFamilyToString(FontFamily::Montserrat) + " " + mapMontserratMapping[qWeight]);
            font.setWeight(qWeight);
            font.setStyle(fItalic ? QFont::StyleItalic : QFont::StyleNormal);
#endif
        } else if (family == FontFamily::Manrope) {
            static std::map <QFont::Weight, QString> mapManropeMapping{
                    {QFont::Thin, "Thin"},
                    {QFont::ExtraLight, "ExtraLight"},
                    {QFont::Light, "Light"},
                    {QFont::Medium, "Medium"},
                    {QFont::DemiBold, "SemiBold"},
                    {QFont::ExtraBold, "ExtraBold"},
                    {QFont::Black, "Black"},
#ifdef Q_OS_MAC
                    {QFont::Normal, "Regular"},
                    {QFont::Bold, "Bold"},
#else
                    {QFont::Normal, "Regular"},
                    {QFont::Bold, "Bold"},
#endif
            };

            assert(mapManropeMapping.count(qWeight));

#ifdef Q_OS_MAC

            QString styleName = mapManropeMapping[qWeight];

            if (fItalic) {
                if (styleName == "Regular") {
                    styleName = "Italic";
                } else {
                    styleName += " Italic";
                }
            }

            font.setFamily(fontFamilyToString(FontFamily::Manrope));
            font.setStyleName(styleName);
#else
            font.setFamily(fontFamilyToString(FontFamily::Manrope) + " " + mapManropeMapping[qWeight]);
            font.setWeight(qWeight);
            font.setStyle(fItalic ? QFont::StyleItalic : QFont::StyleNormal);
#endif
        } else {
            font.setFamily(osDefaultFont->family());
            font.setWeight(qWeight);
            font.setStyle(fItalic ? QFont::StyleItalic : QFont::StyleNormal);
        }

        if (nPointSize != -1) {
            font.setPointSizeF(getScaledFontSize(nPointSize));
        }

        if (gArgs.GetBoolArg("-debug-ui", false)) {
            qDebug() << __func__ << ": font size: " << font.pointSizeF() << " family: " << font.family() << ", style: "
                     << font.styleName() << ", weight:" << font.weight() << " match: " << font.exactMatch();
        }

        return font;
    }

    QFont getFont(QFont::Weight qWeight, bool fItalic, int nPointSize) {
        return getFont(fontFamily, qWeight, fItalic, nPointSize);
    }

    QFont getFont(FontWeight weight, bool fItalic, int nPointSize) {
        return getFont(toQFontWeight(weight), fItalic, nPointSize);
    }

    QFont getFontNormal() {
        return getFont(FontWeight::Normal);
    }

    QFont getFontBold() {
        return getFont(FontWeight::Bold);
    }

    QFont::Weight getSupportedFontWeightNormalDefault() {
        if (!mapDefaultWeights.count(fontFamily)) {
            throw std::runtime_error(strprintf("%s: Font family not loaded: %s", __func__,
                                               fontFamilyToString(fontFamily).toStdString()));
        }
        return mapDefaultWeights[fontFamily].first;
    }

    QFont::Weight getSupportedFontWeightBoldDefault() {
        if (!mapDefaultWeights.count(fontFamily)) {
            throw std::runtime_error(strprintf("%s: Font family not loaded: %s", __func__,
                                               fontFamilyToString(fontFamily).toStdString()));
        }
        return mapDefaultWeights[fontFamily].second;
    }

    std::vector <QFont::Weight> getSupportedWeights() {
        assert(mapSupportedWeights.count(fontFamily));
        return mapSupportedWeights[fontFamily];
    }

    QFont::Weight supportedWeightFromIndex(int nIndex) {
        auto vecWeights = getSupportedWeights();
        assert(vecWeights.size() > uint64_t(nIndex));
        return vecWeights[nIndex];
    }

    int supportedWeightToIndex(QFont::Weight weight) {
        auto vecWeights = getSupportedWeights();
        for (uint64_t index = 0; index < vecWeights.size(); ++index) {
            if (weight == vecWeights[index]) {
                return index;
            }
        }
        return -1;
    }

    bool isSupportedWeight(const QFont::Weight weight) {
        return supportedWeightToIndex(weight) != -1;
    }

    QString getActiveTheme() {
        QSettings settings;
        QString theme = settings.value("theme", defaultTheme).toString();
        if (!isValidTheme(theme)) {
            return defaultTheme;
        }
        return theme;
    }

    bool raptoreumThemeActive() {
        QSettings settings;
        QString theme = settings.value("theme", defaultTheme).toString();
        return theme != traditionalTheme;
    }

    void loadTheme(bool fForce) {
        loadStyleSheet(fForce);
        updateFonts();
        updateMacFocusRects();
    }

    void disableMacFocusRect(const QWidget *w) {
#ifdef Q_OS_MAC
        for (const auto& c : w->findChildren<QWidget*>()) {
            if (c->testAttribute(Qt::WA_MacShowFocusRect)) {
                c->setAttribute(Qt::WA_MacShowFocusRect, !raptoreumThemeActive());
                setRectsDisabled.emplace(c);
            }
        }
#endif
    }

    void updateMacFocusRects() {
#ifdef Q_OS_MAC
        QWidgetList allWidgets = QApplication::allWidgets();
        auto it = setRectsDisabled.begin();
        while (it != setRectsDisabled.end()) {
            if (allWidgets.contains(*it)) {
                (*it)->setAttribute(Qt::WA_MacShowFocusRect, !raptoreumThemeActive());
                ++it;
            } else {
                it = setRectsDisabled.erase(it);
            }
        }
#endif
    }

    void updateButtonGroupShortcuts(QButtonGroup *buttonGroup) {
        if (buttonGroup == nullptr) {
            return;
        }
#ifdef Q_OS_MAC
        auto modifier = Qt::CTRL;
#else
        auto modifier = Qt::ALT;
#endif
        int nKey = 0;
        for (auto button: buttonGroup->buttons()) {
            if (button->isVisible()) {
                button->setShortcut(QKeySequence(modifier + Qt::Key_1 + nKey++));
            } else {
                button->setShortcut(QKeySequence());
            }
        }
    }

    void setClipboard(const QString &str) {
        QApplication::clipboard()->setText(str, QClipboard::Clipboard);
        QApplication::clipboard()->setText(str, QClipboard::Selection);
    }

    fs::path qstringToBoostPath(const QString &path) {
        return fs::path(path.toStdString());
    }

    QString boostPathToQString(const fs::path &path) {
        return QString::fromStdString(path.string());
    }

    QString formatDurationStr(int secs) {
        QStringList strList;
        int days = secs / 86400;
        int hours = (secs % 86400) / 3600;
        int mins = (secs % 3600) / 60;
        int seconds = secs % 60;

        if (days)
            strList.append(QString(QObject::tr("%1 d")).arg(days));
        if (hours)
            strList.append(QString(QObject::tr("%1 h")).arg(hours));
        if (mins)
            strList.append(QString(QObject::tr("%1 m")).arg(mins));
        if (seconds || (!days && !hours && !mins))
            strList.append(QString(QObject::tr("%1 s")).arg(seconds));

        return strList.join(" ");
    }

    QString formatServicesStr(quint64 mask) {
        QStringList strList;

        for (const auto &flag: serviceFlagsToStr(mask)) {
            strList.append(QString::fromStdString(flag));
        }

        if (strList.size())
            return strList.join(", ");
        else
            return QObject::tr("None");
    }

    QString formatPingTime(int64_t ping_usec) {
        return (ping_usec == std::numeric_limits<int64_t>::max() || ping_usec == 0) ? QObject::tr("N/A") : QString(
                QObject::tr("%1 ms")).arg(QString::number((int) (ping_usec / 1000), 10));
    }

    QString formatTimeOffset(int64_t nTimeOffset) {
        return QString(QObject::tr("%1 s")).arg(QString::number((int) nTimeOffset, 10));
    }

    QString formatNiceTimeOffset(qint64 secs) {
        // Represent time from last generated block in human readable text
        QString timeBehindText;
        const int HOUR_IN_SECONDS = 3600;
        const int DAY_IN_SECONDS = 86400;
        const int WEEK_IN_SECONDS = 604800;
        const int YEAR_IN_SECONDS = 31536000; // seconds at common year
        if (secs < 60) {
            timeBehindText = QObject::tr("%n second(s)", "", secs);
        } else if (secs < 2 * HOUR_IN_SECONDS) {
            timeBehindText = QObject::tr("%n minute(s)", "", secs / 60);
        } else if (secs < 2 * DAY_IN_SECONDS) {
            timeBehindText = QObject::tr("%n hour(s)", "", secs / HOUR_IN_SECONDS);
        } else if (secs < 2 * WEEK_IN_SECONDS) {
            timeBehindText = QObject::tr("%n day(s)", "", secs / DAY_IN_SECONDS);
        } else if (secs < YEAR_IN_SECONDS) {
            timeBehindText = QObject::tr("%n week(s)", "", secs / WEEK_IN_SECONDS);
        } else {
            qint64 years = secs / YEAR_IN_SECONDS;
            qint64 remainder = secs % YEAR_IN_SECONDS;
            timeBehindText = QObject::tr("%1 and %2").arg(QObject::tr("%n year(s)", "", years)).arg(
                    QObject::tr("%n week(s)", "", remainder / WEEK_IN_SECONDS));
        }
        return timeBehindText;
    }

    QString formatBytes(uint64_t bytes) {
        if (bytes < 1024)
            return QString(QObject::tr("%1 B")).arg(bytes);
        if (bytes < 1024 * 1024)
            return QString(QObject::tr("%1 KB")).arg(bytes / 1024);
        if (bytes < 1024 * 1024 * 1024)
            return QString(QObject::tr("%1 MB")).arg(bytes / 1024 / 1024);

        return QString(QObject::tr("%1 GB")).arg(bytes / 1024 / 1024 / 1024);
    }

    qreal calculateIdealFontSize(int width, const QString &text, QFont font, qreal minPointSize, qreal font_size) {
        while (font_size >= minPointSize) {
            font.setPointSizeF(font_size);
            QFontMetrics fm(font);
            if (TextWidth(fm, text) < width) {
                break;
            }
            font_size -= 0.5;
        }
        return font_size;
    }

    void ClickableLabel::mouseReleaseEvent(QMouseEvent *event) {
        Q_EMIT clicked(event->pos());
    }

    void ClickableProgressBar::mouseReleaseEvent(QMouseEvent *event) {
        Q_EMIT clicked(event->pos());
    }

    bool ItemDelegate::eventFilter(QObject *object, QEvent *event) {
        if (event->type() == QEvent::KeyPress) {
            if (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
                Q_EMIT keyEscapePressed();
            }
        }
        return QItemDelegate::eventFilter(object, event);
    }

    void PolishProgressDialog(QProgressDialog *dialog) {
#ifdef Q_OS_MAC
        // Workaround for macOS-only QT bug. see: QTBUG-65750, QTBUG-70357.
        const int margin = TextWidth(dialog->fontMetrics(), ("X"));
        dialog->resize(dialog->width() + 2 * margin, dialog->height());
        dialog->show();
#else
        Q_UNUSED(dialog);
#endif
    }

    QDateTime StartOfDay(const QDate &date) {
        return date.startOfDay();
    }

    int TextWidth(const QFontMetrics &fm, const QString &text) {
        return fm.horizontalAdvance(text);
    }

    bool HasPixmap(const QLabel *label) {
        return !label->pixmap(Qt::ReturnByValue).isNull();
    }

    QImage GetImage(const QLabel *label) {
        if (!HasPixmap(label)) {
            return QImage();
        }
        return label->pixmap(Qt::ReturnByValue).toImage();
    }

    void LogQtInfo() {
#ifdef QT_STATIC
        const std::string qt_link{"static"};
#else
        const std::string qt_link{"dynamic"};
#endif
#ifdef QT_STATICPLUGIN
        const std::string plugin_link{"static"};
#else
        const std::string plugin_link{"dynamic"};
#endif
        LogPrintf("Qt %s (%s), plugin=%s (%s)\n", qVersion(), qt_link, QGuiApplication::platformName().toStdString(),
                  plugin_link);
        LogPrintf("System: %s, %s\n", QSysInfo::prettyProductName().toStdString(), QSysInfo::buildAbi().toStdString());
        for (const QScreen *s: QGuiApplication::screens()) {
            LogPrintf("Screen: %s %dx%d, pixel ratio=%.1f\n", s->name().toStdString(), s->size().width(),
                      s->size().height(), s->devicePixelRatio());
        }
    }

} // namespace GUIUtil
