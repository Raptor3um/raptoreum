#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin Core developers
# Distributed under MIT software license, see the accompanying
# file COPYING or https://opensource.org/licenses/mit-license.php.
"""
Test addr relay
"""

import time

from test_framework.messages import (
    CAddress,
    NODE_NETWORK,
    msg_addr,
)
from test_framework.mininode import (
    P2PInterface,
    network_thread_start,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
)


class AddrTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = False
        self.num_nodes = 1
        # CNode starts with a single address-processing token and refills at
        # MAX_ADDR_RATE_PER_SECOND (0.1/s), so an unwhitelisted peer would get
        # one address out of the ten through. Whitelisting skips the limiter.
        self.extra_args = [['-whitelist=127.0.0.1']]

    def run_test(self):
        node = self.nodes[0]

        # The node runs on a mocked clock, so the addresses have to be
        # timestamped against that rather than the wall clock: ProcessMessage
        # rewrites an address more than ten minutes in the node's future to
        # five days in its past, and an address that old is dropped rather than
        # stored.
        now = int(time.time())
        node.setmocktime(now)
        addrs = []
        for i in range(10):
            addr = CAddress()
            addr.time = now + i
            addr.nServices = NODE_NETWORK
            addr.ip = "123.123.123.{}".format(i)
            addr.port = 8333 + i
            addrs.append(addr)

        self.log.info('Create connection that sends addr messages')
        addr_source = node.add_p2p_connection(P2PInterface())
        network_thread_start()
        addr_source.wait_for_verack()
        msg = msg_addr()

        self.log.info('Send too large addr message')
        msg.addrs = addrs * 101
        with node.assert_debug_log(['message addr size() = 1010']):
            addr_source.send_and_ping(msg)

        self.log.info('Check that addr message content is added to addrman')
        msg.addrs = addrs
        with node.assert_debug_log([
                'received: addr (301 bytes) peer=0',
                'Added 10 addresses from 127.0.0.1: 0 tried',
        ]):
            addr_source.send_and_ping(msg)

        self.log.info('Check that the node will serve them back')
        # Onward relay is not assertable: SendMessages gates the addr flush on
        # GetTimeMicros(), a real clock setmocktime does not move. What is
        # deterministic is that the addresses reached addrman. getnodeaddresses
        # serves a 23% sample, so this checks what comes back is ours, not all ten.
        served = node.getnodeaddresses(len(addrs))
        assert len(served) > 0
        expected = {(a.ip, a.port) for a in addrs}
        for a in served:
            assert (a['address'], a['port']) in expected, "unexpected address {}".format(a)
            assert_equal(a['services'], NODE_NETWORK)


if __name__ == '__main__':
    AddrTest().main()
