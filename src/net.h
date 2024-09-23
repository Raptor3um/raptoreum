// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NET_H
#define BITCOIN_NET_H

#include <addrdb.h>
#include <addrman.h>
#include <bloom.h>
#include <compat.h>
#include <fs.h>
#include <hash.h>
#include <limitedmap.h>
#include <netaddress.h>
#include <policy/feerate.h>
#include <protocol.h>
#include <random.h>
#include <saltedhasher.h>
#include <streams.h>
#include <sync.h>
#include <uint256.h>
#include <util/system.h>
#include <threadinterrupt.h>
#include <consensus/params.h>

#include <atomic>
#include <deque>
#include <stdint.h>
#include <thread>
#include <memory>
#include <condition_variable>
#include <unordered_set>
#include <queue>

#ifndef WIN32

#include <arpa/inet.h>

#endif


#ifndef WIN32
#define USE_WAKEUP_PIPE
#endif

class CScheduler;

class CNode;

class BanMan;

namespace boost {
    class thread_group;
} // namespace boost

/** Time between pings automatically sent out for latency probing and keepalive. */
static const int PING_INTERVAL = 2 * 60;
/** Time after which to disconnect, after waiting for a ping response (or inactivity). */
static const int TIMEOUT_INTERVAL = 20 * 60;
/** Minimum time between warnings printed to log. */
static const int WARNING_INTERVAL = 10 * 60;
/** Run the feeler connection loop once every 2 minutes or 120 seconds. */
static const int FEELER_INTERVAL = 120;
/** The maximum number of entries in an 'inv' protocol message */
static const unsigned int MAX_INV_SZ = 50000;
/** The maximum number of entries in a locator */
static const unsigned int MAX_LOCATOR_SZ = 101;
/** The maximum number of new addresses to accumulate before announcing. */
static const unsigned int MAX_ADDR_TO_SEND = 1000;
/** The maximum rate of address records we're willing to process on average. Can be bypassed using
 *  the NetPermissionFlags::Addr permission. */
static constexpr double MAX_ADDR_RATE_PER_SECOND = 0.1;
/** The soft limit of the address processing token bucket (the regular MAX_ADDR_RATE_PER_SECOND
 *  based increments won't go above this, but the MAX_ADDR_TO_SEND increment following GETADDR
 *  is exempt from this limit). */
static constexpr size_t
MAX_ADDR_PROCESSING_TOKEN_BUCKET = MAX_ADDR_TO_SEND;
/** Maximum length of incoming protocol messages (no message over 3 MiB is currently acceptable). */
static const unsigned int MAX_PROTOCOL_MESSAGE_LENGTH = 3 * 1024 * 1024;
/** Maximum length of strSubVer in `version` message */
static const unsigned int MAX_SUBVERSION_LENGTH = 256;
/** Maximum number of automatic outgoing nodes */
static const int MAX_OUTBOUND_CONNECTIONS = 8;
/** Maximum number of addnode outgoing nodes */
static const int MAX_ADDNODE_CONNECTIONS = 8;
/** Eviction protection time for incoming connections  */
static const int INBOUND_EVICTION_PROTECTION_TIME = 1;
/** -listen default */
static const bool DEFAULT_LISTEN = true;
/** The maximum number of peer connections to maintain.
 *  Smartnodes are forced to accept at least this many connections
 */
static const unsigned int DEFAULT_MAX_PEER_CONNECTIONS = 125;
static const unsigned int DEFAULT_SN_MAX_PEER_CONNECTIONS = 80;
/** The default for -maxuploadtarget. 0 = Unlimited */
static const uint64_t DEFAULT_MAX_UPLOAD_TARGET = 0;
/** The default timeframe for -maxuploadtarget. 1 day. */
static const uint64_t MAX_UPLOAD_TIMEFRAME = 60 * 60 * 24;
/** Default for blocks only*/
static const bool DEFAULT_BLOCKSONLY = false;
/** -peertimeout default */
static const int64_t DEFAULT_PEER_CONNECT_TIMEOUT = 60;

static const bool DEFAULT_FORCEDNSSEED = false;
static const size_t DEFAULT_MAXRECEIVEBUFFER = 5 * 1000;
static const size_t DEFAULT_MAXSENDBUFFER = 1 * 1000;

#if defined USE_KQUEUE
#define DEFAULT_SOCKETEVENTS "kqueue"
#elif defined USE_EPOLL
#define DEFAULT_SOCKETEVENTS "epoll"
#elif defined USE_POLL
#define DEFAULT_SOCKETEVENTS "poll"
#else
#define DEFAULT_SOCKETEVENTS "select"
#endif

typedef int64_t NodeId;

struct AddedNodeInfo {
    std::string strAddedNode;
    CService resolvedAddress;
    bool fConnected;
    bool fInbound;
};

class CNodeStats;

class CClientUIInterface;

struct CSerializedNetMsg {
    CSerializedNetMsg() = default;

    CSerializedNetMsg(CSerializedNetMsg &&) = default;

    CSerializedNetMsg &operator=(CSerializedNetMsg &&) = default;

    // No copying, only moves.
    CSerializedNetMsg(const CSerializedNetMsg &msg) = delete;

    CSerializedNetMsg &operator=(const CSerializedNetMsg &) = delete;

    std::vector<unsigned char> data;
    std::string command;
};


class NetEventsInterface;

class CConnman {
    friend class CNode;

public:

    enum NumConnections {
        CONNECTIONS_NONE = 0,
        CONNECTIONS_IN = (1U << 0),
        CONNECTIONS_OUT = (1U << 1),
        CONNECTIONS_ALL = (CONNECTIONS_IN | CONNECTIONS_OUT),
    };

    enum SocketEventsMode {
        SOCKETEVENTS_SELECT = 0,
        SOCKETEVENTS_POLL = 1,
        SOCKETEVENTS_EPOLL = 2,
        SOCKETEVENTS_KQUEUE = 3,
    };

