#!/usr/bin/env python3
# Copyright (c) 2015-2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test node responses to invalid network messages.

Ported from Dash v18.0.0. Upstream drives the network on asyncio and swaps a
connection's magic bytes inside the event loop; this tree still uses the
asyncore thread, so the swap happens under mininode_lock and each section
restarts the network thread around its connections instead.
"""
import struct

from test_framework import messages
from test_framework.mininode import (
    P2PDataStore,
    mininode_lock,
    network_thread_join,
    network_thread_start,
)
from test_framework.test_framework import BitcoinTestFramework


class msg_unrecognized:
    """Nonsensical message. Modeled after similar types in test_framework.messages."""

    command = b'badmsg'

    def __init__(self, *, str_data):
        self.str_data = str_data.encode() if not isinstance(str_data, bytes) else str_data

    def serialize(self):
        return messages.ser_string(self.str_data)

    def __repr__(self):
        return "{}(data={})".format(self.command, self.str_data)


class InvalidMessagesTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def connect(self, count=1, verack=True):
        """Open count fresh connections and start the network thread for them."""
        conns = [self.nodes[0].add_p2p_connection(P2PDataStore()) for _ in range(count)]
        network_thread_start()
        if verack:
            for conn in conns:
                conn.wait_for_verack()
        return conns[0] if count == 1 else conns

    def disconnect(self):
        self.nodes[0].disconnect_p2ps()
        network_thread_join()

    def run_test(self):
        self.test_magic_bytes()
        self.test_checksum()
        self.test_size()
        self.test_command()

        node = self.nodes[0]
        self.node = node
        conn, conn2 = self.connect(2)

        msg_limit = 3 * 1024 * 1024  # 3MB, per MAX_PROTOCOL_MESSAGE_LENGTH
        valid_data_limit = msg_limit - 5  # Account for the 4-byte length prefix

        #
        # 0.
        #
        # Send as large a message as is valid, ensure we aren't disconnected but
        # also can't exhaust resources.
        #
        msg_at_size = msg_unrecognized(str_data="b" * valid_data_limit)
        assert len(msg_at_size.serialize()) == msg_limit

        with node.assert_memory_usage_stable(increase_allowed=0.5):
            self.log.info(
                "Sending a bunch of large, junk messages to test "
                "memory exhaustion. May take a bit...")

            # Run a bunch of times to test for memory exhaustion.
            for _ in range(80):
                conn.send_message(msg_at_size)

            # Check that, even though the node is being hammered by nonsense from one
            # connection, it can still service other peers in a timely way.
            for _ in range(20):
                conn2.sync_with_ping(timeout=2)

            # Peer 1, despite serving up a bunch of nonsense, should still be connected.
            self.log.info("Waiting for node to drop junk messages.")
            conn.sync_with_ping(timeout=320)
            assert conn.is_connected

        #
        # 1.
        #
        # Send an oversized message, ensure we're disconnected.
        #
        msg_over_size = msg_unrecognized(str_data="b" * (valid_data_limit + 1))
        assert len(msg_over_size.serialize()) == (msg_limit + 1)

        # An unknown message type (or *any* message type) over
        # MAX_PROTOCOL_MESSAGE_LENGTH should result in a disconnect.
        conn.send_message(msg_over_size)
        conn.wait_for_disconnect(timeout=4)
        self.disconnect()

        #
        # 2.
        #
        # Send messages with an incorrect data size in the header.
        #
        actual_size = 100
        msg = msg_unrecognized(str_data="b" * actual_size)

        # TODO: handle larger-than cases. I haven't been able to pin down what behavior to expect.
        for wrong_size in (2, 77, 78, 79):
            self.log.info("Sending a message with incorrect size of {}".format(wrong_size))
            conn = self.connect()

            # Unmodified message should submit okay.
            conn.send_and_ping(msg)

            # A message lying about its data size results in a disconnect when the incorrect
            # data size is less than the actual size.
            #
            # TODO: why does behavior change at 78 bytes?
            #
            conn.send_raw_message(self._tweak_msg_data_size(conn, msg, wrong_size))

            # For some reason unknown to me, we sometimes have to push additional data to the
            # peer in order for it to realize a disconnect.
            try:
                conn.send_message(messages.msg_ping(nonce=123123))
            except IOError:
                pass

            conn.wait_for_disconnect(timeout=10)
            self.disconnect()

        # Node is still up.
        conn = self.connect()
        conn.sync_with_ping()
        self.disconnect()

    def test_magic_bytes(self):
        conn = self.connect()
        with mininode_lock:
            # Ignore everything from now on: it arrives with "invalid" magic bytes.
            conn._on_data = lambda: None
            conn.magic_bytes = b'\x00\x11\x22\x32'

        with self.nodes[0].assert_debug_log(['PROCESSMESSAGE: INVALID MESSAGESTART ping']):
            conn.send_message(messages.msg_ping(nonce=0xff))
            conn.wait_for_disconnect(timeout=5)
        self.disconnect()

    def test_checksum(self):
        conn = self.connect()
        # This tree logs the checksum error as
        # "ProcessMessages(badmsg, 2 bytes): CHECKSUM ERROR expected .. was ..",
        # where upstream puts the size in parentheses after the words.
        with self.nodes[0].assert_debug_log(['CHECKSUM ERROR expected 78df0a04 was ffffffff']):
            msg = conn.build_message(msg_unrecognized(str_data="d"))
            cut_len = (
                4 +  # magic
                12 +  # command
                4  # len
            )
            # modify checksum
            msg = msg[:cut_len] + b'\xff' * 4 + msg[cut_len + 4:]
            conn.send_raw_message(msg)
            conn.sync_with_ping(timeout=5)
        self.disconnect()

    def test_size(self):
        conn = self.connect()
        msg = conn.build_message(msg_unrecognized(str_data="d"))
        cut_len = (
            4 +  # magic
            12  # command
        )
        # modify len to MAX_SIZE + 1
        msg = msg[:cut_len] + struct.pack("<I", 0x02000000 + 1) + msg[cut_len + 4:]
        conn.send_raw_message(msg)
        conn.wait_for_disconnect(timeout=5)
        self.disconnect()

    def test_command(self):
        conn = self.connect()
        with self.nodes[0].assert_debug_log(['PROCESSMESSAGE: ERRORS IN HEADER']):
            msg = msg_unrecognized(str_data="d")
            msg.command = b'\xff' * 12
            msg = conn.build_message(msg)
            # Modify command
            msg = msg[:7] + b'\x00' + msg[7 + 1:]
            conn.send_raw_message(msg)
            conn.sync_with_ping(timeout=5)
        self.disconnect()

    def _tweak_msg_data_size(self, conn, message, wrong_size):
        """
        Return a raw message based on another message but with an incorrect data size in
        the message header.
        """
        raw_msg = conn.build_message(message)

        bad_size_bytes = struct.pack("<I", wrong_size)
        num_header_bytes_before_size = 4 + 12

        # Replace the correct data size in the message with an incorrect one.
        raw_msg_with_wrong_size = (
            raw_msg[:num_header_bytes_before_size] +
            bad_size_bytes +
            raw_msg[(num_header_bytes_before_size + len(bad_size_bytes)):]
        )
        assert len(raw_msg) == len(raw_msg_with_wrong_size)

        return raw_msg_with_wrong_size


if __name__ == '__main__':
    InvalidMessagesTest().main()
