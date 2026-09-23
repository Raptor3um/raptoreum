#!/usr/bin/env python3
# Copyright (c) 2020-2023 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test test_framework.test_node's stderr handling and RSS growth check.

Drives the real TestNode.stop_node() and TestNode.assert_memory_usage_stable()
against a stand-in "node" process, rather than a mock of TestNode itself, so
these tests exercise the actual code the functional suite runs.
"""
import os
import shlex
import shutil
import stat
import sys
import tempfile
import textwrap
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.realpath(__file__)), "..", "functional"))
from test_framework.test_node import TestNode, KNOWN_STARTUP_WARNINGS  # noqa: E402


def make_fake_node_binary(datadir, stderr_text=""):
    """A stand-in for raptoreumd: writes stderr_text to stderr and exits 0."""
    path = os.path.join(datadir, "fake_node.sh")
    with open(path, "w", encoding="utf-8") as f:
        f.write(textwrap.dedent("""\
            #!/bin/sh
            if [ -n {text} ]; then
                >&2 printf '%s' {text}
            fi
            exit 0
            """).format(text=shlex.quote(stderr_text)))
    os.chmod(path, os.stat(path).st_mode | stat.S_IEXEC)
    return path


class TestNodeStderrTest(unittest.TestCase):
    def make_node(self, stderr_text=""):
        datadir = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, datadir, ignore_errors=True)
        binary = make_fake_node_binary(datadir, stderr_text)
        node = TestNode(0, datadir, [], "regtest", None, None,
                         binary, "raptoreum-cli", None, 0, None, extra_args=[])
        node.args = [binary]
        node.start()
        node.stop = lambda wait=0: None  # bypass the real RPC stop call
        node.cleanup_on_exit = False  # the stand-in has already exited on its own
        return node

    def test_empty_expected_stderr_passes_with_no_output(self):
        node = self.make_node(stderr_text="")
        node.stop_node(expected_stderr='')  # must not raise

    def test_empty_expected_stderr_rejects_unexpected_output(self):
        node = self.make_node(stderr_text="unexpected diagnostic")
        with self.assertRaises(AssertionError):
            node.stop_node(expected_stderr='')

    def test_empty_expected_stderr_allows_known_startup_warning(self):
        # The real warning raptoreumd writes for an unlocked -usehd=1 wallet.
        # Reproduced end-to-end against a real node in wallet_basic.py; this
        # pins the filtering logic itself.
        node = self.make_node(stderr_text=KNOWN_STARTUP_WARNINGS[0])
        node.stop_node(expected_stderr='')  # must not raise

    def test_matching_expected_stderr_passes(self):
        node = self.make_node(stderr_text="expected warning")
        node.stop_node(expected_stderr='expected warning')  # must not raise

    def test_mismatched_expected_stderr_is_rejected(self):
        node = self.make_node(stderr_text="expected warning")
        with self.assertRaises(AssertionError):
            node.stop_node(expected_stderr='a different warning')


class MemoryUsageStableTest(unittest.TestCase):
    def make_node(self):
        # assert_memory_usage_stable only calls get_mem_rss_kilobytes(), which
        # every test here replaces outright -- no real process is needed.
        return TestNode(0, "/nonexistent", [], "regtest", None, None,
                         "raptoreumd", "raptoreum-cli", None, 0, None, extra_args=[])

    def readings(self, before, after):
        values = iter([before, after])
        return lambda: next(values)

    def test_small_growth_within_threshold_passes(self):
        node = self.make_node()
        node.get_mem_rss_kilobytes = self.readings(100000, 104000)
        with node.assert_memory_usage_stable(increase_allowed=0.05):
            pass  # must not raise

    def test_large_growth_is_flagged(self):
        # The exact reported case: 100000 -> 190000 KB is 90% growth. The old
        # formula (1 - before/after) computed ~47% here and let it through.
        node = self.make_node()
        node.get_mem_rss_kilobytes = self.readings(100000, 190000)
        with self.assertRaises(AssertionError):
            with node.assert_memory_usage_stable(increase_allowed=0.5):
                pass


if __name__ == '__main__':
    unittest.main()
