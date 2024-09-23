// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_RAPTOREUM_H
#define BITCOIN_QT_RAPTOREUM_H

#if defined(HAVE_CONFIG_H)
#include <config/raptoreum-config.h>
#endif

#include <QApplication>
#include <memory>
#include <vector>

#include <interfaces/node.h>

class BitcoinGUI;

class ClientModel;

class NetworkStyle;

class OptionsModel;

class PaymentServer;

class WalletController;

class WalletModel;

/** Class encapsulating Bitcoin Core startup and shutdown.
 * Allows running startup and shutdown in a different thread from the UI thread.
 */
class BitcoinCore : public QObject {
    Q_OBJECT
public:
    explicit BitcoinCore(interfaces::Node &node);

public
    Q_SLOTS:
            void initialize();

    void shutdown();

    void restart(QStringList args);

    Q_SIGNALS:
            void initializeResult(bool
    success,
    interfaces::BlockAndHeaderTipInfo tip_info
    );

    void shutdownResult();

    void runawayException(const QString &message);

private:
    /// Pass fatal exception message to UI thread
    void handleRunawayException(const std::exception_ptr e);

    interfaces::Node &m_node;
};

/** Main Bitcoin application object */
class BitcoinApplication : public QApplication {
    Q_OBJECT
public:
    explicit BitcoinApplication(interfaces::Node &node);

    ~BitcoinApplication();

#ifdef ENABLE_WALLET
    /// Create payment server
    void createPaymentServer();
#endif

    /// parameter interaction/setup based on rules
    void parameterSetup();

    /// Create options model
    void createOptionsModel(bool resetSettings);

    /// Create main window
    void createWindow(const NetworkStyle *networkStyle);

    /// Create splash screen
    void createSplashScreen(const NetworkStyle *networkStyle);

    /// Basic initialization, before starting initialization/shutdown thread. Return true on success.
    bool baseInitialize();

    /// Request core initialization
    void requestInitialize();

    /// Request core shutdown
    void requestShutdown();

    /// Get process return value
    int getReturnValue() const { return returnValue; }

    /// Get window identifier of QMainWindow (BitcoinGUI)
    WId getMainWinId() const;

public
    Q_SLOTS:
            void initializeResult(bool
    success,
    interfaces::BlockAndHeaderTipInfo tip_info
    );

    void shutdownResult();

    /// Handle runaway exceptions. Shows a message box with the problem and quits the program.
    void handleRunawayException(const QString &message);

    Q_SIGNALS:
            void requestedInitialize();

    void requestedRestart(QStringList args);

    void requestedShutdown();

    void splashFinished();

    void windowShown(BitcoinGUI *window);

private:
    QThread *coreThread;
    interfaces::Node &m_node;
    OptionsModel *optionsModel;
    ClientModel *clientModel;
    BitcoinGUI *window;
    QTimer *pollShutdownTimer;
#ifdef ENABLE_WALLET
    PaymentServer* paymentServer{nullptr};
    WalletController* m_wallet_controller{nullptr};
#endif
    int returnValue;
    std::unique_ptr <QWidget> shutdownWindow;

    void startThread();
};

int GuiMain(int argc, char *argv[]);

#endif // BITCOIN_QT_RAPTOREUM_H
