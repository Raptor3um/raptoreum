#!/usr/bin/env python3
# Copyright (c) 2015-2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the setban rpc call."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import connect_nodes, p2p_port


class SetBanTests(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [[], []]

    def setup_network(self):
        self.setup_nodes()

    def run_test(self):
        # Upstream reads a 'permissions' array; this tree reports a
        # 'whitelisted' bool (rpc/net.cpp:145) and has no per-permission list.
        connect_nodes(self.nodes[0], 1)
        assert not self.nodes[1].getpeerinfo()[0]['whitelisted']

        self.nodes[1].setban("127.0.0.1", "add")

        # Node 0 should not be able to reconnect
        with self.nodes[1].assert_debug_log(expected_msgs=['dropped (banned)'], ):
            self.restart_node(1, [])
            self.nodes[0].addnode("127.0.0.1:" + str(p2p_port(1)), "onetry")

        # but it should if it is whitelisted
        self.restart_node(1, ['-whitelist=127.0.0.1'])
        connect_nodes(self.nodes[0], 1)
        assert self.nodes[1].getpeerinfo()[0]['whitelisted']

        # and once unbanned, without being whitelisted
        self.nodes[1].setban("127.0.0.1", "remove")
        self.restart_node(1, [])
        connect_nodes(self.nodes[0], 1)
        assert not self.nodes[1].getpeerinfo()[0]['whitelisted']


if __name__ == '__main__':
    SetBanTests().main()