    struct Options {
        ServiceFlags nLocalServices = NODE_NONE;
        int nMaxConnections = 0;
        int nMaxOutbound = 0;
        int nMaxAddnode = 0;
        int nMaxFeeler = 0;
        int nBestHeight = 0;
        CClientUIInterface *uiInterface = nullptr;
        NetEventsInterface *m_msgproc = nullptr;
        BanMan *m_banman = nullptr;
        unsigned int nSendBufferMaxSize = 0;
        unsigned int nReceiveFloodSize = 0;
        uint64_t nMaxOutboundTimeframe = 0;
        uint64_t nMaxOutboundLimit = 0;
        int64_t m_peer_connect_timeout = DEFAULT_PEER_CONNECT_TIMEOUT;
        std::vector <std::string> vSeedNodes;
        std::vector <CSubNet> vWhitelistedRange;
        std::vector <CService> vBinds, vWhiteBinds;
        bool m_use_addrman_outgoing = true;
        std::vector <std::string> m_specified_outgoing;
        std::vector <std::string> m_added_nodes;
        SocketEventsMode socketEventsMode = SOCKETEVENTS_SELECT;
        std::vector<bool> m_asmap;
    };

    void Init(const Options &connOptions) {
        nLocalServices = connOptions.nLocalServices;
        nMaxConnections = connOptions.nMaxConnections;
        nMaxOutbound = std::min(connOptions.nMaxOutbound, connOptions.nMaxConnections);
        m_use_addrman_outgoing = connOptions.m_use_addrman_outgoing;
        nMaxAddnode = connOptions.nMaxAddnode;
        nMaxFeeler = connOptions.nMaxFeeler;
        nBestHeight = connOptions.nBestHeight;
        clientInterface = connOptions.uiInterface;
        m_banman = connOptions.m_banman;
        m_msgproc = connOptions.m_msgproc;
        nSendBufferMaxSize = connOptions.nSendBufferMaxSize;
        nReceiveFloodSize = connOptions.nReceiveFloodSize;
        m_peer_connect_timeout = connOptions.m_peer_connect_timeout;
        {
            LOCK(cs_totalBytesSent);
            nMaxOutboundTimeframe = connOptions.nMaxOutboundTimeframe;
            nMaxOutboundLimit = connOptions.nMaxOutboundLimit;
        }
        vWhitelistedRange = connOptions.vWhitelistedRange;
        {
            LOCK(cs_vAddedNodes);
            vAddedNodes = connOptions.m_added_nodes;
        }
        socketEventsMode = connOptions.socketEventsMode;
    }

    CConnman(uint64_t seed0, uint64_t seed1);

    ~CConnman();

    bool Start(CScheduler &scheduler, const Options &options);

    void Stop();

    void Interrupt();

    bool GetNetworkActive() const { return fNetworkActive; };

    bool GetUseAddrmanOutgoing() const { return m_use_addrman_outgoing; };

    void SetNetworkActive(bool active);

    SocketEventsMode GetSocketEventsMode() const { return socketEventsMode; }

    void
    OpenNetworkConnection(const CAddress &addrConnect, bool fCountFailure, CSemaphoreGrant *grantOutbound = nullptr,
                          const char *strDest = nullptr, bool fOneShot = false, bool fFeeler = false,
                          bool manual_connection = false, bool smartnode_connection = false,
                          bool smartnode_probe_connection = false);

    void OpenSmartnodeConnection(const CAddress &addrConnect, bool probe = false);

    bool CheckIncomingNonce(uint64_t nonce);

    struct CFullyConnectedOnly {
        bool operator()(const CNode *pnode) const {
            return NodeFullyConnected(pnode);
        }
    };

    constexpr static const CFullyConnectedOnly
    FullyConnectedOnly{};

    struct CAllNodes {
        bool operator()(const CNode *) const { return true; }
    };

    constexpr static const CAllNodes
    AllNodes{};

    bool ForNode(NodeId id, std::function<bool(const CNode *pnode)> cond, std::function<bool(CNode *pnode)> func);

    bool
    ForNode(const CService &addr, std::function<bool(const CNode *pnode)> cond, std::function<bool(CNode *pnode)> func);

    template<typename Callable>
    bool ForNode(const CService &addr, Callable &&func) {
        return ForNode(addr, FullyConnectedOnly, func);
    }

    template<typename Callable>
    bool ForNode(NodeId id, Callable &&func) {
        return ForNode(id, FullyConnectedOnly, func);
    }

    bool IsConnected(const CService &addr, std::function<bool(const CNode *pnode)> cond) {
        return ForNode(addr, cond, [](CNode *pnode) {
            return true;
        });
    }

    bool IsSmartnodeOrDisconnectRequested(const CService &addr);

    void PushMessage(CNode *pnode, CSerializedNetMsg &&msg);

    template<typename Condition, typename Callable>
    bool ForEachNodeContinueIf(const Condition &cond, Callable &&func) {
        LOCK(cs_vNodes);
        for (auto &&node: vNodes)
            if (cond(node))
                if (!func(node))
                    return false;
        return true;
    };

    template<typename Callable>
    bool ForEachNodeContinueIf(Callable &&func) {
        return ForEachNodeContinueIf(FullyConnectedOnly, func);
    }

    template<typename Condition, typename Callable>
    bool ForEachNodeContinueIf(const Condition &cond, Callable &&func) const {
        LOCK(cs_vNodes);
        for (const auto &node: vNodes)
            if (cond(node))
                if (!func(node))
                    return false;
        return true;
    };

    template<typename Callable>
    bool ForEachNodeContinueIf(Callable &&func) const {
        return ForEachNodeContinueIf(FullyConnectedOnly, func);
    }

    template<typename Condition, typename Callable>
    void ForEachNode(const Condition &cond, Callable &&func) {
        LOCK(cs_vNodes);
        for (auto &&node: vNodes) {
            if (cond(node))
                func(node);
        }
    };

    template<typename Callable>
    void ForEachNode(Callable &&func) {
        ForEachNode(FullyConnectedOnly, func);
    }

