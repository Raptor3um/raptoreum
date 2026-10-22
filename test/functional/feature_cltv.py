#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test BIP65 (CHECKLOCKTIMEVERIFY) enforcement.

Raptoreum has no soft-fork activation for BIP65: consensus.BIP65Enabled is a
bool that is true on every network (chainparams.cpp), so SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY
is in the consensus flags from genesis. The block-version ratchet that went
with the activation is commented out in ContextualCheckBlockHeader, so a
low-version block is not rejected either. What is left to test is enforcement:
a transaction failing CLTV must be refused by both the mempool and the block
validator, and its fixed-up form must be accepted.
"""

from io import BytesIO

from test_framework.blocktools import create_block, create_coinbase
from test_framework.mininode import (
    CTransaction,
    P2PInterface,
    ToHex,
    mininode_lock,
    msg_block,
    network_thread_start,
)
from test_framework.script import (
    CScript,
    CScriptNum,
    OP_1NEGATE,
    OP_CHECKLOCKTIMEVERIFY,
    OP_DROP,
)
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

REJECT_INVALID = 16
REJECT_NONSTANDARD = 64


def cltv_invalidate(tx):
    '''Modify the signature in vin 0 of the tx to fail CLTV

    Prepends -1 CLTV DROP in the scriptSig itself.

    TODO: test more ways that transactions using CLTV could be invalid (eg
    locktime requirements fail, sequence time requirements fail, etc).
    '''
    tx.vin[0].scriptSig = CScript([OP_1NEGATE, OP_CHECKLOCKTIMEVERIFY, OP_DROP] +
                                  list(CScript(tx.vin[0].scriptSig)))


def cltv_validate(node, tx, height):
    '''Modify the signature in vin 0 of the tx to pass CLTV
    Prepends <height> CLTV DROP in the scriptSig, and sets
    the locktime to height'''
    tx.vin[0].nSequence = 0
    tx.nLockTime = height

    # Need to re-sign, since nSequence and nLockTime changed
    signed_result = node.signrawtransactionwithwallet(ToHex(tx))
    new_tx = CTransaction()
    new_tx.deserialize(BytesIO(hex_str_to_bytes(signed_result['hex'])))

    new_tx.vin[0].scriptSig = CScript([CScriptNum(height), OP_CHECKLOCKTIMEVERIFY, OP_DROP] +
                                      list(CScript(new_tx.vin[0].scriptSig)))
    return new_tx


def create_transaction(node, coinbase, to_address, amount):
    from_txid = node.getblock(coinbase)['tx'][0]
    inputs = [{"txid": from_txid, "vout": 0}]
    outputs = {to_address: amount}
    rawtx = node.createrawtransaction(inputs, outputs)
    signresult = node.signrawtransactionwithwallet(rawtx)
    tx = CTransaction()
    tx.deserialize(BytesIO(hex_str_to_bytes(signresult['hex'])))
    return tx


class BIP65Test(BitcoinTestFramework):
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

        self.log.info("Test that an invalid-according-to-CLTV transaction is rejected from the mempool")
        spendtx = create_transaction(node, self.coinbase_blocks[0], self.nodeaddress, 1.0)
        cltv_invalidate(spendtx)
        spendtx.rehash()
        assert_raises_rpc_error(-26, 'non-mandatory-script-verify-flag (Negative locktime) (code 64)',
                                node.sendrawtransaction, bytes_to_hex_str(spendtx.serialize()), 0)

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
            if node.p2p.last_message["reject"].code == REJECT_INVALID:
                # Generic rejection when a block is invalid
                assert_equal(node.p2p.last_message["reject"].reason, b'block-validation-failed')
            else:
                assert b'Negative locktime' in node.p2p.last_message["reject"].reason
            del node.p2p.last_message["reject"]

        self.log.info("Test that a block with a valid-according-to-CLTV transaction is accepted")
        spendtx = cltv_validate(node, spendtx, height - 1)
        spendtx.rehash()

        block = create_block(int(tip, 16), create_coinbase(height), block_time, node=node)
        block.vtx.append(spendtx)
        block.hashMerkleRoot = block.calc_merkle_root()
        block.solve()

        node.p2p.send_and_ping(msg_block(block))
        assert_equal(int(node.getbestblockhash(), 16), block.sha256)


if __name__ == '__main__':
    BIP65Test().main()
