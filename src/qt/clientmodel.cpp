// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/clientmodel.h>

#include <qt/bantablemodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/peertablemodel.h>

#include <evo/deterministicmns.h>

#include <clientversion.h>
#include <governance/governance-object.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <net.h>
#include <netbase.h>
#include <util/system.h>

#include <stdint.h>

#include <QDebug>
#include <QThread>
#include <QTimer>

namespace pl = std::placeholders;

static int64_t nLastHeaderTipUpdateNotification = 0;
static int64_t nLastBlockTipUpdateNotification = 0;

ClientModel::ClientModel(interfaces::Node &node, OptionsModel *_optionsModel, QObject *parent) :
        QObject(parent),
        m_node(node),
        optionsModel(_optionsModel),
        peerTableModel(nullptr),
        banTableModel(nullptr),
        m_thread(new QThread(this)) {
    cachedBestHeaderHeight = -1;
    cachedBestHeaderTime = -1;
    peerTableModel = new PeerTableModel(m_node, this);
    banTableModel = new BanTableModel(m_node, this);
    mnListCached = std::make_shared<CDeterministicMNList>();

    QTimer *timer = new QTimer;
    timer->setInterval(MODEL_UPDATE_DELAY);
    connect(timer, &QTimer::timeout, [this] {
        Q_EMIT mempoolSizeChanged(m_node.getMempoolSize(), m_node.getMempoolDynamicUsage());
        Q_EMIT islockCountChanged(m_node.llmq().getInstantSentLockCount());
    });
    connect(m_thread, &QThread::finished, timer, &QObject::deleteLater);
    connect(m_thread, &QThread::started, [timer] { timer->start(); });
    timer->moveToThread(m_thread);
    m_thread->start();

    subscribeToCoreSignals();
}

ClientModel::~ClientModel() {
    unsubscribeFromCoreSignals();

    m_thread->quit();
    m_thread->wait();
}

int ClientModel::getNumConnections(unsigned int flags) const {
    CConnman::NumConnections connections = CConnman::CONNECTIONS_NONE;

    if (flags == CONNECTIONS_IN)
        connections = CConnman::CONNECTIONS_IN;
    else if (flags == CONNECTIONS_OUT)
        connections = CConnman::CONNECTIONS_OUT;
    else if (flags == CONNECTIONS_ALL)
        connections = CConnman::CONNECTIONS_ALL;

    return m_node.getNodeCount(connections);
}

void ClientModel::setSmartnodeList(const CDeterministicMNList &mnList) {
    LOCK(cs_mnlinst);
    if (mnListCached->GetBlockHash() == mnList.GetBlockHash()) {
        return;
    }
    mnListCached = std::make_shared<CDeterministicMNList>(mnList);
    Q_EMIT smartnodeListChanged();
}

CDeterministicMNList ClientModel::getSmartnodeList() const {
    LOCK(cs_mnlinst);
    return *mnListCached;
}

void ClientModel::refreshSmartnodeList() {
    LOCK(cs_mnlinst);
    setSmartnodeList(m_node.evo().getListAtChainTip());
}

int ClientModel::getHeaderTipHeight() const {
    if (cachedBestHeaderHeight == -1) {
        // make sure we initially populate the cache via a cs_main lock
        // otherwise we need to wait for a tip update
        int height;
        int64_t blockTime;
        if (m_node.getHeaderTip(height, blockTime)) {
            cachedBestHeaderHeight = height;
            cachedBestHeaderTime = blockTime;
        }
    }
    return cachedBestHeaderHeight;
}

int64_t ClientModel::getHeaderTipTime() const {
    if (cachedBestHeaderTime == -1) {
        int height;
        int64_t blockTime;
        if (m_node.getHeaderTip(height, blockTime)) {
            cachedBestHeaderHeight = height;
            cachedBestHeaderTime = blockTime;
        }
    }
    return cachedBestHeaderTime;
}

void ClientModel::updateNumConnections(int numConnections) {
    Q_EMIT numConnectionsChanged(numConnections);
}

void ClientModel::updateNetworkActive(bool networkActive) {
    Q_EMIT networkActiveChanged(networkActive);
}

void ClientModel::updateAlert() {
    Q_EMIT alertsChanged(getStatusBarWarnings());
}