    template<typename Condition, typename Callable>
    void ForEachNode(const Condition &cond, Callable &&func) const {
        LOCK(cs_vNodes);
        for (auto &&node: vNodes) {
            if (cond(node))
                func(node);
        }
    };

    template<typename Callable>
    void ForEachNode(Callable &&func) const {
        ForEachNode(FullyConnectedOnly, func);
    }

    template<typename Condition, typename Callable, typename CallableAfter>
    void ForEachNodeThen(const Condition &cond, Callable &&pre, CallableAfter &&post) {
        LOCK(cs_vNodes);
        for (auto &&node: vNodes) {
            if (cond(node))
                pre(node);
        }
        post();
    };

    template<typename Callable, typename CallableAfter>
    void ForEachNodeThen(Callable &&pre, CallableAfter &&post) {
        ForEachNodeThen(FullyConnectedOnly, pre, post);
    }

    template<typename Condition, typename Callable, typename CallableAfter>
    void ForEachNodeThen(const Condition &cond, Callable &&pre, CallableAfter &&post) const {
        LOCK(cs_vNodes);
        for (auto &&node: vNodes) {
            if (cond(node))
                pre(node);
        }
        post();
    };

    template<typename Callable, typename CallableAfter>
    void ForEachNodeThen(Callable &&pre, CallableAfter &&post) const {
        ForEachNodeThen(FullyConnectedOnly, pre, post);
    }

    std::vector<CNode *> CopyNodeVector(std::function<bool(const CNode *pnode)> cond);

    std::vector<CNode *> CopyNodeVector();

    void ReleaseNodeVector(const std::vector<CNode *> &vecNodes);

    void RelayTransaction(const CTransaction &tx);

    void RelayInv(CInv &inv, const int minProtoVersion = OLD_MIN_PEER_PROTO_VERSION);

    void
    RelayInvFiltered(CInv &inv, const CTransaction &relatedTx, const int minProtoVersion = OLD_MIN_PEER_PROTO_VERSION);

    // This overload will not update node filters,  so use it only for the cases when other messages will update related transaction data in filters
    void
    RelayInvFiltered(CInv &inv, const uint256 &relatedTxHash, const int minProtoVersion = OLD_MIN_PEER_PROTO_VERSION);

    // Addrman functions
    size_t GetAddressCount() const;

    void SetServices(const CService &addr, ServiceFlags nServices);

    void MarkAddressGood(const CAddress &addr);

    void AddNewAddresses(const std::vector <CAddress> &vAddr, const CAddress &addrFrom, int64_t nTimePenalty = 0);

    std::vector <CAddress> GetAddresses();

    // This allows temporarily exceeding nMaxOutbound, with the goal of finding
    // a peer that is better than all our current peers.
    void SetTryNewOutboundPeer(bool flag);

    bool GetTryNewOutboundPeer();

    // Return the number of outbound peers we have in excess of our target (eg,
    // if we previously called SetTryNewOutboundPeer(true), and have since set
    // to false, we may have extra peers that we wish to disconnect). This may
    // return a value less than (num_outbound_connections - num_outbound_slots)
    // in cases where some outbound connections are not yet fully connected, or
    // not yet fully disconnected.
    int GetExtraOutboundCount();

    bool AddNode(const std::string &node);

    bool RemoveAddedNode(const std::string &node);

    std::vector <AddedNodeInfo> GetAddedNodeInfo();

    bool AddPendingSmartnode(const uint256 &proTxHash);

    void SetSmartnodeQuorumNodes(Consensus::LLMQType llmqType, const uint256 &quorumHash,
                                 const std::set <uint256> &proTxHashes);

    void SetSmartnodeQuorumRelayMembers(Consensus::LLMQType llmqType, const uint256 &quorumHash,
                                        const std::set <uint256> &proTxHashes);

    bool HasSmartnodeQuorumNodes(Consensus::LLMQType llmqType, const uint256 &quorumHash);

    std::set <uint256> GetSmartnodeQuorums(Consensus::LLMQType llmqType);

    // also returns QWATCH nodes
    std::set <NodeId> GetSmartnodeQuorumNodes(Consensus::LLMQType llmqType, const uint256 &quorumHash) const;

    void RemoveSmartnodeQuorumNodes(Consensus::LLMQType llmqType, const uint256 &quorumHash);

    bool IsSmartnodeQuorumNode(const CNode *pnode);

    bool IsSmartnodeQuorumRelayMember(const uint256 &protxHash);

    void AddPendingProbeConnections(const std::set <uint256> &proTxHashes);

    size_t GetNodeCount(NumConnections num);

    size_t GetMaxOutboundNodeCount();

    void GetNodeStats(std::vector <CNodeStats> &vstats);

    bool DisconnectNode(const std::string &node);

    bool DisconnectNode(const CSubNet &subnet);

    bool DisconnectNode(const CNetAddr &addr);

    bool DisconnectNode(NodeId id);

    //! Used to convey which local services we are offering peers during node
    //! connection.
    //!
    //! The data returned by this is used in CNode construction,
    //! which is used to advertise which services we are offering
    //! that peer during `net_processing.cpp:PushNodeVersion()`.
    ServiceFlags GetLocalServices() const;

    //!set the max outbound target in bytes
    void SetMaxOutboundTarget(uint64_t limit);

    uint64_t GetMaxOutboundTarget();

    //!set the timeframe for the max outbound target
    void SetMaxOutboundTimeframe(uint64_t timeframe);

    uint64_t GetMaxOutboundTimeframe();

    //!check if the outbound target is reached
    // if param historicalBlockServingLimit is set true, the function will
    // response true if the limit for serving historical blocks has been reached
    bool OutboundTargetReached(bool historicalBlockServingLimit);

    //! response the bytes left in the current max outbound cycle
    //! in case of no limit, it will always response 0
    uint64_t GetOutboundTargetBytesLeft();

    //! returns the time left in the current max outbound cycle
    //! in case of no limit, it will always return 0
    uint64_t GetMaxOutboundTimeLeftInCycle();

    uint64_t GetTotalBytesRecv();

    uint64_t GetTotalBytesSent();

