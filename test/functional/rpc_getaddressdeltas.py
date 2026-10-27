#!/usr/bin/env python3
# Copyright (c) 2026 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test getaddressdeltas timestamps, ordering and pagination."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, wait_until


class GetAddressDeltasTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2
        self.extra_args = [["-sporkkey=cVpnZj4dZvRXmBf7Jze1GjpLQb25iKP92GDXUsKdUJTXhXRo2RFA"],
                           ["-addressindex=1"]]

    def check_deltas(self, query):
        node = self.nodes[1]
        deltas = node.getaddressdeltas(query)
        keys = [(delta["height"], delta["blockindex"]) for delta in deltas]
        assert_equal(keys, sorted(keys))
        for delta in deltas:
            block = node.getblock(node.getblockhash(delta["height"]))
            assert_equal(delta["timestamp"], block["time"])
            assert_equal(delta["blockindex"], block["tx"].index(delta["txid"]))
        pages = []
        for skip in range(0, len(deltas), 2):
            pages.extend(node.getaddressdeltas(dict(query, skip=skip, count=2)))
        assert_equal(pages, deltas)
        assert_equal(node.getaddressdeltas(dict(query, skip=1, count=0)), deltas[1:])
        assert_equal(node.getaddressdeltas(dict(query, skip=len(deltas), count=1)), [])
        assert_equal(node.getaddressdeltas(dict(query, skip=len(deltas) + 1)), [])
        for field in ("skip", "count"):
            assert_raises_rpc_error(-8, field + " must be a non-negative integer",
                                    node.getaddressdeltas, dict(query, **{field: -1}))
        return deltas

    def check_append_and_reorg(self, query, address, mining_address):
        miner, node = self.nodes
        before = self.check_deltas(query)
        first_page = node.getaddressdeltas(dict(query, count=2))
        txid = miner.sendtoaddress(address, 0.25)
        self.bump_mocktime(600)
        new_hash = miner.generatetoaddress(1, mining_address)[0]
        self.sync_all()
        after = self.check_deltas(query)
        assert_equal(after[:-1], before)
        assert_equal(after[-1]["txid"], txid)
        assert_equal(first_page + node.getaddressdeltas(dict(query, skip=2)), after)

        for peer in self.nodes:
            peer.invalidateblock(new_hash)
        self.sync_all()
        assert_equal(node.getaddressdeltas(query), before)
        # Remine the disconnected transaction at the same height with a new time.
        self.bump_mocktime(600)
        replacement = miner.generatetoaddress(2, mining_address)[0]
        assert replacement != new_hash
        self.sync_all()
        reorganized = self.check_deltas(query)
        assert_equal(reorganized[:-1], before)
        assert_equal(reorganized[-1]["txid"], txid)
        assert reorganized[-1]["timestamp"] != after[-1]["timestamp"]

    def run_test(self):
        miner, node = self.nodes
        mining_address = miner.getnewaddress()
        miner.generatetoaddress(105, mining_address)
        addresses = [node.getnewaddress(), node.getnewaddress()]
        empty_address = node.getnewaddress()

        self.log.info("Checking RTM timestamps, both address orders and stable ties")
        for recipients in (addresses, list(reversed(addresses))):
            for address in recipients:
                miner.sendtoaddress(address, 0.25)
            self.bump_mocktime(600)
            miner.generatetoaddress(1, mining_address)
        # More than 16 entries avoid std::sort's small-range insertion sort.
        tie_addresses = addresses + [node.getnewaddress() for _ in range(18)]
        shared_txid = miner.sendmany("", {address: 0.25 for address in tie_addresses})
        self.bump_mocktime(600)
        miner.generatetoaddress(1, mining_address)
        self.sync_all()
        for requested in ([addresses[0]], [addresses[1]], addresses, list(reversed(addresses))):
            deltas = self.check_deltas({"addresses": requested})
            assert_equal(len(deltas), 3 * len(requested))
            assert_equal([delta["address"] for delta in deltas if delta["txid"] == shared_txid], requested)
        for requested in (tie_addresses, list(reversed(tie_addresses))):
            deltas = self.check_deltas({"addresses": requested})
            assert_equal(len(deltas), 24)
            assert_equal([delta["address"] for delta in deltas if delta["txid"] == shared_txid], requested)
        assert_equal(node.getaddressdeltas({"addresses": [empty_address]}), [])
        self.check_append_and_reorg({"addresses": list(reversed(addresses))}, addresses[0], mining_address)

        self.log.info("Checking interleaved RTM and asset deltas for one address")
        # Activate round voting and keep the difficulty low as the chain grows.
        while miner.getblockcount() < 310:
            self.bump_mocktime(6000)
            miner.generatetoaddress(min(10, 310 - miner.getblockcount()), mining_address)
        self.sync_all()
        miner.spork("SPORK_22_SPECIAL_TX_FEE", 256)
        wait_until(lambda: node.spork("show")["SPORK_22_SPECIAL_TX_FEE"] == 256)
        asset_address = node.getnewaddress()
        asset_id = miner.createasset({
            "name": "DELTA_TEST", "is_root": True, "decimalpoint": 0,
            "maxMintCount": 2, "amount": 10, "targetAddress": asset_address,
            "ownerAddress": mining_address,
        })["txid"]
        miner.generatetoaddress(1, mining_address)
        mint_txid = miner.mintasset(asset_id)["txid"]
        miner.generatetoaddress(1, mining_address)
        # A later RTM delta sorts before the earlier asset delta in the raw index.
        rtm_txid = miner.sendtoaddress(asset_address, 0.25)
        miner.generatetoaddress(1, mining_address)
        self.sync_all()
        query = {"addresses": [asset_address], "asset": "*"}
        deltas = self.check_deltas(query)
        assert_equal([delta["txid"] for delta in deltas], [mint_txid, rtm_txid])
        assert_equal(deltas[0]["asset"], "DELTA_TEST")
        assert_equal(deltas[0]["assetId"], asset_id)
        assert_equal(self.check_deltas(dict(query, asset="DELTA_TEST")), deltas[:1])
        assert_equal(self.check_deltas({"addresses": [asset_address]}), deltas[1:])
        self.check_append_and_reorg(query, asset_address, mining_address)


if __name__ == '__main__':
    GetAddressDeltasTest().main()