enum BlockSource ClientModel::getBlockSource() const {
    if (m_node.getReindex())
        return BlockSource::REINDEX;
    else if (m_node.getImporting())
        return BlockSource::DISK;
    else if (getNumConnections() > 0)
        return BlockSource::NETWORK;

    return BlockSource::NONE;
}

QString ClientModel::getStatusBarWarnings() const {
    return QString::fromStdString(m_node.getWarnings());
}

OptionsModel *ClientModel::getOptionsModel() {
    return optionsModel;
}

PeerTableModel *ClientModel::getPeerTableModel() {
    return peerTableModel;
}

BanTableModel *ClientModel::getBanTableModel() {
    return banTableModel;
}

QString ClientModel::formatFullVersion() const {
    return QString::fromStdString(FormatFullVersion());
}

QString ClientModel::formatSubVersion() const {
    return QString::fromStdString(strSubVersion);
}

bool ClientModel::isReleaseVersion() const {
    return CLIENT_VERSION_IS_RELEASE;
}

QString ClientModel::formatClientStartupTime() const {
    return QDateTime::fromTime_t(GetStartupTime()).toString();
}

QString ClientModel::dataDir() const {
    return GUIUtil::boostPathToQString(GetDataDir());
}

QString ClientModel::blocksDir() const {
    return GUIUtil::boostPathToQString(GetBlocksDir());
}

void ClientModel::updateBanlist() {
    banTableModel->refresh();
}

// Handlers for core signals
static void ShowProgress(ClientModel *clientmodel, const std::string &title, int nProgress) {
    // emits signal "showProgress"
    bool invoked = QMetaObject::invokeMethod(clientmodel, "showProgress", Qt::QueuedConnection,
                                             Q_ARG(QString, QString::fromStdString(title)),
                                             Q_ARG(int, nProgress));
    assert(invoked);
}

static void NotifyNumConnectionsChanged(ClientModel *clientmodel, int newNumConnections) {
    // Too noisy: qDebug() << "NotifyNumConnectionsChanged: " + QString::number(newNumConnections);
    bool invoked = QMetaObject::invokeMethod(clientmodel, "updateNumConnections", Qt::QueuedConnection,
                                             Q_ARG(int, newNumConnections));
    assert(invoked);
}

static void NotifyNetworkActiveChanged(ClientModel *clientmodel, bool networkActive) {
    bool invoked = QMetaObject::invokeMethod(clientmodel, "updateNetworkActive", Qt::QueuedConnection,
                                             Q_ARG(bool, networkActive));
    assert(invoked);
}

static void NotifyAlertChanged(ClientModel *clientmodel) {
    qDebug() << "NotifyAlertChanged";
    bool invoked = QMetaObject::invokeMethod(clientmodel, "updateAlert", Qt::QueuedConnection);
    assert(invoked);
}

static void BannedListChanged(ClientModel *clientmodel) {
    qDebug() << QString("%1: Requesting update for peer banlist").arg(__func__);
    bool invoked = QMetaObject::invokeMethod(clientmodel, "updateBanlist", Qt::QueuedConnection);
    assert(invoked);
}

static void BlockTipChanged(ClientModel *clientmodel, bool initialSync, int height, int64_t blockTime,
                            const std::string &strBlockHash, double verificationProgress, bool fHeader) {
    // lock free async UI updates in case we have a new block tip
    // during initial sync, only update the UI if the last update
    // was > 250ms (MODEL_UPDATE_DELAY) ago
    int64_t now = 0;
    if (initialSync)
        now = GetTimeMillis();

    int64_t &nLastUpdateNotification = fHeader ? nLastHeaderTipUpdateNotification : nLastBlockTipUpdateNotification;

    if (fHeader) {
        // cache best headers time and height to reduce future cs_main locks
        clientmodel->cachedBestHeaderHeight = height;
        clientmodel->cachedBestHeaderTime = blockTime;
    }
    // During initial sync, block notifications, and header notifications from reindexing are both throttled.
    if (!initialSync || (fHeader && !clientmodel->node().getReindex()) ||
        now - nLastUpdateNotification > MODEL_UPDATE_DELAY) {
        //pass an async signal to the UI thread
        bool invoked = QMetaObject::invokeMethod(clientmodel, "numBlocksChanged", Qt::QueuedConnection,
                                                 Q_ARG(int, height),
                                                 Q_ARG(QDateTime, QDateTime::fromTime_t(blockTime)),
                                                 Q_ARG(QString, QString::fromStdString(strBlockHash)),
                                                 Q_ARG(double, verificationProgress),
                                                 Q_ARG(bool, fHeader));
        assert(invoked);
        nLastUpdateNotification = now;
    }
}

