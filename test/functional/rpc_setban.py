#!/usr/bin/env python3
# Copyright (c) 2015-2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the setban rpc call."""

import contextlib
import os

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import connect_nodes, get_chain_folder, p2p_port, wait_until


@contextlib.contextmanager
def wait_for_debug_log(node, expected_msg, timeout=10):
    """Like TestNode.assert_debug_log, but polls instead of checking once.

    addnode is fire-and-forget: it returns once the connection attempt is
    dispatched, not once the receiving node has processed and logged a
    rejection. assert_debug_log reads the log exactly once, right when its
    `with` block exits, and only sometimes wins that race.
    """
    chain = get_chain_folder(node.datadir, node.chain)
    debug_log = os.path.join(node.datadir, chain, 'debug.log')
    with open(debug_log, encoding='utf-8') as dl:
        dl.seek(0, 2)
        start = dl.tell()

    yield

    def found():
        with open(debug_log, encoding='utf-8') as dl:
            dl.seek(start)
            return expected_msg in dl.read()

    wait_until(found, timeout=timeout)


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
        with wait_for_debug_log(self.nodes[1], 'dropped (banned)'):
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
