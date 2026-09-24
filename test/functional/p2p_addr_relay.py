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
    mininode_lock,
    network_thread_start,
    wait_until,
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

        expected = {(a.ip, a.port) for a in addrs}

        self.log.info('Create connection that sends addr messages')
        addr_source = node.add_p2p_connection(P2PInterface())
        network_thread_start()
        addr_source.wait_for_verack()
        msg = msg_addr()

        # Connected before the real addr message below is processed, so
        # RelayAddress (which fans out to peers connected at that instant)
        # has this connection to pick from.
        self.log.info('Create connection that receives relayed addr messages')
        addr_receiver = node.add_p2p_connection(P2PInterface())
        addr_receiver.wait_for_verack()

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

        self.log.info('Check that the node relays the addresses to the other peer')
        # These are routable IPv4 addresses with exactly two peers connected,
        # so RelayAddress always selects both; addr_source is filtered out by
        # its own addrKnown record, not by selection. The only uncertainty is
        # timing: nNextAddrSend gates the flush on GetTimeMicros(), a real
        # clock setmocktime does not move, and missing the first (near-
        # instant) flush means waiting out a PoissonNextSend(30s) interval --
        # exponential, so a long tail. 180s keeps the miss probability low
        # (e^(-180/30) ~ 0.25%). The predicate checks for one of our own
        # addresses, not just any 'addr' message, since AdvertiseLocal can
        # also flush the node's own address to a fresh connection.
        def relayed_to_receiver():
            msg = addr_receiver.last_message.get('addr')
            return msg is not None and any((a.ip, a.port) in expected for a in msg.addrs)

        wait_until(relayed_to_receiver, timeout=180, lock=mininode_lock)
        with mininode_lock:
            relayed = addr_receiver.last_message['addr'].addrs
        assert len(relayed) > 0
        for a in relayed:
            assert (a.ip, a.port) in expected, "unexpected address {}".format(a)
            assert_equal(a.nServices, NODE_NETWORK)

        self.log.info('Check that the node will serve them back')
        # getnodeaddresses serves a 23% sample, so this checks what comes back
        # is ours, not that all ten do.
        served = node.getnodeaddresses(len(addrs))
        assert len(served) > 0
        for a in served:
            assert (a['address'], a['port']) in expected, "unexpected address {}".format(a)
            assert_equal(a['services'], NODE_NETWORK)


if __name__ == '__main__':
    AddrTest().main()
