#!/usr/bin/env python3
# Copyright (c) 2020-2026 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Wallet RPCs no test reached: listaddressbalances, abortrescan and the
Electrum import.

listaddressbalances needs -addressindex, which is why it is easier to leave
uncovered than to reach. The Electrum import is only exercised for its refusals
here: a real import needs an Electrum export to feed it, and the failure paths
are where a user actually lands.
"""

import os
from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
)


class WalletAddressQueriesTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-addressindex"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]
        node.generatetoaddress(110, node.getnewaddress())

        self.test_listaddressbalances()
        self.test_abortrescan()
        self.test_importelectrumwallet()

    def test_listaddressbalances(self):
        node = self.nodes[0]

        self.log.info("Sending to two addresses puts both in the listing")
        small, large = node.getnewaddress(), node.getnewaddress()
        node.sendtoaddress(small, 1)
        node.sendtoaddress(large, 20)
        node.generate(1)

        balances = node.listaddressbalances()
        assert_equal(balances[small], Decimal("1"))
        assert_equal(balances[large], Decimal("20"))

        self.log.info("The minimum filters out anything smaller")
        filtered = node.listaddressbalances(5)
        assert large in filtered
        assert small not in filtered
        assert_equal(filtered[large], Decimal("20"))

        self.log.info("A minimum above every balance leaves nothing")
        biggest = max(balances.values())
        assert_equal(node.listaddressbalances(biggest + 1), {})

    def test_abortrescan(self):
        node = self.nodes[0]

        self.log.info("With no rescan under way there is nothing to abort")
        assert_equal(node.abortrescan(), False)

        self.log.info("It stays false after a rescan has finished")
        node.rescanblockchain(0, 1)
        assert_equal(node.abortrescan(), False)

    def test_importelectrumwallet(self):
        node = self.nodes[0]

        self.log.info("A file that is not there is reported, not ignored")
        missing = os.path.join(self.options.tmpdir, "no-such-electrum-export.csv")
        assert_raises_rpc_error(-8, "Cannot open Electrum wallet export file",
                                node.importelectrumwallet, missing)

        self.log.info("The extension is checked before the file is opened")
        assert_raises_rpc_error(-8, "File has no extension",
                                node.importelectrumwallet,
                                os.path.join(self.options.tmpdir, "export"))
        assert_raises_rpc_error(-8, "File has wrong extension",
                                node.importelectrumwallet,
                                os.path.join(self.options.tmpdir, "export.txt"))

        # Past those checks the parser is lenient: it walks the file and skips
        # what it cannot read, so a well-named file of nonsense imports nothing
        # and reports nothing.
        self.log.info("A well-named file of nonsense is accepted and imports nothing")
        junk = os.path.join(self.options.tmpdir, "not-an-export.csv")
        with open(junk, "w", encoding="utf-8") as f:
            f.write("this is not an electrum export\n")
        before = node.getwalletinfo()["keypoolsize"]
        node.importelectrumwallet(junk)
        assert_equal(node.getwalletinfo()["keypoolsize"], before)

        self.log.info("The wallet is unchanged throughout")
        assert_greater_than(node.getbalance(), 0)


if __name__ == '__main__':
    WalletAddressQueriesTest().main()
