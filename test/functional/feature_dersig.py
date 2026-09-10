#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test BIP66 (DER SIG) enforcement.

Raptoreum has no soft-fork activation for BIP66: consensus.BIP66Enabled is a
bool that is true on every network (chainparams.cpp), so SCRIPT_VERIFY_DERSIG
is in the consensus flags from genesis. The block-version ratchet that went
with the activation is commented out in ContextualCheckBlockHeader, so a
low-version block is not rejected either. What is left to test is enforcement:
a non-DER signature must be refused by both the mempool and the block
validator.
"""

from io import BytesIO

from test_framework.blocktools import create_block, create_coinbase
from test_framework.mininode import (
    CTransaction,
    P2PInterface,
    mininode_lock,
    msg_block,
    network_thread_start,
)
from test_framework.script import CScript
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    bytes_to_hex_str,
    hex_str_to_bytes,
    wait_until,
)

# Enough blocks for the first coinbase to mature (COINBASE_MATURITY = 100).
MATURITY_BLOCKS = 101

DERSIG_ERROR = "non-mandatory-script-verify-flag (Non-canonical DER signature) (code 64)"

REJECT_INVALID = 16
REJECT_NONSTANDARD = 64


# A canonical signature consists of:
# <30> <total len> <02> <len R> <R> <02> <len S> <S> <hashtype>
def unDERify(tx):
    """
    Make the signature in vin 0 of a tx non-DER-compliant,
    by adding padding after the S-value.
    """
    scriptSig = CScript(tx.vin[0].scriptSig)
    newscript = []
    for i in scriptSig:
        if (len(newscript) == 0):
            newscript.append(i[0:-1] + b'\0' + i[-1:])
        else:
            newscript.append(i)
    tx.vin[0].scriptSig = CScript(newscript)


def create_transaction(node, coinbase, to_address, amount):
    from_txid = node.getblock(coinbase)['tx'][0]
    inputs = [{"txid": from_txid, "vout": 0}]
    outputs = {to_address: amount}
    rawtx = node.createrawtransaction(inputs, outputs)
    signresult = node.signrawtransactionwithwallet(rawtx)
    tx = CTransaction()
    tx.deserialize(BytesIO(hex_str_to_bytes(signresult['hex'])))
    return tx


class BIP66Test(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [['-whitelist=127.0.0.1']]
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0]
        node.add_p2p_connection(P2PInterface())

        network_thread_start()
        node.p2p.wait_for_verack()

        self.log.info("Mining %d blocks", MATURITY_BLOCKS)
        self.coinbase_blocks = node.generate(MATURITY_BLOCKS)
        self.nodeaddress = node.getnewaddress()

        tip = node.getbestblockhash()
        block_time = node.getblockheader(tip)['mediantime'] + 1
        height = node.getblockcount() + 1

        self.log.info("Test that a transaction with a non-DER signature is rejected from the mempool")
        spendtx = create_transaction(node, self.coinbase_blocks[0], self.nodeaddress, 1.0)
        unDERify(spendtx)
        spendtx.rehash()
        assert_raises_rpc_error(-26, DERSIG_ERROR, node.sendrawtransaction,
                                bytes_to_hex_str(spendtx.serialize()), 0)

        self.log.info("Test that a block containing it is rejected too")
        block = create_block(int(tip, 16), create_coinbase(height), block_time, node=node)
        block.vtx.append(spendtx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()

        node.p2p.send_and_ping(msg_block(block))
        assert_equal(node.getbestblockhash(), tip)

        wait_until(lambda: "reject" in node.p2p.last_message.keys(), lock=mininode_lock)
        with mininode_lock:
            assert node.p2p.last_message["reject"].code in [REJECT_INVALID, REJECT_NONSTANDARD]
            assert_equal(node.p2p.last_message["reject"].data, block.sha256)
            del node.p2p.last_message["reject"]

        self.log.info("Test that the same block with a DER-compliant signature is accepted")
        spendtx = create_transaction(node, self.coinbase_blocks[0], self.nodeaddress, 1.0)
        spendtx.rehash()

        block = create_block(int(tip, 16), create_coinbase(height), block_time, node=node)
        block.vtx.append(spendtx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()

        node.p2p.send_and_ping(msg_block(block))
        assert_equal(int(node.getbestblockhash(), 16), block.sha256)


if __name__ == '__main__':
    BIP66Test().main()