static void NotifyChainLock(ClientModel *clientmodel, const std::string &bestChainLockHash, int bestChainLockHeight) {
    // emits signal "chainlockChanged"
    bool invoked = QMetaObject::invokeMethod(clientmodel, "chainLockChanged", Qt::QueuedConnection,
                                             Q_ARG(QString, QString::fromStdString(bestChainLockHash)),
                                             Q_ARG(int, bestChainLockHeight));
    assert(invoked);
}

static void NotifySmartnodeListChanged(ClientModel *clientmodel, const CDeterministicMNList &newList) {
    clientmodel->setSmartnodeList(newList);
}

static void NotifyAdditionalDataSyncProgressChanged(ClientModel *clientmodel, double nSyncProgress) {
    bool invoked = QMetaObject::invokeMethod(clientmodel, "additionalDataSyncProgressChanged", Qt::QueuedConnection,
                                             Q_ARG(double, nSyncProgress));
    assert(invoked);
}

void ClientModel::subscribeToCoreSignals() {
    // Connect signals to client
    m_handler_show_progress = m_node.handleShowProgress(std::bind(ShowProgress, this, pl::_1, pl::_2));
    m_handler_notify_num_connections_changed = m_node.handleNotifyNumConnectionsChanged(
            std::bind(NotifyNumConnectionsChanged, this, pl::_1));
    m_handler_notify_network_active_changed = m_node.handleNotifyNetworkActiveChanged(
            std::bind(NotifyNetworkActiveChanged, this, pl::_1));
    m_handler_notify_alert_changed = m_node.handleNotifyAlertChanged(std::bind(NotifyAlertChanged, this));
    m_handler_banned_list_changed = m_node.handleBannedListChanged(std::bind(BannedListChanged, this));
    m_handler_notify_block_tip = m_node.handleNotifyBlockTip(
            std::bind(BlockTipChanged, this, pl::_1, pl::_2, pl::_3, pl::_4, pl::_5, false));
    m_handler_notify_chainlock = m_node.handleNotifyChainLock(std::bind(NotifyChainLock, this, pl::_1, pl::_2));
    m_handler_notify_header_tip = m_node.handleNotifyHeaderTip(
            std::bind(BlockTipChanged, this, pl::_1, pl::_2, pl::_3, pl::_4, pl::_5, true));
    m_handler_notify_smartnodelist_changed = m_node.handleNotifySmartnodeListChanged(
            std::bind(NotifySmartnodeListChanged, this, pl::_1));
    m_handler_notify_additional_data_sync_progess_changed = m_node.handleNotifyAdditionalDataSyncProgressChanged(
            std::bind(NotifyAdditionalDataSyncProgressChanged, this, pl::_1));
}

void ClientModel::unsubscribeFromCoreSignals() {
    // Disconnect signals from client
    m_handler_show_progress->disconnect();
    m_handler_notify_num_connections_changed->disconnect();
    m_handler_notify_network_active_changed->disconnect();
    m_handler_notify_alert_changed->disconnect();
    m_handler_banned_list_changed->disconnect();
    m_handler_notify_block_tip->disconnect();
    m_handler_notify_chainlock->disconnect();
    m_handler_notify_header_tip->disconnect();
    m_handler_notify_smartnodelist_changed->disconnect();
    m_handler_notify_additional_data_sync_progess_changed->disconnect();
}

bool ClientModel::getProxyInfo(std::string &ip_port) const {
    proxyType ipv4, ipv6;
    if (m_node.getProxy((Network) 1, ipv4) && m_node.getProxy((Network) 2, ipv6)) {
        ip_port = ipv4.proxy.ToStringIPPort();
        return true;
    }
    return false;
}