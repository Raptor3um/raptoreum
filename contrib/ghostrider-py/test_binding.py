#!/usr/bin/env python3
"""Unit tests for the raptoreum_hash binding (ghostridermodule.cpp).

Run after building the extension in place:

    cd contrib/ghostrider-py
    pip install .
    python3 test_binding.py

These are not part of the functional test suite (test/functional/) -- this
module is a *build dependency* the functional framework imports, not a node
test itself. It has no test coverage of its own otherwise.
"""

import unittest

import raptoreum_hash

INT_MAX = 2 ** 31 - 1


class GetPoWHashTests(unittest.TestCase):
    def test_normal_80_byte_header(self):
        # The real, documented usage: a serialised CBlockHeader.
        result = raptoreum_hash.getPoWHash(bytes(80))
        self.assertEqual(len(result), 32)

    def test_documented_minimum_of_36_bytes(self):
        result = raptoreum_hash.getPoWHash(bytes(36))
        self.assertEqual(len(result), 32)

    def test_rejects_below_the_documented_minimum(self):
        with self.assertRaises(ValueError):
            raptoreum_hash.getPoWHash(bytes(35))

    def test_known_answer_vectors_from_real_mainnet_blocks(self):
        # An earlier version of this test only checked that two headers
        # differing in bytes 4..36 (the previous block hash) hash
        # differently -- meant to prove HashSelection's algorithm-sequence
        # choice is wired to the real prev-hash bytes, not e.g. a zeroed or
        # misaligned slice. That doesn't actually prove it: those same bytes
        # are ALSO part of the raw data round 0 hashes, so two differing
        # headers produce different output regardless of whether the
        # prev-hash extraction used for algorithm SELECTION is correct or
        # broken. A real known-answer test is the only thing that actually
        # pins correctness.
        #
        # Each (header, hash) pair below is a genuine mainnet block, fetched
        # from a live node (`rtm getblockheader <hash> false`) and computed
        # here independently -- not copied from anywhere else. Each hash was
        # separately confirmed to satisfy its own block's real `nBits`
        # target (height 500000 and 1000000 use ordinary mainnet difficulty,
        # not the near-zero genesis-era target at height 1, so those two are
        # a meaningful check on their own: a wrong hash clearing either
        # target by chance is astronomically unlikely).
        vectors = [
            # height 1
            (
                "00000020a0eeca416f685330d93dda38dc86f5d3a9bfff55c68fda7a56b97872f05d9"
                "eb7484810e6592b02c2ddc52dbf88cc4c9c8ea9f80e5d1afb11ee22c2f9128d06a99a"
                "653960ff1f0020b9020000",
                "238408f6619db7d71175d359bd31d651fc02dcfdc84bc6d28da6436feda51f00",
            ),
            # height 500000
            (
                "0000002081468e1e4c62fab08e1216945386e7550e091e4ec55655d773c82394603147"
                "acf133a23c7816150c7f779528ed9e5ca5369161b1faa307822890e7b10e4b69de33fe"
                "de634aca001ddc822200",
                "d293536ab21784d6532dd6415ad34077d44a566ad716fa0ec290866900000000",
            ),
            # height 1000000
            (
                "00000020665ca72dd661aa59542889d1fd7a48a9fbff215d7eb5edad6a07a61b18f19d"
                "cf4d98c6630b6593f3c1c55d59aa1c3e8b7076cd33429686fdfaa3a6fac7ff8f256f22"
                "85677784011d030b1400",
                "d2a4979308adc916b4668b75f0badd01128ebe92a3a9b2890be6142801000000",
            ),
        ]
        for header_hex, expected_hash_hex in vectors:
            header = bytes.fromhex(header_hex)
            self.assertEqual(len(header), 80)
            result = raptoreum_hash.getPoWHash(header)
            self.assertEqual(result.hex(), expected_hash_hex, f"mismatch for header {header_hex}")

    def test_rejects_a_length_that_would_overflow_int_internally(self):
        # HashGR (src/hash.h) narrows (pend - pbegin) * sizeof(*pbegin) into
        # a plain `int` internally. Py_ssize_t is wider than int on every
        # platform this extension targets, so a caller-supplied length just
        # above INT_MAX previously reached HashGR unchecked.
        #
        # This was reproduced directly, not merely argued: calling the
        # PRE-FIX binding with exactly this length (INT_MAX + 2000 bytes, a
        # buffer differing only in its final byte from an otherwise-identical
        # one) did not just return a wrong hash -- it SEGFAULTED the whole
        # Python process. The negative `int` produced by the narrowing
        # (2**31 - 1 + 2000, taken modulo 2**32, is negative) is almost
        # certainly reinterpreted as an enormous unsigned length somewhere
        # downstream, causing an out-of-bounds read. That makes this a
        # memory-safety issue for the calling process, not merely "a wrong
        # answer for an unrealistic input" as first reported.
        #
        # The fix rejects any length above INT_MAX before HashGR is ever
        # called. PyArg_ParseTuple's "y#" format needs a real bytes-like
        # object with a genuine underlying buffer, so this test still needs
        # a real ~2GiB allocation to reach the C-level check.
        #
        # The actual regression this guards against is the crash itself --
        # without the fix, this call segfaults the whole process, so
        # assertRaises below is what catches a real regression (the test
        # run dies outright, it doesn't fail cleanly). The timing check is a
        # secondary, weaker guard against a hypothetical DIFFERENT bug
        # (hash-then-reject instead of reject-before-hashing): hashing 2GiB+
        # through GhostRider takes multiple seconds (measured ~2.8s for
        # exactly INT_MAX bytes) vs. microseconds to reject outright, so 1
        # second is a generous bound for that narrower case.
        import time
        # Allocating ~2GiB is cheap under default overcommit, but under
        # ulimit -v, overcommit_memory=2, or a 32-bit interpreter, this
        # allocation itself can raise MemoryError before the C-level check
        # under test is ever reached -- that says nothing about the fix, so
        # skip rather than report a false ERROR in those environments.
        try:
            oversized = bytes(INT_MAX + 2000)
        except MemoryError:
            self.skipTest("environment cannot allocate an INT_MAX+2000-byte buffer")
        start = time.monotonic()
        with self.assertRaises(ValueError):
            raptoreum_hash.getPoWHash(oversized)
        elapsed = time.monotonic() - start
        self.assertLess(elapsed, 1.0,
                         "rejection took long enough to suggest hashing was attempted "
                         "before the length was checked")


if __name__ == "__main__":
    unittest.main()
