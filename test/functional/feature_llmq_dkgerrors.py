#!/usr/bin/env python3
# Copyright (c) 2015-2020 The Dash Core developers
# Copyright (c) 2020-2022 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import RaptoreumTestFramework, LLMQ_TEST_TYPE
from test_framework.util import assert_equal

'''
feature_llmq_dkgerrors.py

Simulate and check DKG errors

'''

class LLMQDKGErrors(RaptoreumTestFramework):
    def set_test_params(self):
        # Three smartnodes for a quorum of three, so every smartnode is a member
        # of every session. With five, only three are chosen and mninfo[0] --
        # the one this test tells to misbehave -- is often not among them, so
        # its simulated errors go unnoticed and no one complains.
        self.set_raptoreum_test_params(4, 3, [["-whitelist=127.0.0.1"]] * 4, fast_dip3_enforcement=True)

    def run_test(self):

        self.wait_for_dip8_activation()
        self.sync_blocks(self.nodes, timeout=60*5)

        self.nodes[0].spork("SPORK_17_QUORUM_DKG_ENABLED", 0)
        self.wait_for_sporks_same()

        self.log.info("Mine one quorum without simulating any errors")
        qh = self.mine_quorum()
        self.assert_member_valid(qh, self.mninfo[0].proTxHash, True)

        self.log.info("Lets omit the contribution")
        self.mninfo[0].node.quorum('dkgsimerror', 'contribution-omit', '1')
        qh = self.mine_quorum(expected_contributions=2)
        self.assert_member_valid(qh, self.mninfo[0].proTxHash, False)

        # Heal here too. A bad DKG costs 66 against a cap of 100 and decay is one
        # per block, so two within 100 blocks ban the member permanently, leaving
        # a three-member quorum unable to form.
        self.heal_smartnodes(33)

        self.log.info("Lets lie in the contribution but provide a correct justification")
        self.mninfo[0].node.quorum('dkgsimerror', 'contribution-omit', '0')
        self.mninfo[0].node.quorum('dkgsimerror', 'contribution-lie', '1')
        qh = self.mine_quorum(expected_contributions=3, expected_complaints=2, expected_justifications=1)
        self.assert_member_valid(qh, self.mninfo[0].proTxHash, True)

        self.log.info("Lets lie in the contribution and then omit the justification")
        self.mninfo[0].node.quorum('dkgsimerror', 'justify-omit', '1')
        qh = self.mine_quorum(expected_contributions=3, expected_complaints=2)
        self.assert_member_valid(qh, self.mninfo[0].proTxHash, False)

        # Heal some damage (don't get PoSe banned)
        self.heal_smartnodes(33)

        self.log.info("Lets lie in the contribution and then also lie in the justification")
        self.mninfo[0].node.quorum('dkgsimerror', 'justify-omit', '0')
        self.mninfo[0].node.quorum('dkgsimerror', 'justify-lie', '1')
        qh = self.mine_quorum(expected_contributions=3, expected_complaints=2, expected_justifications=1)
        self.assert_member_valid(qh, self.mninfo[0].proTxHash, False)

        self.log.info("Lets lie about another MN")
        self.mninfo[0].node.quorum('dkgsimerror', 'contribution-lie', '0')
        self.mninfo[0].node.quorum('dkgsimerror', 'justify-lie', '0')
        self.mninfo[0].node.quorum('dkgsimerror', 'complain-lie', '1')
        qh = self.mine_quorum(expected_contributions=3, expected_complaints=1, expected_justifications=2)
        self.assert_member_valid(qh, self.mninfo[0].proTxHash, True)

        self.log.info("Lets omit 1 premature commitments")
        self.mninfo[0].node.quorum('dkgsimerror', 'complain-lie', '0')
        self.mninfo[0].node.quorum('dkgsimerror', 'commit-omit', '1')
        qh = self.mine_quorum(expected_contributions=3, expected_complaints=0, expected_justifications=0, expected_commitments=2)
        self.assert_member_valid(qh, self.mninfo[0].proTxHash, True)

        self.log.info("Lets lie in 1 premature commitments")
        self.mninfo[0].node.quorum('dkgsimerror', 'commit-omit', '0')
        self.mninfo[0].node.quorum('dkgsimerror', 'commit-lie', '1')
        qh = self.mine_quorum(expected_contributions=3, expected_complaints=0, expected_justifications=0, expected_commitments=2)
        self.assert_member_valid(qh, self.mninfo[0].proTxHash, True)

    def assert_member_valid(self, quorumHash, proTxHash, expectedValid):
        q = self.nodes[0].quorum('info', LLMQ_TEST_TYPE, quorumHash, True)
        for m in q['members']:
            if m['proTxHash'] == proTxHash:
                if expectedValid:
                    assert(m['valid'])
                else:
                    assert(not m['valid'])
            else:
                assert(m['valid'])

    def heal_smartnodes(self, blockCount):
        # We're not testing PoSe here, so lets heal the MNs :)
        #
        # Mine until every penalty is actually back to zero rather than a fixed
        # number of blocks. A DKG that marks a member bad costs 66 against a cap
        # of 100 (CalcMaxPoSePenalty is max(100, mn count), and there are three
        # smartnodes here), the decay is one per block, and a member that reaches
        # the cap is banned for good -- DecreasePoSePenalties skips banned nodes,
        # so no amount of mining lifts a ban. With three smartnodes filling a
        # three-member quorum, one banned member means no quorum can form and the
        # next session never leaves phase 1. Upstream's fixed 33 blocks are not
        # enough on this chain: the penalty ran 0 -> 66 -> 87 -> 100 and the
        # member was banned before the last two scenarios could run.
        self.nodes[0].spork("SPORK_17_QUORUM_DKG_ENABLED", 4070908800)
        self.wait_for_sporks_same()

        def penalties():
            return [self.nodes[0].protx('info', mn.proTxHash)['state']['PoSePenalty'] for mn in self.mninfo]

        mined = 0
        while any(p > 0 for p in penalties()) and mined < 300:
            self.bump_mocktime(1)
            self.nodes[0].generate(1)
            mined += 1
        assert_equal(penalties(), [0] * len(self.mninfo))
        self.sync_all()
        self.nodes[0].spork("SPORK_17_QUORUM_DKG_ENABLED", 0)
        self.wait_for_sporks_same()


if __name__ == '__main__':
    LLMQDKGErrors().main()
