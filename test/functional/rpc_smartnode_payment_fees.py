#!/usr/bin/env python3
# Copyright (c) 2026 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Check actual payees and ordinary/special fees with and without a txindex."""

from test_framework.test_framework import RaptoreumTestFramework
from test_framework.util import assert_equal


class SmartnodePaymentFeesTest(RaptoreumTestFramework):
    def set_test_params(self):
        # Payments require ten smartnodes; node 1 is a plain node without txindex.
        args = [[] for _ in range(12)]
        args[1] = ["-txindex=0"]
        self.set_raptoreum_test_params(12, 10, extra_args=args, fast_dip3_enforcement=True)

    def check_payments(self, txid):
        node = self.nodes[0]
        template = node.getblocktemplate()
        assert txid in [tx["hash"] for tx in template["transactions"]]
        expected = template["smartnode"]
        assert expected, "the test must exercise actual smartnode payments"
        blockhash = node.generate(1)[0]
        self.sync_all()
        payments = node.smartnode("payments", blockhash)
        assert_equal(self.nodes[1].smartnode("payments", blockhash), payments)
        payment = payments[0]["smartnodes"][0]
        assert_equal(payment["payees"], [
            {"address": p["payee"], "script": p["script"], "amount": p["amount"]}
            for p in expected
        ])
        assert_equal(payment["amount"], sum(p["amount"] for p in expected))
        assert_equal(node.protx("info", payment["proTxHash"])["state"]["payoutAddress"],
                     payment["payees"][0]["address"])
        return blockhash, payments

    def run_test(self):
        node = self.nodes[0]
        # Regtest's smartnode payment schedule starts at height 240.
        while node.getblockcount() < 240:
            node.generate(10)
        self.sync_all()

        self.log.info("Ordinary transaction fees match the miner's payment template")
        self.check_payments(node.sendtoaddress(node.getnewaddress(), 1))

        self.log.info("Special fees use their separate smartnode reward share")
        self.bump_mocktime(1)
        node.spork("SPORK_22_SPECIAL_TX_FEE", 3)
        self.wait_for_sporks_same()
        txid = node.sendtoaddress(node.getnewaddress(), 1,
                                 {"future_maturity": 1, "future_locktime": -1})
        assert_equal(node.getrawtransaction(txid, True)["futureTx"]["fee"], 3)
        blockhash, payments = self.check_payments(txid)

        self.log.info("Historical fees do not depend on today's special-fee setting")
        self.bump_mocktime(1)
        node.spork("SPORK_22_SPECIAL_TX_FEE", 5)
        self.wait_for_sporks_same()
        assert_equal(node.smartnode("payments", blockhash), payments)
        assert_equal(self.nodes[1].smartnode("payments", blockhash), payments)

        self.log.info("Historical payments remain stable after a smartnode leaves")
        assert_equal(node.smartnode("count")["total"], 10)
        self.remove_smartnode(0)
        assert_equal(node.smartnode("count")["total"], 9)
        assert_equal(node.smartnode("payments", blockhash), payments)
        assert_equal(self.nodes[1].smartnode("payments", blockhash), payments)


if __name__ == '__main__':
    SmartnodePaymentFeesTest().main()
