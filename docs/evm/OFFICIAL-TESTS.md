# Running the Official Ethereum Test Suite (T1)

This doc covers how to run the canonical `ethereum/tests` fixtures against
the same `libevmone` we link at build time. It's the first layer (Capa A)
of test gap T1 identified in the plan engineering review:

> **T1 (CRITICAL):** Phase 0 success requires the 3 smoke tests of the
> plan plus the Ethereum `tests/` suite (10000+ cases) passing 100%.
> Without this we cannot honestly claim EVM equivalence.

## What this validates

**Capa A — _this_ runner.** Drives `libevmone` directly via its own
`evmone-blockchaintest` (and optionally `evmone-statetest`) binary, with
the same pinned source and compiler flags Raptoreum links. This catches:

- Version regressions: if someone bumps `EVMONE_VERSION` in
  `depends/packages/evmone.mk` to a release that ships with broken or
  incompatible behaviour.
- Build-environment skew: if our compiler / `_FORTIFY_SOURCE` / stack
  protector flags happen to break evmone's arithmetic helpers.
- Submodule drift: evmone vendors `evmc`, `intx`, `ethash`, etc. — a
  Hunter or submodule pin mismatch would surface here.

**Capa B — not yet built.** Drives _our_ `RtmEvmcHost` and
`ApplyEvmTx()` pipeline through the same JSON fixtures. That validates
that our integration (state cache, gas accounting, EIP-1559 wrapper,
reorg-undo) doesn't deviate from reference semantics. This is the
higher-value test and the actual T1 deliverable; Capa A is just the
prerequisite scaffolding.

## Quick start (inside the `rtm-builder` Docker container)

```bash
docker exec rtm-builder bash -lc \
  '/repo/test/evm-official/run_official_state_tests.sh'
```

First invocation:

- Clones `evmone @ v0.12.0` with submodules into `~/.cache/raptoreum-evm-official/evmone-src/`.
- Configures + builds `evmone-blockchaintest` (~10 minutes on 32 cores).
- Clones `ethereum/tests @ v14.0` into `~/.cache/raptoreum-evm-official/tests-data/` (~400 MB shallow).
- Runs the full `BlockchainTests/GeneralStateTests/` set (~2800 JSON
  files; ~10000 individual test cases across forks Frontier→Cancun).

Subsequent runs reuse the cache and skip straight to execution (a few
minutes total).

## Configuration

All knobs are environment variables; defaults shown.

| Variable          | Default                  | Purpose                                           |
|-------------------|--------------------------|---------------------------------------------------|
| `EVMONE_VERSION`  | `v0.12.0`                | Must match `depends/packages/evmone.mk`           |
| `TESTS_TAG`       | `v14.0`                  | `ethereum/tests` release matching evmone version  |
| `RUNNER`          | `blockchain`             | `blockchain` or `state`                           |
| `SUBSET`          | (per-runner default)     | Subpath under `tests-data/`                       |
| `CACHE_DIR`       | `$HOME/.cache/raptoreum-evm-official` | Where to clone + build              |
| `JOBS`            | `nproc`                  | Build parallelism                                 |

Useful flags:

```bash
# Only one subset (faster smoke check)
./run_official_state_tests.sh --subset BlockchainTests/GeneralStateTests/stChainId

# Filter to specific cases via gtest pattern
./run_official_state_tests.sh --filter '*Cancun*'

# Trace EVM execution (verbose; only useful when debugging a single test)
./run_official_state_tests.sh --filter 'stChainId.chainId' --trace

# Re-use everything in the cache (no fetch, no rebuild)
./run_official_state_tests.sh --skip-fetch --skip-build
```

Each invocation writes a timestamped subdir under `$CACHE_DIR/runs/`
containing `stdout.log` and a `results.xml` JUnit report.

## Layout in `ethereum/tests` v14.0

The test fixtures changed shape in v13/v14. Reference for future
maintenance:

```
tests-data/
├── BasicTests/                                  # transaction encoding etc.
├── BlockchainTests/
│   ├── GeneralStateTests/                       # ← Capa A runs here by default
│   │   ├── stArgsZeroOneBalance/
│   │   ├── stCallCodes/
│   │   ├── ...
│   │   └── Pyspecs/                             # newer pyspec-generated
│   ├── InvalidBlocks/                           # header/block validation
│   └── ValidBlocks/
├── ABITests/
└── LegacyTests/  (git submodule, not auto-init) # legacy state-test format
```

If you need the state-test format (`evmone-statetest`) instead of
blockchain-test format, run with `RUNNER=state` and manually init the
`LegacyTests` submodule first; the script does not init submodules of
the test repo to keep cache size down.

## Known slow tests (auto-skipped)

Upstream `evmone-statetest` defines a default exclusion filter to skip
heavyweight performance tests. Those exclusions don't apply to
`evmone-blockchaintest`; if a particular case runs for minutes,
`--filter '-stTimeConsuming.*'` is a reasonable starting point.

## Capa B (planned, not implemented yet)

Capa B will add a Boost-based runner in `src/test/evm_official_*.cpp`
that:

1. Loads JSON fixtures using `evmone`'s loader headers (already
   linked via depends).
2. Constructs an `RtmEvmcHost` against an in-memory `CEvmStateCache`.
3. Replays the transaction list through `ApplyEvmTx()`.
4. Compares the post-state account map, storage map, logs, and root
   hash against the fixture's expected output.
5. Reports per-fixture pass/fail with the same JUnit format.

This catches divergences in:

- Gas accounting between our pipeline and reference.
- EIP-1559 base-fee math.
- Storage trie key/value encoding.
- Log topic ordering and bloom filter consistency.
- CREATE / CREATE2 address derivation (we ship a custom path).
- Per-fork activation logic if we ever opt to skip a fork (we don't
  today — Cancun is the only target).

Capa B is the next milestone after Capa A is green on develop. Issue
to track: TBD.
