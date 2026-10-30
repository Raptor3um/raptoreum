#!/usr/bin/env python3
# Copyright (c) 2020-2023 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test test_runner's check_script_list().

Drives the real function against a temporary script directory and a
monkeypatched ALL_SCRIPTS/DISABLED_SCRIPTS/NON_SCRIPTS, rather than a mock of
check_script_list() itself, so these tests exercise the actual code the
functional suite's own completeness check runs under --ci.
"""
import contextlib
import io
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.realpath(__file__)), "..", "functional"))
import test_runner  # noqa: E402


class CheckScriptListTest(unittest.TestCase):
    def make_src_dir(self, script_names):
        """A temporary tree with an empty .py file per name under test/functional/."""
        src_dir = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, src_dir, ignore_errors=True)
        script_dir = os.path.join(src_dir, "test", "functional")
        os.makedirs(script_dir)
        for name in script_names:
            open(os.path.join(script_dir, name), "w", encoding="utf-8").close()
        return src_dir

    def set_lists(self, *, all_scripts=(), disabled_scripts=(), non_scripts=()):
        for attr, value in (("ALL_SCRIPTS", list(all_scripts)),
                            ("DISABLED_SCRIPTS", list(disabled_scripts)),
                            ("NON_SCRIPTS", list(non_scripts))):
            original = getattr(test_runner, attr)
            self.addCleanup(setattr, test_runner, attr, original)
            setattr(test_runner, attr, value)

    def run_captured(self, *, src_dir, fail_on_warn):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            test_runner.check_script_list(src_dir=src_dir, fail_on_warn=fail_on_warn)
        return out.getvalue()

    # The reviewer's own three-file ask: registered, disabled, forgotten.
    def test_registered_and_disabled_scripts_produce_no_warning(self):
        src_dir = self.make_src_dir(["test_registered_script.py", "test_disabled_script.py"])
        self.set_lists(all_scripts=["test_registered_script.py"],
                       disabled_scripts=["test_disabled_script.py"])
        output = self.run_captured(src_dir=src_dir, fail_on_warn=False)
        self.assertEqual(output, "")

    def test_forgotten_script_warns_but_does_not_abort_without_ci(self):
        src_dir = self.make_src_dir(["test_forgotten_script.py"])
        self.set_lists()
        output = self.run_captured(src_dir=src_dir, fail_on_warn=False)
        self.assertIn("test_forgotten_script.py", output)

    def test_forgotten_script_aborts_under_ci(self):
        src_dir = self.make_src_dir(["test_forgotten_script.py"])
        self.set_lists()
        with self.assertRaises(SystemExit) as cm:
            self.run_captured(src_dir=src_dir, fail_on_warn=True)
        self.assertEqual(cm.exception.code, 1)

    # A name in both ALL_SCRIPTS and DISABLED_SCRIPTS would run while
    # claiming to be disabled.
    def test_script_listed_as_both_registered_and_disabled_warns_but_does_not_abort_without_ci(self):
        src_dir = self.make_src_dir(["dup.py"])
        self.set_lists(all_scripts=["dup.py"], disabled_scripts=["dup.py"])
        output = self.run_captured(src_dir=src_dir, fail_on_warn=False)
        self.assertIn("dup.py", output)

    def test_script_listed_as_both_registered_and_disabled_aborts_under_ci(self):
        src_dir = self.make_src_dir(["dup.py"])
        self.set_lists(all_scripts=["dup.py"], disabled_scripts=["dup.py"])
        with self.assertRaises(SystemExit) as cm:
            self.run_captured(src_dir=src_dir, fail_on_warn=True)
        self.assertEqual(cm.exception.code, 1)

    # A NON_SCRIPTS entry (a known non-test file, e.g. combine_logs.py)
    # mistakenly also listed in DISABLED_SCRIPTS is the same mistake by a
    # different route -- neither the missed-tests check (the file exists)
    # nor the stale-entry check (it's still a real file) would catch it
    # without this.
    def test_script_listed_as_both_non_script_and_disabled_warns_but_does_not_abort_without_ci(self):
        src_dir = self.make_src_dir(["helper.py"])
        self.set_lists(non_scripts=["helper.py"], disabled_scripts=["helper.py"])
        output = self.run_captured(src_dir=src_dir, fail_on_warn=False)
        self.assertIn("helper.py", output)

    def test_script_listed_as_both_non_script_and_disabled_aborts_under_ci(self):
        src_dir = self.make_src_dir(["helper.py"])
        self.set_lists(non_scripts=["helper.py"], disabled_scripts=["helper.py"])
        with self.assertRaises(SystemExit) as cm:
            self.run_captured(src_dir=src_dir, fail_on_warn=True)
        self.assertEqual(cm.exception.code, 1)

    # A DISABLED_SCRIPTS entry whose file has since been deleted should not
    # go unnoticed.
    def test_stale_disabled_entry_warns_but_does_not_abort_without_ci(self):
        src_dir = self.make_src_dir([])
        self.set_lists(disabled_scripts=["ghost.py"])
        output = self.run_captured(src_dir=src_dir, fail_on_warn=False)
        self.assertIn("ghost.py", output)

    def test_stale_disabled_entry_aborts_under_ci(self):
        src_dir = self.make_src_dir([])
        self.set_lists(disabled_scripts=["ghost.py"])
        with self.assertRaises(SystemExit) as cm:
            self.run_captured(src_dir=src_dir, fail_on_warn=True)
        self.assertEqual(cm.exception.code, 1)


if __name__ == '__main__':
    unittest.main()
