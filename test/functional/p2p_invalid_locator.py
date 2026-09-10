#!/usr/bin/env python3
# Copyright (c) 2015-2017 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test node responses to invalid locators."""

from test_framework.messages import msg_getheaders, msg_getblocks, MAX_LOCATOR_SZ
from test_framework.mininode import P2PInterface, network_thread_start
from test_framework.test_framework import BitcoinTestFramework


class InvalidLocatorTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = False

    def run_test(self):
        node = self.nodes[0]
        node.generate(1)  # get the node out of IBD
        block_count = node.getblockcount()

        # All connections are opened before the network thread starts: this
        # framework cannot add one afterwards, and the oversized cases below
        # get disconnected by the node.
        conns = [node.add_p2p_connection(P2PInterface()) for _ in range(4)]
        network_thread_start()
        for conn in conns:
            conn.wait_for_verack()

        def locator(msg, size):
            msg.locator.vHave = [int(node.getblockhash(i - 1), 16)
                                 for i in range(block_count, block_count - size, -1)]
            return msg

        self.log.info('Test max locator size')
        for i, msg in enumerate([msg_getheaders(), msg_getblocks()]):
            self.log.info('Wait for disconnect when sending %d hashes in locator' % (MAX_LOCATOR_SZ + 1))
            conns[i * 2].send_message(locator(msg, MAX_LOCATOR_SZ + 1))
            conns[i * 2].wait_for_disconnect()

            self.log.info('Wait for response when sending %d hashes in locator' % MAX_LOCATOR_SZ)
            conns[i * 2 + 1].send_message(locator(msg, MAX_LOCATOR_SZ))
            if isinstance(msg, msg_getheaders):
                conns[i * 2 + 1].wait_for_header(node.getbestblockhash())
            else:
                conns[i * 2 + 1].wait_for_block(int(node.getbestblockhash(), 16))


if __name__ == '__main__':
    InvalidLocatorTest().main()
