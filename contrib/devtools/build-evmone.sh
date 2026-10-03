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
#   - cmake from the SYSTEM (>= 3.18) — NOT the cmake built by depends/,
#     which lacks HTTPS support and breaks Hunter downloads.
#   - g++ with C++20 support (evmone 0.12), or the matching HOST toolchain
#     (aarch64-linux-gnu or x86_64-w64-mingw32 with POSIX threads)
#   - depends/ tree already built for HOST. Run this AFTER:
#       make -C depends -j$(nproc) HOST=$HOST NO_QT=1 NO_UPNP=1
#
# What this installs:
#   $PREFIX/lib/libevmone.a
#   $PREFIX/lib/libevmone-standalone.a   (bundles ethash; Makefile.am uses this)
#   $PREFIX/include/evmone/evmone.h
#   $PREFIX/include/evmc/*
#   $PREFIX/include/intx/*
#   $PREFIX/include/ethash/*             (dependency headers are copied below)
#
# where $PREFIX = $REPO_ROOT/depends/$HOST.
#
# Exit codes:
#   0  success
#   1  prerequisite missing or build failure

set -euo pipefail

EVMONE_VERSION="${EVMONE_VERSION:-v0.12.0}"

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
    echo "       Run make -C depends ... first." >&2
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

# Select the target explicitly; HOST must affect Hunter dependencies as well as
# evmone, rather than only the installation directory.
case "$HOST" in
    x86_64-linux-gnu|x86_64-*-linux-gnu)
        TARGET_SYSTEM=Linux
        TARGET_PROCESSOR=x86_64
        TOOL_PREFIX=""
        if [[ "$(./depends/config.guess)" != x86_64-*-linux-gnu ]]; then
            TOOL_PREFIX=x86_64-linux-gnu-
        fi
        ;;
    aarch64-linux-gnu|aarch64-*-linux-gnu)
        TARGET_SYSTEM=Linux
        TARGET_PROCESSOR=aarch64
        TOOL_PREFIX=aarch64-linux-gnu-
        ;;
    x86_64-w64-mingw32)
        TARGET_SYSTEM=Windows
        TARGET_PROCESSOR=x86_64
        TOOL_PREFIX=x86_64-w64-mingw32-
        ;;
    *)
        echo "ERROR: unsupported evmone target: $HOST" >&2
        exit 1
        ;;
esac

TARGET_CC="${CC:-${TOOL_PREFIX}gcc}"
TARGET_CXX="${CXX:-${TOOL_PREFIX}g++}"
if [[ "$TARGET_SYSTEM" == Windows ]]; then
    TARGET_CC="${CC:-${TOOL_PREFIX}gcc-posix}"
    TARGET_CXX="${CXX:-${TOOL_PREFIX}g++-posix}"
fi
TARGET_AR="${AR:-${TOOL_PREFIX}ar}"
TARGET_RANLIB="${RANLIB:-${TOOL_PREFIX}ranlib}"
for tool in "$TARGET_CC" "$TARGET_CXX" "$TARGET_AR" "$TARGET_RANLIB"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERROR: target tool not found: $tool" >&2
        exit 1
    fi
done

# Reject a native CC/CXX accidentally inherited by a cross-compilation job.
for compiler in "$TARGET_CC" "$TARGET_CXX"; do
    COMPILER_TARGET="$("$compiler" -dumpmachine)"
    case "$TARGET_SYSTEM/$TARGET_PROCESSOR/$COMPILER_TARGET" in
        Linux/x86_64/x86_64-linux-gnu|Linux/x86_64/x86_64-*-linux-gnu|\
        Linux/aarch64/aarch64-linux-gnu|Linux/aarch64/aarch64-*-linux-gnu|\
        Windows/x86_64/x86_64-w64-mingw32) ;;
        *)
            echo "ERROR: $compiler targets $COMPILER_TARGET, expected $HOST." >&2
            exit 1
            ;;
    esac
done

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

# evmone 0.12 selects the last enabled language for its standalone archive.
# MinGW enables RC after C/CXX, but RC cannot create static libraries. Keep
# the same archive members and select the C++ archiver rules explicitly.
if [[ "$TARGET_SYSTEM" == Windows ]]; then
    LIBRARY_TOOLS="$SRC/cmake/LibraryTools.cmake"
    if [[ "$(grep -Fc 'list(GET enabled_languages -1 lang)' "$LIBRARY_TOOLS")" != 1 ]]; then
        echo "ERROR: unexpected evmone standalone library configuration." >&2
        exit 1
    fi
    sed -i 's/list(GET enabled_languages -1 lang)/set(lang CXX)/' "$LIBRARY_TOOLS"
    STACK_STATE="$SRC/lib/evmone/execution_state.hpp"
    if [[ "$(grep -Fxc '#ifdef _MSC_VER' "$STACK_STATE")" != 2 ||
          "$(grep -Fxc '#include <memory>' "$STACK_STATE")" != 1 ]]; then
        echo "ERROR: unexpected evmone/EVMC Windows portability configuration." >&2
        exit 1
    fi
    # Use the existing Windows allocator and matching deallocator for MinGW.
    sed -i 's/#ifdef _MSC_VER/#if defined(_MSC_VER) || defined(__MINGW32__)/' "$STACK_STATE"
    sed -i '/#include <memory>/a #ifdef __MINGW32__\n#include <malloc.h>\n#endif' "$STACK_STATE"
    # libstdc++ 10 lacks make_unique_for_overwrite. The equivalent array
    # allocation is also uninitialized and retains unique_ptr's delete[].
    BASELINE_ANALYSIS="$SRC/lib/evmone/baseline_analysis.cpp"
    if [[ "$(grep -Fc 'std::make_unique_for_overwrite<uint8_t[]>(code.size() + padding)' "$BASELINE_ANALYSIS")" != 1 ]]; then
        echo "ERROR: unexpected evmone baseline allocation configuration." >&2
        exit 1
    fi
    sed -i 's|std::make_unique_for_overwrite<uint8_t\[\]>(code.size() + padding)|std::unique_ptr<uint8_t[]>(new uint8_t[code.size() + padding])|' "$BASELINE_ANALYSIS"
