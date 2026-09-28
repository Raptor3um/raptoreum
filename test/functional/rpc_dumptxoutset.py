#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the generation of UTXO snapshots using `dumptxoutset`.

This file was already in the tree but no runner entry referenced it, so it had
never run. The hashes below are Raptoreum's, not the inherited Bitcoin ones, and
are stable across runs; they pin the snapshot format so a future change to it
cannot pass unnoticed.
"""
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

import hashlib
from pathlib import Path


class DumptxoutsetTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def run_test(self):
        """Test a trivial usage of the dumptxoutset RPC command."""
        node = self.nodes[0]
        mocktime = node.getblockheader(node.getblockhash(0))['time'] + 1
        node.setmocktime(mocktime)
        node.generate(100)

        FILENAME = 'txoutset.dat'
        out = node.dumptxoutset(FILENAME)
        expected_path = Path(node.datadir) / self.chain / FILENAME

        assert expected_path.is_file()

        # Two coins per block, not one: every Raptoreum coinbase carries a
        # second output for the founder payment, which is present but zero
        # valued below the founder start height.
        assert_equal(out['coins_written'], 200)
        assert_equal(out['base_height'], 100)
        assert_equal(out['path'], str(expected_path))
        # Blockhash should be deterministic based on mocked time. These two
        # values move whenever the set of quorum types regtest registers
        # changes, since that changes which commitments every block in the
        # DKG mining window carries.
        assert_equal(
            out['base_hash'],
            '0a17fcd5051e169c998b551198a65e7eab52f54b2bd7963ea2ec9c14f21f05d9')

        with open(str(expected_path), 'rb') as f:
            digest = hashlib.sha256(f.read()).hexdigest()
            # UTXO snapshot hash should be deterministic based on mocked time.
            assert_equal(
                digest, '03cde3c0a493e8f1ab232f00a7f4b54f544418a803bf74c670cf0ac1ce229ad3')

        # Specifying a path to an existing file will fail.
        assert_raises_rpc_error(
            -8, '{} already exists'.format(FILENAME),  node.dumptxoutset, FILENAME)

if __name__ == '__main__':
    DumptxoutsetTest().main()
