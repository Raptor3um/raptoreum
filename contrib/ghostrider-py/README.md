# raptoreum_hash — GhostRider for the functional tests

The functional test framework needs to compute GhostRider proof-of-work hashes in
Python (`test/functional/test_framework/messages.py`). This module provides that.

    cd contrib/ghostrider-py
    pip install .

## Why it lives here

It is a **binding over Raptoreum Core's own implementation**, not a reimplementation:
`setup.py` compiles `src/hash_selection.cpp` and the crypto primitives straight out
of the tree, so `getPoWHash()` returns exactly what `CBlockHeader::ComputeHash()`
returns. It cannot drift from consensus, because there is no second copy of the
algorithm to drift from.

That matters more than it might look. GhostRider picks its sequence of 15 core
algorithms and 6 CryptoNight variants from the **previous block hash** (see
`HashSelection`), so an independent implementation can be correct for some headers
and wrong for others — a failure mode that spot checks do not catch. The framework
previously imported `dash_hash`, which is Dash's X11 and computes something else
entirely, while `ci/Dockerfile.builder` installed a `raptoreum_hash` from a
repository that no longer exists.

## Usage

    >>> import raptoreum_hash
    >>> raptoreum_hash.getPoWHash(header_80_bytes)   # -> 32 bytes, internal order

`header` must be at least 36 bytes; bytes 4..36 are the previous block hash, which
selects the algorithm sequence. The result is in internal (little-endian) byte
order, matching the node.

`header` must be a read-only bytes-like object (`bytes`, the normal case, works;
a `bytearray` or writable `memoryview`/`mmap` does not -- `getPoWHash` raises
`TypeError` for those).

## Requirements

- A C++ compiler. Built and tested with GCC/G++ 13 on Linux; any compiler that
  builds the rest of this tree should work, since this extension compiles the
  same sources.
- Python development headers (`Python.h`) for the interpreter you build
  against -- the `python3-dev`/`python3-devel` package on most distributions,
  or whatever provides them if you're using a pyenv/venv-managed Python.
- `setuptools`, if your interpreter's environment doesn't already have it
  (recent venvs often don't ship it by default): `pip install setuptools`.
- Boost headers, found in this order: `$BOOST_INCLUDEDIR`, then `depends/*/include`
  from a Core build, then the system.

## Tests

    python3 test_binding.py -v

Exercises the documented length boundaries (the 36-byte minimum, and the
`INT_MAX` ceiling `HashGR` narrows its own length calculation to internally --
see the comment on that check in `ghostridermodule.cpp` for why exceeding it
without a guard is a memory-safety issue for the calling process, not merely a
wrong answer) and that the previous-block-hash bytes are wired to the position
`HashSelection` actually reads.

## Checking it against the chain

A block's PoW hash must satisfy its own `nBits` target, which is a strong check —
a wrong implementation clears the target with probability about 2^-24 per block:

    hdr  = rtm getblockheader <hash> false
    bits = rtm getblockheader <hash> true | jq -r .bits
    # int.from_bytes(getPoWHash(bytes.fromhex(hdr)[:80]), 'little') <= target(bits)

This module was validated that way against 12 mainnet blocks spanning height 1 to
the chain tip, all passing.
