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

## Capa B — drive fixtures through our own pipeline

Capa B is the higher-value test: drive the same JSON fixtures
through `RtmEvmcHost` + `CEvmStateCache` + `ApplyEvmCallTx` /
`ApplyEvmDeployTx`, not just through libevmone standalone. This
catches divergences in our wiring (gas accounting, storage
encoding, log capture, CREATE address derivation, EIP-1559 fee
math, refund handling).

Source: `src/test/evm_official_blockchaintest_tests.cpp`. Opt-in via
`EVM_OFFICIAL_TESTS_PATH` (skip-clean when unset). Configurable
`EVM_OFFICIAL_TESTS_LIMIT` caps the number of files for quick
iteration.

Typical run:

```bash
docker exec rtm-builder bash -lc "
  EVM_OFFICIAL_TESTS_PATH=/root/.cache/raptoreum-evm-official/tests-data/BlockchainTests/GeneralStateTests \
    /repo/src/test/test_raptoreum --run_test=evm_official_blockchaintest_tests \
                                  --log_level=message"
```

### Current coverage (as of commit 191e563a8)

Running against `ethereum/tests` v14.0 `BlockchainTests/GeneralStateTests`:

```
Cancun fixtures: 6321 pass, 13764 fail, 2294 skip
```

Pass rate over what we actually exercise (excluding correct-by-design
skips): roughly **30%** across CALL + CREATE + multi-tx + multi-block
fixtures.

**Skip categories** (all by design, not failures):

| Count | Reason |
|---|---|
| 1428 | Block has no `blockHeader` (degenerate InvalidBlocks-style entries that happen to live under GeneralStateTests). |
| 613 | Non-Cancun forks — out of scope per design decision D6. |
| 253 | Fixture only ships `postStateHash` (the canonical MPT root), no expanded `postState`. Computing the MPT root is a planned follow-up. |

### What the harness simulates (Phase 2.4 surface)

Our production `ApplyEvmCallTx` deliberately omits fee/balance
bookkeeping per its Phase 2.3a docstring; that's Phase 2.4 work.
For Capa B we replicate the standard Ethereum transaction harness
in the test runner:

1. **EIP-4788 beacon-roots system pre-call** — once per block,
   before any user transactions, writes `parentBeaconBlockRoot`
   into the predeploy at 0x000F...beac02.
2. **Pre-debit + nonce bump** of the sender by
   `gasLimit * effectiveGasPrice`.
3. **Run** the EVM via `ApplyEvmCallTx` / `ApplyEvmDeployTx`.
4. **Intrinsic gas**: 21000 for CALL, 53000 for CREATE, plus per
   byte (4 zero, 16 non-zero) and EIP-3860 init-code metering (2
   gas/word) for CREATE.
5. **Refund**: `min(gasUsed/5, evmone.gas_refund)` per EIP-3529.
6. **Coinbase credit** of `gasUsed * priorityPerGas` where
   `priorityPerGas = effectiveGasPrice − baseFee`.

EIP-1559 vs legacy distinction is detected from the JSON `type`
field. Effective gas price for type 2 is
`min(maxFeePerGas, baseFee + maxPriorityFeePerGas)`; for legacy
it's `gasPrice` directly.

### What our production pipeline now does (Capa B-driven)

The bringup of Capa B uncovered real spec gaps that this commit
landed in the production pipeline:

- `ApplyResult.gasRefund` exposes `evmc_result.gas_refund` so
  upstream fee accounting can apply the EIP-3529 cap.
- `CEvmHost::WarmAddress` / `CEvmHost::WarmStorage` allow callers
  to pre-populate the EIP-2929 access lists.
- `ApplyEvmCallTx` / `ApplyEvmDeployTx` pre-warm the spec-mandated
  access set on every tx: sender, recipient, coinbase (EIP-3651
  Cancun), and the standard precompiles 0x01..0x0a.

### Remaining failure breakdown

The 13764 failures fall into clear, actionable categories. Each
failure prints the specific account/slot diff so the next
iteration can drill down into one category at a time.

| Count | Category |
|---|---|
| 12669 | balance — residual gas accounting (likely culprits below) |
| 360+ | storage — usually downstream of wrong gas → wrong control flow |
| 291 | address — account expected to exist in post but missing |
| 133 | code — deployed contract code differs (CREATE-specific) |
| 81 | nonce — CREATE-specific account.nonce timing |

Likely follow-ups, in order of expected ROI:

1. **EIP-2930 access list pre-warming.** Requires extending
   `CEvmCallTx` with an `accessList` field. Significant share of
   the type-2 EIP-1559 fixtures use non-empty access lists; today
   they pay cold (2600) for what the spec says is warm (100). Big
   payoff and a real pipeline improvement.
2. **Nested-call gas budget pricing.** Some `stCallCodes` / `stCall*`
   fixtures show storage diffs at the inner call's contract, which
   points at gas-budget propagation between frames. Likely
   off-by-something in our CALL/STATICCALL gas forwarding.
3. **CREATE / CREATE2 corner cases.** EIP-161 nonce initialization,
   selfdestruct-during-init handling, init code returning code with
   the wrong size, etc. Each affects a small share of fixtures but
   the categories are well-defined.
4. **`postStateHash` support** — compute the canonical MPT root
   over the cache and compare. Heavy work (full MPT impl, RLP
   already present); ~253 fixtures unlock.
