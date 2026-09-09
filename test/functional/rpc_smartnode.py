#!/usr/bin/env python3
# Copyright (c) 2020-2021 The Dash Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the "smartnode" RPC subcommands.

Note on what this can and cannot check: on regtest the smartnode payout is zero
unless there are at least 10 smartnodes (GetSmartnodePayment() only makes an
exception for testnet, not regtest) and the block is above height 240 (the
regtest reward percentage is 0 up to that height). This test runs with three
smartnodes, so every payout here is zero and the "payees" arrays are empty.
What is exercised is the structure of the results, the block selection options
of `payments`, and the payee rotation reported by `winners`.
"""
from test_framework.test_framework import RaptoreumTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class RPCSmartnodeTest(RaptoreumTestFramework):
    def set_test_params(self):
        self.set_raptoreum_test_params(4, 3, fast_dip3_enforcement=True)

    def run_test(self):
        node = self.nodes[0]

        registered = node.protx('list', 'registered', True)
        assert_equal(len(registered), 3)
        known_protx = set(entry['proTxHash'] for entry in registered)

        node.generate(5)
        self.sync_all()
        tip_height = node.getblockcount()
        tip_hash = node.getbestblockhash()

        self.log.info("`payments` describes the block it was asked about")
        payments = node.smartnode('payments', tip_hash)
        assert_equal(len(payments), 1)
        assert_equal(payments[0]['height'], tip_height)
        assert_equal(payments[0]['blockhash'], tip_hash)
        # one entry per paid smartnode; the payee itself is always reported
        assert_equal(len(payments[0]['smartnodes']), 1)
        assert payments[0]['smartnodes'][0]['proTxHash'] in known_protx

        self.log.info("`payments` defaults to the chain tip")
        assert_equal(node.smartnode('payments'), payments)

        # Both 1 and -1 mean "just the starting block", per the RPC help.
        assert_equal(node.smartnode('payments', tip_hash, -1), payments)

        self.log.info("`payments` rejects a block that is not on the chain")
        assert_raises_rpc_error(-5, "Block not found", node.smartnode, 'payments',
                                '00' * 32)

        self.log.info("`winners` reports a payee for recent and upcoming heights")
        winners = node.smartnode('winners', '10')
        # every recent height is covered, and so are the projected ones
        for height in range(tip_height - 9, tip_height + 1):
            assert str(height) in winners, "height %d missing from winners" % height
        assert str(tip_height + 1) in winners, "winners should project future heights"
        # A payee must have been resolved for every height at which smartnodes were
        # already registered. Heights from before that report "Unknown", which is
        # correct: there was nobody to pay.
        for height in range(tip_height - 4, tip_height + 2):
            payee = winners[str(height)]
            assert payee != "Unknown", "no payee resolved for height %d" % height
            assert payee.strip() != "", "empty payee for height %d" % height

        self.log.info("`winners` rotates between the registered smartnodes")
        payout_addresses = set(entry['state']['payoutAddress'] for entry in registered)
        recent = [winners[str(h)].split(',')[0].strip()
                  for h in range(tip_height - 4, tip_height + 1)]
        for payee in recent:
            assert payee in payout_addresses, "unexpected payee %s" % payee
        assert len(set(recent)) > 1, "the payee should rotate between smartnodes"

        self.log.info("`winners` honours the filter argument")
        one_payee = recent[0]
        filtered = node.smartnode('winners', '10', one_payee)
        assert len(filtered) > 0
        for payee in filtered.values():
            assert one_payee in payee


if __name__ == '__main__':
    RPCSmartnodeTest().main()