    void SetBestHeight(int height);

    int GetBestHeight() const;

    /** Get a unique deterministic randomizer. */
    CSipHasher GetDeterministicRandomizer(uint64_t id) const;

    unsigned int GetReceiveFloodSize() const;

    void WakeMessageHandler();

    void WakeSelect();

    /** Attempts to obfuscate tx time through exponentially distributed emitting.
        Works assuming that a single interval is used.
        Variable intervals will result in privacy decrease.
    */
    int64_t PoissonNextSendInbound(int64_t now, int average_interval_seconds);

    void SetAsmap(std::vector<bool> asmap) { addrman.m_asmap = std::move(asmap); }

private:
    struct ListenSocket {
        SOCKET socket;
        bool whitelisted;

        ListenSocket(SOCKET socket_, bool whitelisted_) : socket(socket_), whitelisted(whitelisted_) {}
    };

    bool BindListenPort(const CService &bindAddr, std::string &strError, bool fWhitelisted = false);

    bool Bind(const CService &addr, unsigned int flags);

    bool InitBinds(const std::vector <CService> &binds, const std::vector <CService> &whiteBinds);

    void ThreadOpenAddedConnections();

    void AddOneShot(const std::string &strDest);

    void ProcessOneShot();

    void ThreadOpenConnections(std::vector <std::string> connect);

    void ThreadMessageHandler();

    void AcceptConnection(const ListenSocket &hListenSocket);

    void DisconnectNodes();

    void NotifyNumConnectionsChanged();

    void CalculateNumConnectionsChangedStats();

    void InactivityCheck(CNode *pnode) const;

    bool GenerateSelectSet(std::set <SOCKET> &recv_set, std::set <SOCKET> &send_set, std::set <SOCKET> &error_set);

#ifdef USE_KQUEUE
    void SocketEventsKqueue(std::set<SOCKET> &recv_set, std::set<SOCKET> &send_set, std::set<SOCKET> &error_set, bool fOnlyPoll);
#endif
#ifdef USE_EPOLL
    void SocketEventsEpoll(std::set<SOCKET> &recv_set, std::set<SOCKET> &send_set, std::set<SOCKET> &error_set, bool fOnlyPoll);
#endif
#ifdef USE_POLL
    void SocketEventsPoll(std::set<SOCKET> &recv_set, std::set<SOCKET> &send_set, std::set<SOCKET> &error_set, bool fOnlyPoll);
#endif

    void SocketEventsSelect(std::set <SOCKET> &recv_set, std::set <SOCKET> &send_set, std::set <SOCKET> &error_set,
                            bool fOnlyPoll);

    void SocketEvents(std::set <SOCKET> &recv_set, std::set <SOCKET> &send_set, std::set <SOCKET> &error_set,
                      bool fOnlyPoll);

    void SocketHandler();

    void ThreadSocketHandler();

    void ThreadDNSAddressSeed();

    void ThreadOpenSmartnodeConnections();

    uint64_t CalculateKeyedNetGroup(const CAddress &ad) const;

    CNode *FindNode(const CNetAddr &ip, bool fExcludeDisconnecting = true);

    CNode *FindNode(const CSubNet &subNet, bool fExcludeDisconnecting = true);

    CNode *FindNode(const std::string &addrName, bool fExcludeDisconnecting = true);

    CNode *FindNode(const CService &addr, bool fExcludeDisconnecting = true);

    bool AttemptToEvictConnection();

    CNode *ConnectNode(CAddress addrConnect, const char *pszDest = nullptr, bool fCountFailure = false,
                       bool manual_connection = false);

    bool IsWhitelistedRange(const CNetAddr &addr);

    void DeleteNode(CNode *pnode);

    NodeId GetNewNodeId();

    size_t SocketSendData(CNode *pnode);

    size_t SocketRecvData(CNode *pnode);

    void DumpAddresses();

    // Network stats
    void RecordBytesRecv(uint64_t bytes);

    void RecordBytesSent(uint64_t bytes);

    // Whether the node should be passed out in ForEach* callbacks
    static bool NodeFullyConnected(const CNode *pnode);

    void RegisterEvents(CNode *pnode);

    void UnregisterEvents(CNode *pnode);

    // Network usage totals
    RecursiveMutex cs_totalBytesRecv;
    RecursiveMutex cs_totalBytesSent;
    uint64_t nTotalBytesRecv
    GUARDED_BY(cs_totalBytesRecv) {0};
    uint64_t nTotalBytesSent
    GUARDED_BY(cs_totalBytesSent) {0};

    // outbound limit & stats
    uint64_t nMaxOutboundTotalBytesSentInCycle
    GUARDED_BY(cs_totalBytesSent);
    uint64_t nMaxOutboundCycleStartTime
    GUARDED_BY(cs_totalBytesSent);
    uint64_t nMaxOutboundLimit
    GUARDED_BY(cs_totalBytesSent);
    uint64_t nMaxOutboundTimeframe
    GUARDED_BY(cs_totalBytesSent);

    // P2P timeout in seconds
    int64_t m_peer_connect_timeout;

    // Whitelisted ranges. Any node connecting from these is automatically
    // whitelisted (as well as those connecting to whitelisted binds).
    std::vector <CSubNet> vWhitelistedRange;

    unsigned int nSendBufferMaxSize{0};
    unsigned int nReceiveFloodSize{0};

