#!/usr/bin/env python3
# Copyright (c) 2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test NULLDUMMY (BIP147) enforcement.

Raptoreum has no soft-fork activation for BIP147: consensus.BIP147Enabled is a
bool that is true on every network (chainparams.cpp), so SCRIPT_VERIFY_NULLDUMMY
is in the consensus flags from genesis. What is left to test is enforcement:
the dummy element consumed by CHECKMULTISIG must be the empty push, in the
mempool and in a block alike.
"""

from io import BytesIO

from test_framework.blocktools import create_block, create_coinbase
from test_framework.mininode import CTransaction, network_thread_start
from test_framework.script import CScript
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    bytes_to_hex_str,
    hex_str_to_bytes,
)

# Enough blocks for the first two coinbases to mature (COINBASE_MATURITY = 100).
MATURITY_BLOCKS = 100

NULLDUMMY_ERROR = "non-mandatory-script-verify-flag (Dummy CHECKMULTISIG argument must be zero) (code 64)"


def trueDummy(tx):
    scriptSig = CScript(tx.vin[0].scriptSig)
    newscript = []
    for i in scriptSig:
        if (len(newscript) == 0):
            assert(len(i) == 0)
            newscript.append(b'\x51')
        else:
            newscript.append(i)
    tx.vin[0].scriptSig = CScript(newscript)
    tx.rehash()


class NULLDUMMYTest(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-whitelist=127.0.0.1']]

    def run_test(self):
        self.address = self.nodes[0].getnewaddress()
        self.ms_address = self.nodes[0].addmultisigaddress(1, [self.address])['address']

        network_thread_start()
        self.coinbase_blocks = self.nodes[0].generate(2)
        coinbase_txid = [self.nodes[0].getblock(i)['tx'][0] for i in self.coinbase_blocks]
        self.nodes[0].generate(MATURITY_BLOCKS)
        self.lastblockhash = self.nodes[0].getbestblockhash()
        self.tip = int(self.lastblockhash, 16)
        self.lastblockheight = self.nodes[0].getblockcount()
        self.lastblocktime = self.nodes[0].getblockheader(self.lastblockhash)['mediantime']

        self.log.info("NULLDUMMY compliant transactions are accepted to the mempool and mined")
        # The chain is still in the launch window, so a coinbase pays 4 RTM.
        tx1 = self.create_transaction(self.nodes[0], coinbase_txid[0], self.ms_address, 3)
        txid1 = self.nodes[0].sendrawtransaction(bytes_to_hex_str(tx1.serialize()), 0)
        tx2 = self.create_transaction(self.nodes[0], txid1, self.ms_address, 2)
        txid2 = self.nodes[0].sendrawtransaction(bytes_to_hex_str(tx2.serialize()), 0)
        self.block_submit(self.nodes[0], [tx1, tx2], accept=True)

        self.log.info("A non-NULLDUMMY multisig spend is rejected from the mempool")
        badtx = self.create_transaction(self.nodes[0], txid2, self.ms_address, 1)
        goodtx = CTransaction(badtx)
        trueDummy(badtx)
        assert_raises_rpc_error(-26, NULLDUMMY_ERROR, self.nodes[0].sendrawtransaction,
                                bytes_to_hex_str(badtx.serialize()), 0)

        self.log.info("A block containing it is rejected too")
        self.block_submit(self.nodes[0], [badtx], accept=False)

        self.log.info("The same spend with a compliant dummy is accepted")
        self.nodes[0].sendrawtransaction(bytes_to_hex_str(goodtx.serialize()), 0)
        self.block_submit(self.nodes[0], [goodtx], accept=True)

    def create_transaction(self, node, txid, to_address, amount):
        inputs = [{"txid": txid, "vout": 0}]
        outputs = {to_address: amount}
        rawtx = node.createrawtransaction(inputs, outputs)
        signresult = node.signrawtransactionwithwallet(rawtx)
        tx = CTransaction()
        tx.deserialize(BytesIO(hex_str_to_bytes(signresult['hex'])))
        return tx

    def block_submit(self, node, txs, accept=False):
        block = create_block(self.tip, create_coinbase(self.lastblockheight + 1),
                             self.lastblocktime + 1, node=node)
        for tx in txs:
            tx.rehash()
            block.vtx.append(tx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()
        node.submitblock(bytes_to_hex_str(block.serialize()))
        if accept:
            assert_equal(node.getbestblockhash(), block.hash)
            self.tip = block.sha256
            self.lastblockhash = block.hash
            self.lastblocktime += 1
            self.lastblockheight += 1
        else:
            assert_equal(node.getbestblockhash(), self.lastblockhash)


if __name__ == '__main__':
    NULLDUMMYTest().main()
