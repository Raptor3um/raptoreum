#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test mempool re-org scenarios.

Test re-org scenarios with a mempool that contains transactions
that spend (directly or indirectly) coinbase transactions.
"""

from decimal import Decimal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

# Create one-input, one-output, no-fee transaction:
class MempoolCoinbaseTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.extra_args = [["-checkmempool"]] * 2

    alert_filename = None  # Set by setup_network

    def run_test(self):
        # Start with the cached chain
        assert_equal(self.nodes[0].getblockcount(), CACHE_HEIGHT)

        # Mine four blocks. After this the last three of the blocks selected
        # below are spend-able, and the first is one block short of maturity.
        new_blocks = self.nodes[1].generate(4)
        self.sync_all()

        node0_address = self.nodes[0].getnewaddress()
        node1_address = self.nodes[1].getnewaddress()

        # Three scenarios for re-orging coinbase spends in the memory pool:
        # 1. Direct coinbase spend  :  spend_101
        # 2. Indirect (coinbase spend in chain, child in mempool) : spend_102 and spend_102_1
        # 3. Indirect (coinbase and child both in chain) : spend_103 and spend_103_1
        # Use invalidatblock to make all of the above coinbase spends invalid (immature coinbase),
        # and make sure the mempool code behaves correctly.
        b = [ self.nodes[0].getblockhash(n) for n in
              range(CACHE_HEIGHT - 99, CACHE_HEIGHT - 95) ]
        coinbase_txids = [ self.nodes[0].getblock(h)['tx'][0] for h in b ]
        spend_101_raw = create_tx(self.nodes[0], coinbase_txids[1], node1_address, float(REGTEST_SUBSIDY - Decimal("0.1")))
        spend_102_raw = create_tx(self.nodes[0], coinbase_txids[2], node0_address, float(REGTEST_SUBSIDY - Decimal("0.1")))
        spend_103_raw = create_tx(self.nodes[0], coinbase_txids[3], node0_address, float(REGTEST_SUBSIDY - Decimal("0.1")))

        # Create a transaction which is time-locked to two blocks in the future
        timelock_tx = self.nodes[0].createrawtransaction([{"txid": coinbase_txids[0], "vout": 0}], {node0_address: float(REGTEST_SUBSIDY - Decimal("0.1"))})
        # Set the time lock
        timelock_tx = timelock_tx.replace("ffffffff", "11111191", 1)
        # nLockTime is 4 bytes little-endian. The original hex()[2:] + "000000"
        # only produced 8 hex chars while the height was two digits, so it broke
        # as soon as the chain passed block 255.
        locktime = self.nodes[0].getblockcount() + 2
        timelock_tx = timelock_tx[:-8] + locktime.to_bytes(4, "little").hex()
        timelock_tx = self.nodes[0].signrawtransactionwithwallet(timelock_tx)["hex"]
        # This will raise an exception because the timelock transaction is too immature to spend
        assert_raises_rpc_error(-26, "non-final", self.nodes[0].sendrawtransaction, timelock_tx)

        # Broadcast and mine spend_102 and 103:
        spend_102_id = self.nodes[0].sendrawtransaction(spend_102_raw)
        spend_103_id = self.nodes[0].sendrawtransaction(spend_103_raw)
        self.nodes[0].generate(1)
        # Time-locked transaction is still too immature to spend
        assert_raises_rpc_error(-26,'non-final', self.nodes[0].sendrawtransaction, timelock_tx)

        # Create 102_1 and 103_1:
        spend_102_1_raw = create_tx(self.nodes[0], spend_102_id, node1_address, float(REGTEST_SUBSIDY - Decimal("0.2")))
        spend_103_1_raw = create_tx(self.nodes[0], spend_103_id, node1_address, float(REGTEST_SUBSIDY - Decimal("0.2")))

        # Broadcast and mine 103_1:
        spend_103_1_id = self.nodes[0].sendrawtransaction(spend_103_1_raw)
        last_block = self.nodes[0].generate(1)
        # Time-locked transaction can now be spent
        timelock_tx_id = self.nodes[0].sendrawtransaction(timelock_tx)

        # ... now put spend_101 and spend_102_1 in memory pools:
        spend_101_id = self.nodes[0].sendrawtransaction(spend_101_raw)
        spend_102_1_id = self.nodes[0].sendrawtransaction(spend_102_1_raw)

        self.sync_all()

        assert_equal(set(self.nodes[0].getrawmempool()), {spend_101_id, spend_102_1_id, timelock_tx_id})

        for node in self.nodes:
            node.invalidateblock(last_block[0])
        # Time-locked transaction is now too immature and has been removed from the mempool
        # spend_103_1 has been re-orged out of the chain and is back in the mempool
        assert_equal(set(self.nodes[0].getrawmempool()), {spend_101_id, spend_102_1_id, spend_103_1_id})

        # Use invalidateblock to re-org back and make all those coinbase spends
        # immature/invalid:
        for node in self.nodes:
            node.invalidateblock(new_blocks[0])

        self.sync_all()

        # mempool should be empty.
        assert_equal(set(self.nodes[0].getrawmempool()), set())

if __name__ == '__main__':
    MempoolCoinbaseTest().main()