    std::vector <ListenSocket> vhListenSocket;
    std::atomic<bool> fNetworkActive{true};
    bool fAddressesInitialized{false};
    CAddrMan addrman;
    std::deque <std::string> vOneShots
    GUARDED_BY(cs_vOneShots);
    RecursiveMutex cs_vOneShots;
    std::vector <std::string> vAddedNodes
    GUARDED_BY(cs_vAddedNodes);
    RecursiveMutex cs_vAddedNodes;
    std::vector <uint256> vPendingSmartnodes;
    mutable RecursiveMutex cs_vPendingSmartnodes;
    std::map <std::pair<Consensus::LLMQType, uint256>, std::set<uint256>> smartnodeQuorumNodes
    GUARDED_BY(cs_vPendingSmartnodes);
    std::map <std::pair<Consensus::LLMQType, uint256>, std::set<uint256>> smartnodeQuorumRelayMembers
    GUARDED_BY(cs_vPendingSmartnodes);
    std::set <uint256> smartnodePendingProbes
    GUARDED_BY(cs_vPendingSmartnodes);
    std::vector<CNode *> vNodes
    GUARDED_BY(cs_vNodes);
    std::list<CNode *> vNodesDisconnected;
    std::unordered_map<SOCKET, CNode *> mapSocketToNode;
    mutable RecursiveMutex cs_vNodes;
    std::atomic <NodeId> nLastNodeId{0};
    unsigned int nPrevNodeCount{0};

    /**
     * Services this instance offers.
     *
     * This data is replicated in each CNode instance we create during peer
     * connection (in ConnectNode()) under a member also called
     * nLocalServices.
     *
     * This data is not marked const, but after being set it should not
     * change. See the note in CNode::nLocalServices documentation.
     *
     * \sa CNode::nLocalServices
     */
    ServiceFlags nLocalServices;

    std::unique_ptr <CSemaphore> semOutbound;
    std::unique_ptr <CSemaphore> semAddnode;
    int nMaxConnections;
    int nMaxOutbound;
    int nMaxAddnode;
    int nMaxFeeler;
    bool m_use_addrman_outgoing;
    std::atomic<int> nBestHeight;
    CClientUIInterface *clientInterface;
    NetEventsInterface *m_msgproc;
    BanMan *m_banman;

    /** SipHasher seeds for deterministic randomness */
    const uint64_t nSeed0, nSeed1;

    /** flag for waking the message processor. */
    bool fMsgProcWake
    GUARDED_BY(mutexMsgProc);

    std::condition_variable condMsgProc;
    Mutex mutexMsgProc;
    std::atomic<bool> flagInterruptMsgProc{false};

    CThreadInterrupt interruptNet;

#ifdef USE_WAKEUP_PIPE
    /** a pipe which is added to select() calls to wakeup before the timeout */
    int wakeupPipe[2]{-1, -1};
#endif
    std::atomic<bool> wakeupSelectNeeded{false};

    SocketEventsMode socketEventsMode;
#ifdef USE_KQUEUE
    int kqueuefd{-1};
#endif
#ifdef USE_EPOLL
    int epollfd{-1};
#endif

    /** Protected by cs_vNodes */
    std::unordered_map<NodeId, CNode *> mapReceivableNodes
    GUARDED_BY(cs_vNodes);
    std::unordered_map<NodeId, CNode *> mapSendableNodes
    GUARDED_BY(cs_vNodes);
    /** Protected by cs_mapNodesWithDataToSend */
    std::unordered_map<NodeId, CNode *> mapNodesWithDataToSend
    GUARDED_BY(cs_mapNodesWithDataToSend);
    mutable RecursiveMutex cs_mapNodesWithDataToSend;

    std::thread threadDNSAddressSeed;
    std::thread threadSocketHandler;
    std::thread threadOpenAddedConnections;
    std::thread threadOpenConnections;
    std::thread threadOpenSmartnodeConnections;
    std::thread threadMessageHandler;

    /** flag for deciding to connect to an extra outbound peer,
     *  in excess of nMaxOutbound
     *  This takes the place of a feeler connection */
    std::atomic_bool m_try_another_outbound_peer;

    std::atomic <int64_t> m_next_send_inv_to_incoming{0};

    friend struct CConnmanTest;
};

void Discover();

unsigned short GetListenPort();

bool BindListenPort(const CService &bindAddr, std::string &strError, bool fWhitelisted = false);

struct CombinerAll {
    typedef bool result_type;

    template<typename I>
    bool operator()(I first, I last) const {
        while (first != last) {
            if (!(*first)) return false;
            ++first;
        }
        return true;
    }
};

/**
 * Interface for message handling
 */
class NetEventsInterface {
public:
    virtual bool ProcessMessages(CNode *pnode, std::atomic<bool> &interrupt) = 0;

    virtual bool SendMessages(CNode *pnode) = 0;

    virtual void InitializeNode(CNode *pnode) = 0;

    virtual void FinalizeNode(NodeId id, bool &update_connection_time) = 0;

protected:
    /**
     * Protected destructor so that instances can only be deleted by derived classes.
     * If that restriction is no longer desired, this should be made public and virtual.
     */
    ~NetEventsInterface() = default;
};

enum {
    LOCAL_NONE,   // unknown
    LOCAL_IF,     // address a local interface listens on
    LOCAL_BIND,   // address explicit bound to
    LOCAL_MAPPED,   // address reported by UPnP or NAT-PMP
    LOCAL_MANUAL, // address explicitly specified (-externalip=)

    LOCAL_MAX
};

bool IsPeerAddrLocalGood(CNode *pnode);

void AdvertiseLocal(CNode *pnode);

void SetLimited(enum Network net, bool fLimited = true);

bool IsLimited(enum Network net);

bool IsLimited(const CNetAddr &addr);

bool AddLocal(const CService &addr, int nScore = LOCAL_NONE);

bool AddLocal(const CNetAddr &addr, int nScore = LOCAL_NONE);

void RemoveLocal(const CService &addr);

bool SeenLocal(const CService &addr);

bool IsLocal(const CService &addr);

bool GetLocal(CService &addr, const CNetAddr *paddrPeer = nullptr);

bool IsReachable(enum Network net);

bool IsReachable(const CNetAddr &addr);

CAddress GetLocalAddress(const CNetAddr *paddrPeer, ServiceFlags nLocalServices);


extern bool fDiscover;
extern bool fListen;
extern bool fRelayTxes;

/** Subversion as sent to the P2P network in `version` messages */
extern std::string strSubVersion;

struct LocalServiceInfo {
    int nScore;
    int nPort;
};

extern RecursiveMutex cs_mapLocalHost;
extern std::map <CNetAddr, LocalServiceInfo> mapLocalHost
GUARDED_BY(cs_mapLocalHost);

