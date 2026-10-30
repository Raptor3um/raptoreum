#!/usr/bin/env python3
# Copyright (c) 2015-2021 The Dash Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import LLMQ_TEST_NAME, RaptoreumTestFramework
from test_framework.util import *

'''
feature_llmq_connections.py

Checks intra quorum connections

'''

class LLMQConnections(RaptoreumTestFramework):
    def set_test_params(self):
        self.set_raptoreum_test_params(15, 14, fast_dip3_enforcement=True)
        self.set_raptoreum_llmq_test_params(5, 3)

    def run_test(self):
        self.nodes[0].spork("SPORK_17_QUORUM_DKG_ENABLED", 0)
        self.wait_for_sporks_same()

        q = self.mine_quorum()

        self.log.info("checking for old intra quorum connections")
        total_count = 0
        for mn in self.get_quorum_smartnodes(q):
            count = self.get_mn_connection_count(mn.node)
            total_count += count
            assert_greater_than_or_equal(count, 2)
        assert(total_count < 40)

        self.check_reconnects(2)

        self.log.info("Activating SPORK_25_QUORUM_POSE")
        self.nodes[0].spork("SPORK_25_QUORUM_POSE", 0)
        self.wait_for_sporks_same()

        self.log.info("mining one block and waiting for all members to connect to each other")
        self.nodes[0].generate(1)
        for mn in self.get_quorum_smartnodes(q):
            self.wait_for_mnauth(mn.node, 4)

        self.log.info("mine a new quorum and verify that all members connect to each other")
        q = self.mine_quorum()

        self.log.info("checking that all MNs got probed")
        for mn in self.get_quorum_smartnodes(q):
            wait_until(lambda: self.get_mn_probe_count(mn.node, q, False) == 4)

        self.log.info("checking that probes age")
        self.bump_mocktime(60)
        for mn in self.get_quorum_smartnodes(q):
            wait_until(lambda: self.get_mn_probe_count(mn.node, q, False) == 0)

        self.log.info("mine a new quorum and re-check probes")
        q = self.mine_quorum()
        for mn in self.get_quorum_smartnodes(q):
            wait_until(lambda: self.get_mn_probe_count(mn.node, q, True) == 4)

        self.log.info("Activating SPORK_23_QUORUM_ALL_CONNECTED")
        self.nodes[0].spork("SPORK_23_QUORUM_ALL_CONNECTED", 0)
        self.wait_for_sporks_same()

        self.check_reconnects(4)

        self.log.info("check that quorums leaving the keepOldConnections window are dropped")
        # keepOldConnections is 3, so a fourth quorum pushes the oldest out.
        old_quorum = self.nodes[0].quorum("list", 3)[LLMQ_TEST_NAME][-1]
        mn = self.get_quorum_smartnodes(old_quorum)[0]
        with mn.node.assert_debug_log(['removing smartnodes quorum connections for quorum %s' % old_quorum]):
            self.mine_quorum()

    def check_reconnects(self, expected_connection_count):
        self.log.info("disable and re-enable networking on all masternodes")
        for mn in self.mninfo:
            mn.node.setnetworkactive(False)
        for mn in self.mninfo:
            wait_until(lambda: len(mn.node.getpeerinfo()) == 0)
        for mn in self.mninfo:
            mn.node.setnetworkactive(True)
        self.bump_mocktime(60)

        # Reconnect before forcing the sync: connecting resets it, and an
        # unsynced smartnode opens no quorum connections.
        for i in range(1, len(self.nodes)):
            connect_nodes(self.nodes[i], 0)
        for mn in self.mninfo:
            force_finish_mnsync(mn.node)

        self.log.info("verify that all masternodes re-connected")
        for q in self.nodes[0].quorum('list')[LLMQ_TEST_NAME]:
            for mn in self.get_quorum_smartnodes(q):
                self.wait_for_mnauth(mn.node, expected_connection_count)

        # Also re-connect non-masternode connections
        for i in range(1, len(self.nodes)):
            connect_nodes(self.nodes[i], 0)
            self.nodes[i].ping()
        # wait for ping/pong so that we can be sure that spork propagation works
        time.sleep(1) # needed to make sure we don't check before the ping is actually sent (fPingQueued might be true but SendMessages still not called)
        for i in range(1, len(self.nodes)):
            wait_until(lambda: all('pingwait' not in peer for peer in self.nodes[i].getpeerinfo()))

    def get_mn_connection_count(self, node):
        peers = node.getpeerinfo()
        count = 0
        for p in peers:
            if 'verified_proregtx_hash' in p and p['verified_proregtx_hash'] != '':
                count += 1
        return count

    def get_mn_probe_count(self, node, q, check_peers):
        count = 0
        mnList = node.protx('list', 'registered', 1)
        peerList = node.getpeerinfo()
        mnMap = {}
        peerMap = {}
        for mn in mnList:
            mnMap[mn['proTxHash']] = mn
        for p in peerList:
            if 'verified_proregtx_hash' in p and p['verified_proregtx_hash'] != '':
                peerMap[p['verified_proregtx_hash']] = p
        for mn in self.get_quorum_smartnodes(q):
            pi = mnMap[mn.proTxHash]
            if pi['metaInfo']['lastOutboundSuccessElapsed'] < 60:
                count += 1
            elif check_peers and mn.proTxHash in peerMap:
                count += 1
        return count


if __name__ == '__main__':
    LLMQConnections().main()