fi

# ---------------------------------------------------------------------------
# Configure and build
# ---------------------------------------------------------------------------

BUILD="$SRC/build_tmp"
mkdir -p "$BUILD"
cd "$BUILD"

# Hunter forwards this toolchain to ethash/intx. Keep C++17/PIC settings from
# evmone's default toolchain while making the target independent of the host.
TOOLCHAIN="$BUILD_ROOT/target.cmake"
cat > "$TOOLCHAIN" <<EOF
set(CMAKE_SYSTEM_NAME "$TARGET_SYSTEM")
set(CMAKE_SYSTEM_PROCESSOR "$TARGET_PROCESSOR")
set(CMAKE_C_COMPILER "$(command -v "$TARGET_CC")")
set(CMAKE_CXX_COMPILER "$(command -v "$TARGET_CXX")")
set(CMAKE_AR "$(command -v "$TARGET_AR")")
set(CMAKE_RANLIB "$(command -v "$TARGET_RANLIB")")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
EOF

echo "[evmone] configuring with system cmake for $TARGET_SYSTEM/$TARGET_PROCESSOR..."
"$SYSTEM_CMAKE" .. \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE=Release \
    -DEVMONE_X86_64_ARCH_LEVEL=1 \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_INSTALL_INCLUDEDIR="$PREFIX/include" \
    -DCMAKE_INSTALL_LIBDIR="$PREFIX/lib" \
    -DBUILD_SHARED_LIBS=OFF \
    -DEVMONE_TESTING=OFF \
    -DEVMONE_FUZZING=OFF

echo "[evmone] building..."
# These are all installed libraries and all libraries linked by Raptoreum.
# Upstream's auxiliary evmone_precompiles target is neither installed nor used.
"$SYSTEM_CMAKE" --build . --target evmone evmone-standalone \
    --parallel "${EVMONE_BUILD_JOBS:-$(nproc 2>/dev/null || echo 4)}"

echo "[evmone] installing libs and evmone/evmone.h..."
"$SYSTEM_CMAKE" --install .

# evmone's install target does NOT copy evmc headers (they live in the
# submodule). Copy them manually so the raptoreumd build can find
# <evmc/evmc.hpp>, <evmc/mocked_host.hpp>, etc.
echo "[evmone] installing evmc headers..."
cp -r "$SRC/evmc/include/evmc" "$PREFIX/include/"

# Use the package directories resolved by THIS configure, never an arbitrary
# Hunter cache entry (which may belong to a different version or target).
for package in intx ethash; do
    PACKAGE_DIR="$(sed -n "s|^${package}_DIR:PATH=||p" "$BUILD/CMakeCache.txt")"
    if [[ -z "$PACKAGE_DIR" || ! -f "$PACKAGE_DIR/${package}Config.cmake" ]]; then
        echo "ERROR: configured $package package directory is missing: $PACKAGE_DIR" >&2
        exit 1
    fi
    PACKAGE_PREFIX="$(cd "$PACKAGE_DIR/../../.." && pwd)"
    if [[ ! -f "$PACKAGE_PREFIX/include/$package/$package.hpp" &&
          ! -f "$PACKAGE_PREFIX/include/$package/$package.h" ]]; then
        echo "ERROR: configured $package headers are missing in $PACKAGE_PREFIX" >&2
        exit 1
    fi
    cp -r "$PACKAGE_PREFIX/include/$package" "$PREFIX/include/"
done

# ---------------------------------------------------------------------------
# Verify
# ---------------------------------------------------------------------------

REQUIRED_FILES=(
    "$PREFIX/lib/libevmone.a"
    "$PREFIX/lib/libevmone-standalone.a"
    "$PREFIX/include/evmone/evmone.h"
    "$PREFIX/include/evmc/evmc.hpp"
    "$PREFIX/include/evmc/mocked_host.hpp"
    "$PREFIX/include/intx/intx.hpp"
    "$PREFIX/include/ethash/keccak.hpp"
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