extern const std::string NET_MESSAGE_COMMAND_OTHER;
typedef std::map <std::string, uint64_t> mapMsgCmdSize; //command, total bytes

class CNodeStats {
public:
    NodeId nodeid;
    ServiceFlags nServices;
    bool fRelayTxes;
    int64_t nLastSend;
    int64_t nLastRecv;
    int64_t nTimeConnected;
    int64_t nTimeOffset;
    std::string addrName;
    int nVersion;
    std::string cleanSubVer;
    bool fInbound;
    bool m_manual_connection;
    int nStartingHeight;
    uint64_t nSendBytes;
    mapMsgCmdSize mapSendBytesPerMsgCmd;
    uint64_t nRecvBytes;
    mapMsgCmdSize mapRecvBytesPerMsgCmd;
    bool fWhitelisted;
    int64_t m_ping_usec;
    int64_t m_ping_wait_usec;
    int64_t m_min_ping_usec;
    // Our address, as reported by the peer
    std::string addrLocal;
    // Address of this peer
    CAddress addr;
    // Bind address of our side of the connection
    CAddress addrBind;
    uint32_t m_mapped_as;
    // In case this is a verified MN, this value is the proTx of the MN
    uint256 verifiedProRegTxHash;
    // In case this is a verified MN, this value is the hashed operator pubkey of the MN
    uint256 verifiedPubKeyHash;
    bool m_smartnode_connection;

    // Copied from CNode for addr rate limiting
    std::chrono::microseconds addrTokenTimestamp;
    double addrTokenBucket;
    uint64_t nAddrRateLimited;
    uint64_t nAddrProcessed;
};


class CNetMessage {
private:
    mutable CHash256 hasher;
    mutable uint256 data_hash;
public:
    bool in_data;                   // parsing header (false) or data (true)

    CDataStream hdrbuf;             // partially received header
    CMessageHeader hdr;             // complete header
    unsigned int nHdrPos;

    CDataStream vRecv;              //!< received message data
    unsigned int nDataPos;

    int64_t nTime;                  //!< time of message receipt.

    CNetMessage(const CMessageHeader::MessageStartChars &pchMessageStartIn, int nTypeIn, int nVersionIn) : hdrbuf(
            nTypeIn, nVersionIn), hdr(pchMessageStartIn), vRecv(nTypeIn, nVersionIn) {
        hdrbuf.resize(24);
        in_data = false;
        nHdrPos = 0;
        nDataPos = 0;
        nTime = 0;
    }

    bool complete() const {
        if (!in_data)
            return false;
        return (hdr.nMessageSize == nDataPos);
    }

    const uint256 &GetMessageHash() const;

    void SetVersion(int nVersionIn) {
        hdrbuf.SetVersion(nVersionIn);
        vRecv.SetVersion(nVersionIn);
    }

    int readHeader(const char *pch, unsigned int nBytes);

    int readData(const char *pch, unsigned int nBytes);
};


/** Information about a peer */
class CNode {
    friend class CConnman;

public:
    // socket
    std::atomic <ServiceFlags> nServices;
    SOCKET hSocket
    GUARDED_BY(cs_hSocket);
    size_t nSendSize; // total size of all vSendMsg entries
    size_t nSendOffset; // offset inside the first vSendMsg already sent
    uint64_t nSendBytes
    GUARDED_BY(cs_vSend);
    std::list <std::vector<unsigned char>> vSendMsg
    GUARDED_BY(cs_vSend);
    std::atomic <size_t> nSendMsgSize;
    RecursiveMutex cs_vSend;
    RecursiveMutex cs_hSocket;
    RecursiveMutex cs_vRecv;

    RecursiveMutex cs_vProcessMsg;
    std::list <CNetMessage> vProcessMsg
    GUARDED_BY(cs_vProcessMsg);
    size_t nProcessQueueSize;

    RecursiveMutex cs_sendProcessing;

    std::deque <CInv> vRecvGetData;
    uint64_t nRecvBytes
    GUARDED_BY(cs_vRecv);
    std::atomic<int> nRecvVersion;

    std::atomic <int64_t> nLastSend;
    std::atomic <int64_t> nLastRecv;
    const int64_t nTimeConnected;
    std::atomic <int64_t> nTimeOffset;
    std::atomic <int64_t> nLastWarningTime;
    std::atomic <int64_t> nTimeFirstMessageReceived;
    std::atomic<bool> fFirstMessageIsMNAUTH;
    // Address of this peer
    const CAddress addr;
    // Bind address of our side of the connection
    const CAddress addrBind;

    /** Number of addresses that can be processed from this peer. Start at 1 to permit self-announcement. */
    double addrTokenBucket{1.0};
    /** When m_addr_token_bucket was last updated */
    std::chrono::microseconds addrTokenTimestamp{GetTime<std::chrono::microseconds>()};
    /** Total number of addresses that were dropped due to rate limiting. */
    std::atomic <uint64_t> nAddrRateLimited{0};
    /** Total number of addresses that were processed (excludes rate-limited ones). */
    std::atomic <uint64_t> nAddrProcessed{0};

