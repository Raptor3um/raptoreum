#!/usr/bin/env python3
# Copyright (c) 2026 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Exercise negotiated transport, bounded recovery and parent-bound proof relay."""

import copy
import struct

from feature_llmq_txvalidation import TxValidationTest, statement_with_result
from test_framework.messages import (
    BlockTransactions, CBlock, CBlockHeader, CDecoupledBlock, CDecoupledTxRef, CInv, CTransaction,
    CTxValidationCertificate, FromHex, HeaderAndShortIDs, MSG_BLOCK, MSG_DECOUPLED_BLOCK,
    MSG_TX, MSG_TX_CERTIFICATE, ToHex, msg_block, msg_blocktxn, msg_cmpctblock, msg_dblock,
    msg_getdata, msg_inv, msg_mempool, msg_notfound, msg_senddblock, msg_txcert,
    ser_compact_size,
)
from test_framework.mininode import P2PInterface, mininode_lock, network_thread_start
from test_framework.test_framework import LLMQ_TEST_TYPE
from test_framework.util import assert_equal, assert_raises_rpc_error, connect_nodes, wait_until


class TransportPeer(P2PInterface):
    def __init__(self):
        super().__init__()
        self.invs = set()
        self.inv_counts = {}
        self.requests = []
        self.missing = {}
        self.transactions = set()

    def on_inv(self, message):
        for inv in message.inv:
            key = (inv.type, inv.hash)
            self.invs.add(key)
            self.inv_counts[key] = self.inv_counts.get(key, 0) + 1

    def on_tx(self, message):
        self.transactions.add(message.tx.rehash())

    def on_getdata(self, message):
        self.requests.extend((inv.type, inv.hash) for inv in message.inv)

    def on_getblocktxn(self, message):
        request = message.block_txn_request
        self.missing[request.blockhash] = request.to_absolute()

    def negotiate(self, version=1):
        self.wait_for_verack()
        message = msg_senddblock()
        message.version = version
        message.announce = True
        self.send_and_ping(message)

    def request(self, inv_type, blockhash):
        self.send_and_ping(msg_getdata([CInv(inv_type, blockhash)]))

    def wait_request(self, inv_type, blockhash):
        wait_until(lambda: (inv_type, blockhash) in self.requests, lock=mininode_lock)

    def reply(self, block, indexes=None):
        indexes = self.missing[block.sha256] if indexes is None else indexes
        message = msg_blocktxn()
        message.block_transactions = BlockTransactions(block.sha256, [block.vtx[i] for i in indexes])
        self.send_and_ping(message)


class RawDecoupledMessage:
    command = b"dblock"

    def __init__(self, data):
        self.data = data

    def serialize(self):
        return self.data


