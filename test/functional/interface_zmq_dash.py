#!/usr/bin/env python3
# Copyright (c) 2018-2021 The Dash Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the dash specific ZMQ notification interfaces."""

import configparser
from enum import Enum
import io
import random
import struct
import time

# Upstream guarded this with `try: import zmq / finally: pass`, and finally does
# not swallow the ImportError, so the module failed to load instead of skipping.
# It has to bind at module scope: run_test is not the only method that uses it.
try:
    import zmq
except ImportError:
    zmq = None

from test_framework.test_framework import (
     LLMQ_TEST_TYPE, RaptoreumTestFramework, skip_if_no_bitcoind_zmq, skip_if_no_py3_zmq)
from test_framework.mininode import P2PInterface, network_thread_start
from test_framework.util import assert_equal, assert_raises_rpc_error, bytes_to_hex_str
from test_framework.messages import (
    CBlock,
    CInv,
    CRecoveredSig,
    CTransaction,
    FromHex,
    hash256,
    msg_clsig,
    msg_inv,
    msg_isdlock,
    msg_tx,
    ser_string,
    uint256_from_str,
    uint256_to_string
)


class ZMQPublisher(Enum):
    hash_chain_lock = "hashchainlock"
    hash_tx_lock = "hashtxlock"
    hash_instantsend_doublespend = "hashinstantsenddoublespend"
    hash_recovered_sig = "hashrecoveredsig"
    raw_chain_lock = "rawchainlock"
    raw_chain_lock_sig = "rawchainlocksig"
    raw_tx_lock = "rawtxlock"
    raw_tx_lock_sig = "rawtxlocksig"
    raw_instantsend_doublespend = "rawinstantsenddoublespend"
    raw_recovered_sig = "rawrecoveredsig"


class TestP2PConn(P2PInterface):
    def __init__(self):
        super().__init__()
        self.islocks = {}
        self.txes = {}

    def send_islock(self, islock):
        hash = uint256_from_str(hash256(islock.serialize()))
        self.islocks[hash] = islock

        inv = msg_inv([CInv(30, hash)])
        self.send_message(inv)

    def send_tx(self, tx):
        hash = uint256_from_str(hash256(tx.serialize()))
        self.txes[hash] = tx

        inv = msg_inv([CInv(30, hash)])
        self.send_message(inv)

    def on_getdata(self, message):
        for inv in message.inv:
            if inv.hash in self.islocks:
                self.send_message(self.islocks[inv.hash])
            if inv.hash in self.txes:
                self.send_message(self.txes[inv.hash])