    std::atomic<int> nNumWarningsSkipped;
    std::atomic<int> nVersion;
    /**
     * cleanSubVer is a sanitized string of the user agent byte array we read
     * from the wire. This cleaned string can safely be logged or displayed.
     */
    std::string strSubVer
    GUARDED_BY(cs_SubVer){};
    std::string cleanSubVer
    GUARDED_BY(cs_SubVer){};
    RecursiveMutex cs_SubVer; // used for both cleanSubVer and strSubVer
    bool m_prefer_evict{false}; // This peer is preferred for eviction.
    bool fWhitelisted; // This peer can bypass DoS banning.
    bool fFeeler; // If true this node is being used as a short lived feeler.
    bool fOneShot;
    bool m_manual_connection;
    bool fClient; // set by version message
    bool m_limited_node; // after BIP159, set by version message
    const bool fInbound;
    std::atomic_bool fSuccessfullyConnected;
    std::atomic_bool fDisconnect;
    std::atomic <int64_t> nDisconnectLingerTime{0};
    std::atomic_bool fSocketShutdown{false};
    std::atomic_bool fOtherSideDisconnected{false};
    // We use fRelayTxes for two purposes
    // a) it allows us to not relay tx invs before receiving the peer's version message
    // b) the peer may tell us in its version message that we should not relay tx invs
    //    unless it loads a bloom filter.
    bool fRelayTxes
    GUARDED_BY(cs_filter);
    bool fSentAddr;
    // If 'true' this node will be disconnected on CSmartnodeMan::ProcessSmartnodeConnections()
    std::atomic<bool> m_smartnode_connection;
    // If 'true' this node will be disconnected after MNAUTH
    std::atomic<bool> m_smartnode_probe_connection;
    // If 'true', we identified it as an intra-quorum relay connection
    std::atomic<bool> m_smartnode_iqr_connection{false};
    CSemaphoreGrant grantOutbound;
    mutable RecursiveMutex cs_filter;
    std::unique_ptr <CBloomFilter> pfilter
    PT_GUARDED_BY(cs_filter){nullptr};
    std::atomic<int> nRefCount;

    const uint64_t nKeyedNetGroup;

    std::atomic_bool fPauseRecv;
    std::atomic_bool fPauseSend;

    std::atomic_bool fHasRecvData;
    std::atomic_bool fCanSendData;

protected:
    mapMsgCmdSize mapSendBytesPerMsgCmd;
    mapMsgCmdSize mapRecvBytesPerMsgCmd
    GUARDED_BY(cs_vRecv);

public:
    uint256 hashContinue;
    std::atomic<int> nStartingHeight;

    // flood relay
    std::vector <CAddress> vAddrToSend;
    CRollingBloomFilter addrKnown;
    bool fGetAddr;
    std::set <uint256> setKnown;
    int64_t nNextAddrSend
    GUARDED_BY(cs_sendProcessing);
    int64_t nNextLocalAddrSend
    GUARDED_BY(cs_sendProcessing);

    // inventory based relay
    CRollingBloomFilter filterInventoryKnown
    GUARDED_BY(cs_inventory);
    // Set of transaction ids we still have to announce.
    // They are sorted by the mempool before relay, so the order is not important.
    std::set <uint256> setInventoryTxToSend;
    // List of block ids we still have announce.
    // There is no final sorting before sending, as they are always sent immediately
    // and in the order requested.
    std::vector <uint256> vInventoryBlockToSend
    GUARDED_BY(cs_inventory);
    // List of non-tx/non-block inventory items
    std::vector <CInv> vInventoryOtherToSend;
    RecursiveMutex cs_inventory;
    std::chrono::microseconds nNextInvSend{0};
    // Used for headers announcements - unfiltered blocks to relay
    // Also protected by cs_inventory
    std::vector <uint256> vBlockHashesToAnnounce;
    // Used for BIP35 mempool sending, also protected by cs_inventory
    bool fSendMempool;

    // Block and TXN accept times
    std::atomic <int64_t> nLastBlockTime;
    std::atomic <int64_t> nLastTXTime;

    // Last time a "MEMPOOL" request was serviced.
    std::atomic <int64_t> timeLastMempoolReq;
    // Ping time measurement:
    // The pong reply we're expecting, or 0 if no pong expected.
    std::atomic <uint64_t> nPingNonceSent;
    // Time (in usec) the last ping was sent, or 0 if no ping was ever sent.
    std::atomic <int64_t> nPingUsecStart;
    // Last measured round-trip time.
    std::atomic <int64_t> nPingUsecTime;
    // Best measured round-trip time.
    std::atomic <int64_t> nMinPingUsecTime;
    // Whether a ping is requested.
    std::atomic<bool> fPingQueued;

    // If true, we will send him CoinJoin queue messages
    std::atomic<bool> fSendDSQueue{false};

    // If true, we will announce/send him plain recovered sigs (usually true for full nodes)
    std::atomic<bool> fSendRecSigs{false};
    // If true, we will send him all quorum related messages, even if he is not a member of our quorums
    std::atomic<bool> qwatch{false};

    std::set <uint256> orphan_work_set;

    CNode(NodeId id, ServiceFlags nLocalServicesIn, int nMyStartingHeightIn, SOCKET hSocketIn, const CAddress &addrIn,
          uint64_t nKeyedNetGroupIn, uint64_t nLocalHostNonceIn, const CAddress &addrBindIn,
          const std::string &addrNameIn = "", bool fInboundIn = false);

    ~CNode();

    CNode(const CNode &) = delete;

    CNode &operator=(const CNode &) = delete;

private:
    const NodeId id;
    const uint64_t nLocalHostNonce;

    //! Services offered to this peer.
    //!
    //! This is supplied by the parent CConnman during peer connection
    //! (CConnman::ConnectNode()) from its attribute of the same name.
    //!
    //! This is const because there is no protocol defined for renegotiating
    //! services initially offered to a peer. The set of local services we
    //! offer should not change after initialization.
    //!
    //! An interesting example of this is NODE_NETWORK and initial block
    //! download: a node which starts up from scratch doesn't have any blocks
    //! to serve, but still advertises NODE_NETWORK because it will eventually
    //! fulfill this role after IBD completes. P2P code is written in such a
    //! way that it can gracefully handle peers who don't make good on their
    //! service advertisements.
    const ServiceFlags nLocalServices;

    const int nMyStartingHeight;
    int nSendVersion;
    std::list <CNetMessage> vRecvMsg;  // Used only by SocketHandler thread

    mutable RecursiveMutex cs_addrName;
    std::string addrName
    GUARDED_BY(cs_addrName);

    // Our address, as reported by the peer
    CService addrLocal
    GUARDED_BY(cs_addrLocal);
    mutable RecursiveMutex cs_addrLocal;

