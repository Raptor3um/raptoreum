#!/usr/bin/env python3
# Copyright (c) 2020-2026 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""What createasset and mintasset refuse, and what they quietly accept.

Companion to feature_assets.py, which walks the happy path. This one covers
the rules: naming, metadata bounds, sub-assets, unique assets, duplicate
detection in both the chain and the mempool, and the mint ceiling.

One of these pins behaviour that contradicts the RPC's own help: the unique
flag is read under a different name than the help documents. It is marked
where it appears and is meant to fail loudly if the two are reconciled.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    wait_until,
)

SPORK22_FEE_100 = 25600
# Round Voting is active by 300, but the launch subsidy only pays 4 RTM a block
# until 720. Each asset transaction costs 100, so the chain is built past the
# subsidy step and far enough for those blocks to mature.
CHAIN_HEIGHT = 830


class AssetsRulesTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [[
            "-sporkkey=cVpnZj4dZvRXmBf7Jze1GjpLQb25iKP92GDXUsKdUJTXhXRo2RFA",
            "-assetindex",
        ]]

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

    def mine(self, blocks=1):
        self.nodes[0].generate(blocks)
        self.nodes[0].getwalletinfo()

    def run_test(self):
        node = self.nodes[0]
        self.owner = node.getnewaddress()

        self.log.info("Building a chain with Round Voting active and coins to spend")
        while node.getblockcount() < CHAIN_HEIGHT:
            node.generatetoaddress(min(100, CHAIN_HEIGHT - node.getblockcount()), self.owner)
        assert_equal(node.getblockchaininfo()["rip1_softforks"]["Round Voting"]["status"], "active")
        node.spork("SPORK_22_SPECIAL_TX_FEE", SPORK22_FEE_100)
        wait_until(lambda: node.spork("show")["SPORK_22_SPECIAL_TX_FEE"] == SPORK22_FEE_100, timeout=30)

        self.test_root_names()
        self.test_metadata_bounds()
        self.test_unique()
        self.test_sub_assets()
        self.test_duplicates()
        self.test_mint_ceiling()
        self.test_distribution_types()
        self.test_reorg()

    def test_root_names(self):
        node = self.nodes[0]

        self.log.info("Root names are upper case, at least three characters, no spaces")
        for name in ("lowercase", "AB", "BAD NAME", "Mixed"):
            assert_raises_rpc_error(-8, "Invalid asset name", node.createasset, self.metadata(name))

        self.log.info("The chain's own tickers are reserved")
        for name in ("RTM", "RAPTOREUM", "WRTM", "RTMCOIN"):
            assert_raises_rpc_error(-8, "Invalid asset name", node.createasset, self.metadata(name))

        self.log.info("Digits, dots and underscores are allowed")
        node.createasset(self.metadata("A.B_1"))
        self.mine()
        assert "A.B_1" in node.listassets()

    def test_metadata_bounds(self):
        node = self.nodes[0]

        self.log.info("Decimal point is limited to 0 through 8")
        assert_raises_rpc_error(-8, "decimalpoint out off range", node.createasset,
                                self.metadata("DPNINE", decimalpoint=9))

        self.log.info("Mint count must fit in two bytes and be non-zero")
        for bad in (0, 0x10000):
            assert_raises_rpc_error(-3, "Invalid maxMintCount", node.createasset,
                                    self.metadata("MAXBAD", maxMintCount=bad))

        self.log.info("Amount must be positive")
        assert_raises_rpc_error(-3, "Invalid amount", node.createasset,
                                self.metadata("AMTZERO", amount=0))

        self.log.info("Reference hash is capped at 128 characters")
        node.createasset(self.metadata("REF.OK", referenceHash="x" * 128))
        assert_raises_rpc_error(-8, "Invalid referenceHash", node.createasset,
                                self.metadata("REFLONG", referenceHash="x" * 129))
        self.mine()

    def test_unique(self):
        node = self.nodes[0]

        self.log.info("A unique asset is forced to zero decimals and made immutable")
        created = node.createasset(self.metadata("UNIQUE.REAL", isunique=True, decimalpoint=2))
        assert_equal(created["Isunique"], True)
        assert_equal(created["Decimalpoint"], 0)
        assert_equal(created["Updatable"], False)

        # createasset reads "isunique"; its own argument list documents
        # "is_unique", which is therefore ignored. Asking for a unique asset the
        # documented way silently produces an ordinary divisible one.
        self.log.info("The documented is_unique spelling is ignored")
        created = node.createasset(self.metadata("UNIQUE.DOC", is_unique=True))
        assert_equal(created["Isunique"], False)
        assert_equal(created["Decimalpoint"], 2)

        # The help mentions a cap of 500, but scoped to "if type is not manual",
        # and non-manual distribution is refused at consensus, so the cap governs
        # a path nothing can reach. No check exists; none is reachable either.
        self.log.info("A unique asset takes an amount above the figure in the help")
        created = node.createasset(self.metadata("UNIQUE.BIG", isunique=True, amount=600))
        assert_equal(created["Isunique"], True)
        self.mine()

        # "Unique" means indivisible rather than singular: the decimal point is
        # forced to zero, but the distribution amount is whatever was asked for,
        # so a mint issues that many whole tokens and they move independently.
        self.log.info("Minting a unique asset issues its amount as whole tokens")
        unit_id = node.createasset(self.metadata("UNIQUE.UNIT", isunique=True, amount=3))["txid"]
        self.mine()
        node.mintasset(unit_id)
        self.mine()

        details = node.getassetdetailsbyid(unit_id)
        assert_equal(details["MintCount"], 1)
        assert_equal(details["Circulating_supply"], 3)
        assert_equal(details["Decimalpoint"], 0)
        assert_equal(node.listassetsbalance()["UNIQUE.UNIT"]["Balance"], 3)

        self.log.info("They transfer one at a time like any other holding")
        node.sendasset(unit_id, 1, node.getnewaddress())
        self.mine()
        assert_equal(node.listassetsbalance()["UNIQUE.UNIT"]["Balance"], 3)
        assert_equal(node.getassetdetailsbyid(unit_id)["Circulating_supply"], 3)

        self.log.info("A unique asset is immutable, so it cannot be updated")
        assert_raises_rpc_error(-8, "", node.updateasset,
                                {"name": "UNIQUE.UNIT", "referenceHash": "abc"})

    def test_sub_assets(self):
        node = self.nodes[0]

        self.log.info("A sub-asset has to name its root")
        assert_raises_rpc_error(-8, "Root asset not found", node.createasset,
                                self.metadata("orphan", is_root=False))

        self.log.info("An unknown root is refused")
        assert_raises_rpc_error(-8, "Root asset", node.createasset,
                                self.metadata("stray", is_root=False, root_name="NOSUCHROOT"))

        self.log.info("Sub-asset names are looser than root names and allow lower case")
        node.createasset(self.metadata("ROOTA"))
        self.mine()
        node.createasset(self.metadata("subone", is_root=False, root_name="ROOTA"))
        self.mine()

        self.log.info("The stored name is the root and the child joined by a pipe")
        assert "ROOTA|subone" in node.listassets()
        details = node.getassetdetailsbyname("ROOTA|subone")
        assert_equal(details["Asset_name"], "ROOTA|subone")

    def test_duplicates(self):
        node = self.nodes[0]

        self.log.info("A name already on chain cannot be taken again")
        assert_raises_rpc_error(-8, "Asset already exist", node.createasset, self.metadata("ROOTA"))

        self.log.info("Nor can one that is only sitting in the mempool")
        node.createasset(self.metadata("PENDING"))
        assert_raises_rpc_error(-8, "Asset already exist on mempool", node.createasset,
                                self.metadata("PENDING"))
        self.mine()
        assert "PENDING" in node.listassets()

    def test_mint_ceiling(self):
        node = self.nodes[0]

        self.log.info("Minting stops at maxMintCount")
        asset_id = node.createasset(self.metadata("LIMIT.ONE", maxMintCount=1))["txid"]
        self.mine()

        node.mintasset(asset_id)
        self.log.info("A second mint is refused while the first is still in the mempool")
        assert_raises_rpc_error(-8, "Asset mint or update tx exist on mempool",
                                node.mintasset, asset_id)
        self.mine()

        assert_equal(node.getassetdetailsbyid(asset_id)["MintCount"], 1)
        assert_raises_rpc_error(-8, "max mint count reached", node.mintasset, asset_id)

    def test_distribution_types(self):
        """Only manual distribution actually works.

        createasset takes a type of 0 through 3 and reports the name back, but
        CheckNewAssetTx refuses any type other than 0 that carries no collateral
        address, and createasset has no parameter to supply one. So the RPC
        answers with a txid and full metadata for a transaction the network
        drops, and the asset never exists.

        The bounds check beside it, at providertx.cpp, reads
        `type < 0 && type > 3` where it means `||`, so it can never fire and a
        nonsense type reaches the payload unchallenged.
        """
        node = self.nodes[0]

        for dtype, label in ((1, "coinbase"), (2, "address"), (3, "schedule")):
            self.log.info("Distribution type %d is accepted by the RPC and dropped by the node", dtype)
            # The node refuses it as the wallet submits it, not when mining.
            with node.assert_debug_log(["bad-assets-collateralAddress"]):
                created = node.createasset(self.metadata("DIST%d" % dtype, type=dtype, issueFrequency=10))
            assert_equal(created["Distribution"]["Type"], label)

            self.mine()
            assert ("DIST%d" % dtype) not in node.listassets()
            assert_equal(node.gettransaction(created["txid"])["confirmations"], 0)

        self.log.info("An out-of-range type is not rejected either, only unminted")
        created = node.createasset(self.metadata("DIST99", type=99))
        assert_equal(created["Distribution"]["Type"], "invalid")
        self.mine()
        assert "DIST99" not in node.listassets()

    def test_reorg(self):
        node = self.nodes[0]

        self.log.info("An asset disappears when its block is disconnected")
        node.createasset(self.metadata("REORGME"))
        self.mine()
        assert "REORGME" in node.listassets()

        blockhash = node.getbestblockhash()
        node.invalidateblock(blockhash)
        node.getwalletinfo()
        assert "REORGME" not in node.listassets()

        self.log.info("And comes back when the block is reconsidered")
        node.reconsiderblock(blockhash)
        node.getwalletinfo()
        assert_equal(node.getbestblockhash(), blockhash)
        assert "REORGME" in node.listassets()


if __name__ == '__main__':
    AssetsRulesTest().main()
