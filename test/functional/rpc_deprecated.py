#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test deprecation of RPC calls."""
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

class DeprecatedRpcTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        # node1 re-enables both deprecated calls; node0 leaves them off.
        self.extra_args = [
            [],
            ["-deprecatedrpc=smartnode_winner", "-deprecatedrpc=smartnode_current"],
        ]

    def skip_test_is_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        # This test verifies the behaviour of deprecated RPC methods with and
        # without the -deprecatedrpc flag. Add a case here whenever a method is
        # deprecated.
        #
        # Upstream checks the wallet's `generate`; this tree has no such RPC.

        for method, arg in (("winner", "smartnode_winner"),
                            ("current", "smartnode_current")):
            self.log.info("Test 'smartnode %s' is deprecated", method)
            # src/rpc/smartnode.cpp throws before the help is even checked, so
            # the call fails whatever else is wrong with the chain.
            assert_raises_rpc_error(
                -1, "DEPRECATED: set -deprecatedrpc={} to enable it".format(arg),
                self.nodes[0].smartnode, method)

            # With the flag the guard is gone and the call goes through. With no
            # smartnodes configured it answers "unknown", which is a successful
            # return rather than an error; any exception here fails the test.
            assert_equal(self.nodes[1].smartnode(method), "unknown")

if __name__ == '__main__':
    DeprecatedRpcTest().main()
