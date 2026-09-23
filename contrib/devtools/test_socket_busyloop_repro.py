#!/usr/bin/env python3
"""Check the reproducer's protocol and failure reporting without a node."""
import importlib.util
import contextlib
import io
from pathlib import Path
import socket
import sys
import threading
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "repro", Path(__file__).with_name("repro-socket-busyloop.py"))
repro = importlib.util.module_from_spec(spec)
spec.loader.exec_module(repro)

VERSION_SIZE = len(repro.frame(b"version", repro.version_payload()))
VERACK_SIZE = len(repro.frame(b"verack", b""))
HANDSHAKE = repro.frame(b"version", repro.version_payload()) + repro.frame(b"verack", b"")


def read_exact(sock, size):
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            break
        data.extend(chunk)
    return bytes(data)


def drain(sock):
    try:
        while sock.recv(65536):
            pass
    except OSError:
        pass


class ShortReadSocket:
    """Return at most `step` bytes per recv(), then EOF once the data is consumed."""

    def __init__(self, data, step):
        self.data, self.step = data, step

    def recv(self, size):
        chunk = self.data[:min(size, self.step)]
        self.data = self.data[len(chunk):]
        return chunk


class ReproducerTest(unittest.TestCase):
    def run_main(self, peer, *args, cpu=0):
        """Run main() against a peer thread; return its exit status, stdout and stderr."""
        writer, reader = socket.socketpair()
        thread = threading.Thread(target=peer, args=(reader,))
        thread.start()
        output, errors = io.StringIO(), io.StringIO()
        cpu = {"side_effect": cpu} if isinstance(cpu, list) else {"return_value": cpu}
        try:
            with patch.object(sys, "argv", ["repro", "--port", "19940", "--pid", "1", *args]), \
                    patch.object(repro, "thread_cpu", **cpu), \
                    patch.object(socket, "create_connection", return_value=writer), \
                    contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
                code = repro.main()
        finally:
            writer.close()
            thread.join(timeout=2)
        self.assertFalse(thread.is_alive())
        return code, output.getvalue(), errors.getvalue()

    def test_invalid_setup_has_a_distinct_exit_code(self):
        errors = io.StringIO()
        with patch.object(sys, "argv", ["repro", "--port", "19940", "--pid", "1"]), \
                contextlib.redirect_stderr(errors):
            with patch.object(repro, "thread_cpu", return_value=None):
                self.assertEqual(repro.main(), 2)
            self.assertIn("no thread named", errors.getvalue())
            with patch.object(repro, "thread_cpu", return_value=0), \
                    patch.object(socket, "create_connection", side_effect=ConnectionRefusedError):
                self.assertEqual(repro.main(), 2)
        with patch.object(sys, "argv", ["repro", "--port", "19940", "--pid", "1", "--seconds", "0"]), \
                contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as result:
                repro.main()
        self.assertEqual(result.exception.code, 2)

    def test_network_magic(self):
        for network, magic in (("main", "72746d2e"), ("test", "7472746d"),
                               ("regtest", "fcc1b7dc")):
            self.assertEqual(repro.frame(b"verack", b"", network)[:4].hex(), magic)

    def test_disconnect_is_a_failed_workload(self):
        for worker in (repro.flood_pause, repro.flood_sendqueue):
            writer, reader = socket.socketpair()
            reader.close()
            stop = threading.Event()
            written = repro.Written()
            with writer:
                worker(writer, stop, "regtest", written)
            self.assertEqual(written.n, 0)
            self.assertIsNotNone(written.error)
            self.assertTrue(stop.is_set())

    def test_handshake_close_is_not_a_result(self):
        def peer(reader):
            with reader:
                read_exact(reader, VERSION_SIZE)
        code, output, errors = self.run_main(peer)
        self.assertEqual(code, 2)
        self.assertIn("handshake failed", errors)
        self.assertEqual(output, "")

    def test_invalid_handshake_reply_is_not_a_result(self):
        # Any reply bytes are not a handshake: an open peer answering garbage
        # must not let a low CPU reading pass as a fix.
        def peer(reader):
            with reader:
                reader.sendall(b"\xff" * VERSION_SIZE)
                drain(reader)
        code, output, errors = self.run_main(peer, "--seconds", "0.2")
        self.assertEqual(code, 2)
        self.assertIn("invalid handshake message header", errors)
        self.assertEqual(output, "")

    def test_disconnect_after_handshake_cannot_report_low_cpu_success(self):
        def peer(reader):
            with reader:
                reader.sendall(HANDSHAKE)
                read_exact(reader, VERSION_SIZE + VERACK_SIZE)
        code, output, errors = self.run_main(peer, "--seconds", "1")
        self.assertEqual(code, 2)
        self.assertIn("workload interrupted", errors)
        self.assertEqual(output, "")

    def test_missing_cpu_observation_is_not_a_result(self):
        def peer(reader):
            with reader:
                reader.sendall(HANDSHAKE)
                drain(reader)
        code, output, errors = self.run_main(peer, "--seconds", "0.2", cpu=[0, 0, None])
        self.assertEqual(code, 2)
        self.assertIn("no workload or CPU observation", errors)
        self.assertEqual(output, "")

    def test_fragmented_message_and_closed_connection(self):
        # Every recv() returns fewer bytes than requested, splitting both the
        # header and the payload, so a single read cannot pass by chance.
        sock = ShortReadSocket(repro.frame(b"version", repro.version_payload()), step=7)
        self.assertEqual(repro.read_message(sock, "regtest"), b"version")
        with self.assertRaises(ConnectionError):
            repro.read_message(sock, "regtest")

    def test_invalid_message_is_rejected(self):
        for message in (repro.frame(b"verack", b"", "main"),
                        repro.frame(b"verack", b"")[:20] + bytes(4)):
            writer, reader = socket.socketpair()
            with writer, reader:
                writer.sendall(message)
                with self.assertRaises(ValueError):
                    repro.read_message(reader, "regtest")


if __name__ == "__main__":
    unittest.main()
