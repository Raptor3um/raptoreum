// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Unit tests for denial-of-service detection/prevention code

#include <banman.h>
#include <chainparams.h>
#include <keystore.h>
#include <net.h>
#include <net_processing.h>
#include <pow.h>
#include <script/sign.h>
#include <serialize.h>
#include <util/system.h>
#include <validation.h>

#include <test/test_raptoreum.h>

#include <stdint.h>

#ifdef USE_WAKEUP_PIPE
#include <fcntl.h>
#include <unistd.h>
#endif

#include <boost/test/unit_test.hpp>

struct CConnmanTest : public CConnman {
    using CConnman::CConnman;

    void AddNode(CNode &node) {
        LOCK(cs_vNodes);
        vNodes.push_back(&node);
    }

    void ClearNodes() {
        LOCK(cs_vNodes);
        for (CNode *node: vNodes) {
            delete node;
        }
        vNodes.clear();
    }

#ifdef USE_WAKEUP_PIPE
    // CConnman's own wakeupPipe is only opened by Start(), which also spins up the real
    // network threads -- too heavy for a unit test. Wire a private pipe into the same
    // member instead, so WakeSelect() has somewhere to write and a test can observe it.
    void SetupWakeupPipeForTest() {
        int fds[2];
        if (pipe(fds) == 0) {
            wakeupPipe[0] = fds[0];
            wakeupPipe[1] = fds[1];
            // Non-blocking read end: a test asking whether a wakeup happened must get an
            // answer either way, not block forever when the code under test failed to write.
            int flags = fcntl(wakeupPipe[0], F_GETFL, 0);
            fcntl(wakeupPipe[0], F_SETFL, flags | O_NONBLOCK);
        }
    }

    bool WakeupPipeSignaled() {
        char buf;
        return read(wakeupPipe[0], &buf, 1) == 1;
    }

    ~CConnmanTest() {
        if (wakeupPipe[0] != -1) close(wakeupPipe[0]);
        if (wakeupPipe[1] != -1) close(wakeupPipe[1]);
        wakeupPipe[0] = wakeupPipe[1] = -1;
    }
#endif
};

// Tests these internal-to-net_processing.cpp methods:
extern bool AddOrphanTx(const CTransactionRef &tx, NodeId peer);

extern void EraseOrphansFor(NodeId peer);

extern unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans);
// We don't need this, since we kept declaration in net_processing.h when backporting (#13417)
// extern void Misbehaving(NodeId nodeid, int howmuch, const std::string& message="");

struct COrphanTx {
    CTransactionRef tx;
    NodeId fromPeer;
    int64_t nTimeExpire;
};
extern std::map <uint256, COrphanTx> mapOrphanTransactions;

CService ip(uint32_t i) {
    struct in_addr s;
    s.s_addr = i;
    return CService(CNetAddr(s), Params().GetDefaultPort());
}

static NodeId id = 0;

void UpdateLastBlockAnnounceTime(NodeId node, int64_t time_in_seconds);

BOOST_FIXTURE_TEST_SUITE(denialofservice_tests, TestingSetup
)

