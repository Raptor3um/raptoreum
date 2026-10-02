#!/usr/bin/env python3
# Copyright (c) 2026 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise harness failure detection without starting a node.

Run directly through test_runner.py, or with:
PYTHONPATH=test/functional python3 -m unittest feature_framework
"""

import contextlib
import io
import logging
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

import test_runner
from test_framework.test_node import KNOWN_STARTUP_WARNINGS, TestNode


class HarnessChecks(unittest.TestCase):
    def make_node(self, datadir):
        return TestNode(0, str(datadir), [], 'regtest', None, 2,
                        sys.executable, '', None, 0, None, extra_args=[])

    def test_ci_runs_a_real_child_process(self):
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()):
            scripts = Path(directory, 'test', 'functional')
            scripts.mkdir(parents=True)
            marker = Path(directory, 'probe')
            (scripts / 'feature_probe.py').write_text(
                'from pathlib import Path\nPath(%r).write_text("executed")\n' % str(marker), encoding='utf-8')
            run_dir = Path(directory, 'run')
            run_dir.mkdir()
            with self.assertRaises(SystemExit) as result, patch.object(
                    logging.getLogger(), 'handlers', [logging.NullHandler()]):
                test_runner.run_tests(test_list=['feature_probe.py'], src_dir=directory,
                                      build_dir=directory, tmpdir=str(run_dir), runs_ci=True)
            self.assertEqual(result.exception.code, 0)
            self.assertEqual(marker.read_text(), 'executed')

    def test_stderr_lines_after_known_warnings(self):
        # Combinations that test/util/test_node-test.py does not cover.
        warning = KNOWN_STARTUP_WARNINGS[0]
        for diagnostic, expected, fails in (
                (warning + '\nexpected diagnostic', 'expected diagnostic', False),
                ('first\nsecond', 'first\r\nsecond', False),
                (warning + '\nunexpected diagnostic', '', True),
                ('expected diagnostic\nunexpected diagnostic', 'expected diagnostic', True)):
            with self.subTest(diagnostic=diagnostic), tempfile.TemporaryDirectory() as directory:
                node = self.make_node(directory)
                node.args = [sys.executable, '-c', 'import sys; sys.stderr.write(%r)' % diagnostic]
                node.start()
                node.stop = lambda **kwargs: None
                try:
                    with self.assertRaises(AssertionError) if fails else contextlib.nullcontext():
                        node.stop_node(expected_stderr=expected)
                finally:
                    node.wait_until_stopped()

    def test_stderr_is_checked_on_direct_exit_and_reset_on_restart(self):
        with tempfile.TemporaryDirectory() as directory:
            node = self.make_node(directory)
            node.args = [sys.executable, '-c', 'import sys; sys.stderr.write("expected fatal diagnostic")']
            node.start()
            node._expected_stderr = 'expected fatal diagnostic'
            node.wait_until_stopped()
            node.args = [sys.executable, '-c', 'import sys; sys.stderr.write("unexpected")']
            node.start()
            with self.assertRaisesRegex(AssertionError, 'unexpected'):
                node.wait_until_stopped()
            node.args = [sys.executable, '-c', 'pass']
            node.start()
            node.wait_until_stopped()

    def test_shutdown_failure_preserves_logs(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory, 'config.ini')
            config.write_text('[environment]\nBUILDDIR={}\nEXEEXT=\n'.format(directory), encoding='utf-8')
            datadir = Path(directory, 'run')
            script = '''
import sys
sys.path.insert(0, %r)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_node import TestNode

class ShutdownFailureTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 0
    def setup_chain(self):
        pass
    def setup_network(self):
        node = TestNode(0, self.options.tmpdir, [], 'regtest', None, 2,
                        sys.executable, '', None, 0, None, extra_args=[])
        node.args = [sys.executable, '-c', 'import sys; sys.stderr.write("unexpected shutdown diagnostic")']
        node.stop = lambda **kwargs: None
        node.start()
        self.nodes = [node]
    def run_test(self):
        pass

ShutdownFailureTest().main()
''' % str(Path(__file__).resolve().parent)
            result = subprocess.run(
                [sys.executable, '-c', script, '--configfile=' + str(config), '--tmpdir=' + str(datadir)],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=10,
            )
            self.assertEqual(result.returncode, 1, result.stdout)
            self.assertIn('Unexpected stderr', result.stdout)
            self.assertTrue((datadir / 'node_stderr.log').is_file(), result.stdout)
            self.assertEqual((datadir / 'node_stderr.log').read_text(), 'unexpected shutdown diagnostic')
            self.assertIn('Unexpected exception caught during shutdown',
                          (datadir / 'test_framework.log').read_text())

    def test_rss_growth_boundaries(self):
        # Boundaries that test/util/test_node-test.py does not cover.
        with tempfile.TemporaryDirectory() as directory:
            node = self.make_node(directory)
            for before, after, fails in ((100000, 150000, False), (100000, 150001, True),
                                         (100000, 90000, False), (None, 190000, False),
                                         (100000, None, False)):
                with self.subTest(before=before, after=after), patch.object(
                        node, 'get_mem_rss_kilobytes', side_effect=[before, after]):
                    with self.assertLogs(node.log, level='WARNING') if not (before and after) else contextlib.nullcontext():
                        with self.assertRaises(AssertionError) if fails else contextlib.nullcontext():
                            with node.assert_memory_usage_stable(increase_allowed=0.5):
                                pass

    def test_debug_log_waits_for_delayed_messages(self):
        with tempfile.TemporaryDirectory() as directory:
            node = self.make_node(directory)
            log = Path(directory, 'regtest', 'debug.log')
            log.parent.mkdir()
            log.touch()
            writer = threading.Timer(0.05, lambda: log.write_text('remote action completed\n', encoding='utf-8'))
            try:
                with node.assert_debug_log(['remote action completed'], timeout=1):
                    writer.start()
            finally:
                if writer.ident is not None:
                    writer.join()
            with self.assertRaises(AssertionError):
                with node.assert_debug_log(['missing message'], timeout=0.05):
                    pass
            with self.assertRaisesRegex(RuntimeError, 'body failed'):
                with node.assert_debug_log(['completed']):
                    with log.open('a', encoding='utf-8') as output:
                        output.write('completed\n')
                    raise RuntimeError('body failed')


if __name__ == '__main__':
    from test_framework.test_framework import BitcoinTestFramework

    class FrameworkTest(BitcoinTestFramework):
        def set_test_params(self):
            self.num_nodes = 0
            self.setup_clean_chain = True

        def setup_network(self):
            pass

        def run_test(self):
            result = unittest.TextTestRunner(stream=sys.stdout).run(
                unittest.defaultTestLoader.loadTestsFromTestCase(HarnessChecks))
            assert result.wasSuccessful(), 'Harness regression checks failed'

    FrameworkTest().main()
