#!/usr/bin/env python3
# Copyright (c) 2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the unknown-block-version warnings.

UpdateTip() in src/validation.cpp compares each new tip's nVersion against
UpdateManager::ComputeBlockVersion() and builds two warnings: one when the tip
itself sets a bit no deployment expects, and one counting how many of the last
100 blocks did. Both are detected correctly.

Neither is surfaced, though. Raptoreum kept DoWarning() -- which is what puts a
warning into strMiscWarning, and from there into getmininginfo()/getnetworkinfo()
and -alertnotify -- but dropped every call to it, so the function is dead code
and the warnings only ever reach debug.log. That is what this test asserts: the
log lines, and the empty RPC warnings field that goes with them. If the RPC
assertions here ever start failing, DoWarning() was wired back up and these
assertions belong on the RPC side instead.
"""
import re

from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import msg_block
from test_framework.mininode import P2PInterface, network_thread_start
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

VB_TOP_BITS = 0x20000000
VB_UNKNOWN_BIT = 27       # Choose a bit unassigned to any deployment
VB_UNKNOWN_VERSION = VB_TOP_BITS | (1 << VB_UNKNOWN_BIT)

WARN_UNKNOWN_RULES_ACTIVE = "Warning: unknown new rules activated"
VB_PATTERN = re.compile("unknown new rules activated")


class VersionBitsWarningTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def send_blocks_with_version(self, peer, numblocks, version):
        """Send numblocks blocks to peer with version set"""
        tip = self.nodes[0].getbestblockhash()
        height = self.nodes[0].getblockcount()
        block_time = self.nodes[0].getblockheader(tip)["time"] + 1
        tip = int(tip, 16)

        for _ in range(numblocks):
            block = create_block(tip, create_coinbase(height + 1), block_time, node=self.nodes[0])
            block.nVersion = version
            block.solve()
            peer.send_message(msg_block(block))
            block_time += 1
            height += 1
            tip = block.sha256
        peer.sync_with_ping()

    def run_test(self):
        node = self.nodes[0]
        node.add_p2p_connection(P2PInterface())
        network_thread_start()
        node.p2p.wait_for_verack()

        # Leave initial block download; the warnings are not computed during it.
        node.generate(1)
        assert not node.getblockchaininfo()['initialblockdownload']

        self.log.info("A block signalling an unknown bit warns about unknown new rules")
        with node.assert_debug_log([WARN_UNKNOWN_RULES_ACTIVE]):
            self.send_blocks_with_version(node.p2p, 1, VB_UNKNOWN_VERSION)

        self.log.info("The last-100 counter grows with each such block")
        with node.assert_debug_log(["4 of last 100 blocks have unexpected version"]):
            self.send_blocks_with_version(node.p2p, 3, VB_UNKNOWN_VERSION)

        self.log.info("Blocks of the expected version do not warn")
        expected_version = node.getblocktemplate({'rules': []})['version']
        with node.assert_debug_log(["4 of last 100 blocks have unexpected version"]):
            self.send_blocks_with_version(node.p2p, 1, expected_version)

        self.log.info("None of this reaches the RPC warnings field: DoWarning() is dead code")
        assert_equal(VB_PATTERN.search(node.getmininginfo()["warnings"]), None)
        assert_equal(VB_PATTERN.search(node.getnetworkinfo()["warnings"]), None)


if __name__ == '__main__':
    VersionBitsWarningTest().main()