// Test eviction of an outbound peer whose chain never advances
// Mock a node connection, and use mocktime to simulate a peer
// which never sends any headers messages.  PeerLogic should
// decide to evict that outbound peer, after the appropriate timeouts.
// Note that we protect 4 outbound nodes from being subject to
// this logic; this test takes advantage of that protection only
// being applied to nodes which send headers with sufficient
// work.
BOOST_AUTO_TEST_CASE(outbound_slow_chain_eviction)
        {
                auto connman = MakeUnique<CConnman>(0x1337, 0x1337);
        auto peerLogic = MakeUnique<PeerLogicValidation>(connman.get(), nullptr, *m_node.scheduler, *m_node.chainman, *m_node.mempool, false);

        // Mock an outbound peer
        CAddress addr1(ip(0xa0b0c001), NODE_NONE);
        CNode dummyNode1(id++, ServiceFlags(NODE_NETWORK), 0, INVALID_SOCKET, addr1, 0, 0, CAddress(), "", /*fInboundIn=*/ false);
        dummyNode1.SetSendVersion(PROTOCOL_VERSION);

        peerLogic->InitializeNode(&dummyNode1);
        dummyNode1.nVersion = 1;
        dummyNode1.fSuccessfullyConnected = true;

        // This test requires that we have a chain with non-zero work.
        {
            LOCK(cs_main);
            BOOST_CHECK(::ChainActive().Tip() != nullptr);
            BOOST_CHECK(::ChainActive().Tip()->nChainWork > 0);
        }

        // Test starts here
        {
            LOCK2(cs_main, dummyNode1.cs_sendProcessing);
            peerLogic->SendMessages(&dummyNode1); // should result in getheaders
        }
        {
            LOCK2(cs_main, dummyNode1.cs_vSend);
            BOOST_CHECK(dummyNode1.vSendMsg.size() > 0);
            dummyNode1.vSendMsg.clear();
            dummyNode1.nSendMsgSize = 0;
        }

        int64_t nStartTime = GetTime();
        // Wait 21 minutes
        SetMockTime(nStartTime+21*60);
        {
            LOCK2(cs_main, dummyNode1.cs_sendProcessing);
            peerLogic->SendMessages(&dummyNode1); // should result in getheaders
        }
        {
            LOCK2(cs_main, dummyNode1.cs_vSend);
            BOOST_CHECK(dummyNode1.vSendMsg.size() > 0);
        }
        // Wait 3 more minutes
        SetMockTime(nStartTime+24*60);
        {
            LOCK2(cs_main, dummyNode1.cs_sendProcessing);
            peerLogic->SendMessages(&dummyNode1); // should result in disconnect
        }
        BOOST_CHECK(dummyNode1.fDisconnect == true);
        SetMockTime(0);

        bool dummy;
        peerLogic->FinalizeNode(dummyNode1.GetId(), dummy);
        }

static void AddRandomOutboundPeer(std::vector<CNode *> &vNodes, PeerLogicValidation &peerLogic, CConnmanTest *connman) {
    CAddress addr(ip(g_insecure_rand_ctx.randbits(32)), NODE_NONE);
    vNodes.emplace_back(
            new CNode(id++, ServiceFlags(NODE_NETWORK), 0, INVALID_SOCKET, addr, 0, 0, CAddress(), "", /*fInboundIn=*/
                      false));
    CNode &node = *vNodes.back();
    node.SetSendVersion(PROTOCOL_VERSION);

    peerLogic.InitializeNode(&node);
    node.nVersion = 1;
    node.fSuccessfullyConnected = true;

    connman->AddNode(node);
}

BOOST_AUTO_TEST_CASE(stale_tip_peer_management)
        {
                auto connman = MakeUnique<CConnmanTest>(0x1337, 0x1337);
        auto peerLogic = MakeUnique<PeerLogicValidation>(connman.get(), nullptr, *m_node.scheduler, *m_node.chainman, *m_node.mempool, false);

        const Consensus::Params& consensusParams = Params().GetConsensus();
        constexpr int nMaxOutbound = 8;
        CConnman::Options options;
        options.nMaxConnections = 125;
        options.nMaxOutbound = nMaxOutbound;
        options.nMaxFeeler = 1;

        connman->Init(options);
        std::vector<CNode *> vNodes;

        // Mock some outbound peers
        for (int i=0; i<nMaxOutbound; ++i) {
            AddRandomOutboundPeer(vNodes, *peerLogic, connman.get());
        }

        peerLogic->CheckForStaleTipAndEvictPeers(consensusParams);

        // No nodes should be marked for disconnection while we have no extra peers
        for (const CNode *node : vNodes) {
            BOOST_CHECK(node->fDisconnect == false);
        }

        SetMockTime(GetTime() + 3*consensusParams.nPowTargetSpacing + 1);

        // Now tip should definitely be stale, and we should look for an extra
        // outbound peer
        peerLogic->CheckForStaleTipAndEvictPeers(consensusParams);
        BOOST_CHECK(connman->GetTryNewOutboundPeer());

        // Still no peers should be marked for disconnection
        for (const CNode *node : vNodes) {
            BOOST_CHECK(node->fDisconnect == false);
        }

        // If we add one more peer, something should get marked for eviction
        // on the next check (since we're mocking the time to be in the future, the
        // required time connected check should be satisfied).
        AddRandomOutboundPeer(vNodes, *peerLogic, connman.get());

        peerLogic->CheckForStaleTipAndEvictPeers(consensusParams);
        for (int i=0; i<nMaxOutbound; ++i) {
            BOOST_CHECK(vNodes[i]->fDisconnect == false);
        }
        // Last added node should get marked for eviction
        BOOST_CHECK(vNodes.back()->fDisconnect == true);

        vNodes.back()->fDisconnect = false;

        // Update the last announced block time for the last
        // peer, and check that the next newest node gets evicted.
        UpdateLastBlockAnnounceTime(vNodes.back()->GetId(), GetTime());

        peerLogic->CheckForStaleTipAndEvictPeers(consensusParams);
        for (int i=0; i<nMaxOutbound-1; ++i) {
            BOOST_CHECK(vNodes[i]->fDisconnect == false);
        }
        BOOST_CHECK(vNodes[nMaxOutbound-1]->fDisconnect == true);
        BOOST_CHECK(vNodes.back()->fDisconnect == false);

        bool dummy;
        for (const CNode *node : vNodes) {
            peerLogic->FinalizeNode(node->GetId(), dummy);
        }

        connman->ClearNodes();
        }

