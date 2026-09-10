#!/usr/bin/env python3
# Copyright (c) 2015-2020 The Dash Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.messages import COutPoint, CTransaction, CTxIn, CTxOut, ToHex
from test_framework.mininode import COIN
from test_framework.script import CScript, OP_CAT
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, satoshi_round

'''
feature_dip0020_activation.py

This test checks activation of DIP0020 opcodes.

Upstream drives this with a BIP9 versionbits deployment. Raptoreum deleted
versionbits and gates the opcodes on its own miner-voted v17 update instead
(GetBlockScriptFlags sets SCRIPT_ENABLE_DIP0020_OPCODES when
Updates().IsActive(EUpdate::DEPLOYMENT_V17, pindex)). On regtest v17 locks in at
height 110 and activates at 210, and the cached chain starts at 200, so both
sides of the rule are reachable. The deployment status is read from
getblockchaininfo's rip1_softforks rather than from get_bip9_status.

This is a consensus rule and it still runs: every node syncing from genesis
replays the 427392 blocks before mainnet activation with the opcodes disabled.
'''

DISABLED_OPCODE_ERROR = "non-mandatory-script-verify-flag (Attempted to use a disabled opcode)"


class DIP0020ActivationTest(BitcoinTestFramework):
    def set_test_params(self):
        # Clean chain, unlike upstream: the shared cache starts at height 921,
        # past v17's activation at 210, so the disabled side is unreachable.
        self.setup_clean_chain = True
        self.num_nodes = 1

    def run_test(self):
        self.node = self.nodes[0]
        self.relayfee = satoshi_round(self.nodes[0].getnetworkinfo()["relayfee"])

        self.node.generate(105)  # COINBASE_MATURITY is 100
        assert self.node.getblockchaininfo()["rip1_softforks"]["v17"]["status"] != "active"

        # We should have some coins already
        utxos = self.node.listunspent()
        assert (len(utxos) > 0)

        # Send some coins to a P2SH address constructed using disabled opcodes
        utxo = utxos[len(utxos) - 1]
        value = int(satoshi_round(utxo["amount"] - self.relayfee) * COIN)
        tx = CTransaction()
        tx.vin.append(CTxIn(COutPoint(int(utxo["txid"], 16), utxo["vout"])))
        tx.vout.append(CTxOut(value, CScript([b'1', b'2', OP_CAT])))
        tx_signed_hex = self.node.signrawtransactionwithwallet(ToHex(tx))["hex"]
        txid = self.node.sendrawtransaction(tx_signed_hex)

        # This tx should be completely valid, should be included in mempool and mined in the next block
        assert (txid in set(self.node.getrawmempool()))
        self.node.generate(1)
        assert (txid not in set(self.node.getrawmempool()))

        # Create spending tx
        value = int(value - self.relayfee * COIN)
        tx0 = CTransaction()
        tx0.vin.append(CTxIn(COutPoint(int(txid, 16), 0)))
        tx0.vout.append(CTxOut(value, CScript([])))
        tx0.rehash()
        tx0_hex = ToHex(tx0)

        # This tx isn't valid yet
        assert self.node.getblockchaininfo()["rip1_softforks"]["v17"]["status"] != "active"
        assert_raises_rpc_error(-26, DISABLED_OPCODE_ERROR, self.node.sendrawtransaction, tx0_hex)

        # Generate enough blocks to activate DIP0020 opcodes
        self.activate_v17()
        assert_equal(self.node.getblockchaininfo()["rip1_softforks"]["v17"]["status"], "active")

        # Upstream needs one more block here because it mines to exactly the
        # activation height; activate_v17 batches and lands past it.

        # Should be spendable now
        tx0id = self.node.sendrawtransaction(tx0_hex)
        assert (tx0id in set(self.node.getrawmempool()))


if __name__ == '__main__':
    DIP0020ActivationTest().main()
