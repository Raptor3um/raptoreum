#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test raptoreum-wallet."""
import subprocess
import textwrap

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

class ToolWalletTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def raptoreum_wallet_process(self, *args):
        binary = self.config["environment"]["BUILDDIR"] + '/src/raptoreum-wallet' + self.config["environment"]["EXEEXT"]
        args = ['-datadir={}'.format(self.nodes[0].datadir), '-regtest'] + list(args)
        return subprocess.Popen([binary] + args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)

    def assert_raises_tool_error(self, error, *args):
        p = self.raptoreum_wallet_process(*args)
        stdout, stderr = p.communicate()
        assert_equal(p.poll(), 1)
        assert_equal(stdout, '')
        assert_equal(stderr.strip(), error)

    def assert_tool_output(self, output, *args):
        p = self.raptoreum_wallet_process(*args)
        stdout, stderr = p.communicate()
        assert_equal(p.poll(), 0)
        assert_equal(stderr, '')
        assert_equal(stdout, output)

    def run_test(self):

        self.assert_raises_tool_error('Invalid command: foo', 'foo')
        # `raptoreum-wallet help` is an error. Use `raptoreum-wallet -help`
        self.assert_raises_tool_error('Invalid command: help', 'help')
        self.assert_raises_tool_error('Error: two methods provided (info and create). Only one method should be provided.', 'info', 'create')
        self.assert_raises_tool_error('Error parsing command line arguments: Invalid parameter -foo', '-foo')
        self.assert_raises_tool_error('Error loading wallet.dat. Is wallet being used by other process?', '-wallet=wallet.dat', 'info')
        self.assert_raises_tool_error('Error: no wallet file at nonexistent.dat', '-wallet=nonexistent.dat', 'info')

        # stop the node to close the wallet to call info command
        self.stop_node(0)

        out = textwrap.dedent('''\
            Wallet info
            ===========
            Encrypted: no
            HD (hd seed available): no
            Keypool Size: 1
            Transactions: 0
            Address Book: 0
        ''')
        self.assert_tool_output(out, '-wallet=wallet.dat', 'info')

        self.start_node(0)
        self.nodes[0].upgradetohd()
        self.stop_node(0)

        out = textwrap.dedent('''\
            Wallet info
            ===========
            Encrypted: no
            HD (hd seed available): yes
            Keypool Size: 2
            Transactions: 0
            Address Book: 0
        ''')
        self.assert_tool_output(out, '-wallet=wallet.dat', 'info')

        # mutate the wallet to check the info command output changes accordingly
        self.start_node(0)
        self.nodes[0].generate(1)
        self.stop_node(0)

        out = textwrap.dedent('''\
            Wallet info
            ===========
            Encrypted: no
            HD (hd seed available): yes
            Keypool Size: 1
            Transactions: 1
            Address Book: 0
        ''')
        self.assert_tool_output(out, '-wallet=wallet.dat', 'info')

        out = textwrap.dedent('''\
            Topping up keypool...
            Wallet info
            ===========
            Encrypted: no
            HD (hd seed available): no
            Keypool Size: 1000
            Transactions: 0
            Address Book: 0
        ''')
        self.assert_tool_output(out, '-wallet=foo', 'create')

        self.start_node(0, ['-wallet=foo'])
        out = self.nodes[0].getwalletinfo()
        self.stop_node(0)

        assert_equal(0, out['txcount'])
        assert_equal(1000, out['keypoolsize'])

        self.log_wallet_timestamp_comparison(timestamp_before, timestamp_after)
        assert_equal(timestamp_before, timestamp_after)
        assert_equal(shasum_after, shasum_before)
        self.log.debug('Wallet file shasum unchanged\n')

    def test_salvage(self):
        # TODO: Check salvage actually salvages and doesn't break things. https://github.com/bitcoin/bitcoin/issues/7463
        self.log.info('Check salvage')
        self.start_node(0, ['-wallet=salvage'])
        self.stop_node(0)

        self.assert_tool_output('', '-wallet=salvage', 'salvage')

    def run_test(self):
        self.wallet_path = os.path.join(self.nodes[0].datadir, 'regtest', 'wallets', 'wallet.dat')
        self.test_invalid_tool_commands_and_args()
        # Warning: The following tests are order-dependent.
        self.test_tool_wallet_info()
        self.test_tool_wallet_info_after_transaction()
        self.test_tool_wallet_create_on_existing_wallet()
        self.test_getwalletinfo_on_different_wallet()
        self.test_salvage()

if __name__ == '__main__':
    ToolWalletTest().main()
