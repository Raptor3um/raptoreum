#!/usr/bin/env python3
# Copyright (c) 2020-2026 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Asset lifecycle: create, mint, send, update, and survive a restart.

Assets are a Raptoreum construct with no upstream equivalent, so this is
written from the RPC behaviour rather than ported.

Two gates have to be open. Assets ride on the Round Voting update, and every
create/update/mint refuses to run unless SPORK_22_SPECIAL_TX_FEE carries a
non-zero fee in its second byte.

The chain is built here rather than taken from the cache. listaddressesbyasset
and listassetbalancesbyaddress return a refusal string unless the node runs
with -assetindex, and switching that on over the cache demands a reindex the
framework does not wait for.
"""

from test_framework.test_framework import BitcoinTestFramework
from decimal import Decimal

from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
    connect_nodes,
    wait_until,
)

# Second byte is the fee in whole RTM, so 25600 asks 100 RTM per asset transaction.
SPORK22_FEE_100 = 25600
ASSET_FEE = 100
# Round Voting is active by 300, but the launch subsidy pays only 4 RTM a block
# until 720 and every asset transaction costs 100, so the chain is built past
# the subsidy step and far enough for those blocks to mature.
CHAIN_HEIGHT = 830
MINTED = 10000
COIN = 100000000


class AssetsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [
            ["-sporkkey=cVpnZj4dZvRXmBf7Jze1GjpLQb25iKP92GDXUsKdUJTXhXRo2RFA", "-assetindex"],
            ["-assetindex"],
        ]

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def mine(self, blocks=1):
        self.nodes[0].generate(blocks)
        self.sync_all()

    def run_test(self):
        node = self.nodes[0]
        self.owner = node.getnewaddress()

        self.log.info("Building a chain with Round Voting active and coins to spend")
        while node.getblockcount() < CHAIN_HEIGHT:
            node.generatetoaddress(min(100, CHAIN_HEIGHT - node.getblockcount()), self.owner)
        self.sync_all()

        self.test_gates()
        asset_id = self.test_create()
        self.test_mint(asset_id)
        self.test_send(asset_id)
        self.test_update(asset_id)
        self.test_only_the_owner_mints(asset_id)
        self.test_ownership_moves()
        self.test_listing_arguments()
        self.test_survives_restart(asset_id)

    def test_gates(self):
        node = self.nodes[0]

        self.log.info("Round Voting is active, so the asset RPCs are reachable")
        assert_equal(node.getblockchaininfo()["rip1_softforks"]["Round Voting"]["status"], "active")
        assert_equal(node.listassets(), {})

        self.log.info("Creating an asset is refused while the fee spork is off")
        assert_raises_rpc_error(-8, "Under maintenance", node.createasset, self.metadata("REFUSED"))

        self.log.info("Opening the fee spork")
        node.spork("SPORK_22_SPECIAL_TX_FEE", SPORK22_FEE_100)
        wait_until(lambda: all(n.spork("show")["SPORK_22_SPECIAL_TX_FEE"] == SPORK22_FEE_100
                               for n in self.nodes), timeout=30)

    def metadata(self, name, **overrides):
        meta = {
            "name": name,
            "updatable": True,
            "is_root": True,
            "is_unique": False,
            "decimalpoint": 2,
            "referenceHash": "",
            "maxMintCount": 10,
            "type": 0,
            "targetAddress": self.owner,
            "issueFrequency": 0,
            "amount": 10000,
            "ownerAddress": self.owner,
        }
        meta.update(overrides)
        return meta

    def test_create(self):
        node = self.nodes[0]

        self.log.info("Creating an asset")
        created = node.createasset(self.metadata("LIFECYCLE"))
        asset_id = created["txid"]
        assert_equal(created["Name"], "LIFECYCLE")
        assert_equal(created["fee"], ASSET_FEE)
        assert_equal(created["ownerAddress"], self.owner)

        self.log.info("It only appears once the creating transaction is mined")
        assert_equal(node.listassets(), {})
        self.mine()
        assert_equal(node.listassets(), {"LIFECYCLE": {"Asset_Id": asset_id}})

        self.log.info("Both lookups describe the same asset")
        by_name = node.getassetdetailsbyname("LIFECYCLE")
        assert_equal(by_name, node.getassetdetailsbyid(asset_id))
        assert_equal(by_name["Asset_id"], asset_id)
        assert_equal(by_name["Asset_name"], "LIFECYCLE")
        assert_equal(by_name["owner"], self.owner)
        assert_equal(by_name["Decimalpoint"], 2)
        assert_equal(by_name["maxMintCount"], 10)

        self.log.info("Nothing is in circulation before the first mint")
        assert_equal(by_name["Circulating_supply"], 0)
        assert_equal(by_name["MintCount"], 0)
        assert_equal(node.listassetsbalance(), {})

        self.log.info("Unknown assets are reported, not invented")
        assert_raises_rpc_error(-8, "", node.getassetdetailsbyname, "NOSUCHASSET")

        return asset_id

    def test_mint(self, asset_id):
        node = self.nodes[0]

        self.log.info("Minting issues the distribution amount to the target address")
        node.mintasset(asset_id)
        self.mine()

        details = node.getassetdetailsbyid(asset_id)
        assert_equal(details["MintCount"], 1)
        assert_equal(details["Circulating_supply"], MINTED)

        balances = node.listassetsbalance()
        assert_equal(balances["LIFECYCLE"]["Asset_Id"], asset_id)
        assert_equal(balances["LIFECYCLE"]["Balance"], MINTED)

        self.log.info("The minted coins are spendable outputs held by the owner")
        unspent = node.listunspentassets()
        assert_greater_than(len(unspent), 0)
        # listunspentassets reports raw amounts where listassetsbalance divides by COIN.
        assert_equal(sum(u["amount"] for u in unspent), MINTED * COIN)
        assert_equal({u["assetId"] for u in unspent}, {asset_id})

        # Both of these return the raw amount as a string: rpcassets.cpp calls
        # .str() with the ValueFromAmount conversion commented out beside it.
        by_address = node.listassetbalancesbyaddress(self.owner)
        assert_equal(by_address["LIFECYCLE"], str(MINTED * COIN))

        holders = node.listaddressesbyasset("LIFECYCLE")
        assert_equal(holders[self.owner], str(MINTED * COIN))

    def test_send(self, asset_id):
        node, other = self.nodes

        self.log.info("Sending part of the balance to the second wallet")
        recipient = other.getnewaddress()
        node.sendasset(asset_id, 25, recipient)
        self.mine()

        assert_equal(node.listassetsbalance()["LIFECYCLE"]["Balance"], MINTED - 25)
        assert_equal(other.listassetsbalance()["LIFECYCLE"]["Balance"], 25)

        self.log.info("The recipient shows up as a holder and the supply is unchanged")
        assert_equal(node.listassetbalancesbyaddress(recipient)["LIFECYCLE"], str(25 * COIN))
        holders = node.listaddressesbyasset("LIFECYCLE")
        assert_equal(holders[recipient], str(25 * COIN))
        assert_equal(sum(int(v) for v in holders.values()), MINTED * COIN)
        assert_equal(node.getassetdetailsbyid(asset_id)["Circulating_supply"], MINTED)

    def test_update(self, asset_id):
        node = self.nodes[0]

        self.log.info("Each updatable field takes on its new value")
        node.updateasset({"name": "LIFECYCLE", "referenceHash": "abc123"})
        self.mine()
        node.updateasset({"name": "LIFECYCLE", "maxMintCount": 25})
        self.mine()

        details = node.getassetdetailsbyid(asset_id)
        assert_equal(details["ReferenceHash"], "abc123")
        assert_equal(details["maxMintCount"], 25)

        self.log.info("Updating leaves the supply and the mint count alone")
        assert_equal(details["Circulating_supply"], MINTED)
        assert_equal(details["MintCount"], 1)

        self.log.info("Marking it immutable is itself an update, and the last one allowed")
        node.updateasset({"name": "LIFECYCLE", "updatable": False})
        self.mine()
        assert_equal(node.getassetdetailsbyid(asset_id)["Updatable"], False)
        assert_raises_rpc_error(-8, "", node.updateasset,
                                {"name": "LIFECYCLE", "referenceHash": "deadbeef"})

    def test_ownership_moves(self):
        """Changing ownerAddress hands over the right to mint.

        Ownership lives in the metadata rather than in a token, so an update is
        all it takes, and the old owner loses the asset with it.
        """
        node, other = self.nodes

        self.log.info("An asset whose owner is moved to the second wallet")
        moved = node.createasset(self.metadata("HANDOVER"))["txid"]
        self.mine()
        node.mintasset(moved)
        self.mine()
        assert_equal(node.getassetdetailsbyid(moved)["MintCount"], 1)

        recipient = other.getnewaddress()
        node.updateasset({"name": "HANDOVER", "ownerAddress": recipient})
        self.mine()
        assert_equal(node.getassetdetailsbyid(moved)["owner"], recipient)

        self.log.info("The former owner can no longer mint it")
        assert_raises_rpc_error(-32603, "Asset owner key not in wallet", node.mintasset, moved)

        self.log.info("A sub-asset under a root you do not own is refused too")
        assert_raises_rpc_error(-32603, "Root asset key not in wallet", node.createasset,
                                self.metadata("child", is_root=False, root_name="HANDOVER"))

    def test_listing_arguments(self):
        node = self.nodes[0]

        self.log.info("listassets pages through the assets, count then start")
        every = set(node.listassets().keys())
        assert_greater_than(len(every), 1)

        # The listing has its own order rather than an alphabetical one, so page
        # by page the pieces have to fit together rather than match a sort.
        first = set(node.listassets(False, 1))
        second = set(node.listassets(False, 1, 1))
        assert_equal(len(first), 1)
        assert_equal(len(second), 1)
        assert_equal(first & second, set())
        assert first | second <= every

        self.log.info("The verbose form carries the details the summary omits")
        verbose = node.listassets(True)
        assert_equal(set(verbose.keys()), every)
        assert_equal(verbose["LIFECYCLE"]["Circulating_supply"], MINTED)
        assert "Circulating_supply" not in node.listassets()["LIFECYCLE"]

        self.log.info("The address views can answer with a count instead of a list")
        holders = node.listaddressesbyasset("LIFECYCLE")
        assert_equal(node.listaddressesbyasset("LIFECYCLE", True), len(holders))
        balances = node.listassetbalancesbyaddress(self.owner)
        assert_equal(node.listassetbalancesbyaddress(self.owner, True), len(balances))

    def test_only_the_owner_mints(self, asset_id):
        """Minting is gated on a signature from the owner address.

        CheckMintAssetTx verifies a message signed by the owner, so holding a
        balance in the asset is not enough. The second wallet holds 25 of it and
        still cannot mint, and the refusal comes from the wallet rather than the
        network, so no phantom transaction is left behind.
        """
        node, other = self.nodes

        # The fee check runs before the ownership check, so the second wallet
        # needs coins of its own or the refusal is about funds instead.
        node.sendtoaddress(other.getnewaddress(), 500)
        self.mine()
        self.mine()

        self.log.info("A holder who does not own the asset cannot mint it")
        assert_equal(other.listassetsbalance()["LIFECYCLE"]["Balance"], 25)
        assert_raises_rpc_error(-32603, "Asset owner key not in wallet", other.mintasset, asset_id)

        before = node.getassetdetailsbyid(asset_id)["MintCount"]
        self.mine()
        assert_equal(node.getassetdetailsbyid(asset_id)["MintCount"], before)

    def test_survives_restart(self, asset_id):
        node = self.nodes[0]

        self.log.info("The asset database survives a restart")
        before = node.getassetdetailsbyid(asset_id)
        every_asset = set(node.listassets())
        self.stop_node(0)
        self.start_node(0)
        connect_nodes(self.nodes[0], 1)
        wait_until(lambda: self.nodes[0].getblockcount() == self.nodes[1].getblockcount(), timeout=30)

        node = self.nodes[0]
        assert_equal(node.getassetdetailsbyid(asset_id), before)
        assert_equal(node.listassets()["LIFECYCLE"], {"Asset_Id": asset_id})
        assert_equal(set(node.listassets()), every_asset)


if __name__ == '__main__':
    AssetsTest().main()