BOOST_AUTO_TEST_CASE(DoS_banning)
        {
                auto banman = MakeUnique<BanMan>(GetDataDir() / "banlist.dat", nullptr, DEFAULT_MISBEHAVING_BANTIME);
        auto connman = MakeUnique<CConnman>(0x1337, 0x1337);
        auto peerLogic = MakeUnique<PeerLogicValidation>(connman.get(), banman.get(), *m_node.scheduler, *m_node.chainman, *m_node.mempool, false);

        banman->ClearBanned();
        CAddress addr1(ip(0xa0b0c001), NODE_NONE);
        CNode dummyNode1(id++, NODE_NETWORK, 0, INVALID_SOCKET, addr1, 0, 0, CAddress(), "", true);
        dummyNode1.SetSendVersion(PROTOCOL_VERSION);
        peerLogic->InitializeNode(&dummyNode1);
        dummyNode1.nVersion = 1;
        dummyNode1.fSuccessfullyConnected = true;
        {
            LOCK(cs_main);
            Misbehaving(dummyNode1.GetId(), 100); // Should get banned
        }
        {
            LOCK2(cs_main, dummyNode1.cs_sendProcessing);
            peerLogic->SendMessages(&dummyNode1);
        }
        BOOST_CHECK(banman->IsBanned(addr1));
        BOOST_CHECK(!banman->IsBanned(ip(0xa0b0c001|0x0000ff00))); // Different IP, not banned

        CAddress addr2(ip(0xa0b0c002), NODE_NONE);
        CNode dummyNode2(id++, NODE_NETWORK, 0, INVALID_SOCKET, addr2, 1, 1, CAddress(), "", true);
        dummyNode2.SetSendVersion(PROTOCOL_VERSION);
        peerLogic->InitializeNode(&dummyNode2);
        dummyNode2.nVersion = 1;
        dummyNode2.fSuccessfullyConnected = true;
        {
            LOCK(cs_main);
            Misbehaving(dummyNode2.GetId(), 50);
        }
        {
            LOCK2(cs_main, dummyNode2.cs_sendProcessing);
            peerLogic->SendMessages(&dummyNode2);
        }
        BOOST_CHECK(!banman->IsBanned(addr2)); // 2 not banned yet...
        BOOST_CHECK(banman->IsBanned(addr1));  // ... but 1 still should be
        {
            LOCK(cs_main);
            Misbehaving(dummyNode2.GetId(), 50);
        }
        {
            LOCK2(cs_main, dummyNode2.cs_sendProcessing);
            peerLogic->SendMessages(&dummyNode2);
        }
        BOOST_CHECK(banman->IsBanned(addr2));

        bool dummy;
        peerLogic->FinalizeNode(dummyNode1.GetId(), dummy);
        peerLogic->FinalizeNode(dummyNode2.GetId(), dummy);
        }

