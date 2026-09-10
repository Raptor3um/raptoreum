#!/usr/bin/env python3
"""Build the GhostRider hashing module used by the functional test framework.

Sources are compiled straight out of the Core tree, so this module cannot drift
from consensus: it *is* the node's implementation, exposed to Python.

    cd contrib/ghostrider-py && pip install .
"""

import glob
import os
from setuptools import setup, Extension

HERE = os.path.abspath(os.path.dirname(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(ROOT, "src")


def boost_include_dir():
    """serialize.h needs boost headers. Prefer an explicit path, then the
    depends/ tree a Core build already produced, then the system."""
    env = os.environ.get("BOOST_INCLUDEDIR")
    if env:
        return [env]
    found = glob.glob(os.path.join(ROOT, "depends", "*", "include", "boost", "version.hpp"))
    return [os.path.dirname(os.path.dirname(p)) for p in found[:1]]


def s(*parts):
    return os.path.join(SRC, *parts)


# HashGR (src/hash.h) -> HashSelection (src/hash_selection.cpp) -> 15 sph_*
# core algorithms + 6 CryptoNight variants.
sources = [
    os.path.join(HERE, "ghostridermodule.cpp"),
    s("hash_selection.cpp"),
    s("uint256.cpp"),
    s("util", "strencodings.cpp"),
    # core algorithms
    s("crypto", "aes_helper.c"),
    s("crypto", "blake.c"),
    s("crypto", "bmw.c"),
    s("crypto", "cubehash.c"),
    s("crypto", "echo.c"),
    s("crypto", "groestl.c"),
    s("crypto", "jh.c"),
    s("crypto", "keccak.c"),
    s("crypto", "luffa.c"),
    s("crypto", "shavite.c"),
    s("crypto", "simd.c"),
    s("crypto", "skein.c"),
    s("crypto", "sph_fugue.c"),
    s("crypto", "sph_hamsi.c"),
    s("crypto", "sph_hamsi_helper.c"),
    s("crypto", "sph_sha2.c"),
    s("crypto", "sph_sha512.c"),
    s("crypto", "sph_shabal.c"),
    s("crypto", "sph_whirlpool.c"),
    # CryptoNight variants
    s("cryptonote", "aesb.c"),
    s("cryptonote", "c_blake256.c"),
    s("cryptonote", "c_groestl.c"),
    s("cryptonote", "c_jh.c"),
    s("cryptonote", "c_keccak.c"),
    s("cryptonote", "c_skein.c"),
    s("cryptonote", "hash-ops.c"),
    s("cryptonote", "oaes_lib.c"),
    s("cryptonote", "slow-hash.c"),
    s("cryptonote", "wild_keccak.cpp"),
]

setup(
    name="raptoreum_hash",
    version="1.0.0",
    description="GhostRider proof-of-work hashing, bound to Raptoreum Core's implementation",
    ext_modules=[
        Extension(
            "raptoreum_hash",
            sources=sources,
            include_dirs=[SRC, os.path.join(SRC, "config")] + boost_include_dir(),
            extra_compile_args=["-O2"],
        )
    ],
)
