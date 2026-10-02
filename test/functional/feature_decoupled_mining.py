#!/usr/bin/env python3
# Copyright (c) 2026 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise the opt-in mining interface and unavailable-body handling."""

from io import BytesIO
from decimal import Decimal
import copy
import struct
import time

from test_framework.blocktools import create_coinbase, get_legacy_sigopcount_tx
from test_framework.messages import (
    CBlock, CBlockHeader, CTransaction, CTxOut, ser_compact_size, ser_uint256,
)
from test_framework.script import CScript, OP_TRUE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, connect_nodes, wait_until


def encode_decoupled_block(block, references):
    """Encode references by canonical position, retaining coinbase as a body."""
    bodies = [tx for index, tx in enumerate(block.vtx) if index not in references]
    encoded = struct.pack("<H", 1) + CBlockHeader(block).serialize()
    encoded += ser_compact_size(len(bodies))
    encoded += b"".join(tx.serialize() for tx in bodies)
    encoded += ser_compact_size(len(references))
    for index, txid in sorted(references.items()):
        encoded += struct.pack("<H", index) + ser_uint256(int(txid, 16))
    return encoded.hex()


def block_from_template(node, template):
    """Resolve a template and check that metadata names its canonical bodies."""
    bodies = {}
    for position, entry in enumerate(template["transactions"], 1):
        tx = CTransaction()
        tx.deserialize(BytesIO(bytes.fromhex(entry["data"])))
        tx.rehash()
        assert_equal(tx.hash, entry["hash"])
        bodies[entry.get("index", position)] = tx
    references = {}
    if template.get("vtxids"):
        metadata = template["vtxidmetadata"]
        assert_equal(len(template["vtxids"]), len(metadata))
        response = node.getdecoupledblocktransactions(
            template["workid"], [entry["index"] for entry in metadata])
        assert_equal(response["workid"], template["workid"])
        assert_equal(len(response["transactions"]), len(metadata))
        for txid, entry, returned in zip(template["vtxids"], metadata, response["transactions"]):
            index = entry["index"]
            assert index > 0 and index not in bodies
            assert_equal(returned["index"], index)
            assert_equal(returned["txid"], txid)
            tx = CTransaction()
            tx.deserialize(BytesIO(bytes.fromhex(returned["data"])))
            tx.rehash()
            assert_equal(tx.hash, txid)
            assert_equal(len(tx.serialize()), entry["size"])
            assert all(0 < dependency < index for dependency in entry["depends"])
            bodies[index] = tx
            references[index] = txid
    assert_equal(sorted(bodies), list(range(1, len(bodies) + 1)))

    coinbase = create_coinbase(template["height"])
    coinbase.vExtraPayload = bytes.fromhex(template["coinbase_payload"])
    coinbase.vout = [coinbase.vout[0]]
    payments = template["smartnode"] + template["superblock"]
    if template["founder"]:
        payments += [template["founder"]]
    for payment in payments:
        coinbase.vout.append(CTxOut(payment["amount"], bytes.fromhex(payment["script"])))
    coinbase.vout[0].nValue = template["coinbasevalue"] - sum(txout.nValue for txout in coinbase.vout[1:])
    coinbase.rehash()
    block = CBlock()
    block.nVersion = template["version"]
    block.hashPrevBlock = int(template["previousblockhash"], 16)
    block.nTime = template["curtime"]
    block.nBits = int(template["bits"], 16)
    block.vtx = [coinbase] + [bodies[index] for index in sorted(bodies)]
    block.hashMerkleRoot = block.calc_merkle_root()
    block.rehash()
    return block, references