BOOST_AUTO_TEST_CASE(DoS_banscore)
        {
                auto banman = MakeUnique<BanMan>(GetDataDir() / "banlist.dat", nullptr, DEFAULT_MISBEHAVING_BANTIME);
        auto connman = MakeUnique<CConnman>(0x1337, 0x1337);
        auto peerLogic = MakeUnique<PeerLogicValidation>(connman.get(), banman.get(), *m_node.scheduler, *m_node.chainman, *m_node.mempool, false);

        banman->ClearBanned();
        gArgs.ForceSetArg("-banscore", "111"); // because 11 is my favorite number
        CAddress addr1(ip(0xa0b0c001), NODE_NONE);
        CNode dummyNode1(id++, NODE_NETWORK, 0, INVALID_SOCKET, addr1, 3, 1, CAddress(), "", true);
        dummyNode1.SetSendVersion(PROTOCOL_VERSION);
        peerLogic->InitializeNode(&dummyNode1);
        dummyNode1.nVersion = 1;
        dummyNode1.fSuccessfullyConnected = true;
        {
            LOCK(cs_main);
            Misbehaving(dummyNode1.GetId(), 100);
        }
        {
            LOCK2(cs_main, dummyNode1.cs_sendProcessing);
            peerLogic->SendMessages(&dummyNode1);
        }
        BOOST_CHECK(!banman->IsBanned(addr1));
        {
            LOCK(cs_main);
            Misbehaving(dummyNode1.GetId(), 10);
        }
        {
            LOCK2(cs_main, dummyNode1.cs_sendProcessing);
            peerLogic->SendMessages(&dummyNode1);
        }
        BOOST_CHECK(!banman->IsBanned(addr1));
        {
            LOCK(cs_main);
            Misbehaving(dummyNode1.GetId(), 1);
        }
        {
            LOCK2(cs_main, dummyNode1.cs_sendProcessing);
            peerLogic->SendMessages(&dummyNode1);
        }
        BOOST_CHECK(banman->IsBanned(addr1));
        gArgs.ForceSetArg("-banscore", std::to_string(DEFAULT_BANSCORE_THRESHOLD));

        bool dummy;
        peerLogic->FinalizeNode(dummyNode1.GetId(), dummy);
        }

BOOST_AUTO_TEST_CASE(DoS_bantime)
        {
                auto banman = MakeUnique<BanMan>(GetDataDir() / "banlist.dat", nullptr, DEFAULT_MISBEHAVING_BANTIME);
        auto connman = MakeUnique<CConnman>(0x1337, 0x1337);
        auto peerLogic = MakeUnique<PeerLogicValidation>(connman.get(), banman.get(), *m_node.scheduler, *m_node.chainman, *m_node.mempool, false);

        banman->ClearBanned();
        int64_t nStartTime = GetTime();
        SetMockTime(nStartTime); // Overrides future calls to GetTime()

        CAddress addr(ip(0xa0b0c001), NODE_NONE);
        CNode dummyNode(id++, NODE_NETWORK, 0, INVALID_SOCKET, addr, 4, 4, CAddress(), "", true);
        dummyNode.SetSendVersion(PROTOCOL_VERSION);
        peerLogic->InitializeNode(&dummyNode);
        dummyNode.nVersion = 1;
        dummyNode.fSuccessfullyConnected = true;

        {
            LOCK(cs_main);
            Misbehaving(dummyNode.GetId(), 100);
        }
        {
            LOCK2(cs_main, dummyNode.cs_sendProcessing);
            peerLogic->SendMessages(&dummyNode);
        }
        BOOST_CHECK(banman->IsBanned(addr));

        SetMockTime(nStartTime+60*60);
        BOOST_CHECK(banman->IsBanned(addr));

        SetMockTime(nStartTime+60*60*24+1);
        BOOST_CHECK(!banman->IsBanned(addr));

        bool dummy;
        peerLogic->FinalizeNode(dummyNode.GetId(), dummy);
        }

CTransactionRef RandomOrphan() {
    std::map<uint256, COrphanTx>::iterator it;
    LOCK(cs_main);
    it = mapOrphanTransactions.lower_bound(InsecureRand256());
    if (it == mapOrphanTransactions.end())
        it = mapOrphanTransactions.begin();
    return it->second.tx;
}

