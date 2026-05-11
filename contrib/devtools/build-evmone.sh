#!/usr/bin/env bash
# Copyright (c) 2026 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Build and install evmone into the depends/ prefix.
#
# This script automates the manual workaround documented in
# docs/evm/PHASE-0-HANDOFF.md § "Issue 2". The proper depends/ packaging
# (vendor evmc + intx + ethash as separate packages and disable Hunter)
# is a Phase 2 deliverable. Until then this script is the canonical way
# to get libevmone.a into the depends prefix so that raptoreumd can
# link it.
#
# Usage:
#   contrib/devtools/build-evmone.sh [HOST]
#
# HOST defaults to $(./depends/config.guess). For native builds this is
# usually x86_64-pc-linux-gnu.
#
# Requirements:
#   - git (must be able to clone GitHub over HTTPS)
#   - cmake from the SYSTEM (>= 3.16) — NOT the cmake built by depends/,
#     which lacks HTTPS support and breaks Hunter downloads.
#   - g++ >= 9 (C++17 with std::filesystem)
#   - depends/ tree already built for HOST. Run this AFTER:
#       make -C depends -j$(nproc) HOST=$HOST NO_QT=1 NO_UPNP=1
#
# What this installs:
#   $PREFIX/lib/libevmone.a
#   $PREFIX/lib/libevmone-standalone.a   (bundles ethash; Makefile.am uses this)
#   $PREFIX/include/evmone/evmone.h
#   $PREFIX/include/evmc/*               (manually copied — evmone install
#                                         does not include evmc headers)
#
# where $PREFIX = $REPO_ROOT/depends/$HOST.
#
# Exit codes:
#   0  success
#   1  prerequisite missing or build failure

set -euo pipefail

EVMONE_VERSION="${EVMONE_VERSION:-v0.12.0}"
EVMONE_SHA256_TARBALL="${EVMONE_SHA256_TARBALL:-5dddc1fbb816b951ef3c5b5065830814c19f70064d0dc8ab27b1bb2b1012dbea}"

# ---------------------------------------------------------------------------
# Resolve paths
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

HOST="${1:-$(./depends/config.guess)}"
PREFIX="$REPO_ROOT/depends/$HOST"

echo "[evmone] repo root:    $REPO_ROOT"
echo "[evmone] HOST:         $HOST"
echo "[evmone] PREFIX:       $PREFIX"
echo "[evmone] version:      $EVMONE_VERSION"

# ---------------------------------------------------------------------------
# Sanity checks
# ---------------------------------------------------------------------------

if [ ! -d "$PREFIX" ]; then
    echo "ERROR: depends prefix $PREFIX does not exist." >&2
    echo "       Run `make -C depends ...` first." >&2
    exit 1
fi

# Use SYSTEM cmake explicitly. The depends/-built cmake lacks HTTPS,
# which Hunter (evmone's package manager) needs for intx/ethash.
SYSTEM_CMAKE="${SYSTEM_CMAKE:-/usr/bin/cmake}"
if ! "$SYSTEM_CMAKE" --version >/dev/null 2>&1; then
    echo "ERROR: system cmake not found at $SYSTEM_CMAKE." >&2
    echo "       Set SYSTEM_CMAKE to your system cmake binary." >&2
    exit 1
fi

if ! command -v git >/dev/null 2>&1; then
    echo "ERROR: git is required." >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Fetch evmone with submodules (evmc is a submodule, not a Hunter package)
# ---------------------------------------------------------------------------

BUILD_ROOT="$(mktemp -d -t evmone-build.XXXXXX)"
trap 'rm -rf "$BUILD_ROOT"' EXIT

echo "[evmone] cloning into $BUILD_ROOT/evmone (with submodules)..."
git -C "$BUILD_ROOT" clone --quiet \
    --branch "$EVMONE_VERSION" \
    --depth 1 \
    --recurse-submodules \
    https://github.com/ethereum/evmone evmone

SRC="$BUILD_ROOT/evmone"

# Sanity check: evmc submodule present (provides mocked_host.hpp etc.)
if [ ! -f "$SRC/evmc/include/evmc/mocked_host.hpp" ]; then
    echo "ERROR: evmc submodule did not initialize ($SRC/evmc missing)." >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Configure and build
# ---------------------------------------------------------------------------

BUILD="$SRC/build_tmp"
mkdir -p "$BUILD"
cd "$BUILD"

echo "[evmone] configuring with system cmake..."
"$SYSTEM_CMAKE" .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_INSTALL_INCLUDEDIR="$PREFIX/include" \
    -DCMAKE_INSTALL_LIBDIR="$PREFIX/lib" \
    -DBUILD_SHARED_LIBS=OFF \
    -DEVMONE_TESTING=OFF \
    -DEVMONE_FUZZING=OFF

echo "[evmone] building..."
"$SYSTEM_CMAKE" --build . --parallel "$(nproc 2>/dev/null || echo 4)"

echo "[evmone] installing libs and evmone/evmone.h..."
"$SYSTEM_CMAKE" --build . --target install

# evmone's install target does NOT copy evmc headers (they live in the
# submodule). Copy them manually so the raptoreumd build can find
# <evmc/evmc.hpp>, <evmc/mocked_host.hpp>, etc.
echo "[evmone] installing evmc headers..."
cp -r "$SRC/evmc/include/evmc" "$PREFIX/include/"

# ---------------------------------------------------------------------------
# Verify
# ---------------------------------------------------------------------------

REQUIRED_FILES=(
    "$PREFIX/lib/libevmone.a"
    "$PREFIX/lib/libevmone-standalone.a"
    "$PREFIX/include/evmone/evmone.h"
    "$PREFIX/include/evmc/evmc.hpp"
    "$PREFIX/include/evmc/mocked_host.hpp"
)

for f in "${REQUIRED_FILES[@]}"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: expected file not present after install: $f" >&2
        exit 1
    fi
done

echo "[evmone] ✓ install complete:"
for f in "${REQUIRED_FILES[@]}"; do
    echo "         $f"
done

echo ""
echo "[evmone] Next step: configure and build raptoreumd:"
echo "  ./autogen.sh"
echo "  CONFIG_SITE=\$PWD/depends/$HOST/share/config.site \\"
echo "    ./configure --enable-debug --enable-tests \\"
echo "                --disable-bench --without-gui \\"
echo "                --disable-zmq --disable-fuzz --disable-fuzz-binary \\"
echo "                --with-miniupnpc=no --with-natpmp \\"
echo "                --enable-wallet --with-incompatible-bdb"
echo "  make -j\$(nproc)"
