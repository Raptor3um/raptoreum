// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_CLIENTMODEL_H
#define BITCOIN_QT_CLIENTMODEL_H

#include <interfaces/node.h>
#include <sync.h>

#include <QObject>
#include <QDateTime>

#include <atomic>
#include <memory>

class BanTableModel;

class OptionsModel;

class PeerTableModel;

class CBlockIndex;

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

enum class BlockSource {
    NONE,
    REINDEX,
    DISK,
    NETWORK
};

enum NumConnections {
    CONNECTIONS_NONE = 0,
    CONNECTIONS_IN = (1U << 0),
    CONNECTIONS_OUT = (1U << 1),
    CONNECTIONS_ALL = (CONNECTIONS_IN | CONNECTIONS_OUT),
};

class CDeterministicMNList;

using CDeterministicMNListPtr = std::shared_ptr<CDeterministicMNList>;

/** Model for Raptoreum network client. */
class ClientModel : public QObject {
    Q_OBJECT

public:
    explicit ClientModel(interfaces::Node &node, OptionsModel *optionsModel, QObject *parent = nullptr);

    ~ClientModel();

    interfaces::Node &node() const { return m_node; }

    interfaces::Smartnode::Sync &smartnodeSync() const { return m_node.smartnodeSync(); }

    interfaces::CoinJoin::Options &coinJoinOptions() const { return m_node.coinJoinOptions(); }

    OptionsModel *getOptionsModel();

    PeerTableModel *getPeerTableModel();

    BanTableModel *getBanTableModel();

    //! Return number of connections, default is in- and outbound (total)
    int getNumConnections(unsigned int flags = CONNECTIONS_ALL) const;

    int getHeaderTipHeight() const;

    int64_t getHeaderTipTime() const;

    void setSmartnodeList(const CDeterministicMNList &mnList);

    CDeterministicMNList getSmartnodeList() const;

    void refreshSmartnodeList();

    //! Returns enum BlockSource of the current importing/syncing state
    enum BlockSource getBlockSource() const;

    //! Return warnings to be displayed in status bar
    QString getStatusBarWarnings() const;

    QString formatFullVersion() const;

    QString formatSubVersion() const;

    bool isReleaseVersion() const;

    QString formatClientStartupTime() const;

    QString dataDir() const;

    QString blocksDir() const;

    bool getProxyInfo(std::string &ip_port) const;

    // caches for the best header
    mutable std::atomic<int> cachedBestHeaderHeight;
    mutable std::atomic <int64_t> cachedBestHeaderTime;

private:
    interfaces::Node &m_node;
    std::unique_ptr <interfaces::Handler> m_handler_show_progress;
    std::unique_ptr <interfaces::Handler> m_handler_notify_num_connections_changed;
    std::unique_ptr <interfaces::Handler> m_handler_notify_network_active_changed;
    std::unique_ptr <interfaces::Handler> m_handler_notify_alert_changed;
    std::unique_ptr <interfaces::Handler> m_handler_banned_list_changed;
    std::unique_ptr <interfaces::Handler> m_handler_notify_block_tip;
    std::unique_ptr <interfaces::Handler> m_handler_notify_chainlock;
    std::unique_ptr <interfaces::Handler> m_handler_notify_header_tip;
    std::unique_ptr <interfaces::Handler> m_handler_notify_smartnodelist_changed;
    std::unique_ptr <interfaces::Handler> m_handler_notify_additional_data_sync_progess_changed;
    OptionsModel *optionsModel;
    PeerTableModel *peerTableModel;
    BanTableModel *banTableModel;

    //! A thread tp interact with m_node asynchronously
    QThread *const m_thread;

    // The cache for mn list is not technically needed because CDeterministicMNManager
    // caches it internally for recent blocks but it's not enough to get consistent
    // representation of the list in UI during initial sync/reindex, so we cache it here too.
    mutable RecursiveMutex cs_mnlinst; // protects mnListCached
    CDeterministicMNListPtr mnListCached;

    void subscribeToCoreSignals();

    void unsubscribeFromCoreSignals();

    Q_SIGNALS:
            void numConnectionsChanged(int
    count);

    void smartnodeListChanged() const;

    void chainLockChanged(const QString &bestChainLockHash, int bestChainLockHeight);

    void numBlocksChanged(int count, const QDateTime &blockDate, const QString &blockHash, double nVerificationProgress,
                          bool header);

    void additionalDataSyncProgressChanged(double nSyncProgress);

    void mempoolSizeChanged(long count, size_t mempoolSizeInBytes);

    void islockCountChanged(size_t count);

    void networkActiveChanged(bool networkActive);

    void alertsChanged(const QString &warnings);

    //! Fired when a message should be reported to the user
    void message(const QString &title, const QString &message, unsigned int style);

    // Show progress dialog e.g. for verifychain
    void showProgress(const QString &title, int nProgress);

public
    Q_SLOTS:
            void updateNumConnections(int
    numConnections);

    void updateNetworkActive(bool networkActive);

    void updateAlert();

    void updateBanlist();
};

#endif // BITCOIN_QT_CLIENTMODEL_H
