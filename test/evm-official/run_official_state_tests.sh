#!/usr/bin/env bash
#
# Run the official Ethereum GeneralStateTests against the same evmone version
# Raptoreum links at build time. First layer (Capa A) of T1 from the plan
# engineering review:
#
#     Test that the libevmone we depend on agrees with the reference
#     execution-layer test fixtures. This validates the version pin and the
#     compiler/flags we built with; it does NOT exercise our integration
#     (RtmEvmcHost, state cache, ApplyEvmTx). Capa B will do that.
#
# Usage:
#   test/evm-official/run_official_state_tests.sh           # default: Cancun on GeneralStateTests
#   EVMONE_VERSION=v0.12.0 TESTS_TAG=v14.0 ./run_official_state_tests.sh
#   ./run_official_state_tests.sh --filter '*Add*'          # gtest filter
#   ./run_official_state_tests.sh --subset stArgsZeroOneBalance
#   ./run_official_state_tests.sh --trace                   # verbose EVM trace
#
# Outputs:
#   - Pass/fail counts on stdout
#   - JUnit XML report at $CACHE_DIR/run-$(date)/results.xml
#   - Non-zero exit code on any failure
#
# Cache layout (defaults to ~/.cache/raptoreum-evm-official/):
#   evmone-src/          cloned evmone @ EVMONE_VERSION
#   evmone-build/        cmake build dir (EVMONE_TESTING=ON)
#   tests-data/          cloned ethereum/tests @ TESTS_TAG
#   runs/                timestamped run logs + xml reports

set -euo pipefail

# ---- defaults ---------------------------------------------------------------

EVMONE_VERSION="${EVMONE_VERSION:-v0.12.0}"
EVMONE_REPO="${EVMONE_REPO:-https://github.com/ethereum/evmone.git}"

# ethereum/tests v14.0 is the release matching evmone 0.12.x per evmone's
# CHANGELOG. Pin the git tag rather than a release tarball; the repo is large
# (~hundreds of MB) but a single shallow clone is the cheapest option.
TESTS_TAG="${TESTS_TAG:-v14.0}"
TESTS_REPO="${TESTS_REPO:-https://github.com/ethereum/tests.git}"

# Subset of the dataset to run. In ethereum/tests v14.0+ the filled
# GeneralStateTests live under BlockchainTests/GeneralStateTests/ in
# blockchain-test format (one block per case, full header validation).
# That's what we run by default with evmone-blockchaintest.
#
# Two runners are supported:
#   blockchain  -> evmone-blockchaintest (default, matches v14.0 layout)
#   state       -> evmone-statetest (legacy state-test JSONs, mostly in
#                   the LegacyTests submodule on v14.0; we don't init it
#                   by default)
RUNNER="${RUNNER:-blockchain}"
case "$RUNNER" in
    blockchain) SUBSET="${SUBSET:-BlockchainTests/GeneralStateTests}" ;;
    state)      SUBSET="${SUBSET:-GeneralStateTests}" ;;
    *) echo "RUNNER must be 'blockchain' or 'state' (got: $RUNNER)" >&2; exit 2 ;;
esac

# Build/run knobs.
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
CACHE_DIR="${CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/raptoreum-evm-official}"
CMAKE="${CMAKE:-cmake}"

# ---- arg parsing ------------------------------------------------------------

GTEST_FILTER=""
TRACE_FLAG=""
DRY_RUN=0
SKIP_BUILD=0
SKIP_FETCH=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --filter) GTEST_FILTER="$2"; shift 2 ;;
        --subset) SUBSET="$2"; shift 2 ;;
        --trace) TRACE_FLAG="--trace"; shift ;;
        --trace-summary) TRACE_FLAG="--trace-summary"; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        --skip-build) SKIP_BUILD=1; shift ;;
        --skip-fetch) SKIP_FETCH=1; shift ;;
        -h|--help)
            sed -n '2,30p' "$0"
            exit 0
            ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

# ---- helpers ----------------------------------------------------------------

log()  { printf '[official-tests] %s\n' "$*"; }
fail() { printf '[official-tests] ERROR: %s\n' "$*" >&2; exit 1; }

require_tool() {
    command -v "$1" >/dev/null 2>&1 || fail "missing required tool: $1"
}

require_tool git
require_tool "$CMAKE"
require_tool make
require_tool gcc
require_tool g++

mkdir -p "$CACHE_DIR/runs"

# ---- step 1: fetch evmone @ pinned version (with submodules) ----------------

EVMONE_SRC="$CACHE_DIR/evmone-src"
if [[ $SKIP_FETCH -eq 0 ]]; then
    if [[ -d "$EVMONE_SRC/.git" ]]; then
        log "evmone source already present at $EVMONE_SRC"
        ( cd "$EVMONE_SRC" && git fetch --tags --quiet )
    else
        log "cloning evmone $EVMONE_VERSION (with submodules)..."
        git clone --branch "$EVMONE_VERSION" --recurse-submodules --depth 1 \
            "$EVMONE_REPO" "$EVMONE_SRC"
    fi
    # Confirm exact tag (in case re-pointing later).
    ( cd "$EVMONE_SRC" \
        && git -c advice.detachedHead=false checkout "$EVMONE_VERSION" --quiet \
        && git submodule update --init --recursive --quiet )
