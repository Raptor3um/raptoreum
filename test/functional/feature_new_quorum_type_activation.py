#!/usr/bin/env python3
# Copyright (c) 2020 The Dash Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

'''
feature_new_quorum_type_activation.py

Tests the activation of a new quorum type at the v17 deployment.

Upstream drives this with a BIP9 versionbits deployment and -vbparams.
Raptoreum deleted versionbits and uses its own miner-voted update instead, so
the deployment state is read from getblockchaininfo's rip1_softforks and the
chain is simply mined past the activation height (210 on regtest, locked in at
110). The substance is unchanged: llmq_test_v17 must be absent from
quorum list while the deployment is inactive and present once it activates,
which is what CLLMQUtils::IsQuorumTypeEnabled gates.
'''


class NewQuorumTypeActivationTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def run_test(self):
        node = self.nodes[0]

        def status():
            return node.getblockchaininfo()["rip1_softforks"]["v17"]["status"]

        assert status() != "active"
        ql = node.quorum("list")
        assert_equal(len(ql), 1)
        assert ("llmq_test_v17" not in ql)

        # Still inactive partway up, and the type is still hidden
        node.generate(100)
        assert status() != "active"
        ql = node.quorum("list")
        assert_equal(len(ql), 1)
        assert ("llmq_test_v17" not in ql)

        self.activate_v17()
        assert_equal(status(), "active")
        ql = node.quorum("list")
        assert_equal(len(ql), 2)
        assert ("llmq_test_v17" in ql)


if __name__ == '__main__':
    NewQuorumTypeActivationTest().main()
