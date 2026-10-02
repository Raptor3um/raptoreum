#!/usr/bin/env python3
# Copyright (c) 2026 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Reproducible local RPC/relay/block samples, not a saturation TPS benchmark.

Run alone for measurements. Repeated samples include RPC and polling overhead;
payment signing/funding and asset-cache population are outside measured stages.
Asset RPC admission also includes wallet construction/signing. Every workload
must reach both mempools and the mined block before it becomes a result.
"""
from decimal import Decimal
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import time

from feature_assets import ASSET_FEE, AssetsTest, CHAIN_HEIGHT
from test_framework.messages import CTransaction, FromHex, ToHex
from test_framework.util import assert_equal, assert_raises_rpc_error, sync_blocks, sync_mempools


class DecouplingProfile(AssetsTest):
    def add_options(self, parser):
        parser.add_option("--profile-count", type="int", default=20)
        parser.add_option("--profile-repetitions", type="int", default=3)
        parser.add_option("--profile-assets", type="int", default=25)
        parser.add_option("--profile-output", help="Write verified samples to this JSON file")
        parser.add_option("--profile-transport-node", type="int", action="append", default=[],
                          help="Enable -txdecoupling=1 on this node index (repeatable). With one of "
                               "two nodes, the pair falls back to ordinary relay; enable both to "
                               "exchange reference blocks")

    def setup_nodes(self):
        # Transport on a subset of nodes checks that old and new nodes agree.
        for index in self.options.profile_transport_node:
            assert 0 <= index < self.num_nodes
            self.extra_args[index] = self.extra_args[index] + ["-txdecoupling=1"]
        super().setup_nodes()

    def resources(self):
        result = []
        for node in self.nodes:
            snapshot = {"net": node.getnettotals(), "mempool": node.getmempoolinfo()}
            proc = Path("/proc") / str(node.process.pid)
            if proc.is_dir():
                fields = (proc / "stat").read_text().rsplit(")", 1)[1].split()
                snapshot["cpu_seconds"] = (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")
                snapshot["rss_bytes"] = int((proc / "statm").read_text().split()[1]) * os.sysconf("SC_PAGE_SIZE")
            result.append(snapshot)
        return result

    def measure(self, name, repetition, operations, expected=None):
        assert operations
        assert all(not peer.getrawmempool() for peer in self.nodes)
        before = self.resources()
        started = time.monotonic()
        txids = [operation() for operation in operations]
        admitted_at = time.monotonic()
        assert_equal(len(set(txids)), len(operations))
        if expected is not None:
            assert_equal(set(txids), set(expected))
        assert_equal(set(self.nodes[0].getrawmempool()), set(txids))
        sync_mempools(self.nodes, wait=0.01, timeout=120)
        relayed_at = time.monotonic()
        for peer in self.nodes:
            assert_equal(set(peer.getrawmempool()), set(txids))
        relay_resources = self.resources()
        byte_count = sum(len(self.nodes[0].getrawtransaction(txid)) // 2 for txid in txids)
        mining_started = time.monotonic()
        block_hash = self.nodes[0].generate(1)[0]
        mined_at = time.monotonic()
        sync_blocks(self.nodes, wait=0.01, timeout=120)
        connected_at = time.monotonic()
        for peer in self.nodes:
            assert set(txids) <= set(peer.getblock(block_hash)["tx"])
            assert not set(txids) & set(peer.getrawmempool())
        sample = {
            "workload": name, "repetition": repetition,
            "offered": len(operations), "admitted": len(txids),
            "relayed": len(txids), "mined": len(txids), "txids": txids,
            "transaction_bytes": byte_count,
            "block_bytes": self.nodes[0].getblock(block_hash)["size"],
            "seconds": {"admission_rpc": admitted_at - started,
                        "remaining_relay": relayed_at - admitted_at,
                        "mining_rpc": mined_at - mining_started,
                        "remaining_block_connection": connected_at - mined_at},
            "resources": {"before": before, "after_relay": relay_resources,
                          "after_block": self.resources()},
        }
        self.samples.append(sample)
        self.log.info("Verified %s repetition %d: %d admitted/relayed/mined, %d bytes",
                      name, repetition, len(txids), byte_count)

    def reserve_asset_inputs(self, count):
        """Fund inputs that always leave change after an asset creation.

        createasset has no recipient output. When coin selection leaves no change it
        returns the id of a zero-output transaction that the mempool rejects
        (bad-txns-vout-empty), so every creation spends one of these reserved inputs.
        They stay locked until used, so payments and funding cannot spend them.
        """
        node = self.nodes[0]
        funding = node.sendmany("", {node.getnewaddress(): ASSET_FEE + 5 for _ in range(count)})
        self.mine()
        self.asset_inputs = [{"txid": coin["txid"], "vout": coin["vout"]} for coin in node.listunspent(1)
                             if coin["txid"] == funding and coin["amount"] == ASSET_FEE + 5]
        assert_equal(len(self.asset_inputs), count)
        node.lockunspent(False, self.asset_inputs)

    def lock_all_but_asset_input(self):
        node = self.nodes[0]
        # Locked coins are not listed, so this locks every spendable coin except the reserved one.
        others = [{"txid": coin["txid"], "vout": coin["vout"]} for coin in node.listunspent(0)]
        node.lockunspent(False, others)
        node.lockunspent(True, [self.asset_inputs.pop()])
        return others

    def payments(self, input_count, repetition):
        node = self.nodes[0]
        count = self.options.profile_count
        coins = [coin for coin in node.listunspent(1)
                 if coin["spendable"] and coin["amount"] > Decimal("0.02")]
        if len(coins) < input_count * count:
            # Asset operations may consolidate the wallet's mature outputs.
            # Fund independent inputs outside the measured admission/relay stages.
            funding = node.sendmany("", {node.getnewaddress(): Decimal("0.1")
                                         for _ in range(input_count * count)})
            self.mine()
            coins = [coin for coin in node.listunspent(1)
                     if coin["txid"] == funding and coin["spendable"] and coin["amount"] > Decimal("0.02")]
        assert len(coins) >= input_count * count, "Not enough confirmed independent inputs"
        signed = []
        expected = []
        for index in range(count):
            selected = coins[index * input_count:(index + 1) * input_count]
            inputs = [{"txid": coin["txid"], "vout": coin["vout"]} for coin in selected]
            each = ((sum(coin["amount"] for coin in selected) - Decimal("0.01")) /
                    input_count).quantize(Decimal("0.00000001"))
            outputs = {node.getnewaddress(): each for _ in range(input_count)}
            result = node.signrawtransactionwithwallet(node.createrawtransaction(inputs, outputs))
            assert result["complete"]
            signed.append(result["hex"])
            expected.append(node.decoderawtransaction(result["hex"])["txid"])

        invalid = FromHex(CTransaction(), signed[0])
        invalid.vin[0].scriptSig = b""
        assert_raises_rpc_error(-26, "", node.sendrawtransaction, ToHex(invalid), 0)
        assert not node.getrawmempool(), "Rejected traffic must not count as admission"
        conflict = FromHex(CTransaction(), signed[0])
        conflict.vout[0].nValue -= 1000000
        conflict = node.signrawtransactionwithwallet(ToHex(conflict))
        assert conflict["complete"]

        self.measure("payment_%din_%dout" % (input_count, input_count), repetition,
                     [lambda raw=raw: node.sendrawtransaction(raw, 0) for raw in signed], expected)
        # A differently signed transaction spending the now-consumed input must fail.
        assert_raises_rpc_error(-25, "", node.sendrawtransaction, conflict["hex"], 0)
        assert not node.getrawmempool()

    def run_test(self):
        assert 1 <= self.options.profile_count <= 50
        assert 3 <= self.options.profile_repetitions <= 20
        assert 0 <= self.options.profile_assets <= 10000
        if self.options.profile_output:
            assert not Path(self.options.profile_output).exists(), "Refusing to reuse an existing result"
        node = self.nodes[0]
        self.owner = node.getnewaddress()
        while node.getblockcount() < CHAIN_HEIGHT:
            node.generatetoaddress(min(100, CHAIN_HEIGHT - node.getblockcount()), self.owner)
        self.sync_all()
        self.test_gates()
        while node.getbalance() < ASSET_FEE * (self.options.profile_assets + 10):
            self.mine(100)
        self.reserve_asset_inputs(self.options.profile_assets + self.options.profile_repetitions)
        for index in range(self.options.profile_assets):
            locked = self.lock_all_but_asset_input()
            node.createasset(self.metadata("PROFILECACHE" + str(index)))
            node.lockunspent(True, locked)
            if (index + 1) % 10 == 0:
                self.mine()
        self.mine()
        assert_equal(len(node.listassets(False, 100000)), self.options.profile_assets)
        # Relay timers must advance in real time during measurement, not via accelerated mocktime.
        self.disable_mocktime()
        for peer in self.nodes:
            peer.setmocktime(0)
        self.mine()
        self.samples = []
        for repetition in range(self.options.profile_repetitions):
            for count in (1, 10):
                self.payments(count, repetition)
            name = "PROFILEASSET" + str(repetition)
            # Input reservation stays outside the measured stages.
            locked = self.lock_all_but_asset_input()
            self.measure("asset_create", repetition,
                         [lambda: node.createasset(self.metadata(name))["txid"]])
            node.lockunspent(True, locked)
            asset_id = node.getassetdetailsbyname(name)["Asset_id"]
            self.measure("asset_mint", repetition, [lambda: node.mintasset(asset_id)["txid"]])
            recipient = self.nodes[1].getnewaddress()
            self.measure("asset_transfer", repetition, [lambda: node.sendasset(asset_id, 25, recipient)["txid"]])
            assert_equal(self.nodes[1].listassetsbalance()[name]["Balance"], 25)

        # Transport options must not change consensus: every node reaches one chain,
        # UTXO set and asset state. Only the on-disk chainstate size may differ.
        states = [(peer.getbestblockhash(),
                   {key: value for key, value in peer.gettxoutsetinfo().items() if key != "disk_size"},
                   peer.listassets(True, 100000)) for peer in self.nodes]
        for state in states[1:]:
            assert_equal(state, states[0])
        summaries = {}
        for name in sorted({sample["workload"] for sample in self.samples}):
            subset = [sample for sample in self.samples if sample["workload"] == name]
            summaries[name] = {}
            for stage in subset[0]["seconds"]:
                values = [sample["seconds"][stage] for sample in subset]
                summaries[name][stage] = {"minimum": min(values), "median": statistics.median(values),
                                          "maximum": max(values)}
        with open(self.nodes[0].binary, "rb") as executable:
            binary_hash = hashlib.sha256(executable.read()).hexdigest()
        result = {"schema_version": 1, "complete": True,
                  "measurement": "Sequential local RPC samples with concurrent relay; no saturation claim",
                  "platform": platform.platform(), "python": platform.python_version(),
                  "daemon_sha256": binary_hash, "node_args": [n.process.args for n in self.nodes],
                  "initial_assets": self.options.profile_assets,
                  "samples": self.samples, "seconds_summary": summaries}
        destination = Path(self.options.profile_output or Path(self.options.tmpdir) / "profile.json")
        with destination.open("x") as output:
            output.write(json.dumps(result, indent=2, default=str) + "\n")
        self.log.info("Verified profile written to %s", destination)


if __name__ == "__main__":
    DecouplingProfile().main()
