#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test mining RPCs

- getmininginfo
- getblocktemplate proposal mode
- submitblock"""

import copy
from decimal import Decimal

from test_framework.blocktools import create_coinbase, create_quorum_commitments
from test_framework.messages import (
    CBlock,
    CBlockHeader,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxOut,
)
from test_framework.script import CScript, CScriptNum, OP_TRUE
from test_framework.mininode import (
    P2PDataStore,
    network_thread_start,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    CACHE_HEIGHT,
    assert_equal,
    assert_raises_rpc_error,
    bytes_to_hex_str as b2x,
)

def assert_submitblock(node, block, result_str_1='invalid', result_str_2='duplicate-invalid'):
    """Submit a block twice; the second time it must be remembered as invalid.

    Upstream expects the reject reason on the first submit. Here it is the
    generic "invalid": the header is already known by then, so submitblock
    takes the !new_block path in rpc/mining.cpp rather than reporting state.
    """
    block.solve()
    assert_equal(result_str_1, node.submitblock(hexdata=b2x(block.serialize())))
    assert_equal(result_str_2, node.submitblock(hexdata=b2x(block.serialize())))


def assert_template(node, block, expect, rehash=True):
    if rehash:
        block.hashMerkleRoot = block.calc_merkle_root()
    rsp = node.getblocktemplate({'data': b2x(block.serialize()), 'mode': 'proposal'})
    assert_equal(rsp, expect)

class MiningTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = False

    def run_test(self):
        node = self.nodes[0]

        self.log.info('getmininginfo')
        mining_info = node.getmininginfo()
        assert_equal(mining_info['blocks'], CACHE_HEIGHT)
        assert_equal(mining_info['chain'], self.chain)
        assert_equal(mining_info['currentblocksize'], 0)
        assert_equal(mining_info['currentblocktx'], 0)
        assert_equal(mining_info['difficulty'], Decimal('4.656542373906925E-10'))
        assert_equal(mining_info['networkhashps'], Decimal('0.01282051282051282'))
        assert_equal(mining_info['pooledtx'], 0)

        # Mine a block to leave initial block download
        node.generate(1)
        tmpl = node.getblocktemplate()
        self.log.info("getblocktemplate: Test capability advertised")
        assert 'proposal' in tmpl['capabilities']
        assert 'coinbasetxn' not in tmpl

        # tmpl["height"] is already the height of the block being built. The
        # extra +1 inherited from upstream is invisible on a chain that does
        # not check it, but Raptoreum's CbTx carries the height and rejects a
        # mismatch with "bad-cbtx-height".
        coinbase_tx = create_coinbase(height=int(tmpl["height"]))
        # sequence numbers must not be max for nLockTime to have effect
        coinbase_tx.vin[0].nSequence = 2 ** 32 - 2
        coinbase_tx.rehash()

        # round-trip the encoded bip34 block height commitment
        assert_equal(CScriptNum.decode(coinbase_tx.vin[0].scriptSig), int(tmpl["height"]))
        # round-trip negative and multi-byte CScriptNums to catch python regression
        assert_equal(CScriptNum.decode(CScriptNum.encode(CScriptNum(1500))), 1500)
        assert_equal(CScriptNum.decode(CScriptNum.encode(CScriptNum(-1500))), -1500)
        assert_equal(CScriptNum.decode(CScriptNum.encode(CScriptNum(-1))), -1)

        block = CBlock()
        block.nVersion = tmpl["version"]
        block.hashPrevBlock = int(tmpl["previousblockhash"], 16)
        block.nTime = tmpl["curtime"]
        block.nBits = int(tmpl["bits"], 16)
        block.nNonce = 0
        # a block in a DKG mining window needs its (null) commitments, as the
        # node's own miner would have added them
        block.vtx = [coinbase_tx] + create_quorum_commitments(node, int(tmpl["height"]))

        self.log.info("getblocktemplate: Test valid block")
        assert_template(node, block, None)

        self.log.info("submitblock: Test block decode failure")
        assert_raises_rpc_error(-22, "Block decode failed", node.submitblock, b2x(block.serialize()[:-15]))

        self.log.info("getblocktemplate: Test bad input hash for coinbase transaction")
        bad_block = copy.deepcopy(block)
        bad_block.vtx[0].vin[0].prevout.hash += 1
        bad_block.vtx[0].rehash()
        assert_template(node, bad_block, 'bad-cb-missing')

        self.log.info("submitblock: Test invalid coinbase transaction")
        assert_raises_rpc_error(-22, "Block does not start with a coinbase", node.submitblock, b2x(bad_block.serialize()))

        self.log.info("getblocktemplate: Test truncated final transaction")
        assert_raises_rpc_error(-22, "Block decode failed", node.getblocktemplate, {'data': b2x(block.serialize()[:-1]), 'mode': 'proposal'})

        self.log.info("getblocktemplate: Test duplicate coinbase")
        bad_block = copy.deepcopy(block)
        bad_block.vtx.append(bad_block.vtx[0])
        assert_template(node, bad_block, 'bad-txns-duplicate')
        assert_submitblock(node, bad_block, 'invalid', 'invalid')

        self.log.info("getblocktemplate: Test invalid transaction")
        bad_block = copy.deepcopy(block)
        # Upstream copies the coinbase and rewrites its input. Raptoreum's
        # coinbase is a CbTx, so a second one is rejected as bad-cbtx-invalid
        # before the missing input is ever looked at -- which tests something
        # else entirely. Append a plain transaction spending nothing instead.
        bad_tx = CTransaction()
        bad_tx.vin.append(CTxIn(COutPoint(255, 0)))
        bad_tx.vout.append(CTxOut(0, CScript([OP_TRUE])))
        bad_tx.calc_sha256()
        bad_block.vtx.append(bad_tx)
        assert_template(node, bad_block, 'bad-txns-inputs-missingorspent')

        self.log.info("getblocktemplate: Test nonfinal transaction")
        bad_block = copy.deepcopy(block)
        bad_block.vtx[0].nLockTime = 2 ** 32 - 1
        bad_block.vtx[0].rehash()
        assert_template(node, bad_block, 'bad-txns-nonfinal')
        assert_submitblock(node, bad_block)

        self.log.info("getblocktemplate: Test bad tx count")
        # The tx count is immediately after the block header
        TX_COUNT_OFFSET = 80
        bad_block_sn = bytearray(block.serialize())
        assert_equal(bad_block_sn[TX_COUNT_OFFSET], len(block.vtx))
        bad_block_sn[TX_COUNT_OFFSET] += 1
        assert_raises_rpc_error(-22, "Block decode failed", node.getblocktemplate, {'data': b2x(bad_block_sn), 'mode': 'proposal'})

        self.log.info("getblocktemplate: Test bad bits")
        bad_block = copy.deepcopy(block)
        bad_block.nBits = 469762303  # impossible in the real world
        assert_template(node, bad_block, 'bad-diffbits')

        self.log.info("getblocktemplate: Test bad merkle root")
        bad_block = copy.deepcopy(block)
        bad_block.hashMerkleRoot += 1
        assert_template(node, bad_block, 'bad-txnmrklroot', False)

        self.log.info("getblocktemplate: Test bad timestamps")
        bad_block = copy.deepcopy(block)
        bad_block.nTime = 2 ** 31 - 1
        assert_template(node, bad_block, 'time-too-new')
        bad_block.nTime = 0
        assert_template(node, bad_block, 'time-too-old')

        self.log.info("getblocktemplate: Test not best block")
        bad_block = copy.deepcopy(block)
        bad_block.hashPrevBlock = 123
        assert_template(node, bad_block, 'inconclusive-not-best-prevblk')

        self.log.info('submitheader tests')
        assert_raises_rpc_error(-22, 'Block header decode failed', lambda: node.submitheader(hexdata='xx' * 80))
        assert_raises_rpc_error(-22, 'Block header decode failed', lambda: node.submitheader(hexdata='ff' * 78))
        assert_raises_rpc_error(-25, 'Must submit previous header', lambda: node.submitheader(hexdata='ff' * 80))

        block.solve()

        def filter_tip_keys(chaintips):
            """
            Dash chaintips rpc returns extra info in each tip (difficulty, chainwork, and
            forkpoint). Filter down to relevant ones checked in this test.
            """
            check_keys = ["hash", "height", "branchlen", "status"]
            filtered_tips = []
            for tip in chaintips:
                filtered_tips.append({k: tip[k] for k in check_keys})
            return filtered_tips

        def chain_tip(b_hash, *, status='headers-only', branchlen=1):
            return {'hash': b_hash, 'height': int(tmpl["height"]), 'branchlen': branchlen, 'status': status}
        assert chain_tip(block.hash) not in filter_tip_keys(node.getchaintips())
        node.submitheader(hexdata=b2x(block.serialize()))
        assert chain_tip(block.hash) in filter_tip_keys(node.getchaintips())
        node.submitheader(hexdata=b2x(CBlockHeader(block).serialize()))  # Noop
        assert chain_tip(block.hash) in filter_tip_keys(node.getchaintips())

        bad_block_root = copy.deepcopy(block)
        bad_block_root.hashMerkleRoot += 2
        bad_block_root.solve()
        assert chain_tip(bad_block_root.hash) not in filter_tip_keys(node.getchaintips())
        node.submitheader(hexdata=b2x(CBlockHeader(bad_block_root).serialize()))
        assert chain_tip(bad_block_root.hash) in filter_tip_keys(node.getchaintips())
        # Should still reject invalid blocks, even if we have the header:
        assert_equal(node.submitblock(hexdata=b2x(bad_block_root.serialize())), 'invalid')
        assert chain_tip(bad_block_root.hash) in filter_tip_keys(node.getchaintips())
        # We know the header for this invalid block, so should just return early without error:
        node.submitheader(hexdata=b2x(CBlockHeader(bad_block_root).serialize()))
        assert chain_tip(bad_block_root.hash) in filter_tip_keys(node.getchaintips())

        bad_block_lock = copy.deepcopy(block)
        bad_block_lock.vtx[0].nLockTime = 2**32 - 1
        bad_block_lock.vtx[0].rehash()
        bad_block_lock.hashMerkleRoot = bad_block_lock.calc_merkle_root()
        bad_block_lock.solve()
        assert_equal(node.submitblock(hexdata=b2x(bad_block_lock.serialize())), 'invalid')
        # Build a "good" block on top of the submitted bad block
        bad_block2 = copy.deepcopy(block)
        bad_block2.hashPrevBlock = bad_block_lock.sha256
        bad_block2.solve()
        assert_raises_rpc_error(-25, 'bad-prevblk', lambda: node.submitheader(hexdata=b2x(CBlockHeader(bad_block2).serialize())))

        # Should reject invalid header right away
        bad_block_time = copy.deepcopy(block)
        bad_block_time.nTime = 1
        bad_block_time.solve()
        assert_raises_rpc_error(-25, 'time-too-old', lambda: node.submitheader(hexdata=b2x(CBlockHeader(bad_block_time).serialize())))

        # Should ask for the block from a p2p node, if they announce the header as well:
        # This section was ported from a newer upstream, where the network thread
        # starts itself and send_blocks_and_test names its node argument "node".
        # Neither holds in this framework: without the explicit start the socket
        # is accepted but no message ever flows, and the node never even sees a
        # version.
        node.add_p2p_connection(P2PDataStore())
        network_thread_start()
        node.p2p.wait_for_verack()
        node.p2p.wait_for_getheaders(timeout=5)  # Drop the first getheaders
        node.p2p.send_blocks_and_test(blocks=[block], rpc=node)
        # Must be active now:
        assert chain_tip(block.hash, status='active', branchlen=0) in filter_tip_keys(node.getchaintips())

        # Building a few blocks should give the same results
        node.generate(10)
        assert_raises_rpc_error(-25, 'time-too-old', lambda: node.submitheader(hexdata=b2x(CBlockHeader(bad_block_time).serialize())))
        assert_raises_rpc_error(-25, 'bad-prevblk', lambda: node.submitheader(hexdata=b2x(CBlockHeader(bad_block2).serialize())))
        node.submitheader(hexdata=b2x(CBlockHeader(block).serialize()))
        node.submitheader(hexdata=b2x(CBlockHeader(bad_block_root).serialize()))

if __name__ == '__main__':
    MiningTest().main()
