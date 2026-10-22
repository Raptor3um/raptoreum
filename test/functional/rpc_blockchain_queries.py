#!/usr/bin/env python3
# Copyright (c) 2020-2026 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Block query RPCs inherited from Dash that no test touched.

getblockheaders, getmerkleblocks and getspecialtxes are all registered in the
blockchain category and none of them was reached by any test in the suite.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_raises_rpc_error,
)

# A serialised CBloomFilter: eight bytes of vData with every bit set, one hash
# function, no tweak, no flags. Every query against it matches, so every block
# comes back.
MATCH_ALL_FILTER = "08" + "ff" * 8 + "01000000" + "00000000" + "00"

# The filter from the RPC's own help, which matches nothing on this chain.
HELP_FILTER = ("2303028005802040100040000008008400048141010000f84004208000800250"
               "04000004130000000000000001")

# Coinbase transactions are CbTx, which is special transaction type 5.
TRANSACTION_COINBASE = 5


class BlockchainQueriesTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-txindex"]]

    def run_test(self):
        node = self.nodes[0]
        self.address = node.getnewaddress()
        node.generatetoaddress(20, self.address)

        self.test_getblockheaders()
        self.test_getmerkleblocks()
        self.test_getspecialtxes()

    def test_getblockheaders(self):
        node = self.nodes[0]
        start = node.getblockhash(1)

        self.log.info("getblockheaders walks forward from the hash it is given")
        headers = node.getblockheaders(start, 3)
        assert_equal(len(headers), 3)
        assert_equal(headers[0]["hash"], start)
        assert_equal(headers[0]["height"], 1)

        self.log.info("Each header names the one before it")
        for earlier, later in zip(headers, headers[1:]):
            assert_equal(later["previousblockhash"], earlier["hash"])
            assert_equal(later["height"], earlier["height"] + 1)

        self.log.info("It agrees with getblockheader on the same block")
        assert_equal(headers[0], node.getblockheader(start))

        self.log.info("Unverbose returns the raw 80-byte headers")
        raw = node.getblockheaders(start, 2, False)
        assert_equal(len(raw), 2)
        for entry in raw:
            assert_equal(len(entry), 160)
        assert_equal(raw[0], node.getblockheader(start, False))

        self.log.info("The count is capped by how far the chain reaches")
        tip = node.getbestblockhash()
        assert_equal(len(node.getblockheaders(tip, 50)), 1)

        assert_raises_rpc_error(-5, "Block not found", node.getblockheaders,
                                "00" * 32, 1)

    def test_getmerkleblocks(self):
        node = self.nodes[0]
        tip = node.getbestblockhash()

        self.log.info("A filter that matches everything returns every block asked for")
        start = node.getblockhash(1)
        blocks = node.getmerkleblocks(MATCH_ALL_FILTER, start, 3)
        assert_equal(len(blocks), 3)

        self.log.info("Each one opens with the header of its block")
        headers = node.getblockheaders(start, 3, False)
        for merkleblock, header in zip(blocks, headers):
            assert_equal(merkleblock[:160], header)

        self.log.info("A filter that matches nothing returns nothing")
        assert_equal(node.getmerkleblocks(HELP_FILTER, start, 3), [])

        assert_raises_rpc_error(-5, "Block not found", node.getmerkleblocks,
                                MATCH_ALL_FILTER, "00" * 32, 1)

    def test_getspecialtxes(self):
        node = self.nodes[0]
        tip = node.getbestblockhash()

        self.log.info("Every block here holds exactly one special transaction, its CbTx")
        txids = node.getspecialtxes(tip)
        assert_equal(len(txids), 1)
        assert_equal(txids[0], node.getblock(tip)["tx"][0])

        self.log.info("Filtering by type finds it, and by another type finds nothing")
        assert_equal(node.getspecialtxes(tip, TRANSACTION_COINBASE), txids)
        assert_equal(node.getspecialtxes(tip, TRANSACTION_COINBASE + 1), [])

        self.log.info("Verbosity climbs from txid to hash to decoded transaction")
        decoded = node.getspecialtxes(tip, TRANSACTION_COINBASE, 10, 0, 2)
        assert_equal(decoded[0]["txid"], txids[0])
        assert_equal(decoded[0]["type"], TRANSACTION_COINBASE)
        assert_greater_than(decoded[0]["size"], 0)

        self.log.info("Skipping past the only entry leaves nothing")
        assert_equal(node.getspecialtxes(tip, TRANSACTION_COINBASE, 10, 1), [])

        assert_raises_rpc_error(-5, "Block not found", node.getspecialtxes, "00" * 32)


if __name__ == '__main__':
    BlockchainQueriesTest().main()