BOOST_AUTO_TEST_CASE(DoS_mapOrphans)
        {
                CKey key;
        key.MakeNewKey(true);
        CBasicKeyStore keystore;
        keystore.AddKey(key);

        // 50 orphan transactions:
        for (int i = 0; i < 50; i++)
        {
            CMutableTransaction tx;
            tx.vin.resize(1);
            tx.vin[0].prevout.n = 0;
            tx.vin[0].prevout.hash = InsecureRand256();
            tx.vin[0].scriptSig << OP_1;
            tx.vout.resize(1);
            tx.vout[0].nValue = 1 * CENT;
            tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());

            AddOrphanTx(MakeTransactionRef(tx), i);
        }

        // ... and 50 that depend on other orphans:
        for (int i = 0; i < 50; i++)
        {
            CTransactionRef txPrev = RandomOrphan();

            CMutableTransaction tx;
            tx.vin.resize(1);
            tx.vin[0].prevout.n = 0;
            tx.vin[0].prevout.hash = txPrev->GetHash();
            tx.vout.resize(1);
            tx.vout[0].nValue = 1 * CENT;
            tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
            SignSignature(keystore, *txPrev, tx, 0, SIGHASH_ALL);

            AddOrphanTx(MakeTransactionRef(tx), i);
        }

        // This really-big orphan should be ignored:
        for (int i = 0; i < 10; i++)
        {
            CTransactionRef txPrev = RandomOrphan();

            CMutableTransaction tx;
            tx.vout.resize(1);
            tx.vout[0].nValue = 1 * CENT;
            tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
            tx.vin.resize(2777);
            for (unsigned int j = 0; j < tx.vin.size(); j++) {
                tx.vin[j].prevout.n = j;
                tx.vin[j].prevout.hash = txPrev->GetHash();
            }
            SignSignature(keystore, *txPrev, tx, 0, SIGHASH_ALL);
            // Re-use same signature for other inputs
            // (they don't have to be valid for this test)
            for (unsigned int j = 1; j < tx.vin.size(); j++)
                tx.vin[j].scriptSig = tx.vin[0].scriptSig;

            BOOST_CHECK(!AddOrphanTx(MakeTransactionRef(tx), i));
        }

        LOCK(cs_main);
        // Test EraseOrphansFor:
        for (NodeId i = 0; i < 3; i++)
        {
            size_t sizeBefore = mapOrphanTransactions.size();
            EraseOrphansFor(i);
            BOOST_CHECK(mapOrphanTransactions.size() < sizeBefore);
        }

        // Test LimitOrphanTxSize() function:
        LimitOrphanTxSize(40);
        BOOST_CHECK(mapOrphanTransactions.size() <= 40);
        LimitOrphanTxSize(10);
        BOOST_CHECK(mapOrphanTransactions.size() <= 10);
        LimitOrphanTxSize(0);
        BOOST_CHECK(mapOrphanTransactions.empty());
        }

