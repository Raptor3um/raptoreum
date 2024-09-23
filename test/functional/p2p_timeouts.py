#!/usr/bin/env python3
# Copyright (c) 2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test various net timeouts.

- Create three peers:

    no_verack_node - we never send a verack in response to their version
    no_version_node - we never send a version (only a ping)
    no_send_node - we never send any P2P message.

- Wait 1 second
- Assert that we're connected
- Send a ping to no_verack_node and no_version_node
- Wait 1 second
- Assert that we're still connected
- Send a ping to no_verack_node and no_version_node
- Wait 2 seconds
- Assert that we're no longer connected (timeout to receive version/verack is 3 seconds)
"""

from test_framework.mininode import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

import time

class TestP2PConn(P2PInterface):
    def on_version(self, message):
        # Don't send a verack in response
        pass

class TimeoutsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        # set timeout to receive version/verack to 3 seconds
        self.extra_args = [["-peertimeout=3"]]

    def mock_forward(self, delta):
        self.mock_time += delta
        self.nodes[0].setmocktime(self.mock_time)

    def run_test(self):
        self.mock_time = int(time.time())
        self.mock_forward(0)

        # Setup the p2p connections and start up the network thread.
        no_verack_node = self.nodes[0].add_p2p_connection(TestP2PConn())
        no_version_node = self.nodes[0].add_p2p_connection(TestP2PConn(), send_version=False)
        no_send_node = self.nodes[0].add_p2p_connection(TestP2PConn(), send_version=False)

        network_thread_start()

        self.mock_forward(1)

        assert no_verack_node.is_connected
        assert no_version_node.is_connected
        assert no_send_node.is_connected

        no_verack_node.send_message(msg_ping())
        no_version_node.send_message(msg_ping())

        self.mock_forward(1)

        assert "version" in no_verack_node.last_message

        assert no_verack_node.is_connected
        assert no_version_node.is_connected
        assert no_send_node.is_connected

        no_verack_node.send_message(msg_ping())
        no_version_node.send_message(msg_ping())

        expected_timeout_logs = [
            "version handshake timeout from 0",
            "socket no message in first 3 seconds, 1 0 from 1",
            "socket no message in first 3 seconds, 0 0 from 2",
        ]

        with self.nodes[0].assert_debug_log(expected_msgs=expected_timeout_logs):
            self.mock_forward(2)
            no_verack_node.wait_for_disconnect(timeout=1)
            no_version_node.wait_for_disconnect(timeout=1)
            no_send_node.wait_for_disconnect(timeout=1)

if __name__ == '__main__':
    TimeoutsTest().main()
