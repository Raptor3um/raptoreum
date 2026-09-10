#!/usr/bin/env python3
# Copyright (c) 2026 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test future transactions (TRANSACTION_FUTURE).

Futures are Raptoreum's own, with no counterpart in Dash or Bitcoin, and had no
functional coverage at all. A future locks one of its outputs until it matures.

src/consensus/tx_verify.cpp validateFutureCoin() decides when that output may be
spent:

    isBlockMature = maturity >= 0 and nSpendHeight - coin.nHeight >= maturity
    isTimeMature  = lockTime >= 0 and now - confirmedTime        >= lockTime

whichever comes first. A negative value switches that path off, so a future with
maturity 3 and lockTime -1 can only be released by blocks. Spending early is
rejected with bad-txns-premature-spend-of-future. Only the output named by
lockOutputIndex is locked; the change output is spendable straight away.
"""

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
    set_node_times,
    wait_until,
)

# src/primitives/transaction.h
TRANSACTION_FUTURE = 7

# Comfortably above the relay minimum and well under -maxtxfee.
FEE = Decimal("0.001")


class FuturesTest(BitcoinTestFramework):
    def set_test_params(self):
        # One node. Futures maturity is decided locally by consensus, so a peer
        # adds nothing, and the time-maturity check jumps the clock forward,
        # which makes block sync between nodes unreliable.
        self.num_nodes = 1
        # node0 signs sporks, which the SPORK_22 special-fee check below needs.
        self.extra_args = [["-sporkkey=cVpnZj4dZvRXmBf7Jze1GjpLQb25iKP92GDXUsKdUJTXhXRo2RFA"]]

    def set_spork(self, name, value):
        """Only node0's view matters: it signs the spork and builds the futures.
        Waiting for propagation to the other node is unreliable here because the
        time-maturity check above has already jumped the clocks forward."""
        self.nodes[0].spork(name, value)
        wait_until(lambda: self.nodes[0].spork('show')[name] == value)

    def send_future(self, maturity, locktime, amount=100):
        """Send a future to this wallet and confirm it. Returns (txid, payload)."""
        node = self.nodes[0]
        address = node.getnewaddress()
        txid = node.sendtoaddress(
            address=address,
            amount=amount,
            future={"future_maturity": maturity, "future_locktime": locktime},
        )
        assert txid in node.getrawmempool(), "the future was not accepted"
        blockhash = node.generate(1)[0]
        # Pass the block hash: these nodes run without -txindex, so a confirmed
        # transaction cannot be looked up by txid alone.
        tx = node.getrawtransaction(txid, 1, blockhash)
        assert_equal(tx["type"], TRANSACTION_FUTURE)
        return txid, tx

    def spend_locked_output(self, txid, tx):
        """Raw transaction spending only the locked output, signed and ready."""
        node = self.nodes[0]
        n = tx["futureTx"]["lockOutputIndex"]
        value = tx["vout"][n]["value"]
        raw = node.createrawtransaction(
            [{"txid": txid, "vout": n}],
            {node.getnewaddress(): value - FEE},
        )
        return node.signrawtransactionwithwallet(raw)["hex"]

    def run_test(self):
        node = self.nodes[0]

        self.test_wallet_side()

        self.log.info("The payload records what was asked for")
        txid, tx = self.send_future(maturity=3, locktime=-1)
        payload = tx["futureTx"]
        assert_equal(payload["maturity"], 3)
        assert_equal(payload["lockTime"], -1)
        # The locked output is one of the transaction's own outputs.
        assert_greater_than(len(tx["vout"]), payload["lockOutputIndex"])

        self.log.info("The locked output cannot be spent before it matures")
        spend = self.spend_locked_output(txid, tx)
        # One confirmation so far, so nSpendHeight - coin.nHeight is 1, under 3.
        assert_raises_rpc_error(-26, "bad-txns-premature-spend-of-future",
                                node.sendrawtransaction, spend)

        # Consensus is all that enforces futures today. The wallet side is
        # covered separately at the end of this test.

        self.log.info("Blocks release it once maturity confirmations are reached")
        node.generate(2)
        node.sendrawtransaction(spend)
        node.generate(1)

        self.log.info("Time can release a future on its own")
        # maturity -1 switches the block path off, so only lockTime can mature it.
        lock_seconds = 3600
        txid, tx = self.send_future(maturity=-1, locktime=lock_seconds)
        spend = self.spend_locked_output(txid, tx)
        assert_raises_rpc_error(-26, "bad-txns-premature-spend-of-future",
                                node.sendrawtransaction, spend)

        confirmed_time = node.getblock(node.getbestblockhash())["time"]
        # Mining alone must not help: the block path is disabled.
        node.generate(10)
        assert_raises_rpc_error(-26, "bad-txns-premature-spend-of-future",
                                node.sendrawtransaction, spend)

        # Move every node's clock: bumping only one leaves the others behind and
        # the next block sync stalls.
        set_node_times(self.nodes, confirmed_time + lock_seconds)
        node.sendrawtransaction(spend)

        self.log.info("SPORK_22 adds a special fee on top of the miner fee")
        # getFutureFees() is 0 while the spork is off, which is why every payload
        # above carried fee 0. src/future/fee.cpp takes the low byte of the spork
        # value as a whole number of RTM, and checkSpecialTxFee() in
        # src/consensus/tx_verify.cpp requires the payload to match it exactly.
        assert_equal(self.nodes[0].spork('show')['SPORK_22_SPECIAL_TX_FEE'], 4070908800)
        _, tx = self.send_future(maturity=1, locktime=-1)
        assert_equal(tx["futureTx"]["fee"], 0)

        special_fee = 3
        self.set_spork("SPORK_22_SPECIAL_TX_FEE", special_fee)
        _, tx = self.send_future(maturity=1, locktime=-1)
        assert_equal(tx["futureTx"]["fee"], special_fee)

        self.set_spork("SPORK_22_SPECIAL_TX_FEE", 4070908800)

        self.log.info("Only the locked output is locked; change is not")
        txid, tx = self.send_future(maturity=1000, locktime=-1)
        locked = tx["futureTx"]["lockOutputIndex"]
        change = [v["n"] for v in tx["vout"] if v["n"] != locked]
        assert change, "expected the future to have a change output"
        # Spending the change is accepted straight away, while the locked output
        # of the very same transaction is not.
        n = change[0]
        raw = node.createrawtransaction(
            [{"txid": txid, "vout": n}],
            {node.getnewaddress(): tx["vout"][n]["value"] - FEE},
        )
        node.sendrawtransaction(node.signrawtransactionwithwallet(raw)["hex"])
        assert_raises_rpc_error(-26, "bad-txns-premature-spend-of-future",
                                node.sendrawtransaction,
                                self.spend_locked_output(txid, tx))

        self.log.info("Both future fields are required together")
        address = node.getnewaddress()
        assert_raises_rpc_error(-8, "no future_locktime is specified",
                                node.sendtoaddress, address=address, amount=1,
                                future={"future_maturity": 1})
        assert_raises_rpc_error(-8, "no future_maturity is specified",
                                node.sendtoaddress, address=address, amount=1,
                                future={"future_locktime": 1})

    def test_wallet_side(self):
        """What the wallet does with an output consensus will not let it spend.

        AvailableCoins hands the locked output to its callers with
        isFutureSpendable false rather than skipping it, and the path
        sendtoaddress takes does not check that flag. So the wallet selects the
        coin, signs it, files it, and hands back a txid for a transaction the
        node has already refused. Money that looks sent stays put.

        These assertions pin that as it stands. They are meant to fail loudly
        once the wallet honours the lock.
        """
        node = self.nodes[0]

        self.log.info("The wallet flags a locked output but still calls it spendable")
        amount = 100
        txid, tx = self.send_future(maturity=5, locktime=-1, amount=amount)
        locked = (txid, tx["futureTx"]["lockOutputIndex"])

        entry = {(u["txid"], u["vout"]): u for u in node.listunspent(0)}[locked]
        assert_equal(entry["future"], True)
        assert_equal(entry["futureSpendable"], False)
        assert_equal(entry["spendable"], True)

        self.log.info("Coin selection takes it anyway, and the node refuses the result")
        # Just under the locked amount, so that output is the natural single input.
        with node.assert_debug_log(["bad-txns-premature-spend-of-future"]):
            spent = node.sendtoaddress(node.getnewaddress(), amount - 1)
        # getrawtransaction cannot see it: it never reached the mempool and these
        # nodes run without -txindex. The wallet kept it, so read it from there.
        decoded = node.decoderawtransaction(node.gettransaction(spent)["hex"])
        inputs = [(v["txid"], v["vout"]) for v in decoded["vin"]]
        assert locked in inputs, "expected coin selection to reach for the locked output"

        self.log.info("The wallet keeps the transaction the network will not carry")
        assert spent not in node.getrawmempool()
        assert_equal(node.gettransaction(spent)["confirmations"], 0)

        self.log.info("Maturity releases the output and clears the flag")
        node.generate(5)
        node.getwalletinfo()
        entry = {(u["txid"], u["vout"]): u for u in node.listunspent(0)}.get(locked)
        if entry is not None:
            assert_equal(entry["futureSpendable"], True)


if __name__ == '__main__':
    FuturesTest().main()