#ifdef USE_WAKEUP_PIPE
// Regression test: ProcessMessages() must call CConnman::WakeSelect() the moment a peer's
// fPauseRecv clears, or the socket thread does not learn a paused peer is drainable again
// until its next SELECT_TIMEOUT_MILLISECONDS wakeup (see net_processing.cpp). Unlike the
// HasUnpausedReceivableNode() predicate covered in net_tests.cpp, this checks the actual
// WakeSelect() call site: deleting it, while leaving the surrounding logic untouched, would
// not be caught by a test of the predicate alone.
BOOST_AUTO_TEST_CASE(process_messages_wakes_select_on_unpause)
        {
                auto connman = MakeUnique<CConnmanTest>(0x1337, 0x1337);
        connman->SetupWakeupPipeForTest();
        auto peerLogic = MakeUnique<PeerLogicValidation>(connman.get(), nullptr, *m_node.scheduler, *m_node.chainman, *m_node.mempool, false);

        CAddress addr(ip(0xa0b0c002), NODE_NONE);
        CNode node(id++, ServiceFlags(NODE_NETWORK), 0, INVALID_SOCKET, addr, 0, 0, CAddress(), "", /*fInboundIn=*/ true);
        node.SetSendVersion(PROTOCOL_VERSION);
        peerLogic->InitializeNode(&node);
        node.nVersion = 1;
        node.fSuccessfullyConnected = true;

        // A message start that matches no configured network. ProcessMessages() disconnects
        // on it right after the wake_select check, before it would touch validation or the
        // mempool -- exactly the boundary this test needs to stay inside.
        CMessageHeader::MessageStartChars badStart = {0, 0, 0, 0};
        CNetMessage msg(badStart, SER_NETWORK, node.GetRecvVersion());
        const size_t queued_size = msg.vRecv.size() + CMessageHeader::HEADER_SIZE;

        node.fPauseRecv = true;
        // Default-constructed CConnman has a 0-byte receive flood size, so dropping the
        // queue to exactly 0 (not merely below the flood size) is what flips fPauseRecv.
        node.nProcessQueueSize = queued_size;
        {
            LOCK(node.cs_vProcessMsg);
            node.vProcessMsg.push_back(std::move(msg));
        }

        std::atomic<bool> interrupt{false};
        peerLogic->ProcessMessages(&node, interrupt);

        BOOST_CHECK(!node.fPauseRecv);
        BOOST_CHECK(connman->WakeupPipeSignaled());

        bool dummy;
        peerLogic->FinalizeNode(node.GetId(), dummy);
        }

// Companion to process_messages_wakes_select_on_unpause: that test alone would still pass
// if WakeSelect() were called unconditionally on every processed message rather than only
// on a genuine pause->unpause transition, since it never checks the non-paused case. Confirm
// a peer that was never paused leaves the wakeup pipe unsignaled after the same message is
// processed.
BOOST_AUTO_TEST_CASE(process_messages_does_not_wake_select_when_nothing_was_paused)
        {
                auto connman = MakeUnique<CConnmanTest>(0x1337, 0x1337);
        connman->SetupWakeupPipeForTest();
        auto peerLogic = MakeUnique<PeerLogicValidation>(connman.get(), nullptr, *m_node.scheduler, *m_node.chainman, *m_node.mempool, false);

        CAddress addr(ip(0xa0b0c003), NODE_NONE);
        CNode node(id++, ServiceFlags(NODE_NETWORK), 0, INVALID_SOCKET, addr, 0, 0, CAddress(), "", /*fInboundIn=*/ true);
        node.SetSendVersion(PROTOCOL_VERSION);
        peerLogic->InitializeNode(&node);
        node.nVersion = 1;
        node.fSuccessfullyConnected = true;

        // Same message shape as the sibling test -- disconnects right after the wake_select
        // check, before touching validation or the mempool. nProcessQueueSize must still be
        // set to match the message's own size, mirroring what the real receive path would
        // have added when queuing it: ProcessMessages unconditionally SUBTRACTS this same
        // amount when it pops the message (net_processing.cpp), so leaving it at the
        // default 0 underflows (nProcessQueueSize is size_t) into a huge value, which then
        // reads as over the flood size and spuriously pauses the peer -- exactly the
        // pause/wake transition this test exists to rule out, not induce.
        CMessageHeader::MessageStartChars badStart = {0, 0, 0, 0};
        CNetMessage msg(badStart, SER_NETWORK, node.GetRecvVersion());
        const size_t queued_size = msg.vRecv.size() + CMessageHeader::HEADER_SIZE;
        node.nProcessQueueSize = queued_size;
        {
            LOCK(node.cs_vProcessMsg);
            node.vProcessMsg.push_back(std::move(msg));
        }

        BOOST_REQUIRE(!node.fPauseRecv);

        std::atomic<bool> interrupt{false};
        peerLogic->ProcessMessages(&node, interrupt);

        BOOST_CHECK(!node.fPauseRecv);
        BOOST_CHECK(!connman->WakeupPipeSignaled());

        bool dummy;
        peerLogic->FinalizeNode(node.GetId(), dummy);
        }
#endif // USE_WAKEUP_PIPE

BOOST_AUTO_TEST_SUITE_END()
