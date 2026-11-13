#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test logic for skipping signature validation on old blocks.

Test logic for skipping signature validation on blocks which we've assumed
valid (https://github.com/bitcoin/bitcoin/pull/9484)

We build a chain that includes and invalid signature for one of the
transactions:

    0:        genesis block
    1:        block 1 with coinbase transaction output.
    2-101:    bury that block with 100 blocks so the coinbase transaction
              output can be spent
    102:      a block containing a transaction spending the coinbase
              transaction output. The transaction has an invalid signature.
    103-2202: bury the bad block with just over two weeks' worth of blocks
              (2100 blocks)

Start three nodes:

    - node0 has no -assumevalid parameter. Try to sync to block 2202. It will
      reject block 102 and only sync as far as block 101
    - node1 has -assumevalid set to the hash of block 102. Try to sync to
      block 2202. node1 will sync all the way to block 2202.
    - node2 has -assumevalid set to the hash of block 102. Try to sync to
      block 200. node2 will reject block 102 since it's assumed valid, but it
      isn't buried by at least two weeks' work.
"""

from test_framework.blocktools import (create_block, create_coinbase)
from test_framework.key import ECKey
from test_framework.messages import (
    CBlockHeader,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxOut,
    msg_block,
    msg_headers
)
from test_framework.mininode import P2PInterface, network_thread_start, network_thread_join
from test_framework.script import (CScript, OP_TRUE)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (assert_equal, set_node_times, wait_until)

class BaseNode(P2PInterface):
    def send_header_for_blocks(self, new_blocks):
        headers_message = msg_headers()
        headers_message.headers = [CBlockHeader(b) for b in new_blocks]
        self.send_message(headers_message)

class AssumeValidTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = ["-checkblockindex=0"]

    def setup_network(self):
        self.add_nodes(3)
        # Start node0. We don't start the other nodes yet since
        # we need to pre-mine a block with an invalid transaction
        # signature so we can pass in the block hash as assumevalid.
        self.start_node(0, extra_args=self.extra_args)

    def send_blocks_until_disconnected(self, p2p_conn):
        """Keep sending blocks to the node until we're disconnected."""
        for i in range(len(self.blocks)):
            if not p2p_conn.is_connected:
                break
            try:
                p2p_conn.send_message(msg_block(self.blocks[i]))
            except IOError:
                assert not p2p_conn.is_connected
                break

    def assert_blockchain_height(self, node, height):
        """Wait for the node to reach and settle at the expected height.

        Dash's own self.wait_until() call here also omits an explicit timeout,
        but that doesn't mean "wait forever" in either codebase: Dash's
        self.wait_until defaults to timeout=60, and RTM's standalone wait_until()
        (used here; RTM has no self.wait_until) silently clamps to the same 60s
        when both attempts and timeout are left at their infinite defaults. 60s
        already failed once on CI under load; so did an explicit 90s before that.
        300s matches the scale already proven necessary for sync_with_ping below.
        """
        wait_until(lambda: node.getblockcount() >= height, timeout=300)
        assert_equal(node.getblockcount(), height)

    def run_test(self):

        # Connect to node0
        p2p0 = self.nodes[0].add_p2p_connection(BaseNode())

        network_thread_start()
        self.nodes[0].p2p.wait_for_verack()

        # Build the blockchain
        self.tip = int(self.nodes[0].getbestblockhash(), 16)
        self.block_time = self.nodes[0].getblock(self.nodes[0].getbestblockhash())['time'] + 1
        # Pace block_time at >nPowTargetSpacing (120s on regtest) so DGW's 60-block
        # average (DGWBlocksAvg) keeps computing powLimit once nMinimumDifficultyBlocks
        # (2000) stops exempting blocks from a real difficulty check. Exact spacing
        # undershoots the target slightly (60 blocks = 59 gaps), so use a margin.
        BLOCK_TIME_STEP = 150

        self.blocks = []
        # create_quorum_commitments() needs this for session-start blocks we've
        # built but not yet sent to the node -- it can't look them up itself.
        self.block_hashes = {}

        # Get a pubkey for the coinbase TXO
        coinbase_key = ECKey()
        coinbase_key.generate()
        coinbase_pubkey = coinbase_key.get_pubkey().get_bytes()

        # Create the first block with a coinbase output to our key
        height = 1
        block = create_block(self.tip, create_coinbase(height, coinbase_pubkey), self.block_time, node=self.nodes[0], block_hashes=self.block_hashes)
        self.blocks.append(block)
        self.block_time += BLOCK_TIME_STEP
        block.solve()
        # Save the coinbase for later
        self.block1 = block
        self.tip = block.sha256
        self.block_hashes[height] = block.sha256
        height += 1

        # Bury the block 100 deep so the coinbase output is spendable
        for i in range(100):
            block = create_block(self.tip, create_coinbase(height), self.block_time, node=self.nodes[0], block_hashes=self.block_hashes)
            block.solve()
            self.blocks.append(block)
            self.tip = block.sha256
            self.block_hashes[height] = block.sha256
            self.block_time += BLOCK_TIME_STEP
            height += 1

        # Create a transaction spending the coinbase output with an invalid (null) signature.
        # Leaves 1 RTM as an implicit fee, out of Raptoreum's 4 RTM block-1 subsidy (the
        # upstream "49" was Bitcoin's 50 BTC subsidy minus 1, never adjusted for this chain).
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(self.block1.vtx[0].sha256, 0), scriptSig=b""))
        tx.vout.append(CTxOut(3 * 100000000, CScript([OP_TRUE])))
        tx.calc_sha256()

        block102 = create_block(self.tip, create_coinbase(height), self.block_time, node=self.nodes[0], block_hashes=self.block_hashes)
        self.block_time += BLOCK_TIME_STEP
        block102.vtx.extend([tx])
        block102.hashMerkleRoot = block102.calc_merkle_root()
        block102.rehash()
        block102.solve()
        self.blocks.append(block102)
        self.tip = block102.sha256
        self.block_hashes[height] = block102.sha256
        self.block_time += BLOCK_TIME_STEP
        height += 1

        # -assumevalid only skips script checks once GetBlockProofEquivalentTime(tip, block)
        # exceeds two weeks; for a uniform-difficulty chain that reduces to
        # (blocks between block and tip) * nPowTargetSpacing, independent of the
        # difficulty itself. Bitcoin's original 2100 assumed its own 600s spacing
        # (2 weeks = 2016 blocks, +margin). Raptoreum's regtest nPowTargetSpacing
        # is 120s, so it needs 5x as many blocks (>10080), not 4x (8400) -- the old
        # comment/constant here was never recalculated and was short of the mark.
        for i in range(10500):
            block = create_block(self.tip, create_coinbase(height), self.block_time, node=self.nodes[0], block_hashes=self.block_hashes)
            block.nVersion = 4
            block.solve()
            self.blocks.append(block)
            self.tip = block.sha256
            self.block_hashes[height] = block.sha256
            self.block_time += BLOCK_TIME_STEP
            height += 1

        # We're adding new connections so terminate the network thread
        self.nodes[0].disconnect_p2ps()
        network_thread_join()

        # Start node1 and node2 with assumevalid so they accept a block with a bad signature.
        self.start_node(1, extra_args=self.extra_args + ["-assumevalid=" + hex(block102.sha256)])
        self.start_node(2, extra_args=self.extra_args + ["-assumevalid=" + hex(block102.sha256)])

        p2p0 = self.nodes[0].add_p2p_connection(BaseNode())
        p2p1 = self.nodes[1].add_p2p_connection(BaseNode())
        p2p2 = self.nodes[2].add_p2p_connection(BaseNode())

        network_thread_start()

        p2p0.wait_for_verack()
        p2p1.wait_for_verack()
        p2p2.wait_for_verack()

        # Make sure nodes actually accept the many headers
        self.mocktime = self.block_time
        set_node_times(self.nodes, self.mocktime)

        # send header lists to all three nodes.
        # node0 does not need to receive all headers
        # node1 must receive all headers as otherwise assumevalid is ignored in ConnectBlock
        # node2 should NOT receive all headers to force skipping of the assumevalid check in ConnectBlock
        # Unsolicited headers messages over MAX_HEADERS_RESULTS (2000) get the whole
        # message discarded (net_processing.cpp), so node1 needs its own chunking
        # that stays correct regardless of how many blocks are in the chain.
        p2p0.send_header_for_blocks(self.blocks[0:2000])
        for lo in range(0, len(self.blocks), 2000):
            p2p1.send_header_for_blocks(self.blocks[lo:lo + 2000])
        p2p2.send_header_for_blocks(self.blocks[0:200])

        # Send blocks to node0. Block 102 will be rejected.
        self.send_blocks_until_disconnected(p2p0)
        self.assert_blockchain_height(self.nodes[0], 101)

        # Send 200 blocks to node1. All blocks, including block 102, will be accepted.
        for i in range(200):
            p2p1.send_message(msg_block(self.blocks[i]))
        # Syncing so many blocks can take a while on slow systems. Give it plenty of time to sync.
        p2p1.sync_with_ping(960)
        assert_equal(self.nodes[1].getblock(self.nodes[1].getbestblockhash())['height'], 200)

        # Send blocks to node2. Block 102 will be rejected.
        self.send_blocks_until_disconnected(p2p2)
        self.assert_blockchain_height(self.nodes[2], 101)

if __name__ == '__main__':
    AssumeValidTest().main()
