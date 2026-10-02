#!/usr/bin/env python3
# Copyright (c) 2026 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Exercise anchored certificates with a real five-member, threshold-three DKG."""

from decimal import Decimal

from feature_decoupled_mining import block_from_template, exercise_certified_mining, exercise_transport_mining_limits

from test_framework.messages import CTransaction, FromHex, ToHex, hash256
from test_framework.test_framework import LLMQ_TEST_TYPE, RaptoreumTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error, connect_nodes, force_finish_mnsync, initialize_datadir, wait_until


def statement_with_result(encoded, positive):
    certificate = bytearray.fromhex(encoded)
    assert_equal(len(certificate), 236)
    certificate[139] = int(positive)
    message = certificate[:2] + certificate[34:66] + certificate[2:34] + certificate[66:106] + certificate[139:140]
    return certificate, hash256(message)[::-1].hex()


class TxValidationTest(RaptoreumTestFramework):
    def set_test_params(self):
        args = [["-txdecoupling=1", "-txdecouplingheight=1", "-buspoolmaxcount=4"] for _ in range(6)]
        self.set_raptoreum_test_params(6, 5, extra_args=args, fast_dip3_enforcement=True)
        self.set_raptoreum_llmq_test_params(5, 3)

    def payment(self, coin, signed=True):
        node = self.nodes[0]
        raw = node.createrawtransaction([{"txid": coin["txid"], "vout": coin["vout"]}],
                                       {node.getnewaddress(): coin["amount"] - Decimal("0.001")})
        if signed:
            result = node.signrawtransactionwithwallet(raw)
            assert result["complete"]
            raw = result["hex"]
        return raw, node.decoderawtransaction(raw)["txid"]

    def restart_signer(self, mn):
        self.restart_node(mn.nodeIdx, self.extra_args[mn.nodeIdx] + ["-smartnodeblsprivkey=" + mn.keyOperator])
        mn.node = self.nodes[mn.nodeIdx]
        connect_nodes(mn.node, 0)
        force_finish_mnsync(mn.node)
        # Recovered signatures are announced only to authenticated peers. Wait
        # for the restarted member before issuing the next signing request.
        self.bump_mocktime(60)
        self.wait_for_mnauth(mn.node, len(self.mninfo) - 1)
        self.sync_blocks()

    def recover(self, raw, nodes=None):
        if nodes is None:
            nodes = self.nodes
        statements = [node.requesttxvalidation(raw) for node in nodes]
        assert all(item["status"] == "requested" for item in statements)
        assert all(item["requestid"] == statements[0]["requestid"] for item in statements)
        for item in statements:
            assert_equal(statement_with_result(item["hex"], item["positive"])[1], item["messagehash"])
        txid = statements[0]["txid"]
        # Generic recovered signatures are relayed between authenticated
        # smartnodes. An ordinary receiver gets the complete proof explicitly.
        source = next(member.node for member in self.mninfo if member.node in nodes)

        def recovered():
            self.bump_mocktime(1)
            return source.gettxcertificate(txid)["recovered"]
        wait_until(recovered, timeout=60)
        return source.gettxcertificate(txid)

    def check_historical_replay(self, certified_blocks):
        self.log.info("Rotate quorums before syncing a node with no signing history")
        for _ in range(2):
            self.mine_quorum()
        source = self.nodes[0]
        expected_tip = source.getbestblockhash()
        expected_state = source.gettxoutsetinfo()
        expected_blocks = {block: source.getblock(block, 0) for block in certified_blocks}
        fresh_index = len(self.nodes)
        initialize_datadir(self.options.tmpdir, fresh_index, self.chain)
        # The new node has neither a copied datadir nor a smartnode private key.
        args = [arg for arg in self.extra_args[0] if not arg.startswith("-sporkkey=")]
        args.append("-blocksonly=1")
        self.add_nodes(1, extra_args=[args])
        self.num_nodes += 1
        self.start_node(fresh_index)
        fresh = self.nodes[fresh_index]
        connect_nodes(fresh, 0)
        self.sync_blocks()

        def check_state():
            assert_equal(fresh.getbestblockhash(), expected_tip)
            actual = fresh.gettxoutsetinfo()
            for field in ("bestblock", "hash_serialized_2", "txouts", "total_amount"):
                assert_equal(actual[field], expected_state[field])
            for block, encoded in expected_blocks.items():
                assert_equal(fresh.getblock(block, 0), encoded)
            assert_equal(fresh.getbuspoolinfo()["size"], 0)

        check_state()
        for flag in (None, "-reindex", "-reindex-chainstate"):
            self.log.info("Replay canonical certified blocks with %s", flag or "ordinary restart")
            restart_args = args + ["-connect=0", "-checkblockindex=1"]
            if flag:
                restart_args.append(flag)
            self.restart_node(fresh_index, restart_args)
            wait_until(lambda: fresh.getbestblockhash() == expected_tip, timeout=120)
            check_state()

        self.log.info("Disconnect across activation and reconnect without local certificate history")
        first = fresh.getblockhash(1)
        fresh.invalidateblock(first)
        assert_equal(fresh.getblockcount(), 0)
        fresh.reconsiderblock(first)
        wait_until(lambda: fresh.getbestblockhash() == expected_tip, timeout=120)
        check_state()

    def run_test(self):
        node = self.nodes[0]
        assert_equal(node.getbuspoolinfo()["maxcount"], 4)
        coins = node.listunspent(101)
        assert len(coins) >= 14
        raw, txid = self.payment(coins[0])
        assert_equal(node.requesttxvalidation(raw)["status"], "abstain")
        node.spork("SPORK_17_QUORUM_DKG_ENABLED", 0)
        node.spork("SPORK_23_QUORUM_ALL_CONNECTED", 0)
        self.wait_for_sporks_same()
        self.mine_quorum()
        self.bump_mocktime(1)
        node.generate(8)
        self.sync_blocks()

        self.log.info("Recover a positive certificate without admitting the transaction")
        certificate = self.recover(raw)
        assert certificate["positive"]
        assert_equal(node.getrawmempool(), [])
        before = node.getblocktemplate()["longpollid"]
        assert_equal(node.submitbuspooltransaction(raw, certificate["hex"]), txid)
        entry = node.getbuspoolentry(txid)
        assert entry["eligible"] and entry["candidate"]
        assert not entry["locallyvalidated"]
        self.bump_mocktime(6)
        assert node.getblocktemplate()["longpollid"] != before
        assert_raises_rpc_error(-22, "Invalid certificate size", node.submitbuspooltransaction, raw, certificate["hex"] + "00")
        wrong_parent = bytearray.fromhex(certificate["hex"])
        wrong_parent[34] ^= 1
        assert_raises_rpc_error(-26, "bad-txcert-context", node.submitbuspooltransaction, raw, wrong_parent.hex())
        other, _ = self.payment(coins[1])
        assert_raises_rpc_error(-26, "bad-txcert-context", node.submitbuspooltransaction, other, certificate["hex"])

        self.log.info("A new tip demotes the exact-parent proof and locally checks the retained candidate")
        node.prioritisetransaction(txid, -1000000000000)
        assert not node.getbuspoolentry(txid)["locallyvalidated"]
        self.bump_mocktime(1)
        node.generate(1)
        self.sync_blocks()
        assert txid in node.getrawmempool()
        assert node.getbuspoolentry(txid)["locallyvalidated"]
        assert_raises_rpc_error(-26, "bad-txcert-context", node.submitbuspooltransaction, raw, certificate["hex"])
        original_entry = node.getmempoolentry(txid)
        current_certificate = self.recover(raw)
        # Promotion of an existing candidate must itself refresh long-polling miners.
        before = node.getblocktemplate()["longpollid"]
        self.bump_mocktime(6)
        node.submitbuspooltransaction(raw, current_certificate["hex"])
        assert node.getblocktemplate()["longpollid"] != before
        promoted_entry = node.getmempoolentry(txid)
        for field in ("time", "ancestorcount", "descendantcount", "depends", "instantlock"):
            assert_equal(promoted_entry[field], original_entry[field])
        assert node.getbuspoolentry(txid)["locallyvalidated"]

        self.log.info("A rejected block on the current parent keeps current certificates")
        tip = node.getbestblockhash()
        assert node.getbuspoolentry(txid)["certificate"]
        rejected, _ = block_from_template(node, node.getblocktemplate())
        rejected.vtx[0].vout[0].nValue += 1
        rejected.vtx[0].rehash()
        rejected.hashMerkleRoot = rejected.calc_merkle_root()
        rejected.solve()
        # The block is stored, then fails in ConnectBlock with the tip unchanged.
        assert node.submitblock(rejected.serialize().hex()) is not None
        assert_equal(node.getbestblockhash(), tip)
        assert node.getbuspoolentry(txid)["certificate"]
        assert node.gettxcertificate(txid)["recovered"]

        self.log.info("Negative certificates remain diagnostic and missing prevouts cause abstention")
        invalid, invalid_id = self.payment(coins[2], signed=False)
        negative = self.recover(invalid)
        assert not negative["positive"]
        assert_raises_rpc_error(-26, "bad-txcert-format", node.submitbuspooltransaction, invalid, negative["hex"])
        assert invalid_id not in node.getrawmempool()
        missing = FromHex(CTransaction(), invalid)
        missing.vin[0].prevout.hash = 123
        assert_equal(node.requesttxvalidation(ToHex(missing))["status"], "abstain")

        self.log.info("A negative vote never permanently rejects a transaction that becomes final")
        delayed = node.createrawtransaction(
            [{"txid": coins[12]["txid"], "vout": coins[12]["vout"], "sequence": 0xFFFFFFFE}],
            {node.getnewaddress(): coins[12]["amount"] - Decimal("0.001")}, node.getblockcount() + 1)
        delayed = node.signrawtransactionwithwallet(delayed)
        assert delayed["complete"]
        delayed_certificate = self.recover(delayed["hex"])
        assert not delayed_certificate["positive"]
        assert_raises_rpc_error(-26, "non-final", node.sendrawtransaction, delayed["hex"])
        self.bump_mocktime(1)
        node.generate(1)
        self.sync_blocks()
        delayed_id = node.sendrawtransaction(delayed["hex"])
        assert_equal(delayed_id, delayed_certificate["txid"])
        self.bump_mocktime(1)
        node.generate(1)
        self.sync_blocks()

        self.log.info("The same request ID prevents opposite votes in both orders and survives restart")
        signer = self.mninfo[0]
        for coin, negative_first in ((coins[3], True), (coins[4], False)):
            payment, payment_id = self.payment(coin)
            statement = node.requesttxvalidation(payment)
            _, opposite = statement_with_result(statement["hex"], False)
            if negative_first:
                assert signer.node.quorum("sign", LLMQ_TEST_TYPE, statement["requestid"], opposite, statement["quorumhash"])
                assert not signer.node.requesttxvalidation(payment)["submitted"]
            else:
                assert signer.node.requesttxvalidation(payment)["submitted"]
                assert not signer.node.quorum("sign", LLMQ_TEST_TYPE, statement["requestid"], opposite, statement["quorumhash"])
            self.restart_signer(signer)
            if negative_first:
                assert not signer.node.requesttxvalidation(payment)["submitted"]
            else:
                assert not signer.node.quorum("sign", LLMQ_TEST_TYPE, statement["requestid"], opposite, statement["quorumhash"])
            certificate = self.recover(payment, [node] + [mn.node for mn in self.mninfo[1:]])
            assert certificate["positive"]
            assert_equal(certificate["txid"], payment_id)
            assert not signer.node.quorum("hasrecsig", LLMQ_TEST_TYPE, statement["requestid"], opposite)

        self.log.info("A reorg keeps a delegated candidate whose parent returns to the mempool")
        # ChainLocks would reactivate a locked tip after invalidateblock.
        node.spork("SPORK_19_CHAINLOCKS_ENABLED", 4070908800)
        self.wait_for_sporks_same()
        # The confirmed output of the delayed payment is not used elsewhere.
        delayed_coin = {"txid": delayed_id, "vout": 0,
                        "amount": node.decoderawtransaction(delayed["hex"])["vout"][0]["value"]}
        parent_raw, parent_id = self.payment(delayed_coin)
        node.sendrawtransaction(parent_raw)
        self.wait_for_instantlock(parent_id, node)
        self.bump_mocktime(1)
        parent_block = node.generate(1)[0]
        self.sync_blocks()
        assert parent_id in node.getblock(parent_block)["tx"]
        spend = node.createrawtransaction([{"txid": parent_id, "vout": 0}],
                                          {node.getnewaddress(): delayed_coin["amount"] - Decimal("0.002")})
        spend = node.signrawtransactionwithwallet(spend)
        assert spend["complete"]
        spend_certificate = self.recover(spend["hex"])
        spend_id = node.submitbuspooltransaction(spend["hex"], spend_certificate["hex"])
        assert not node.getbuspoolentry(spend_id)["locallyvalidated"]
        node.invalidateblock(parent_block)
        assert parent_id in node.getrawmempool()
        assert spend_id in node.getrawmempool()
        assert node.getbuspoolentry(spend_id)["locallyvalidated"]
        node.reconsiderblock(parent_block)
        assert_equal(node.getbestblockhash(), parent_block)
        assert spend_id in node.getrawmempool()
        self.sync_blocks()
        node.spork("SPORK_19_CHAINLOCKS_ENABLED", 0)
        self.wait_for_sporks_same()

        self.log.info("A controlled threshold proof does not poison local script checks or survive its parent")
        malicious, malicious_id = self.payment(coins[5], signed=False)
        statement = node.requesttxvalidation(malicious)
        assert not statement["positive"]
        proof, positive_message = statement_with_result(statement["hex"], True)
        for mn in self.mninfo:
            assert mn.node.quorum("sign", LLMQ_TEST_TYPE, statement["requestid"], positive_message, statement["quorumhash"])
        recovered = self.get_recovered_sig(statement["requestid"], positive_message)
        proof[140:] = bytes.fromhex(recovered["sig"])
        assert_raises_rpc_error(-26, "script", node.sendrawtransaction, malicious)
        assert_raises_rpc_error(-26, "script", signer.node.submitbuspooltransaction, malicious, proof.hex())
        assert_equal(node.submitbuspooltransaction(malicious, proof.hex()), malicious_id)
        assert not node.getbuspoolentry(malicious_id)["locallyvalidated"]
        # The certificate must not populate local script-success caches: the admitting
        # node and another signer still observe a negative result for these bytes.
        assert not node.requesttxvalidation(malicious)["positive"]
        assert not signer.node.requesttxvalidation(malicious)["positive"]
        child = node.createrawtransaction([{"txid": malicious_id, "vout": 0}],
                                         {node.getnewaddress(): coins[5]["amount"] - Decimal("0.002")})
        child = node.signrawtransactionwithwallet(child)
        assert child["complete"]
        child_id = node.sendrawtransaction(child["hex"])
        node.prioritisetransaction(malicious_id, -1000000000000)
        self.bump_mocktime(1)
        node.generate(1)
        self.sync_blocks()
        assert malicious_id not in node.getrawmempool()
        assert child_id not in node.getrawmempool()
        node.prioritisetransaction(malicious_id, 1000000000000)
        assert_raises_rpc_error(-26, "script", node.sendrawtransaction, malicious)

        self.log.info("Retention evicts old bodies without creating candidates")
        retained = []
        for coin in coins[6:12]:
            payment, payment_id = self.payment(coin)
            result = node.requesttxvalidation(payment)
            assert_equal(result["status"], "requested")
            retained.append(payment_id)
            assert node.getbuspoolinfo()["size"] <= 4
            assert node.getbuspoolinfo()["bytes"] <= node.getbuspoolinfo()["maxbytes"]
            assert payment_id not in node.getrawmempool()
        assert_equal(node.getbuspoolinfo()["size"], 4)
        assert_raises_rpc_error(-5, "not retained", node.getbuspoolentry, retained[0])

        self.log.info("Mine a mixed template using a real recovered certificate and retained bodies")
        mining_raw, _ = self.payment(coins[13])
        mining_certificate = self.recover(mining_raw)
        exercise_certified_mining(self, node, mining_raw, mining_certificate)
        certified_blocks = [node.getbestblockhash()]

        self.log.info("Demonstrate the delegated script trust boundary with a controlled quorum")
        unsigned, unsigned_id = self.payment(coins[5], signed=False)
        statement = node.requesttxvalidation(unsigned)
        assert not statement["positive"]
        proof, positive_message = statement_with_result(statement["hex"], True)
        for mn in self.mninfo:
            assert mn.node.quorum("sign", LLMQ_TEST_TYPE, statement["requestid"], positive_message, statement["quorumhash"])
        recovered = self.get_recovered_sig(statement["requestid"], positive_message)
        proof[140:] = bytes.fromhex(recovered["sig"])
        assert_raises_rpc_error(-26, "script", node.sendrawtransaction, unsigned)
        exercise_certified_mining(self, node, unsigned, {"hex": proof.hex()},
                                  eviction_bodies=[self.payment(coin)[0] for coin in coins[6:12]])
        assert unsigned_id in node.getblock(node.getbestblockhash())["tx"]
        certified_blocks.append(node.getbestblockhash())
        exercise_transport_mining_limits(self)
        self.check_historical_replay(certified_blocks)


if __name__ == "__main__":
    TxValidationTest().main()
