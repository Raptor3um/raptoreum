#!/usr/bin/env python3
# Copyright (c) 2026 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise real transaction acceptance with a populated asset cache.

--asset-count=2500 supplies a larger workload without making the default suite
depend on a throughput target. Timings include RPC overhead and are not TPS
capacity measurements. Provider and future lifecycles have separate tests.
"""
from decimal import Decimal
import time

from feature_assets import ASSET_FEE, AssetsTest
from test_framework.messages import CTransaction, FromHex, ToHex
from test_framework.util import assert_equal, assert_raises_rpc_error


class AssetCacheAcceptanceTest(AssetsTest):
    def add_options(self, parser):
        parser.add_option("--asset-count", type="int", default=25)

    def run_test(self):
        super().run_test()
        node = self.nodes[0]
        assert 0 < self.options.asset_count <= 100000
        while node.getbalance() < ASSET_FEE * (self.options.asset_count + 3) + 1:
            self.mine(100)
        started = time.monotonic()
        for index in range(self.options.asset_count):
            node.createasset(self.metadata("CACHE" + str(index)))
            if (index + 1) % 10 == 0:
                self.mine()
        self.mine()
        expected_assets = {"CACHE" + str(index) for index in range(self.options.asset_count)}
        for peer in self.nodes:
            assert expected_assets <= peer.listassets(False, 100000).keys()
        self.log.info("Created and confirmed %d additional assets in %.3fs",
                      self.options.asset_count, time.monotonic() - started)

        self.log.info("All three special asset paths still validate with the populated cache")
        asset_id = node.createasset(self.metadata("CACHECONTROL"))["txid"]
        assert "CACHECONTROL" not in node.listassets(False, 100000)
        self.mine()
        node.mintasset(asset_id)
        assert_equal(node.getassetdetailsbyid(asset_id)["MintCount"], 0)
        self.mine()
        assert_equal(node.getassetdetailsbyid(asset_id)["MintCount"], 1)
        node.updateasset({"name": "CACHECONTROL", "referenceHash": "updated"})
        assert_equal(node.getassetdetailsbyid(asset_id)["ReferenceHash"], "")
        self.mine()
        assert_equal(node.getassetdetailsbyid(asset_id)["ReferenceHash"], "updated")
        snapshot = node.getassetdetailsbyid(asset_id)

        for input_count, output_count in ((1, 1), (10, 10)):
            self.log.info("Accept, relay and mine a %d-input/%d-output payment",
                          input_count, output_count)
            coins = [coin for coin in node.listunspent(1)
                     if coin["spendable"] and coin["amount"] > 0][:input_count]
            assert_equal(len(coins), input_count)
            total = sum(coin["amount"] for coin in coins)
            inputs = [{"txid": coin["txid"], "vout": coin["vout"]} for coin in coins]
            each = ((total - Decimal("0.01")) / output_count).quantize(Decimal("0.00000001"))
            outputs = {node.getnewaddress(): each for _ in range(output_count)}
            signed = node.signrawtransactionwithwallet(node.createrawtransaction(inputs, outputs))
            assert signed["complete"]
            transaction = FromHex(CTransaction(), signed["hex"])
            transaction.vin[0].scriptSig = b""
            before = set(node.getrawmempool())
            assert_raises_rpc_error(-26, "", node.sendrawtransaction, ToHex(transaction), 0)
            assert_equal(set(node.getrawmempool()), before)
            started = time.monotonic()
            txid = node.sendrawtransaction(signed["hex"], 0)
            self.sync_mempools()
            self.log.info("RPC acceptance and two-node relay took %.6fs", time.monotonic() - started)
            assert all(txid in peer.getrawmempool() for peer in self.nodes)
            self.mine()
            assert txid in node.getblock(node.getbestblockhash())["tx"]
            assert_equal(node.getassetdetailsbyid(asset_id), snapshot)


if __name__ == "__main__":
    AssetCacheAcceptanceTest().main()
