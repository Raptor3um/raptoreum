#!/usr/bin/env python3
# Copyright (c) 2020-2026 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""The founder fee: five percent of every block, forever.

Raptoreum pays a perpetual founder fee out of the coinbase. It has no upstream
equivalent and it is consensus: src/consensus/tx_check.cpp rejects a coinbase
above the start height that does not carry it.

Wallet balance tests already subtract the fee to make their arithmetic work, so
a change in the rate would break them. Nothing until now asserted the rule
itself: who is paid, from which height, and what happens to a block that does
not pay.

The rule is a floor rather than an equality. IsBlockPayeeValid scans the
coinbase for an output paying the founder script at least the required amount,
so underpaying is rejected and overpaying is not.
"""

from test_framework.blocktools import (
    FOUNDER_SCRIPT,
    FOUNDER_START_HEIGHT,
    create_block,
    create_coinbase,
    get_block_subsidy,
    get_founder_payment,
)
from test_framework.messages import CTxOut, ToHex
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

REJECT_REASON = "bad-cb-founder-payment-not-found"


class FounderPaymentTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0]
        self.address = node.getnewaddress()

        self.log.info("Mining up to the founder start height")
        node.generatetoaddress(FOUNDER_START_HEIGHT, self.address)
        assert_equal(node.getblockcount(), FOUNDER_START_HEIGHT)

        self.test_nothing_before_the_start_height()
        self.test_pays_five_percent()
        self.test_missing_payment_is_rejected()
        self.test_wrong_payee_is_rejected()
        self.test_underpayment_is_rejected()
        self.test_overpayment_is_allowed()

    def coinbase_outputs(self, height):
        node = self.nodes[0]
        coinbase = node.getblock(node.getblockhash(height), 2)["tx"][0]
        return coinbase["vout"]

    def test_nothing_before_the_start_height(self):
        self.log.info("Up to the start height the founder output is present but empty")
        for height in (1, FOUNDER_START_HEIGHT // 2, FOUNDER_START_HEIGHT):
            outputs = self.coinbase_outputs(height)
            assert_equal(len(outputs), 2)
            assert_equal(outputs[1]["value"], 0)

    def test_pays_five_percent(self):
        node = self.nodes[0]

        self.log.info("From the next block on, the founder takes five percent")
        node.generatetoaddress(1, self.address)
        height = node.getblockcount()
        assert_equal(height, FOUNDER_START_HEIGHT + 1)

        subsidy = get_block_subsidy(height)
        expected = get_founder_payment(height, subsidy)
        assert expected > 0

        outputs = self.coinbase_outputs(height)
        miner, founder = outputs[0]["value"], outputs[1]["value"]
        assert_equal(int(founder * 100000000), expected)
        assert_equal(int((miner + founder) * 100000000), subsidy)

        self.log.info("It is paid to the address the chain parameters name")
        assert_equal(outputs[1]["scriptPubKey"]["hex"], FOUNDER_SCRIPT.hex())

    def build_block(self, mutate=None):
        """A block on top of the tip, optionally with its coinbase altered."""
        node = self.nodes[0]
        height = node.getblockcount() + 1
        coinbase = create_coinbase(height)
        if mutate is not None:
            mutate(coinbase, height)
            # calc_sha256 keeps a cached hash; rehash clears it first. Without
            # this the merkle root is computed from the pre-mutation coinbase
            # and the block is rejected for the wrong reason.
            coinbase.rehash()
        block = create_block(int(node.getbestblockhash(), 16), coinbase,
                             node.getblock(node.getbestblockhash())["time"] + 1)
        block.solve()
        return block

    def reject(self, block, reason=REJECT_REASON):
        """submitblock only ever says "invalid", so the reason comes from the log.

        Checking it matters: without it a block malformed for some unrelated
        reason would satisfy the assertion and the test would prove nothing."""
        with self.nodes[0].assert_debug_log([reason]):
            assert_equal(self.nodes[0].submitblock(ToHex(block)), "invalid")

    def test_missing_payment_is_rejected(self):
        self.log.info("A coinbase that keeps the whole subsidy is rejected")

        def drop_founder(coinbase, height):
            coinbase.vout = [coinbase.vout[0]]
            coinbase.vout[0].nValue = get_block_subsidy(height)

        self.reject(self.build_block(drop_founder))

    def test_wrong_payee_is_rejected(self):
        self.log.info("Paying the right amount to the wrong script is rejected")

        def redirect(coinbase, height):
            coinbase.vout[1].scriptPubKey = CScript([OP_TRUE])

        self.reject(self.build_block(redirect))

    def test_underpayment_is_rejected(self):
        self.log.info("Paying the founder one satoshi short is rejected")

        def shortchange(coinbase, height):
            coinbase.vout[1].nValue -= 1
            coinbase.vout[0].nValue += 1

        self.reject(self.build_block(shortchange))

    def test_overpayment_is_allowed(self):
        node = self.nodes[0]
        self.log.info("Paying the founder more than required is accepted")

        def overpay(coinbase, height):
            coinbase.vout[1].nValue += 1
            coinbase.vout[0].nValue -= 1

        block = self.build_block(overpay)
        assert_equal(self.nodes[0].submitblock(ToHex(block)), None)
        assert_equal(node.getbestblockhash(), block.hash)

        outputs = self.coinbase_outputs(node.getblockcount())
        height = node.getblockcount()
        required = get_founder_payment(height, get_block_subsidy(height))
        assert_equal(int(outputs[1]["value"] * 100000000), required + 1)


if __name__ == '__main__':
    FounderPaymentTest().main()
