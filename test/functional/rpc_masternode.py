#!/usr/bin/env python3
# Copyright (c) 2020-2021 The Dash Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import RaptoreumTestFramework
from test_framework.util import assert_equal

'''
rpc_masternode.py

Test "smartnode" rpc subcommands
'''

class RPCMasternodeTest(RaptoreumTestFramework):
    def set_test_params(self):
        # Ten smartnodes, not three: GetSmartnodePayment (src/validation.cpp)
        # pays smartnodes nothing at all while the list holds fewer than 10,
        # outside testnet. With fewer, `smartnode payments` returns an empty
        # payee list and there is nothing for `winners` to agree with.
        self.set_raptoreum_test_params(11, 10, fast_dip3_enforcement=True)

    def run_test(self):
        self.log.info("test that results from `winners` and `payments` RPCs match")
        blockhash = ""
        payments = []
        # we expect some smartnodes to have 0 operator reward and some to have non-0 operator reward
        checked_0_operator_reward = False
        checked_non_0_operator_reward = False

        # Nothing is paid to smartnodes before nSmartnodePaymentsStartBlock, which
        # CRegTestParams sets to 240, so `payments` would return an empty payee
        # list and there would be nothing for `winners` to agree with.
        SMARTNODE_PAYMENTS_START = 240
        while self.nodes[0].getblockcount() < SMARTNODE_PAYMENTS_START:
            self.nodes[0].generate(10)
        self.sync_all()

        while not checked_0_operator_reward or not checked_non_0_operator_reward:
            self.nodes[0].generate(1)
            bi = self.nodes[0].getblockchaininfo()
            height = bi["blocks"]
            blockhash = bi["bestblockhash"]
            winners_payee = self.nodes[0].smartnode("winners")[str(height)]
            payments = self.nodes[0].smartnode("payments", blockhash)
            assert_equal(len(payments), 1)
            payments_block = payments[0]
            payments_block_payees = payments_block["smartnodes"][0]["payees"]
            payments_payee = ""
            for i in range(0, len(payments_block_payees)):
                payments_payee += payments_block_payees[i]["address"]
                if i < len(payments_block_payees) - 1:
                    payments_payee += ", "
            assert_equal(payments_block["height"], height)
            assert_equal(payments_block["blockhash"], blockhash)
            assert_equal(winners_payee, payments_payee)
            if len(payments_block_payees) == 1:
                checked_0_operator_reward = True
            if len(payments_block_payees) > 1:
                checked_non_0_operator_reward = True

        self.log.info("test various `payments` RPC options")
        payments1 = self.nodes[0].smartnode("payments", blockhash, -1)
        assert_equal(payments, payments1)
        payments2_1 = self.nodes[0].smartnode("payments", blockhash, 2)
        assert_equal(len(payments2_1), 1)
        assert_equal(payments[0], payments2_1[0])
        payments2_2 = self.nodes[0].smartnode("payments", blockhash, -2)
        assert_equal(len(payments2_2), 2)
        assert_equal(payments[0], payments2_2[-1])

        self.log.info("test that `smartnode payments` results at chaintip match `getblocktemplate` results for that block")
        gbt_smartnode = self.nodes[0].getblocktemplate()["smartnode"]
        self.nodes[0].generate(1)
        payments_smartnode = self.nodes[0].smartnode("payments")[0]["smartnodes"][0]
        for i in range(0, len(gbt_smartnode)):
            assert_equal(gbt_smartnode[i]["payee"], payments_smartnode["payees"][i]["address"])
            assert_equal(gbt_smartnode[i]["script"], payments_smartnode["payees"][i]["script"])
            assert_equal(gbt_smartnode[i]["amount"], payments_smartnode["payees"][i]["amount"])


if __name__ == '__main__':
    RPCMasternodeTest().main()