def exercise_certified_mining(test, node, raw, certificate, eviction_bodies=()):
    """Mine a real certified parent and ordinary child, then exercise its lease."""
    txid = node.decoderawtransaction(raw)["txid"]
    assert_equal(node.submitbuspooltransaction(raw, certificate["hex"]), txid)
    assert not node.getbuspoolentry(txid)["locallyvalidated"]
    amount = node.decoderawtransaction(raw)["vout"][0]["value"]
    child = node.createrawtransaction([{"txid": txid, "vout": 0}],
                                     {node.getnewaddress(): amount - Decimal("0.001")})
    signed = node.signrawtransactionwithwallet(child)
    assert signed["complete"]
    child_id = node.sendrawtransaction(signed["hex"])
    # A script certificate is not an InstantSend lock. Preserve the existing
    # ChainLocks waiting period before mining transactions without such a lock.
    test.bump_mocktime(11 * 60)
    ordinary = node.getblocktemplate()
    assert "vtxids" not in ordinary
    assert all(entry["hash"] not in (txid, child_id) for entry in ordinary["transactions"])
    request = {"capabilities": ["decoupled-v1"]}
    template = node.getblocktemplate(request)
    assert txid in template["vtxids"]
    parent_index = template["vtxidmetadata"][template["vtxids"].index(txid)]["index"]
    child_entry = next(entry for entry in template["transactions"] if entry["hash"] == child_id)
    assert parent_index in child_entry["depends"]
    assert parent_index < child_entry["index"]
    assert "transactions" not in template["mutable"] and "prevblock" not in template["mutable"]
    assert_equal(int.from_bytes(bytes.fromhex(template["coinbase_payload"])[:2], "little"), 0x8001)
    assert_equal(template["expires"], 600)
    first_id = template["workid"]
    # Polling an unchanged template keeps its lease instead of evicting other work.
    assert_equal(node.getblocktemplate(request)["workid"], first_id)
    for _ in range(8):
        # Each rebuilt template gets its own lease; the ninth lease evicts the first.
        test.bump_mocktime(6)
        node.prioritisetransaction(child_id, 0)
        template = node.getblocktemplate(request)
        assert template["workid"] != first_id
    assert_raises_rpc_error(-8, "Unknown or expired workid", node.getdecoupledblocktransactions, first_id, [])
    assert_raises_rpc_error(-8, "Invalid or duplicate transaction index",
                            node.getdecoupledblocktransactions, template["workid"], [-1])
    assert_raises_rpc_error(-8, "Invalid or duplicate transaction index",
                            node.getdecoupledblocktransactions, template["workid"], [1, 1])
    block, references = block_from_template(node, template)
    assert_equal(references[parent_index], txid)
    parent_entry = template["vtxidmetadata"][template["vtxids"].index(txid)]
    child_entry = next(entry for entry in template["transactions"] if entry["hash"] == child_id)
    assert_equal(parent_entry["depends"], [])
    assert_equal(child_entry["depends"], [parent_index])
    for candidate, entry in ((txid, parent_entry), (child_id, child_entry)):
        assert_equal(entry["fee"], int(node.getmempoolentry(candidate)["fees"]["base"] * 100000000))
        assert_equal(entry["fee"], 100000)
        assert_equal(entry["specialTxfee"], 0)
        assert_equal(entry["sigops"], get_legacy_sigopcount_tx(block.vtx[entry["index"]]))
        assert_equal(entry["sigops"], 1)
    tampered = copy.deepcopy(block)
    tampered.hashMerkleRoot ^= 1
    tips = node.getchaintips()
    assert_raises_rpc_error(-22, "Decoupled block reconstruction failed", node.submitdecoupledblock,
                            encode_decoupled_block(tampered, references), template["workid"])
    assert_equal(node.getchaintips(), tips)
    block.solve()
    encoded = encode_decoupled_block(block, references)
    if eviction_bodies:
        # Evict the invalid-script parent from retention and the candidate graph.
        # No tip or clock change may expire its otherwise valid work lease.
        for body in eviction_bodies:
            assert_equal(node.requesttxvalidation(body)["status"], "requested")
        assert_raises_rpc_error(-5, "not retained", node.getbuspoolentry, txid)
        assert txid not in node.getrawmempool() and child_id not in node.getrawmempool()
        assert_equal(node.getbestblockhash(), template["previousblockhash"])
        assert_equal(node.submitdecoupledblock(encoded, ""), {
            "status": "incomplete", "missing": [{"index": parent_index, "txid": txid}]})
        assert_equal(node.getchaintips(), tips)
    assert_equal(node.submitdecoupledblock(encoded, template["workid"]), None)
    test.sync_blocks()
    assert_equal(node.getbestblockhash(), block.hash)
    assert txid in node.getblock(block.hash)["tx"] and child_id in node.getblock(block.hash)["tx"]
    assert_equal(node.getblock(block.hash, 0), block.serialize().hex())
    # Connection removed the candidates; the retained work still supplies bodies.
    assert txid not in node.getrawmempool()
    returned = node.getdecoupledblocktransactions(template["workid"], [parent_index])
    assert_equal(returned["transactions"][0]["data"], raw)
    test.bump_mocktime(601)
    assert_raises_rpc_error(-8, "Unknown or expired workid", node.getdecoupledblocktransactions,
                            template["workid"], [parent_index])


