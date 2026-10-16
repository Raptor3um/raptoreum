#!/usr/bin/env python3
# Copyright (c) 2026 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Acceptance coverage across every transaction category CheckSpecialTx dispatches.

Companion to bench/assets_cache_copy.cpp, which measures the performance
difference this file does not need to: it proves the conditional asset-cache
copy in AcceptToMemoryPoolWorker (validation.cpp, gated by TxNeedsAssetsCache)
does not change acceptance outcomes for any of the five categories -- ordinary
payments, futures, provider registrations, and all three asset transaction
types. The one rejection case here (an invalid asset name) is an RPC-level
parameter check, not a mempool-level rejection -- every asset RPC validates
its own inputs before building a transaction at all, so a genuine
AcceptToMemoryPool-level rejection would need a raw, hand-built transaction
via sendrawtransaction, which this file does not attempt.

The fDryRun path is exercised directly by src/test/txvalidation_tests.cpp
instead: no RPC anywhere in this codebase calls AcceptToMemoryPool with
fDryRun=true except CCoinJoin::IsCollateralValid, on the live CoinJoin
session/collateral path, which nothing here exercises.
"""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    p2p_port,
    wait_until,
)

SPORK22_FEE_100 = 25600
# Round Voting is active by 300, but the launch subsidy only pays 4 RTM a block
# until 720, and each asset transaction costs 100, so the chain is built past
# the subsidy step and far enough for those blocks to mature.
CHAIN_HEIGHT = 830
# src/chainparams.cpp CRegTestParams: nCollaterals = 10 RTM on regtest (not the
# mainnet/testnet value some other test helpers assume).
SMARTNODE_COLLATERAL = Decimal(10)
ASSET_NAME = "ATMPCOPY"


class AssetsCacheCopyAtmpTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-sporkkey=cVpnZj4dZvRXmBf7Jze1GjpLQb25iKP92GDXUsKdUJTXhXRo2RFA"]]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def metadata(self, name, **overrides):
        meta = {
            "name": name,
            "updatable": True,
            "is_root": True,
            "decimalpoint": 2,
            "referenceHash": "",
            "maxMintCount": 10,
            "type": 0,
            "targetAddress": self.owner,
            "issueFrequency": 0,
            "amount": 100,
            "ownerAddress": self.owner,
        }
        meta.update(overrides)
        return meta

    def run_test(self):
        node = self.nodes[0]
        self.owner = node.getnewaddress()

        self.log.info("Building a chain with Round Voting active and coins to spend")
        while node.getblockcount() < CHAIN_HEIGHT:
            node.generatetoaddress(min(100, CHAIN_HEIGHT - node.getblockcount()), self.owner)
        assert_equal(node.getblockchaininfo()["rip1_softforks"]["Round Voting"]["status"], "active")

        node.spork("SPORK_22_SPECIAL_TX_FEE", SPORK22_FEE_100)
        wait_until(lambda: node.spork("show")["SPORK_22_SPECIAL_TX_FEE"] == SPORK22_FEE_100, timeout=30)

        self.test_ordinary_tx()
        self.test_future_tx()
        self.test_provider_tx()
        asset_id = self.test_new_asset_tx()
        self.test_mint_asset_tx(asset_id)
        self.test_update_asset_tx()
        self.test_rejected_asset_tx()

    def test_ordinary_tx(self):
        node = self.nodes[0]
        self.log.info("An ordinary payment (TRANSACTION_NORMAL) is accepted")
        txid = node.sendtoaddress(node.getnewaddress(), 1)
        assert txid in node.getrawmempool(), "ordinary payment was not accepted"
        node.generatetoaddress(1, self.owner)

    def test_future_tx(self):
        node = self.nodes[0]
        self.log.info("A future transaction (TRANSACTION_FUTURE) is accepted")
        future_maturity = 3
        txid = node.sendtoaddress(
            address=node.getnewaddress(),
            amount=1,
            future={"future_maturity": future_maturity, "future_locktime": -1},
        )
        assert txid in node.getrawmempool(), "future transaction was not accepted"
        # Mature the locked output fully before moving on. Left at 1 confirmation, ordinary
        # coin selection (sendtoaddress, in a later test) can still pick this output despite
        # CWalletTx::isFutureSpendable existing precisely to flag it as not yet spendable --
        # a real, separate wallet bug, hit here empirically (~40-50% of runs) as an
        # intermittent bad-txns-premature-spend-of-future rejection in test_provider_tx's
        # unrelated funding transaction. Not this PR's to fix; avoid it here instead.
        node.generatetoaddress(future_maturity, self.owner)

    def test_provider_tx(self):
        node = self.nodes[0]
        self.log.info("A provider registration (TRANSACTION_PROVIDER_REGISTER) is accepted")
        address = node.getnewaddress()
        fund_txid = node.sendtoaddress(address, SMARTNODE_COLLATERAL + Decimal("0.01"))
        assert fund_txid in node.getrawmempool(), "provider collateral funding was not accepted"
        node.generatetoaddress(1, self.owner)

        bls = node.bls("generate")
        owner = node.getnewaddress()
        voting = node.getnewaddress()
        payout = node.getnewaddress()
        # register_fund funds, creates and submits the ProTx in one call; this node's
        # own IP:port is irrelevant since nothing here actually starts a smartnode.
        txid = node.protx(
            "register_fund", address, SMARTNODE_COLLATERAL,
            "127.0.0.1:%d" % p2p_port(1), owner, bls["public"], voting, 0, payout, address,
        )
        assert txid in node.getrawmempool(), "provider registration was not accepted"
        node.generatetoaddress(1, self.owner)

    def test_new_asset_tx(self):
        node = self.nodes[0]
        self.log.info("Creating an asset (TRANSACTION_NEW_ASSET) is accepted")
        created = node.createasset(self.metadata(ASSET_NAME))
        asset_id = created["txid"]
        assert asset_id in node.getrawmempool(), "asset creation was not accepted"
        node.generatetoaddress(1, self.owner)
        return asset_id

    def test_mint_asset_tx(self, asset_id):
        node = self.nodes[0]
        self.log.info("Minting an asset (TRANSACTION_MINT_ASSET) is accepted")
        txid = node.mintasset(asset_id)["txid"]
        assert txid in node.getrawmempool(), "asset mint was not accepted"
        node.generatetoaddress(1, self.owner)

    def test_update_asset_tx(self):
        node = self.nodes[0]
        self.log.info("Updating an asset (TRANSACTION_UPDATE_ASSET) is accepted")
        txid = node.updateasset({"name": ASSET_NAME, "referenceHash": "abc123"})["txid"]
        assert txid in node.getrawmempool(), "asset update was not accepted"
        node.generatetoaddress(1, self.owner)
        assert_equal(node.getassetdetailsbyname(ASSET_NAME)["ReferenceHash"], "abc123")

    def test_rejected_asset_tx(self):
        node = self.nodes[0]
        self.log.info("An invalid asset creation is refused at the RPC layer, not silently accepted")
        assert_raises_rpc_error(-8, "Invalid asset name", node.createasset, self.metadata("lowercase"))


if __name__ == '__main__':
    AssetsCacheCopyAtmpTest().main()
