#!/usr/bin/env python3
# Copyright (c) 2026 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise the opt-in mining interface and unavailable-body handling."""

from io import BytesIO
from decimal import Decimal
import copy
import struct

from test_framework.blocktools import create_coinbase
from test_framework.messages import (
    CBlock, CBlockHeader, CTransaction, CTxOut, ser_compact_size, ser_uint256,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, connect_nodes


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


def exercise_certified_mining(test, node, raw, certificate):
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
    for _ in range(8):
        template = node.getblocktemplate(request)
    assert_raises_rpc_error(-8, "Unknown or expired workid", node.getdecoupledblocktransactions, first_id, [])
    assert_raises_rpc_error(-8, "Invalid or duplicate transaction index",
                            node.getdecoupledblocktransactions, template["workid"], [-1])
    assert_raises_rpc_error(-8, "Invalid or duplicate transaction index",
                            node.getdecoupledblocktransactions, template["workid"], [1, 1])
    block, references = block_from_template(node, template)
    assert_equal(references[parent_index], txid)
    tampered = copy.deepcopy(block)
    tampered.hashMerkleRoot ^= 1
    tips = node.getchaintips()
    assert_raises_rpc_error(-22, "Decoupled block reconstruction failed", node.submitdecoupledblock,
                            encode_decoupled_block(tampered, references), template["workid"])
    assert_equal(node.getchaintips(), tips)
    block.solve()
    assert_equal(node.submitdecoupledblock(encode_decoupled_block(block, references), template["workid"]), None)
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


class DecoupledMiningTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = False

    def run_test(self):
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