def exercise_transport_mining_limits(test):
    """Exercise byte-bound leases and a full parent with an IS-locked reference child."""
    node = test.nodes[0]
    assert_equal(node.getbuspoolinfo()["maxcount"], 4)
    coins = node.listunspent(101)
    assert len(coins) >= 16
    large_ids = []
    test.log.info("Build valid small-output payments to exercise retained-template bytes")
    for coin in coins[:12]:
        raw, _ = test.payment(coin, signed=False)
        tx = CTransaction()
        tx.deserialize(BytesIO(bytes.fromhex(raw)))
        tx.vout.extend(CTxOut(0, CScript([OP_TRUE])) for _ in range(9900))
        signed = node.signrawtransactionwithwallet(tx.serialize().hex())
        assert signed["complete"]
        assert 99000 < len(signed["hex"]) // 2 < 100000
        large_ids.append(node.sendrawtransaction(signed["hex"]))

    def large_locked():
        test.bump_mocktime(3)
        return all(node.getmempoolentry(txid)["instantlock"] for txid in large_ids)

    wait_until(large_locked, timeout=60)
    node.syncwithvalidationinterfacequeue()
    parent_raw, parent_id = test.payment(coins[12])
    assert_equal(node.sendrawtransaction(parent_raw), parent_id)
    test.wait_for_instantlock(parent_id, node)
    child_raw = node.createrawtransaction([{"txid": parent_id, "vout": 0}],
                                         {node.getnewaddress(): coins[12]["amount"] - Decimal("0.002")})
    child = node.signrawtransactionwithwallet(child_raw)
    assert child["complete"]
    child_id = node.sendrawtransaction(child["hex"])
    test.wait_for_instantlock(child_id, node)
    node.syncwithvalidationinterfacequeue()
    assert_equal(node.requesttxvalidation(child["hex"])["status"], "abstain")
    # Retention controls presentation, not dependency order or script validity.
    for coin in coins[13:16]:
        raw, _ = test.payment(coin)
        assert_equal(node.requesttxvalidation(raw)["status"], "requested")
    assert_raises_rpc_error(-5, "not retained", node.getbuspoolentry, parent_id)
    assert parent_id in node.getrawmempool()
    entry = node.getbuspoolentry(child_id)
    assert entry["eligible"] and entry["locallyvalidated"] and not entry["certificate"]

    request = {"capabilities": ["decoupled-v1"]}
    test.bump_mocktime(601)
    parent_tip, fixed_time, started = node.getbestblockhash(), test.mocktime, time.monotonic()
    large_workids = []
    for i in range(8):
        if i:
            # Rebuild the template: an unchanged one would reuse its lease.
            test.bump_mocktime(6)
            node.prioritisetransaction(child_id, 0)
        template = node.getblocktemplate(request)
        assert_equal(template["vtxids"], [child_id])
        assert set(large_ids).issubset(entry["hash"] for entry in template["transactions"])
        assert_equal(template["expires"], 600)
        large_workids.append(template["workid"])
    # Eight leases cannot evict by count. Full bodies exceed 32 MiB here even
    # though their serialized block fits below 2 MB. Do not assume which lease
    # first crosses the byte bound: allocation sizes can vary by platform.
    assert_equal(len(set(large_workids)), 8)
    assert_equal(node.getblocktemplate(request)["workid"], large_workids[-1])
    assert test.mocktime - fixed_time < 600
    assert time.monotonic() - started < 600
    assert_equal(node.getbestblockhash(), parent_tip)
    assert_raises_rpc_error(-8, "Unknown or expired workid", node.getdecoupledblocktransactions,
                            large_workids[0], [])
    assert_equal(node.getdecoupledblocktransactions(large_workids[-1], [0])["transactions"][0]["index"], 0)

    test.log.info("Eight small leases remain live; mine their ordinary parent and referenced child")
    # Keep the allocation fixture out of the chain and subsequent historical replay.
    for txid in large_ids:
        node.prioritisetransaction(txid, -1000000000000)
    test.bump_mocktime(601)
    fixed_time, started = test.mocktime, time.monotonic()
    small_workids = []
    for i in range(8):
        if i:
            test.bump_mocktime(6)
            node.prioritisetransaction(child_id, 0)
        template = node.getblocktemplate(request)
        assert_equal(template["vtxids"], [child_id])
        assert not set(large_ids).intersection(entry["hash"] for entry in template["transactions"])
        small_workids.append(template["workid"])
    assert_equal(len(set(small_workids)), 8)
    assert test.mocktime - fixed_time < 600
    assert time.monotonic() - started < 600
    assert_equal(node.getbestblockhash(), parent_tip)
    assert_equal(node.getdecoupledblocktransactions(small_workids[0], [0])["transactions"][0]["index"], 0)
    block, references = block_from_template(node, template)
    parent_entry = next(entry for entry in template["transactions"] if entry["hash"] == parent_id)
    child_entry = template["vtxidmetadata"][0]
    assert_equal(child_entry["depends"], [parent_entry["index"]])
    assert_equal(parent_entry["depends"], [])
    assert parent_entry["index"] < child_entry["index"]
    assert_equal(references, {child_entry["index"]: child_id})
    assert_equal(block.vtx[parent_entry["index"]].serialize().hex(), parent_raw)
    assert_equal(block.vtx[child_entry["index"]].serialize().hex(), child["hex"])
    for entry in (parent_entry, child_entry):
        assert_equal(entry["fee"], 100000)
        assert_equal(entry["specialTxfee"], 0)
        assert_equal(entry["sigops"], 1)
    assert_equal(int.from_bytes(bytes.fromhex(template["coinbase_payload"])[:2], "little"), 2)
    assert "transactions" in template["mutable"] and "prevblock" in template["mutable"]
    ordinary = node.getblocktemplate()
    assert "vtxids" not in ordinary
    assert {parent_id, child_id}.issubset(entry["hash"] for entry in ordinary["transactions"])
    block.solve()
    assert_equal(node.submitdecoupledblock(encode_decoupled_block(block, references), template["workid"]), None)
    test.sync_blocks()
    assert_equal(node.getbestblockhash(), block.hash)
    assert_equal(node.getblock(block.hash, 0), block.serialize().hex())
    assert set(large_ids).issubset(node.getrawmempool())


class DecoupledMiningTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = False

    def run_test(self):
        self.log.info("Buspool limits are validated at startup")
        self.stop_node(1)
        for args, message in (
                (["-txdecoupling=1", "-buspoolmaxcount=0"], "-buspoolmaxcount must be between 1 and 100000"),
                (["-txdecoupling=1", "-buspoolmaxcount=100001"], "-buspoolmaxcount must be between 1 and 100000"),
                (["-txdecoupling=1", "-buspoolmaxcount=abc"], "-buspoolmaxcount must be between 1 and 100000"),
                (["-txdecoupling=1", "-buspoolmaxbytes=0"], "-buspoolmaxbytes must be between 1 and 1073741824"),
                (["-buspoolmaxcount=10"], "Buspool limits require -txdecoupling=1")):
            self.nodes[1].assert_start_raises_init_error(args, "Error: " + message, partial_match=True)
        self.start_node(1)
        node = self.nodes[0]
        assert_raises_rpc_error(-8, "Transaction decoupling is not enabled",
                                node.getdecoupledblocktransactions, "missing", [])
        assert_raises_rpc_error(-8, "Transaction decoupling is not enabled",
                                node.submitdecoupledblock, "00", "")
        self.restart_node(0, extra_args=["-txdecoupling=1"])
        connect_nodes(self.nodes[0], 1)
        node = self.nodes[0]
        node.generate(1)
        self.sync_all()
        ordinary = node.getblocktemplate()
        experimental = node.getblocktemplate({"capabilities": ["decoupled-v1"]})
        assert_equal(ordinary, experimental)
        assert "vtxids" not in experimental and "workid" not in experimental
        assert_raises_rpc_error(-8, "Unknown or expired workid",
                                node.getdecoupledblocktransactions, "missing", [])
        assert_raises_rpc_error(-22, "Decoupled block decode failed",
                                node.submitdecoupledblock, "00", "")

        block, references = block_from_template(node, experimental)
        assert_equal(references, {})
        # A body missing from every source must not admit or invalidate a header.
        missing_txid = "11" * 32
        missing_index = len(block.vtx)
        unavailable = (struct.pack("<H", 1) + CBlockHeader(block).serialize()
                       + ser_compact_size(len(block.vtx))
                       + b"".join(tx.serialize() for tx in block.vtx)
                       + ser_compact_size(1) + struct.pack("<H", missing_index)
                       + ser_uint256(int(missing_txid, 16))).hex()
        tips = node.getchaintips()
        response = node.submitdecoupledblock(unavailable, "")
        assert_equal(response, {"status": "incomplete", "missing": [
            {"index": missing_index, "txid": missing_txid}]})
        assert_equal(node.getchaintips(), tips)

        block.solve()
        assert_equal(node.submitdecoupledblock(encode_decoupled_block(block, {}), ""), None)
        self.sync_blocks()
        assert_equal(node.getbestblockhash(), block.hash)
        assert_equal(node.submitblock(block.serialize().hex()), "duplicate")


if __name__ == "__main__":
    DecoupledMiningTest().main()