    // Challenge aent in VERSION to be answered with MNAUTH (only between SmartNodes)
    mutable RecursiveMutex cs_mnauth;
    uint256 sentMNAuthChallenge
    GUARDED_BY(cs_mnauth);
    uint256 receivedMNAuthChallenge
    GUARDED_BY(cs_mnauth);
    uint256 verifiedProRegTxHash
    GUARDED_BY(cs_mnauth);
    uint256 verifiedPubKeyHash
    GUARDED_BY(cs_mnauth);

public:

    NodeId GetId() const {
        return id;
    }

    uint64_t GetLocalNonce() const {
        return nLocalHostNonce;
    }

    int GetMyStartingHeight() const {
        return nMyStartingHeight;
    }

    int GetRefCount() const {
        assert(nRefCount >= 0);
        return nRefCount;
    }

    bool ReceiveMsgBytes(const char *pch, unsigned int nBytes, bool &complete);

    void SetRecvVersion(int nVersionIn) {
        nRecvVersion = nVersionIn;
    }

    int GetRecvVersion() const {
        return nRecvVersion;
    }

    void SetSendVersion(int nVersionIn);

    int GetSendVersion() const;

    CService GetAddrLocal() const;

    //! May not be called more than once
    void SetAddrLocal(const CService &addrLocalIn);

    CNode *AddRef() {
        nRefCount++;
        return this;
    }

    void Release() {
        nRefCount--;
    }


    void AddAddressKnown(const CAddress &_addr) {
        addrKnown.insert(_addr.GetKey());
    }

    void PushAddress(const CAddress &_addr, FastRandomContext &insecure_rand) {
        // Known checking here is only to save space from duplicates.
        // SendMessages will filter it again for knowns that were added
        // after addresses were pushed.
        if (_addr.IsValid() && !addrKnown.contains(_addr.GetKey())) {
            if (vAddrToSend.size() >= MAX_ADDR_TO_SEND) {
                vAddrToSend[insecure_rand.randrange(vAddrToSend.size())] = _addr;
            } else {
                vAddrToSend.push_back(_addr);
            }
        }
    }


    void AddInventoryKnown(const CInv &inv) {
        AddInventoryKnown(inv.hash);
    }

    void AddInventoryKnown(const uint256 &hash) {
        {
            LOCK(cs_inventory);
            filterInventoryKnown.insert(hash);
        }
    }

    void PushInventory(const CInv &inv) {
        LOCK(cs_inventory);
        if (inv.type == MSG_TX || inv.type == MSG_DSTX) {
            if (!filterInventoryKnown.contains(inv.hash)) {
                LogPrint(BCLog::NET, "PushInventory --  inv: %s peer=%d\n", inv.ToString(), id);
                setInventoryTxToSend.insert(inv.hash);
            } else {
                LogPrint(BCLog::NET, "PushInventory --  filtered inv: %s peer=%d\n", inv.ToString(), id);
            }
        } else if (inv.type == MSG_BLOCK) {
            LogPrint(BCLog::NET, "PushInventory --  inv: %s peer=%d\n", inv.ToString(), id);
            vInventoryBlockToSend.push_back(inv.hash);
        } else {
            if (!filterInventoryKnown.contains(inv.hash)) {
                LogPrint(BCLog::NET, "PushInventory --  inv: %s peer=%d\n", inv.ToString(), id);
                vInventoryOtherToSend.push_back(inv);
            } else {
                LogPrint(BCLog::NET, "PushInventory --  filtered inv: %s peer=%d\n", inv.ToString(), id);
            }
        }
    }

    void PushBlockHash(const uint256 &hash) {
        LOCK(cs_inventory);
        vBlockHashesToAnnounce.push_back(hash);
    }

    void CloseSocketDisconnect(CConnman *connman);

    void copyStats(CNodeStats &stats, const std::vector<bool> &m_asmap);

    ServiceFlags GetLocalServices() const {
        return nLocalServices;
    }

    std::string GetAddrName() const;

    //! Sets the addrName only if it was not previously set
    void MaybeSetAddrName(const std::string &addrNameIn);

    std::string GetLogString() const;

    bool CanRelay() const { return !m_smartnode_connection || m_smartnode_iqr_connection; }

    uint256 GetSentMNAuthChallenge() const {
        LOCK(cs_mnauth);
        return sentMNAuthChallenge;
    }

    uint256 GetReceivedMNAuthChallenge() const {
        LOCK(cs_mnauth);
        return receivedMNAuthChallenge;
    }

    uint256 GetVerifiedProRegTxHash() const {
        LOCK(cs_mnauth);
        return verifiedProRegTxHash;
    }

    uint256 GetVerifiedPubKeyHash() const {
        LOCK(cs_mnauth);
        return verifiedPubKeyHash;
    }

    void SetSentMNAuthChallenge(const uint256 &newSentMNAuthChallenge) {
        LOCK(cs_mnauth);
        sentMNAuthChallenge = newSentMNAuthChallenge;
    }

    void SetReceivedMNAuthChallenge(const uint256 &newReceivedMNAuthChallenge) {
        LOCK(cs_mnauth);
        receivedMNAuthChallenge = newReceivedMNAuthChallenge;
    }

    void SetVerifiedProRegTxHash(const uint256 &newVerifiedProRegTxHash) {
        LOCK(cs_mnauth);
        verifiedProRegTxHash = newVerifiedProRegTxHash;
    }

    void SetVerifiedPubKeyHash(const uint256 &newVerifiedPubKeyHash) {
        LOCK(cs_mnauth);
        verifiedPubKeyHash = newVerifiedPubKeyHash;
    }
};

class CExplicitNetCleanup {
public:
    static void callCleanup();
};

/** Return a timestamp in the future (in microseconds) for exponentially distributed events. */
int64_t PoissonNextSend(int64_t now, int average_interval_seconds);

/** Wrapper to return mockable type */
inline std::chrono::microseconds PoissonNextSend(std::chrono::microseconds now, std::chrono::seconds average_interval) {
    return std::chrono::microseconds{PoissonNextSend(now.count(), average_interval.count())};
}

#endif // BITCOIN_NET_H