class DecoupledBlocksTest(TxValidationTest):
    def set_test_params(self):
        args = [["-txdecoupling=1", "-txdecouplingheight=1", "-buspoolmaxcount=8",
                 "-blockreconstructionextratxn=0"] for _ in range(7)]
        args[1] += ["-buspoolmaxcount=2", "-mempoolexpiry=1"]
        self.set_raptoreum_test_params(7, 5, extra_args=args, fast_dip3_enforcement=True)
        self.set_raptoreum_llmq_test_params(5, 3)

    def mine(self):
        self.bump_mocktime(1)
        blockhash = self.nodes[0].generate(1)[0]
        block = FromHex(CBlock(), self.nodes[0].getblock(blockhash, 0))
        block.calc_sha256()
        return block

    def deliver(self, peer, block, indexes):
        peer.send_and_ping(msg_dblock(CDecoupledBlock(block, indexes)))

    def deliver_compact(self, peer, block):
        encoded = HeaderAndShortIDs()
        encoded.initialize_from_block(block, prefill_list=list(range(len(block.vtx))))
        peer.send_and_ping(msg_cmpctblock(encoded.to_p2p()))

    def assert_tip(self, block):
        wait_until(lambda: self.nodes[1].getbestblockhash() == block.hash)
        assert_equal(self.nodes[1].getblock(block.hash, 0), ToHex(block))

    def wait_inventory(self, peer, inv_type, item_hash, after=0):
        def announced():
            self.bump_mocktime(1)
            with mininode_lock:
                return peer.inv_counts.get((inv_type, item_hash), 0) > after
        wait_until(announced, timeout=60)

    def assert_legacy_hidden(self, peer, node, txid, control_id):
        # An already-announced local transaction gives BIP35 an observable
        # response. This establishes timeLastMempoolReq before testing GETDATA.
        with mininode_lock:
            before = peer.inv_counts[(MSG_TX, int(control_id, 16))]
        peer.send_and_ping(msg_mempool())
        self.wait_inventory(peer, MSG_TX, int(control_id, 16), after=before)
        peer.request(MSG_TX, int(txid, 16))
        with mininode_lock:
            assert (MSG_TX, int(txid, 16)) not in peer.invs
            assert txid not in peer.transactions
            assert any(inv.type == MSG_TX and inv.hash == int(txid, 16)
                       for inv in peer.last_message["notfound"].vec)
        assert not node.getbuspoolentry(txid)["locallyvalidated"]

    def variants(self, block, count, first=1):
        result = []
        for nonce in range(first, first + count):
            variant = copy.deepcopy(block)
            variant.vtx[0].vin[0].scriptSig += struct.pack("<I", nonce)
            variant.vtx[0].rehash()
            variant.hashMerkleRoot = variant.calc_merkle_root()
            variant.nNonce = 0
            variant.solve()
            variant.calc_sha256()
            result.append(variant)
        return result

    def close_peers(self, peers):
        for peer in peers:
            peer.peer_disconnect()
        for peer in peers:
            peer.wait_for_disconnect()

    def exercise_alternate_recovery(self, peer, alternate, coin):
        producer, receiver = self.nodes[:2]
        source = producer.add_p2p_connection(TransportPeer())
        source.negotiate()
        for compact, fallback in ((False, False), (False, True), (True, False), (True, True)):
            self.log.info("An alternate block recovers a decoupled download (compact=%s, fallback=%s)", compact, fallback)
            if not compact:
                alternate.negotiate()
            raw, txid = self.payment(coin)
            producer.sendrawtransaction(raw)
            block = self.mine()
            index = next(i for i, tx in enumerate(block.vtx) if tx.rehash() == txid)
            # The canonical body remains retained after leaving the candidate graph.
            assert not producer.getbuspoolentry(txid)["candidate"]
            source.request(MSG_DECOUPLED_BLOCK, block.sha256)
            with mininode_lock:
                encoded = copy.deepcopy(source.last_message["dblock"].block)
            assert_equal(encoded.header.rehash(), block.sha256)
            assert (index, int(txid, 16)) in [(ref.index, ref.txid) for ref in encoded.vtxids]
            previous_tip = receiver.getbestblockhash()
            height = receiver.getblockcount() + 1
            # Forward the actual node-produced encoding, not a Python reconstruction.
            peer.send_and_ping(msg_dblock(encoded))
            wait_until(lambda: block.sha256 in peer.missing, lock=mininode_lock)
            assert_equal(peer.missing[block.sha256], [index])
            if fallback:
                peer.reply(block, [])
                peer.wait_request(MSG_BLOCK, block.sha256)
            owner = [info["id"] for info in receiver.getpeerinfo() if height in info["inflight"]]
            assert_equal(len(owner), 1)
            if not compact:
                # Another partial encoding must not start a competing download.
                alternate.send_and_ping(msg_dblock(encoded))
                assert_equal(receiver.getbestblockhash(), previous_tip)
                assert_equal([info["id"] for info in receiver.getpeerinfo() if height in info["inflight"]], owner)
                assert block.sha256 not in alternate.missing
                assert (MSG_BLOCK, block.sha256) not in alternate.requests
            # A corrupt alternative must preserve the original pending request.
            corrupt = copy.deepcopy(block)
            corrupt.vtx[0].vout[0].nValue -= 1
            corrupt.vtx[0].rehash()
            if compact:
                self.deliver_compact(alternate, corrupt)
            else:
                self.deliver(alternate, corrupt, [])
            assert_equal(receiver.getbestblockhash(), previous_tip)
            assert_equal([info["id"] for info in receiver.getpeerinfo() if height in info["inflight"]], owner)
            assert block.sha256 not in alternate.missing
            assert (MSG_BLOCK, block.sha256) not in alternate.requests
            if compact:
                self.deliver_compact(alternate, block)
            else:
                self.deliver(alternate, block, [])
            assert_equal(receiver.getbestblockhash(), block.hash)
            self.assert_tip(block)
            assert all(height not in info["inflight"] for info in receiver.getpeerinfo())
            peer.reply(block)
            assert_equal(receiver.getbestblockhash(), block.hash)
            assert peer.is_connected and alternate.is_connected
            # The next case spends this now-confirmed ordinary output.
            coin = {"txid": txid, "vout": 0, "amount": producer.decoderawtransaction(raw)["vout"][0]["value"]}
        self.close_peers([source])

    def exercise_reconstruction_limits(self, peer, alternate, coin):
        producer, receiver = self.nodes[:2]
        raw, txid = self.payment(coin)
        producer.sendrawtransaction(raw)
        block = self.mine()
        index = next(i for i, tx in enumerate(block.vtx) if tx.rehash() == txid)
        variants = self.variants(block, 18)
        self.log.info("A third partial block from one peer falls back without invalidating its header")
        for variant in variants[:2]:
            self.deliver(peer, variant, [index])
            wait_until(lambda: variant.sha256 in peer.missing, lock=mininode_lock)
        self.deliver(peer, variants[2], [index])
        peer.wait_request(MSG_BLOCK, variants[2].sha256)
        assert variants[2].sha256 not in peer.missing
        assert peer.is_connected
        self.close_peers([peer])

        self.log.info("Sixteen small partial blocks exhaust the shared count across nine peers")
        owners = [receiver.add_p2p_connection(TransportPeer()) for _ in range(9)]
        for owner in owners:
            owner.negotiate()
        for i, variant in enumerate(variants[:16]):
            owner = owners[i // 2]
            self.deliver(owner, variant, [index])
            wait_until(lambda: variant.sha256 in owner.missing, lock=mininode_lock)
        overflow = variants[16]
        self.deliver(owners[8], overflow, [index])
        owners[8].wait_request(MSG_BLOCK, overflow.sha256)
        assert overflow.sha256 not in owners[8].missing
        assert all(owner.is_connected for owner in owners)
        self.close_peers(owners)

        self.log.info("The shared byte quota triggers before the sixteen-object count")
        # These syntactically valid layouts have unknown references. Their valid
        # PoW headers remain recoverable from a complete canonical block; no
        # supplied transaction data may make an incomplete header invalid.
        owners = []
        bounded = False
        for i, variant in enumerate(self.variants(block, 16, first=100)):
            owner = receiver.add_p2p_connection(TransportPeer())
            owners.append(owner)
            owner.negotiate()
            encoded = CDecoupledBlock(variant)
            encoded.vtx = [variant.vtx[0]]
            encoded.vtxids = [CDecoupledTxRef(n, n) for n in range(1, 20001)]
            owner.send_and_ping(msg_dblock(encoded))
            wait_until(lambda: variant.sha256 in owner.missing or
                       (MSG_BLOCK, variant.sha256) in owner.requests, lock=mininode_lock)
            if (MSG_BLOCK, variant.sha256) in owner.requests:
                assert 0 < i < 16
                bounded = True
                break
        assert bounded
        assert all(owner.is_connected for owner in owners)
        self.close_peers(owners)
        alternate.send_and_ping(msg_block(block))
        self.assert_tip(block)
        peer = receiver.add_p2p_connection(TransportPeer())
        peer.negotiate()
        return peer

    def exercise_announcement_limits(self, peer, alternate):
        receiver = self.nodes[1]
        hashes = [0x100000 + i for i in range(4)]
        peer.send_and_ping(msg_inv([CInv(MSG_TX_CERTIFICATE, h) for h in hashes[:3]]))
        for h in hashes[:2]:
            peer.wait_request(MSG_TX_CERTIFICATE, h)
        assert (MSG_TX_CERTIFICATE, hashes[2]) not in peer.requests
        # Request expiry runs on the existing randomized object timer, at most
        # 900 seconds after its last check. It must return announcement capacity.
        self.bump_mocktime(901)
        peer.sync_with_ping()
        peer.send_and_ping(msg_inv([CInv(MSG_TX_CERTIFICATE, hashes[2])]))
        peer.wait_request(MSG_TX_CERTIFICATE, hashes[2])
        peer.send_and_ping(msg_inv([CInv(MSG_TX_CERTIFICATE, hashes[3])]))
        peer.wait_request(MSG_TX_CERTIFICATE, hashes[3])
        self.close_peers([peer])
        peer = receiver.add_p2p_connection(TransportPeer())
        peer.negotiate()
        # FinalizeNode must release the global count as well as the peer count.
        fresh = [0x200000, 0x200001]
        peer.send_and_ping(msg_inv([CInv(MSG_TX_CERTIFICATE, h) for h in fresh]))
        for h in fresh:
            peer.wait_request(MSG_TX_CERTIFICATE, h)
        peer.send_and_ping(msg_notfound([CInv(MSG_TX_CERTIFICATE, h) for h in fresh]))
        refresh = self.mine()
        alternate.send_and_ping(msg_block(refresh))
        self.assert_tip(refresh)
        return peer

    def run_test(self):
        producer, receiver = self.nodes[:2]
        coins = producer.listunspent(101)
        assert len(coins) >= 13
        assert_equal(receiver.getbuspoolinfo()["maxcount"], 2)
        # One ordinary receiver, five authenticated signers and a control node.
        assert all(mn.nodeIdx >= 2 for mn in self.mninfo)
        for peer in receiver.getpeerinfo():
            receiver.disconnectnode(nodeid=peer["id"])
        wait_until(lambda: receiver.getconnectioncount() == 0)
        legacy = receiver.add_p2p_connection(TransportPeer())
        peer = receiver.add_p2p_connection(TransportPeer())
        alternate = receiver.add_p2p_connection(TransportPeer())
        network_thread_start()
        for connected in (legacy, peer, alternate):
            connected.wait_for_verack()
        wait_until(lambda: "senddblock" in peer.last_message, lock=mininode_lock)
        tip = FromHex(CBlock(), receiver.getblock(receiver.getbestblockhash(), 0))
        tip.calc_sha256()

        self.log.info("Experimental messages require a version-one response to negotiation")
        legacy.request(MSG_DECOUPLED_BLOCK, tip.sha256)
        assert "dblock" not in legacy.last_message
        assert_equal(legacy.last_message["notfound"].vec[0].type, MSG_DECOUPLED_BLOCK)
        legacy.request(MSG_BLOCK, tip.sha256)
        assert_equal(ToHex(legacy.last_message["block"].block), ToHex(tip))
        peer.negotiate(version=2)
        peer.request(MSG_DECOUPLED_BLOCK, tip.sha256)
        assert "dblock" not in peer.last_message
        peer.negotiate()
        peer.request(MSG_DECOUPLED_BLOCK, tip.sha256)
        assert_equal(ToHex(peer.last_message["dblock"].block.header), ToHex(CBlockHeader(tip)))

        first = self.mine()
        legacy.send_and_ping(msg_dblock(CDecoupledBlock(first)))
        assert_equal(receiver.getbestblockhash(), tip.hash)
        self.deliver(peer, first, [])
        self.assert_tip(first)
        self.exercise_alternate_recovery(peer, alternate, coins[12])
        self.log.info("Pending certificate announcements recover their count on expiry and disconnect")
        peer = self.exercise_announcement_limits(peer, alternate)

        self.log.info("A reference resolved only by buspool survives cache eviction while another body is missing")
        payment, txid = self.payment(coins[0])
        filler, filler_id = self.payment(coins[1])
        missing, missing_id = self.payment(coins[2])
        replacement, replacement_id = self.payment(coins[3])
        receiver.sendrawtransaction(payment)
        self.bump_mocktime(3601)
        receiver.sendrawtransaction(filler)
        assert txid not in receiver.getrawmempool()
        assert_equal(receiver.getbuspoolentry(txid)["txid"], txid)
        # A tip older than the direct-fetch window uses historical full transport.
        refresh = self.mine()
        self.deliver(peer, refresh, [])
        peer.wait_request(MSG_BLOCK, refresh.sha256)
        alternate.send_and_ping(msg_block(refresh))
        self.assert_tip(refresh)
        assert_equal(receiver.getbuspoolentry(txid)["txid"], txid)
        producer.sendrawtransaction(payment)
        producer.sendrawtransaction(missing)
        block = self.mine()
        indexes = [i for i, tx in enumerate(block.vtx) if tx.rehash() in (txid, missing_id)]
        assert_equal(len(indexes), 2)
        self.deliver(peer, block, indexes)
        wait_until(lambda: block.sha256 in peer.missing, lock=mininode_lock)
        expected = [i for i in indexes if block.vtx[i].hash == missing_id]
        assert_equal(peer.missing[block.sha256], expected)
        receiver.sendrawtransaction(replacement)
        assert_raises_rpc_error(-5, "not retained", receiver.getbuspoolentry, txid)
        assert replacement_id in receiver.getrawmempool()
        peer.reply(block)
        self.assert_tip(block)

        self.log.info("Wrong identities and malformed allocations use full fallback without invalidating the header")
        for coin, wrong_count in ((coins[4], False), (coins[5], True)):
            raw, new_id = self.payment(coin)
            producer.sendrawtransaction(raw)
            block = self.mine()
            index = next(i for i, tx in enumerate(block.vtx) if tx.rehash() == new_id)
            self.deliver(peer, block, [index])
            wait_until(lambda: block.sha256 in peer.missing, lock=mininode_lock)
            # A valid but unexpected body, or a count inconsistent with our request.
            peer.reply(block, [] if wrong_count else [0])
            peer.wait_request(MSG_BLOCK, block.sha256)
            alternate.send_and_ping(msg_block(block))
            self.assert_tip(block)
        for body in (ser_compact_size(65535),
                     b"\x01" + struct.pack("<I", 1) + ser_compact_size(1 << 20),
                     b"\x01" + struct.pack("<I", 1) + ser_compact_size(70000) + b"\x00" * (70000 * 41) + b"\x00" * 6):
            block = self.mine()
            # Tiny truncated counts and a sub-3MiB wire message whose decoded
            # input vector exceeds 8MiB both fall back before allocation.
            malformed = struct.pack("<H", 1) + CBlockHeader(block).serialize() + body
            peer.send_and_ping(RawDecoupledMessage(malformed))
            peer.wait_request(MSG_BLOCK, block.sha256)
            alternate.send_and_ping(msg_block(block))
            self.assert_tip(block)
            assert peer.is_connected

        self.log.info("Withholding a missing body eventually returns to the normal full-block request")
        raw, new_id = self.payment(coins[6])
        producer.sendrawtransaction(raw)
        block = self.mine()
        index = next(i for i, tx in enumerate(block.vtx) if tx.rehash() == new_id)
        self.deliver(peer, block, [index])
        wait_until(lambda: block.sha256 in peer.missing, lock=mininode_lock)
        self.bump_mocktime(61)
        peer.sync_with_ping()
        peer.wait_request(MSG_BLOCK, block.sha256)
        alternate.send_and_ping(msg_block(block))
        self.assert_tip(block)

        peer = self.exercise_reconstruction_limits(peer, alternate, coins[9])

        self.log.info("Unsolicited or malformed BLS prefixes cannot reuse an expensive in-flight decode")
        malformed_proof = CTxValidationCertificate(b"\xff" * 236)
        malformed_message = msg_txcert(malformed_proof, CTransaction())
        rejects = peer.message_count["reject"]
        peer.send_and_ping(malformed_message)
        assert_equal(peer.message_count["reject"], rejects)
        malformed_hash = malformed_proof.get_hash()
        peer.send_and_ping(msg_inv([CInv(MSG_TX_CERTIFICATE, malformed_hash)]))
        peer.wait_request(MSG_TX_CERTIFICATE, malformed_hash)
        peer.send_and_ping(malformed_message)
        count = peer.requests.count((MSG_TX_CERTIFICATE, malformed_hash))
        peer.send_and_ping(malformed_message)
        peer.send_and_ping(msg_inv([CInv(MSG_TX_CERTIFICATE, malformed_hash)]))
        self.bump_mocktime(61)
        peer.sync_with_ping()
        assert_equal(peer.requests.count((MSG_TX_CERTIFICATE, malformed_hash)), count)
        assert peer.is_connected

        self.log.info("Real recovered proofs relay to an ordinary node by certificate hash")
        # Mine receiver-only candidates before the quorum phase and restore the normal network.
        producer.sendrawtransaction(filler)
        producer.sendrawtransaction(replacement)
        connect_nodes(receiver, 0)
        self.bump_mocktime(1)
        producer.generate(1)
        self.sync_blocks()
        producer.spork("SPORK_17_QUORUM_DKG_ENABLED", 0)
        producer.spork("SPORK_23_QUORUM_ALL_CONNECTED", 0)
        self.wait_for_sporks_same()
        self.mine_quorum()
        self.bump_mocktime(1)
        producer.generate(8)
        self.sync_blocks()
        raw, certified_id = self.payment(coins[7])
        certificate = self.recover(raw)
        encoded = CTxValidationCertificate(bytes.fromhex(certificate["hex"]))
        cert_hash = encoded.get_hash()
        peer.send_and_ping(msg_txcert(encoded, FromHex(CTransaction(), raw)))
        assert certified_id not in receiver.getrawmempool()
        bad_flags = bytearray(encoded.serialize())
        bad_flags[102] ^= 1
        bad_flags = CTxValidationCertificate(bytes(bad_flags))
        peer.send_and_ping(msg_inv([CInv(MSG_TX_CERTIFICATE, bad_flags.get_hash())]))
        peer.wait_request(MSG_TX_CERTIFICATE, bad_flags.get_hash())
        peer.send_and_ping(msg_txcert(bad_flags, FromHex(CTransaction(), raw)))
        assert certified_id not in receiver.getrawmempool()
        # Rejecting a proof must never add the otherwise valid txid to recentRejects.
        producer.submitbuspooltransaction(raw, certificate["hex"])

        def relayed():
            self.bump_mocktime(1)
            return certified_id in receiver.getrawmempool()
        wait_until(relayed, timeout=60)
        assert not receiver.getbuspoolentry(certified_id)["locallyvalidated"]
        self.wait_inventory(peer, MSG_TX_CERTIFICATE, cert_hash)
        peer.request(MSG_TX_CERTIFICATE, cert_hash)
        assert_equal(peer.last_message["txcert"].certificate.serialize().hex(), certificate["hex"])
        assert_equal(peer.last_message["txcert"].tx.rehash(), certified_id)
        control_raw, control_id = self.payment(coins[11])
        receiver.sendrawtransaction(control_raw)
        self.wait_inventory(legacy, MSG_TX, int(control_id, 16))
        self.assert_legacy_hidden(legacy, receiver, certified_id, control_id)

        self.log.info("A controlled threshold cannot smuggle an unchecked candidate through legacy TX relay")
        malicious, malicious_id = self.payment(coins[8], signed=False)
        statement = producer.requesttxvalidation(malicious)
        proof, messagehash = statement_with_result(statement["hex"], True)
        for mn in self.mninfo:
            assert mn.node.quorum("sign", LLMQ_TEST_TYPE, statement["requestid"], messagehash, statement["quorumhash"])
        proof[140:] = bytes.fromhex(self.get_recovered_sig(statement["requestid"], messagehash)["sig"])
        body = FromHex(CTransaction(), malicious)
        proof = CTxValidationCertificate(bytes(proof))
        signer = self.mninfo[0].node
        signer_peer = signer.add_p2p_connection(TransportPeer())
        signer_peer.negotiate()
        signer_peer.send_and_ping(msg_inv([CInv(MSG_TX_CERTIFICATE, proof.get_hash())]))
        signer_peer.wait_request(MSG_TX_CERTIFICATE, proof.get_hash())
        signer_score = sum(item.get("banscore", 0) for item in signer.getpeerinfo())
        signer_peer.send_and_ping(msg_txcert(proof, body))
        assert malicious_id not in signer.getrawmempool()
        assert_equal(sum(item.get("banscore", 0) for item in signer.getpeerinfo()), signer_score)
        # A local script decline is not a dishonest relay proof, but still uses
        # the ordinary request interval to avoid repeated expensive validation.
        signer_peer.send_and_ping(msg_inv([CInv(MSG_TX_CERTIFICATE, proof.get_hash())]))
        assert_equal(signer_peer.requests.count((MSG_TX_CERTIFICATE, proof.get_hash())), 1)
        peer.send_and_ping(msg_inv([CInv(MSG_TX_CERTIFICATE, proof.get_hash())]))
        peer.wait_request(MSG_TX_CERTIFICATE, proof.get_hash())
        peer.send_and_ping(msg_txcert(proof, body))
        wait_until(lambda: malicious_id in receiver.getrawmempool())
        self.assert_legacy_hidden(legacy, receiver, malicious_id, control_id)
        assert all(malicious_id not in mn.node.getrawmempool() for mn in self.mninfo)

        self.log.info("A parent transition retires old proof inventory without poisoning the transaction ID")
        producer.prioritisetransaction(certified_id, -1000000000000)
        self.bump_mocktime(1)
        producer.generate(1)
        self.sync_blocks()
        assert malicious_id not in receiver.getrawmempool()
        peer.request(MSG_TX_CERTIFICATE, cert_hash)
        assert any(inv.hash == cert_hash for inv in peer.last_message["notfound"].vec)
        stale = bytearray(encoded.serialize())
        stale[103] ^= 1
        stale = CTxValidationCertificate(bytes(stale))
        score = sum(item.get("banscore", 0) for item in receiver.getpeerinfo())
        peer.send_and_ping(msg_inv([CInv(MSG_TX_CERTIFICATE, stale.get_hash())]))
        peer.wait_request(MSG_TX_CERTIFICATE, stale.get_hash())
        peer.send_and_ping(msg_txcert(stale, FromHex(CTransaction(), raw)))
        assert_equal(sum(item.get("banscore", 0) for item in receiver.getpeerinfo()), score)
        current = self.recover(raw)
        current_hash = CTxValidationCertificate(bytes.fromhex(current["hex"])).get_hash()
        assert current_hash != cert_hash
        producer.submitbuspooltransaction(raw, current["hex"])

        def current_relayed():
            self.bump_mocktime(1)
            return receiver.getbuspoolentry(certified_id).get("parent") == producer.getbestblockhash()
        wait_until(current_relayed, timeout=60)
        assert peer.is_connected and legacy.is_connected

        self.log.info("Block-only mode does not request, accept or serve the negotiated certificate channel")
        fresh_raw, fresh_id = self.payment(coins[10])
        fresh = self.recover(fresh_raw)
        fresh_proof = CTxValidationCertificate(bytes.fromhex(fresh["hex"]))
        receiver.disconnect_p2ps()
        self.restart_node(1, self.extra_args[1] + ["-blocksonly=1"])
        receiver = self.nodes[1]
        blocksonly = receiver.add_p2p_connection(TransportPeer())
        blocksonly.negotiate()
        blocksonly.send_and_ping(msg_inv([CInv(MSG_TX_CERTIFICATE, fresh_proof.get_hash())]))
        assert (MSG_TX_CERTIFICATE, fresh_proof.get_hash()) not in blocksonly.requests
        blocksonly.send_and_ping(msg_txcert(fresh_proof, FromHex(CTransaction(), fresh_raw)))
        assert fresh_id not in receiver.getrawmempool()
        receiver.submitbuspooltransaction(fresh_raw, fresh["hex"])
        blocksonly.request(MSG_TX_CERTIFICATE, fresh_proof.get_hash())
        assert any(inv.hash == fresh_proof.get_hash() for inv in blocksonly.last_message["notfound"].vec)

        self.log.info("A node without the experimental flag preserves the ordinary wire contract")
        receiver.disconnect_p2ps()
        disabled_args = [arg for arg in self.extra_args[1] if not arg.startswith(("-txdecoupling", "-buspool"))]
        self.restart_node(1, disabled_args)
        receiver = self.nodes[1]
        disabled = receiver.add_p2p_connection(TransportPeer())
        disabled.negotiate()
        assert "senddblock" not in disabled.last_message
        tip = int(receiver.getbestblockhash(), 16)
        disabled.request(MSG_DECOUPLED_BLOCK, tip)
        assert "dblock" not in disabled.last_message
        disabled.send_and_ping(msg_inv([CInv(MSG_TX_CERTIFICATE, current_hash)]))
        assert (MSG_TX_CERTIFICATE, current_hash) not in disabled.requests
        disabled.request(MSG_BLOCK, tip)
        assert_equal(disabled.last_message["block"].block.rehash(), int(receiver.getbestblockhash(), 16))


if __name__ == "__main__":
    DecoupledBlocksTest().main()
