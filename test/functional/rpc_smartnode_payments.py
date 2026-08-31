#!/usr/bin/env python3
# Copyright (c) 2024 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the `smartnode payments` RPC around the genesis block.

Regression test for a node crash.

Old behaviour (before the fix):
    `smartnode payments <genesis-hash>` computed the block reward with
    `GetBlockSubsidy(pindex->pprev->nBits, ...)`. For the genesis block
    `pindex->pprev` is a null pointer, so this dereferenced null and
    crashed the whole node (segfault / lost RPC connection).

New behaviour (after the fix):
    the RPC detects that the requested block has no previous block and
    returns a clear JSON-RPC error (RPC_INVALID_PARAMETER, -8) while the
    node keeps running. Normal blocks are unaffected.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class RpcSmartnodePaymentsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0]
        genesis_hash = node.getblockhash(0)

        self.log.info("Mine a few blocks so the chain has a real, non-genesis tip")
        node.generate(5)
        tip_hash = node.getbestblockhash()
        assert_equal(node.getblockcount(), 5)

        self.log.info("`smartnode payments` on a normal block still works")
        payments = node.smartnode("payments", tip_hash)
        assert_equal(len(payments), 1)
        assert_equal(payments[0]["height"], 5)
        assert_equal(payments[0]["blockhash"], tip_hash)

        self.log.info("The `count` argument selects how many blocks are returned")
        # Before the fix the count was read only when more than two parameters were
        # supplied, which never happens for a two-parameter call, so it silently
        # stayed at 1 and every one of these returned a single block.
        assert_equal(len(node.smartnode("payments", tip_hash, -1)), 1)
        assert_equal(len(node.smartnode("payments", tip_hash, 1)), 1)
        # counting backwards from the tip returns the tip last
        back3 = node.smartnode("payments", tip_hash, -3)
        assert_equal([entry["height"] for entry in back3], [3, 4, 5])
        # counting forwards from an earlier block returns that block first
        fwd3 = node.smartnode("payments", node.getblockhash(2), 3)
        assert_equal([entry["height"] for entry in fwd3], [2, 3, 4])
        # the tip cannot be extended forwards, so a large count still stops there
        assert_equal([entry["height"] for entry in node.smartnode("payments", tip_hash, 10)], [5])

        self.log.info("`smartnode payments` on the genesis block returns an error instead of crashing the node")
        # Before the fix this call dereferenced a null `pprev` and took the node down.
        assert_raises_rpc_error(-8, "genesis block", node.smartnode, "payments", genesis_hash)

        self.log.info("The node is still alive and responsive after the genesis query")
        # This is the crux of the regression test: on the unpatched node the call
        # above crashes the daemon and this final RPC never returns.
        assert_equal(node.getblockcount(), 5)
        assert_equal(node.getbestblockhash(), tip_hash)


if __name__ == '__main__':
    RpcSmartnodePaymentsTest().main()