class DashZMQTest (RaptoreumTestFramework):
    def set_test_params(self):
        # That's where the zmq publisher will listen for subscriber
        self.address = "tcp://127.0.0.1:28333"
        # node0 creates all available ZMQ publisher
        node0_extra_args = ["-zmqpub%s=%s" % (pub.value, self.address) for pub in ZMQPublisher]
        node0_extra_args.append("-whitelist=127.0.0.1")
        node0_extra_args.append("-watchquorums")  # have to watch quorums to receive recsigs and trigger zmq

        self.set_raptoreum_test_params(4, 3, fast_dip3_enforcement=True, extra_args=[node0_extra_args, [], [], []])

    def run_test(self):
        # Check that raptoreumd has been built with ZMQ enabled.
        config = configparser.ConfigParser()
        config.read_file(open(self.options.configfile))

        skip_if_no_py3_zmq()
        skip_if_no_bitcoind_zmq(self)

        try:
            # Setup the ZMQ subscriber socket
            self.zmq_context = zmq.Context()
            self.socket = self.zmq_context.socket(zmq.SUB)
            self.socket.connect(self.address)
            # Initialize the network
            self.activate_dip8()
            self.nodes[0].spork("SPORK_17_QUORUM_DKG_ENABLED", 0)
            self.wait_for_sporks_same()
            # Create an LLMQ for testing
            self.quorum_type = LLMQ_TEST_TYPE
            self.quorum_hash = self.mine_quorum()
            self.sync_blocks()
            self.wait_for_chainlocked_block_all_nodes(self.nodes[0].getbestblockhash())
            # Wait a moment to avoid subscribing to recovered sig in the test before the one from the chainlock
            # has been sent which leads to test failure.
            time.sleep(1)
            # Test all dash related ZMQ publisher
            self.test_recovered_signature_publishers()
            self.test_chainlock_publishers()
            self.test_instantsend_publishers()
            # Upstream also tested four governance publishers here. Governance
            # never activates on any Raptoreum network, and two of the four were
            # never registered as arguments, so the node refused to start.
        finally:
            # Destroy the ZMQ context.
            self.log.debug("Destroying ZMQ context")
            self.zmq_context.destroy(linger=None)

    def subscribe(self, publishers):
        # Subscribe to a list of ZMQPublishers
        for pub in publishers:
            self.socket.subscribe(pub.value)

    def unsubscribe(self, publishers):
        # Unsubscribe from a list of ZMQPublishers
        for pub in publishers:
            self.socket.unsubscribe(pub.value)

    def receive(self, publisher, flags=0):
        # Receive a ZMQ message and validate it's sent from the correct ZMQPublisher
        topic, body, seq = self.socket.recv_multipart(flags)
        # Topic should match the publisher value
        assert_equal(topic.decode(), publisher.value)
        return io.BytesIO(body)

    def test_recovered_signature_publishers(self):

        def validate_recovered_sig(request_id, msg_hash):
            # Make sure the recovered sig exists by RPC
            rpc_recovered_sig = self.get_recovered_sig(request_id, msg_hash)
            # Validate hashrecoveredsig
            zmq_recovered_sig_hash = bytes_to_hex_str(self.receive(ZMQPublisher.hash_recovered_sig).read(32))
            assert_equal(zmq_recovered_sig_hash, msg_hash)
            # Validate rawrecoveredsig
            zmq_recovered_sig_raw = CRecoveredSig()
            zmq_recovered_sig_raw.deserialize(self.receive(ZMQPublisher.raw_recovered_sig))
            assert_equal(zmq_recovered_sig_raw.llmqType, rpc_recovered_sig['llmqType'])
            assert_equal(uint256_to_string(zmq_recovered_sig_raw.quorumHash), rpc_recovered_sig['quorumHash'])
            assert_equal(uint256_to_string(zmq_recovered_sig_raw.id), rpc_recovered_sig['id'])
            assert_equal(uint256_to_string(zmq_recovered_sig_raw.msgHash), rpc_recovered_sig['msgHash'])
            assert_equal(bytes_to_hex_str(zmq_recovered_sig_raw.sig), rpc_recovered_sig['sig'])

        recovered_sig_publishers = [
            ZMQPublisher.hash_recovered_sig,
            ZMQPublisher.raw_recovered_sig
        ]
        self.log.info("Testing %d recovered signature publishers" % len(recovered_sig_publishers))
        # Subscribe to recovered signature messages
        self.subscribe(recovered_sig_publishers)
        # Generate a ChainLock and make sure this leads to valid recovered sig ZMQ messages
        rpc_last_block_hash = self.nodes[0].generate(1)[0]
        self.wait_for_chainlocked_block_all_nodes(rpc_last_block_hash)
        height = self.nodes[0].getblockcount()
        rpc_request_id = hash256(ser_string(b"clsig") + struct.pack("<I", height))[::-1].hex()
        validate_recovered_sig(rpc_request_id, rpc_last_block_hash)
        # Sign an arbitrary and make sure this leads to valid recovered sig ZMQ messages
        sign_id = uint256_to_string(random.getrandbits(256))
        sign_msg_hash = uint256_to_string(random.getrandbits(256))
        for mn in self.get_quorum_smartnodes(self.quorum_hash):
            mn.node.quorum("sign", self.quorum_type, sign_id, sign_msg_hash)
        validate_recovered_sig(sign_id, sign_msg_hash)
        # Unsubscribe from recovered signature messages
        self.unsubscribe(recovered_sig_publishers)

    def test_chainlock_publishers(self):
        chain_lock_publishers = [
            ZMQPublisher.hash_chain_lock,
            ZMQPublisher.raw_chain_lock,
            ZMQPublisher.raw_chain_lock_sig
        ]
        self.log.info("Testing %d ChainLock publishers" % len(chain_lock_publishers))
        # Subscribe to ChainLock messages
        self.subscribe(chain_lock_publishers)
        # Generate ChainLock
        generated_hash = self.nodes[0].generate(1)[0]
        self.wait_for_chainlocked_block_all_nodes(generated_hash)
        rpc_best_chain_lock = self.nodes[0].getbestchainlock()
        rpc_best_chain_lock_hash = rpc_best_chain_lock["blockhash"]
        rpc_best_chain_lock_sig = rpc_best_chain_lock["signature"]
        assert_equal(generated_hash, rpc_best_chain_lock_hash)
        rpc_chain_locked_block = self.nodes[0].getblock(rpc_best_chain_lock_hash)
        rpc_chain_lock_height = rpc_chain_locked_block["height"]
        rpc_chain_lock_hash = rpc_chain_locked_block["hash"]
        assert_equal(generated_hash, rpc_chain_lock_hash)
        # Validate hashchainlock
        zmq_chain_lock_hash = bytes_to_hex_str(self.receive(ZMQPublisher.hash_chain_lock).read(32))
        assert_equal(zmq_chain_lock_hash, rpc_best_chain_lock_hash)
        # Validate rawchainlock
        zmq_chain_locked_block = CBlock()
        zmq_chain_locked_block.deserialize(self.receive(ZMQPublisher.raw_chain_lock))
        assert(zmq_chain_locked_block.is_valid())
        assert_equal(zmq_chain_locked_block.hash, rpc_chain_lock_hash)
        # Validate rawchainlocksig
        zmq_chain_lock_sig_stream = self.receive(ZMQPublisher.raw_chain_lock_sig)
        zmq_chain_locked_block = CBlock()
        zmq_chain_locked_block.deserialize(zmq_chain_lock_sig_stream)
        assert(zmq_chain_locked_block.is_valid())
        zmq_chain_lock = msg_clsig()
        zmq_chain_lock.deserialize(zmq_chain_lock_sig_stream)
        assert_equal(zmq_chain_lock.height, rpc_chain_lock_height)
        assert_equal(uint256_to_string(zmq_chain_lock.blockHash), rpc_chain_lock_hash)
        assert_equal(zmq_chain_locked_block.hash, rpc_chain_lock_hash)
        assert_equal(bytes_to_hex_str(zmq_chain_lock.sig), rpc_best_chain_lock_sig)
        # Unsubscribe from ChainLock messages
        self.unsubscribe(chain_lock_publishers)

    def test_instantsend_publishers(self):
        instantsend_publishers = [
            ZMQPublisher.hash_tx_lock,
            ZMQPublisher.raw_tx_lock,
            ZMQPublisher.raw_tx_lock_sig,
            ZMQPublisher.hash_instantsend_doublespend,
            ZMQPublisher.raw_instantsend_doublespend
        ]
        self.log.info("Testing %d InstantSend publishers" % len(instantsend_publishers))
        # Subscribe to InstantSend messages
        self.subscribe(instantsend_publishers)
        # Initialize test node
        self.test_node = self.nodes[0].add_p2p_connection(TestP2PConn())
        network_thread_start()
        self.nodes[0].p2p.wait_for_verack()
        # Make sure all nodes agree
        self.wait_for_chainlocked_block_all_nodes(self.nodes[0].getbestblockhash())
        # Create two raw TXs, they will conflict with each other
        rpc_raw_tx_1 = self.create_raw_tx(self.nodes[0], self.nodes[0], 1, 1, 100)
        rpc_raw_tx_2 = self.create_raw_tx(self.nodes[0], self.nodes[0], 1, 1, 100)
        # Send the first transaction and wait for the InstantLock
        rpc_raw_tx_1_hash = self.nodes[0].sendrawtransaction(rpc_raw_tx_1['hex'])
        self.wait_for_instantlock(rpc_raw_tx_1_hash, self.nodes[0])
        # Validate hashtxlock
        zmq_tx_lock_hash = bytes_to_hex_str(self.receive(ZMQPublisher.hash_tx_lock).read(32))
        assert_equal(zmq_tx_lock_hash, rpc_raw_tx_1['txid'])
        # Validate rawtxlock
        zmq_tx_lock_raw = CTransaction()
        zmq_tx_lock_raw.deserialize(self.receive(ZMQPublisher.raw_tx_lock))
        assert(zmq_tx_lock_raw.is_valid())
        assert_equal(zmq_tx_lock_raw.hash, rpc_raw_tx_1['txid'])
        # Validate rawtxlocksig
        zmq_tx_lock_sig_stream = self.receive(ZMQPublisher.raw_tx_lock_sig)
        zmq_tx_lock_tx = CTransaction()
        zmq_tx_lock_tx.deserialize(zmq_tx_lock_sig_stream)
        assert(zmq_tx_lock_tx.is_valid())
        assert_equal(zmq_tx_lock_tx.hash, rpc_raw_tx_1['txid'])
        zmq_tx_lock = msg_isdlock()
        zmq_tx_lock.deserialize(zmq_tx_lock_sig_stream)
        assert_equal(uint256_to_string(zmq_tx_lock.txid), rpc_raw_tx_1['txid'])
        # Try to send the second transaction. This must throw an RPC error because it conflicts with rpc_raw_tx_1
        # which already got the InstantSend lock.
        assert_raises_rpc_error(-26, "tx-txlock-conflict", self.nodes[0].sendrawtransaction, rpc_raw_tx_2['hex'])
        # Validate hashinstantsenddoublespend
        zmq_double_spend_hash2 = bytes_to_hex_str(self.receive(ZMQPublisher.hash_instantsend_doublespend).read(32))
        zmq_double_spend_hash1 = bytes_to_hex_str(self.receive(ZMQPublisher.hash_instantsend_doublespend).read(32))
        assert_equal(zmq_double_spend_hash2, rpc_raw_tx_2['txid'])
        assert_equal(zmq_double_spend_hash1, rpc_raw_tx_1['txid'])
        # Validate rawinstantsenddoublespend
        zmq_double_spend_tx_2 = CTransaction()
        zmq_double_spend_tx_2.deserialize(self.receive(ZMQPublisher.raw_instantsend_doublespend))
        assert (zmq_double_spend_tx_2.is_valid())
        assert_equal(zmq_double_spend_tx_2.hash, rpc_raw_tx_2['txid'])
        zmq_double_spend_tx_1 = CTransaction()
        zmq_double_spend_tx_1.deserialize(self.receive(ZMQPublisher.raw_instantsend_doublespend))
        assert(zmq_double_spend_tx_1.is_valid())
        assert_equal(zmq_double_spend_tx_1.hash, rpc_raw_tx_1['txid'])
        # No islock notifications when tx is not received yet
        self.nodes[0].generate(1)
        rpc_raw_tx_3 = self.create_raw_tx(self.nodes[0], self.nodes[0], 1, 1, 100)
        islock = self.create_islock(rpc_raw_tx_3['hex'])
        self.test_node.send_islock(islock)
        # Validate NO hashtxlock
        time.sleep(1)
        try:
            self.receive(ZMQPublisher.hash_tx_lock, zmq.NOBLOCK)
            assert(False)
        except zmq.ZMQError:
            # this is expected
            pass
        # Now send the tx itself
        self.test_node.send_tx(FromHex(msg_tx(), rpc_raw_tx_3['hex']))
        self.wait_for_instantlock(rpc_raw_tx_3['txid'], self.nodes[0])
        # Validate hashtxlock
        zmq_tx_lock_hash = bytes_to_hex_str(self.receive(ZMQPublisher.hash_tx_lock).read(32))
        assert_equal(zmq_tx_lock_hash, rpc_raw_tx_3['txid'])
        # Drop test node connection
        self.nodes[0].disconnect_p2ps()
        # Unsubscribe from InstantSend messages
        self.unsubscribe(instantsend_publishers)


if __name__ == '__main__':
    DashZMQTest().main()
