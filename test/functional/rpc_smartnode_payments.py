#!/usr/bin/env python3
# Copyright (c) 2024 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test payment query boundaries and spent-input fees without a txindex."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class RpcSmartnodePaymentsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [[], ["-txindex=0"]]

    def run_test(self):
        node, no_txindex = self.nodes
        genesis_hash = node.getblockhash(0)

        self.log.info("Genesis has no payment and is skipped rather than counted")
        assert_equal(node.smartnode("payments"), [])
        node.generate(5)
        tip_hash = node.getbestblockhash()
        assert_equal(node.getblockcount(), 5)
        assert_equal(node.smartnode("payments", genesis_hash, -1), [])
        assert_equal([entry["height"] for entry in node.smartnode("payments", genesis_hash, 2)], [1, 2])

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
        assert_equal([entry["height"] for entry in node.smartnode("payments", tip_hash, -10)], [1, 2, 3, 4, 5])
        assert_equal(node.smartnode("payments", tip_hash, 0), [])
        assert_raises_rpc_error(-8, "count is out of range", node.smartnode,
                                "payments", tip_hash, -9223372036854775808)

        self.log.info("Reject a still-indexed block outside the active chain")
        # Invalidate its parent, so the queried block itself was never explicitly
        # invalidated and its height now exceeds the active chain's height.
        parent_hash = node.getblock(tip_hash)["previousblockhash"]
        node.invalidateblock(parent_hash)
        assert_raises_rpc_error(-8, "Block is not in the active chain", node.smartnode,
                                "payments", tip_hash)
        node.reconsiderblock(parent_hash)
        self.sync_all()

        self.log.info("The node is still responsive after the boundary queries")
        assert_equal(node.getblockcount(), 5)
        assert_equal(node.getbestblockhash(), tip_hash)

        self.log.info("Read spent-input fees equally with and without the transaction index")
        node.generate(101)
        txid = node.sendtoaddress(node.getnewaddress(), 1)
        node.generate(1)
        self.sync_all()
        blockhash = node.getbestblockhash()
        assert txid in node.getblock(blockhash)["tx"]
        payments = node.smartnode("payments", blockhash, -3)
        assert_equal(len(payments), 3)
        assert_equal(no_txindex.smartnode("payments", blockhash, -3), payments)


if __name__ == '__main__':
    RpcSmartnodePaymentsTest().main()
