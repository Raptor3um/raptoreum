#!/usr/bin/env python3
# Copyright (c) 2014-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test RPCs related to blockchainstate.

Test the following RPCs:
    - getblockchaininfo
    - gettxoutsetinfo
    - getdifficulty
    - getbestblockhash
    - getblockhash
    - getblockheader
    - getchaintxstats
    - getnetworkhashps
    - verifychain

Tests correspond to code in rpc/blockchain.cpp.
"""

from decimal import Decimal
import http.client
import subprocess
import sys

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    REGTEST_LAUNCH_SUBSIDY,
    assert_equal,
    assert_greater_than,
    assert_greater_than_or_equal,
    assert_raises,
    assert_raises_rpc_error,
    assert_is_hex_string,
    assert_is_hash_string,
)
from test_framework.blocktools import (
    create_block,
    create_coinbase,
)
from test_framework.messages import (
    CBlockHeader,
    FromHex,
    msg_block,
)
from test_framework.mininode import (
    P2PInterface,
    network_thread_start,
)


class BlockchainTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.stderr = sys.stdout
        self.extra_args = [['-stopatheight=207', '-prune=1', '-txindex=0']]

    def run_test(self):
        # Have to prepare the chain manually here.
        # txindex=1 by default in Dash which is incompatible with pruning.
        self.set_genesis_mocktime()
        for i in range(200):
            self.bump_mocktime(156)
            self.nodes[0].generate(1)
        # Actual tests
        self._test_getblockchaininfo()
        self._test_getchaintxstats()
        self._test_gettxoutsetinfo()
        self._test_getblockheader()
        self._test_getdifficulty()
        self._test_getnetworkhashps()
        self._test_stopatheight()
        self._test_waitforblockheight()
        assert self.nodes[0].verifychain(4, 0)

    def _test_getblockchaininfo(self):
        self.log.info("Test getblockchaininfo")

        # Alphabetical: these are compared against sorted(res.keys()).
        # rip1_softforks is Raptoreum's own and was inserted here out of order.
        keys = [
            'bestblockhash',
            'blocks',
            'chain',
            'chainwork',
            'difficulty',
            'headers',
            'initialblockdownload',
            'mediantime',
            'pruned',
            'rip1_softforks',
            'size_on_disk',
            'softforks',
            'verificationprogress',
            'warnings',
        ]
        res = self.nodes[0].getblockchaininfo()

        # result should have these additional pruning keys if manual pruning is enabled
        assert_equal(sorted(res.keys()), sorted(['pruneheight', 'automatic_pruning'] + keys))

        # size_on_disk should be > 0
        assert_greater_than(res['size_on_disk'], 0)

        # pruneheight should be greater or equal to 0
        assert_greater_than_or_equal(res['pruneheight'], 0)

        # check other pruning fields given that prune=1
        assert res['pruned']
        assert not res['automatic_pruning']

        self.restart_node(0, ['-stopatheight=207', '-txindex=0'])
        res = self.nodes[0].getblockchaininfo()
        # should have exact keys
        assert_equal(sorted(res.keys()), keys)

        self.restart_node(0, ['-stopatheight=207', '-prune=550', '-txindex=0'])
        res = self.nodes[0].getblockchaininfo()
        # result should have these additional pruning keys if prune=550
        assert_equal(sorted(res.keys()), sorted(['pruneheight', 'automatic_pruning', 'prune_target_size'] + keys))

        # check related fields
        assert res['pruned']
        assert_equal(res['pruneheight'], 0)
        assert res['automatic_pruning']
        assert_equal(res['prune_target_size'], 576716800)
        assert_greater_than(res['size_on_disk'], 0)

    def _test_getchaintxstats(self):
        self.log.info("Test getchaintxstats")

        # Test `getchaintxstats` invalid extra parameters
        assert_raises_rpc_error(-1, 'getchaintxstats', self.nodes[0].getchaintxstats, 0, '', 0)

        # Test `getchaintxstats` invalid `nblocks`
        assert_raises_rpc_error(-1, "JSON value is not an integer as expected", self.nodes[0].getchaintxstats, '')
        assert_raises_rpc_error(-8, "Invalid block count: should be between 0 and the block's height - 1", self.nodes[0].getchaintxstats, -1)
        assert_raises_rpc_error(-8, "Invalid block count: should be between 0 and the block's height - 1", self.nodes[0].getchaintxstats, self.nodes[0].getblockcount())

        # Test `getchaintxstats` invalid `blockhash`
        assert_raises_rpc_error(-1, "JSON value is not a string as expected", self.nodes[0].getchaintxstats, blockhash=0)
        assert_raises_rpc_error(-5, "Block not found", self.nodes[0].getchaintxstats, blockhash='0')
        blockhash = self.nodes[0].getblockhash(200)
        self.nodes[0].invalidateblock(blockhash)
        assert_raises_rpc_error(-8, "Block is not in main chain", self.nodes[0].getchaintxstats, blockhash=blockhash)
        self.nodes[0].reconsiderblock(blockhash)

        chaintxstats = self.nodes[0].getchaintxstats(1)
        # Upstream expects one transaction per block plus genesis. Here a block
        # inside a DKG mining window also carries a quorum commitment, so the
        # count is not one per block; check getchaintxstats against what the
        # blocks actually hold rather than against a fixed number.
        tip_height = self.nodes[0].getblockcount()
        block_tx_counts = [len(self.nodes[0].getblock(self.nodes[0].getblockhash(h))['tx'])
                           for h in range(0, tip_height + 1)]
        assert_equal(chaintxstats['txcount'], sum(block_tx_counts))
        # One block of window, so the rate is that block's transactions over the
        # 156 second spacing.
        assert_equal(round(chaintxstats['txrate'] * 156, 10), Decimal(block_tx_counts[-1]))

        b1_hash = self.nodes[0].getblockhash(1)
        b1 = self.nodes[0].getblock(b1_hash)
        b200_hash = self.nodes[0].getblockhash(200)
        b200 = self.nodes[0].getblock(b200_hash)
        time_diff = b200['mediantime'] - b1['mediantime']

        # The window runs from block 2 to block 200 inclusive; its transaction
        # count is whatever those blocks hold, commitments included.
        window_tx = sum(block_tx_counts[2:201])
        chaintxstats = self.nodes[0].getchaintxstats()
        assert_equal(chaintxstats['time'], b200['time'])
        assert_equal(chaintxstats['txcount'], sum(block_tx_counts))
        assert_equal(chaintxstats['window_final_block_hash'], b200_hash)
        assert_equal(chaintxstats['window_block_count'], 199)
        assert_equal(chaintxstats['window_tx_count'], window_tx)
        assert_equal(chaintxstats['window_interval'], time_diff)
        assert_equal(round(chaintxstats['txrate'] * time_diff, 10), Decimal(window_tx))

        chaintxstats = self.nodes[0].getchaintxstats(blockhash=b1_hash)
        assert_equal(chaintxstats['time'], b1['time'])
        assert_equal(chaintxstats['txcount'], sum(block_tx_counts[0:2]))
        assert_equal(chaintxstats['window_final_block_hash'], b1_hash)
        assert_equal(chaintxstats['window_block_count'], 0)
        assert('window_tx_count' not in chaintxstats)
        assert('window_interval' not in chaintxstats)
        assert('txrate' not in chaintxstats)

    def _test_gettxoutsetinfo(self):
        node = self.nodes[0]
        res = node.gettxoutsetinfo()

        # 200 blocks, all inside the launch window, so each pays the launch
        # subsidy in full: the founder's 5% only starts at height 501.
        assert_equal(res['total_amount'], Decimal(200 * REGTEST_LAUNCH_SUBSIDY))
        # One coinbase per block; quorum commitments create no outputs and so no
        # unspent entries.
        assert_equal(res['transactions'], 200)
        assert_equal(res['height'], 200)
        # Two outputs per coinbase, the second being the founder payment, which
        # exists but is zero valued below the founder start height.
        assert_equal(res['txouts'], 400)
        size = res['disk_size']
        assert size > 6400
        assert size < 64000
        assert_equal(len(res['bestblock']), 64)
        assert_equal(len(res['hash_serialized_2']), 64)

        self.log.info("Test that gettxoutsetinfo() works for blockchain with just the genesis block")
        b1hash = node.getblockhash(1)
        node.invalidateblock(b1hash)

        res2 = node.gettxoutsetinfo()
        assert_equal(res2['transactions'], 0)
        assert_equal(res2['total_amount'], Decimal('0'))
        assert_equal(res2['height'], 0)
        assert_equal(res2['txouts'], 0)
        assert_equal(res2['bogosize'], 0),
        assert_equal(res2['bestblock'], node.getblockhash(0))
        assert_equal(len(res2['hash_serialized_2']), 64)

        self.log.info("Test that gettxoutsetinfo() returns the same result after invalidate/reconsider block")
        node.reconsiderblock(b1hash)

        res3 = node.gettxoutsetinfo()
        # The field 'disk_size' is non-deterministic and can thus not be
        # compared between res and res3.  Everything else should be the same.
        del res['disk_size'], res3['disk_size']
        assert_equal(res, res3)

        self.log.info("Test hash_type option for gettxoutsetinfo()")
        # hash_serialized_2 is the default, so asking for it explicitly must
        # not change the result.
        res4 = node.gettxoutsetinfo(hash_type='hash_serialized_2')
        del res4['disk_size']
        assert_equal(res, res4)

        # hash_type none should not return a UTXO set hash.
        res5 = node.gettxoutsetinfo(hash_type='none')
        assert 'hash_serialized_2' not in res5

        # Upstream also covers hash_type muhash. This tree has no MuHash, and
        # ParseHashType (rpc/blockchain.cpp) offers only hash_serialized_2 and
        # none, so that half is left out.
        assert_raises_rpc_error(-8, "foohash is not a valid hash_type", node.gettxoutsetinfo, "foohash")

    def _test_getblockheader(self):
        node = self.nodes[0]

        assert_raises_rpc_error(-5, "Block not found", node.getblockheader, "nonsense")

        besthash = node.getbestblockhash()
        secondbesthash = node.getblockhash(199)
        header = node.getblockheader(besthash)

        assert_equal(header['hash'], besthash)
        assert_equal(header['height'], 200)
        assert_equal(header['confirmations'], 1)
        assert_equal(header['previousblockhash'], secondbesthash)
        assert_is_hex_string(header['chainwork'])
        assert_equal(header['nTx'], 1)
        assert_is_hash_string(header['hash'])
        assert_is_hash_string(header['previousblockhash'])
        assert_is_hash_string(header['merkleroot'])
        assert_is_hash_string(header['bits'], length=None)
        assert isinstance(header['time'], int)
        assert isinstance(header['mediantime'], int)
        assert isinstance(header['nonce'], int)
        assert isinstance(header['version'], int)
        assert isinstance(int(header['versionHex'], 16), int)

        # verbose=False returns the header as hex; it must round-trip to the
        # same hash.
        header_hex = node.getblockheader(blockhash=besthash, verbose=False)
        assert_is_hex_string(header_hex)
        header_obj = FromHex(CBlockHeader(), header_hex)
        header_obj.calc_sha256()
        assert_equal(header_obj.hash, besthash)
        assert isinstance(header['difficulty'], Decimal)

    def _test_getdifficulty(self):
        difficulty = self.nodes[0].getdifficulty()
        # 1 hash in 2 should be valid, so difficulty should be 1/2**31
        # binary => decimal => binary math is why we do this check
        assert abs(difficulty * 2**31 - 1) < 0.0001

    def _test_getnetworkhashps(self):
        hashes_per_second = self.nodes[0].getnetworkhashps()
        # This should be 2 hashes every 2.6 minutes (156 seconds) or 1/78
        assert abs(hashes_per_second * 78 - 1) < 0.0001

    def _test_stopatheight(self):
        assert_equal(self.nodes[0].getblockcount(), 200)
        self.nodes[0].generate(6)
        assert_equal(self.nodes[0].getblockcount(), 206)
        self.log.debug('Node should not stop at this height')
        assert_raises(subprocess.TimeoutExpired, lambda: self.nodes[0].process.wait(timeout=3))
        try:
            self.nodes[0].generate(1)
        except (ConnectionError, http.client.BadStatusLine):
            pass  # The node already shut down before response
        self.log.debug('Node should stop at this height...')
        self.nodes[0].wait_until_stopped()
        self.start_node(0, ['-txindex=0'])
        assert_equal(self.nodes[0].getblockcount(), 207)

    def _test_waitforblockheight(self):
        self.log.info("Test waitforblockheight")

        node = self.nodes[0]

        # Start a P2P connection since we'll need to create some blocks.
        node.add_p2p_connection(P2PInterface())
        network_thread_start()
        node.p2p.wait_for_verack()

        current_height = node.getblock(node.getbestblockhash())['height']

        # Create a fork somewhere below our current height, invalidate the tip
        # of that fork, and then ensure that waitforblockheight still
        # works as expected.
        #
        # (Previously this was broken based on setting
        # `rpc/blockchain.cpp:latestblock` incorrectly.)
        #
        b20hash = node.getblockhash(20)
        b20 = node.getblock(b20hash)

        def solve_and_send_block(prevhash, height, time):
            b = create_block(prevhash, create_coinbase(height), time)
            b.solve()
            node.p2p.send_message(msg_block(b))
            node.p2p.sync_with_ping()
            return b

        b21f = solve_and_send_block(int(b20hash, 16), 21, b20['time'] + 1)
        b22f = solve_and_send_block(b21f.sha256, 22, b21f.nTime + 1)

        node.invalidateblock(b22f.hash)

        def assert_waitforheight(height, timeout=2):
            assert_equal(
                node.waitforblockheight(height, timeout)['height'],
                current_height)

        assert_waitforheight(0)
        assert_waitforheight(current_height - 1)
        assert_waitforheight(current_height)
        assert_waitforheight(current_height + 1)


if __name__ == '__main__':
    BlockchainTest().main()