else
    log "skipping fetch (use cached $EVMONE_SRC)"
fi

# ---- step 2: build evmone-statetest with testing enabled --------------------

EVMONE_BUILD="$CACHE_DIR/evmone-build"
case "$RUNNER" in
    blockchain) RUNNER_TARGET=evmone-blockchaintest ;;
    state)      RUNNER_TARGET=evmone-statetest ;;
esac
RUNNER_BIN="$EVMONE_BUILD/bin/$RUNNER_TARGET"

if [[ $SKIP_BUILD -eq 0 ]]; then
    log "configuring evmone build (EVMONE_TESTING=ON)..."
    "$CMAKE" -S "$EVMONE_SRC" -B "$EVMONE_BUILD" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DEVMONE_TESTING=ON \
        -DEVMONE_FUZZING=OFF \
        > "$EVMONE_BUILD.cmake.log" 2>&1 \
        || { tail -40 "$EVMONE_BUILD.cmake.log"; fail "cmake configure failed"; }
    log "building $RUNNER_TARGET..."
    "$CMAKE" --build "$EVMONE_BUILD" --target "$RUNNER_TARGET" -j "$JOBS" \
        > "$EVMONE_BUILD.build.log" 2>&1 \
        || { tail -40 "$EVMONE_BUILD.build.log"; fail "evmone build failed"; }
else
    log "skipping build (use cached $EVMONE_BUILD)"
fi

[[ -x "$RUNNER_BIN" ]] || fail "expected runner missing: $RUNNER_BIN"

# ---- step 3: fetch ethereum/tests @ pinned tag ------------------------------

TESTS_DATA="$CACHE_DIR/tests-data"
if [[ $SKIP_FETCH -eq 0 ]]; then
    if [[ -d "$TESTS_DATA/.git" ]]; then
        log "ethereum/tests already present at $TESTS_DATA"
        ( cd "$TESTS_DATA" && git fetch --tags --quiet )
    else
        log "cloning ethereum/tests $TESTS_TAG (shallow, may take a few minutes)..."
        git clone --branch "$TESTS_TAG" --depth 1 "$TESTS_REPO" "$TESTS_DATA"
    fi
    ( cd "$TESTS_DATA" && git -c advice.detachedHead=false checkout "$TESTS_TAG" --quiet )
else
    log "skipping fetch (use cached $TESTS_DATA)"
fi

TESTS_PATH="$TESTS_DATA/$SUBSET"
[[ -d "$TESTS_PATH" ]] || fail "subset path not found: $TESTS_PATH"

# ---- step 4: run -----------------------------------------------------------

RUN_DIR="$CACHE_DIR/runs/$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$RUN_DIR"

EVMONE_BUILDINFO=$("$RUNNER_BIN" --version 2>/dev/null || echo "unknown")
log "evmone runner: $RUNNER_TARGET ($EVMONE_BUILDINFO)"
log "subset:        $SUBSET ($(find "$TESTS_PATH" -name '*.json' | wc -l) JSON files)"
log "filter:        ${GTEST_FILTER:-<default exclusions only>}"
log "run dir:       $RUN_DIR"

if [[ $DRY_RUN -eq 1 ]]; then
    log "DRY RUN — exiting before invocation"
    exit 0
fi

cmd=("$RUNNER_BIN" "$TESTS_PATH" $TRACE_FLAG)
cmd+=("--gtest_output=xml:$RUN_DIR/results.xml")
if [[ -n "$GTEST_FILTER" ]]; then
    cmd+=("--gtest_filter=$GTEST_FILTER")
fi

log "invoking: ${cmd[*]}"
log "(output streamed to $RUN_DIR/stdout.log)"

# Don't abort the script on test failures here; we want to print the summary
# either way. Use a tee to keep terminal output flowing.
set +e
"${cmd[@]}" 2>&1 | tee "$RUN_DIR/stdout.log"
rc=$?
set -e

# ---- step 5: summary --------------------------------------------------------

if [[ -f "$RUN_DIR/results.xml" ]]; then
    # JUnit XML format: <testsuites tests=".." failures=".." errors=".." ..>
    summary=$(grep -oE '<testsuites[^>]*' "$RUN_DIR/results.xml" \
        | grep -oE '(tests|failures|errors|disabled|skipped|time)="[^"]*"' \
        | tr '\n' ' ')
    log "result: $summary"
fi

if [[ $rc -eq 0 ]]; then
    log "all selected official state tests passed."
else
    log "FAILURES detected — see $RUN_DIR/{stdout.log,results.xml}"
fi

exit $rc
