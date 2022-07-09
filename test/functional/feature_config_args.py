#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test various command line arguments and configuration file parameters."""

import os
import re

from test_framework.test_framework import BitcoinTestFramework


class ConfArgsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def run_test(self):
        self.stop_node(0)
        # Remove the -datadir argument so it doesn't override the config file
        self.nodes[0].args = [arg for arg in self.nodes[0].args if not arg.startswith("-datadir")]

        default_data_dir = self.nodes[0].datadir
        new_data_dir = os.path.join(default_data_dir, 'newdatadir')
        new_data_dir_2 = os.path.join(default_data_dir, 'newdatadir2')

        # Check that using -datadir argument on non-existent directory fails
        self.nodes[0].datadir = new_data_dir
        self.nodes[0].assert_start_raises_init_error(['-datadir=' + new_data_dir], 'Error: Specified data directory "' + re.escape(new_data_dir) + '" does not exist.')

        # Check that using non-existent datadir in conf file fails
        conf_file = os.path.join(default_data_dir, "dash.conf")

        # datadir needs to be set before [chain] section
        conf_file_contents = open(conf_file, encoding='utf8').read()
        with open(conf_file, 'w', encoding='utf8') as f:
            f.write("datadir=" + new_data_dir + "\n")
            f.write(conf_file_contents)

        self.nodes[0].assert_start_raises_init_error(['-conf=' + conf_file], 'Error reading configuration file: specified data directory "' + re.escape(new_data_dir) + '" does not exist.')

        # Create the directory and ensure the config file now works
        os.mkdir(new_data_dir)
        self.start_node(0, ['-conf='+conf_file, '-wallet=w1'])
        self.stop_node(0)
        assert os.path.exists(os.path.join(new_data_dir, self.chain, 'wallets', 'w1'))

        # Ensure command line argument overrides datadir in conf
        os.mkdir(new_data_dir_2)
        self.nodes[0].datadir = new_data_dir_2
        self.start_node(0, ['-datadir='+new_data_dir_2, '-conf='+conf_file, '-wallet=w2'])
        assert os.path.exists(os.path.join(new_data_dir_2, self.chain, 'wallets', 'w2'))

if __name__ == '__main__':
    ConfArgsTest().main()
