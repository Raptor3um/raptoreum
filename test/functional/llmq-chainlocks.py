#!/usr/bin/env python3
# Copyright (c) 2015-2020 The Dash Core developers
# Copyright (c) 2020-2022 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import time

from test_framework.mininode import *
from test_framework.test_framework import RaptoreumTestFramework
from test_framework.util import *

'''
llmq-chainlocks.py

Checks LLMQs based ChainLocks

'''

class LLMQChainLocksTest(RaptoreumTestFramework):
    def set_test_params(self):
        self.set_raptoreum_test_params(6, 5, fast_dip3_enforcement=True)

    def run_test(self):

        self.log.info("Wait for dip0008 activation")

        while self.nodes[0].getblockchaininfo()["bip9_softforks"]["dip0008"]["status"] != "active":
            self.nodes[0].generate(10)
        sync_blocks(self.nodes, timeout=60*5)

        self.nodes[0].spork("SPORK_17_QUORUM_DKG_ENABLED", 0)
        self.wait_for_sporks_same()

        self.log.info("Mining 4 quorums")
        for i in range(4):
            self.mine_quorum()

        self.nodes[0].spork("SPORK_19_CHAINLOCKS_ENABLED", 0)
        self.wait_for_sporks_same()

        self.log.info("Mine single block, wait for chainlock")
        self.nodes[0].generate(1)
        self.wait_for_chainlocked_block_all_nodes(self.nodes[0].getbestblockhash())

        self.log.info("Mine many blocks, wait for chainlock")
        self.nodes[0].generate(20)
        # We need more time here due to 20 blocks being generated at once
        self.wait_for_chainlocked_block_all_nodes(self.nodes[0].getbestblockhash(), timeout=30)

        self.log.info("Assert that all blocks up until the tip are chainlocked")
        for h in range(1, self.nodes[0].getblockcount()):
            block = self.nodes[0].getblock(self.nodes[0].getblockhash(h))
            assert(block['chainlock'])

        self.log.info("Isolate node, mine on another, and reconnect")
        isolate_node(self.nodes[0])
        node0_mining_addr = self.nodes[0].getnewaddress()
        node0_tip = self.nodes[0].getbestblockhash()
        self.nodes[1].generatetoaddress(5, node0_mining_addr)
        self.wait_for_chainlocked_block(self.nodes[1], self.nodes[1].getbestblockhash())
        assert(self.nodes[0].getbestblockhash() == node0_tip)
        reconnect_isolated_node(self.nodes[0], 1)
        self.nodes[1].generatetoaddress(1, node0_mining_addr)
        self.wait_for_chainlocked_block(self.nodes[0], self.nodes[1].getbestblockhash())

        self.log.info("Isolate node, mine on both parts of the network, and reconnect")
        isolate_node(self.nodes[0])
        self.nodes[0].generate(5)
        self.nodes[1].generatetoaddress(1, node0_mining_addr)
        good_tip = self.nodes[1].getbestblockhash()
        self.wait_for_chainlocked_block(self.nodes[1], good_tip)
        assert(not self.nodes[0].getblock(self.nodes[0].getbestblockhash())["chainlock"])
        reconnect_isolated_node(self.nodes[0], 1)
        self.nodes[1].generatetoaddress(1, node0_mining_addr)
        self.wait_for_chainlocked_block(self.nodes[0], self.nodes[1].getbestblockhash())
        assert(self.nodes[0].getblock(self.nodes[0].getbestblockhash())["previousblockhash"] == good_tip)
        assert(self.nodes[1].getblock(self.nodes[1].getbestblockhash())["previousblockhash"] == good_tip)

        self.log.info("Keep node connected and let it try to reorg the chain")
        good_tip = self.nodes[0].getbestblockhash()
        self.log.info("Restart it so that it forgets all the chainlocks from the past")
        self.stop_node(0)
        self.start_node(0)
        connect_nodes(self.nodes[0], 1)
        self.nodes[0].invalidateblock(self.nodes[0].getbestblockhash())
        self.log.info("Now try to reorg the chain")
        self.nodes[0].generate(2)
        time.sleep(6)
        assert(self.nodes[1].getbestblockhash() == good_tip)
        self.nodes[0].generate(2)
        time.sleep(6)
        assert(self.nodes[1].getbestblockhash() == good_tip)

        self.log.info("Now let the node which is on the wrong chain reorg back to the locked chain")
        self.nodes[0].reconsiderblock(good_tip)
        assert(self.nodes[0].getbestblockhash() != good_tip)
        self.nodes[1].generatetoaddress(1, node0_mining_addr)
        self.wait_for_chainlocked_block(self.nodes[0], self.nodes[1].getbestblockhash())
        assert(self.nodes[0].getbestblockhash() == self.nodes[1].getbestblockhash())

        self.log.info("Enable LLMQ bases InstantSend, which also enables checks for \"safe\" transactions")
        self.nodes[0].spork("SPORK_2_INSTANTSEND_ENABLED", 0)
        self.nodes[0].spork("SPORK_3_INSTANTSEND_BLOCK_FILTERING", 0)
        self.wait_for_sporks_same()

        self.log.info("Isolate a node and let it create some transactions which won't get IS locked")
        isolate_node(self.nodes[0])
        txs = []
        for i in range(3):
            txs.append(self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 1))
        txs += self.create_chained_txs(self.nodes[0], 1)
        self.log.info("Assert that after block generation these TXs are NOT included (as they are \"unsafe\")")
        self.nodes[0].generate(1)
        for txid in txs:
            tx = self.nodes[0].getrawtransaction(txid, 1)
            assert("confirmations" not in tx)
        time.sleep(1)
        assert(not self.nodes[0].getblock(self.nodes[0].getbestblockhash())["chainlock"])
        self.log.info("Disable LLMQ based InstantSend for a very short time (this never gets propagated to other nodes)")
        self.nodes[0].spork("SPORK_2_INSTANTSEND_ENABLED", 4070908800)
        self.log.info("Now the TXs should be included")
        self.nodes[0].generate(1)
        self.nodes[0].spork("SPORK_2_INSTANTSEND_ENABLED", 0)
        self.log.info("Assert that TXs got included now")
        for txid in txs:
            tx = self.nodes[0].getrawtransaction(txid, 1)
            assert("confirmations" in tx and tx["confirmations"] > 0)
        # Enable network on first node again, which will cause the blocks to propagate and IS locks to happen retroactively
        # for the mined TXs, which will then allow the network to create a CLSIG
        self.log.info("Reenable network on first node and wait for chainlock")
        reconnect_isolated_node(self.nodes[0], 1)
        self.wait_for_chainlocked_block(self.nodes[0], self.nodes[0].getbestblockhash(), timeout=30)

    def create_chained_txs(self, node, amount):
        txid = node.sendtoaddress(node.getnewaddress(), amount)
        tx = node.getrawtransaction(txid, 1)
        inputs = []
        valueIn = 0
        for txout in tx["vout"]:
            inputs.append({"txid": txid, "vout": txout["n"]})
            valueIn += txout["value"]
        outputs = {
            node.getnewaddress(): round(float(valueIn) - 0.0001, 6)
        }

        rawtx = node.createrawtransaction(inputs, outputs)
        rawtx = node.signrawtransaction(rawtx)
        rawtxid = node.sendrawtransaction(rawtx["hex"])

        return [txid, rawtxid]


if __name__ == '__main__':
    LLMQChainLocksTest().main()
