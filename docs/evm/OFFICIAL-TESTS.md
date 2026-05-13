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

### Current coverage (as of commit c6997fa77)

Running against `ethereum/tests` v14.0 `BlockchainTests/GeneralStateTests`:

```
Cancun fixtures: 9202 pass, 11136 fail, 2041 skip
```

| Version | Commit | PASS | FAIL | SKIP |
|---|---|---|---|---|
| Capa B v1 | `976d6967d` | 5820 | 7551 | 9008 |
| Capa B v2 | `191e563a8` | 6321 | 13764 | 2294 |
| Capa B v3 | `5f0bdb4a7` | 6476 | 13609 | 2294 |
| Capa B v4 | `d0b136a31` | 6507 | 13578 | 2294 |
| Capa B v5 | `5da5cf2c2` | 6509 | 13829 | 2041 (MPT) |
| Capa B v6 | `ffd91fcbf` | 8777 | 11561 | 2041 (CALL value xfer) |
| Capa B v7 | `80e461aa1` | 8921 | 11417 | 2041 (CREATE value xfer + pre-seed) |
| Capa B v8 | `7c52cd60d` | 9202 | 11136 | 2041 (eth precompiles 0x01-0x04) |
| Capa B v9 | `c6997fa77` | 9202 | 11136 | 2041 (EIP-2681 + EIP-6780) |
| Capa B v10 | `7daae762d` | 9501 | 10837 | 2041 (MODEXP 0x05) |
| Capa B v11 | `8b8656ba0` | 9635 | 10703 | 2041 (BLAKE2F 0x09) |
| Capa B v12 | `6a0b4337e` | 9815 | 10523 | 2041 (CREATE-failure cleanup) |
| Capa B v13 | `1e9a44b5d` | 10888 | 9450 | 2041 (bn128 BN_ADD/BN_MUL) |
| Capa B v14 | `65c76eb16` | **10895** | 9443 | 2041 (EIP-7610 storage-collision) |

Pass count is **+87% over v1** (5820 → 10895) — every increment came
from a real production-pipeline correctness fix uncovered by running
the fixtures.

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

The bringup of Capa B uncovered and fixed real spec gaps in the
production pipeline (these are NOT just harness concerns — they
ship as part of the EVM stack):

- `ApplyResult.gasRefund` exposes `evmc_result.gas_refund` so
  upstream fee accounting can apply the EIP-3529 cap.
- `CEvmHost::WarmAddress` / `CEvmHost::WarmStorage` allow callers
  to pre-populate the EIP-2929 access lists.
- `ApplyEvmCallTx` / `ApplyEvmDeployTx` pre-warm the spec-mandated
  access set on every tx: sender, recipient, coinbase (EIP-3651
  Cancun), and the standard precompiles 0x01..0x0a.
- `CEvmCallTx` / `CEvmDeployTx` gain an off-wire `accessList` field
  (vector of `AccessListEntry`) consumed by Apply\* for EIP-2930
  pre-warming. The field will graduate into the consensus wire
  format in Phase 2.4 when the full tx envelope is finalised.
- `CEvmAccount::EmptyCodeHash()` and `EmptyStorageRoot()` are now
  stored in big-endian byte order (byte[0]=MSB) to match
  `evm::Keccak256()` output. Previously these constants used
  `uint256S()` which reverses the bytes for Bitcoin-Core hash-
  style display — that meant `account.codeHash == EmptyCodeHash()`
  would ALWAYS evaluate false for accounts whose codeHash was set
  via `Keccak256()`, silently breaking the CREATE collision check
  and any EXTCODEHASH-driven contract logic that compared against
  `keccak256("")`. Fixed in `src/evm/account.cpp` with a
  byte-for-byte canonical constant; `evm_state_tests/
  account_canonical_constants` cross-asserts equality with
  `Keccak256({})`.
- EIP-2929 warm-access tracking now participates in the call-frame
  snapshot/revert. `CEvmHost::call()` and `CEvmHost::CallCreate()`
  capture `warmAddresses` / `warmSlots` at frame entry and restore
  them on any failure status. Without this, addresses/slots that
  a REVERTed sub-frame touched stayed warm in the host, charging
  100 gas (warm) instead of 2600 / 2100 (cold) on the next access
  in the surviving outer frame. Observable in any recursive test
  that has one of the inner frames REVERT and the same address
  re-accessed afterwards.
- Nested CREATE/CREATE2 no longer clobbers the post-init nonce
  with `1`. If the new contract's constructor did its own
  CREATEs, those nonce bumps survive correctly. (The hard-coded
  `finalAcc.nonce = 1` was defensive code that turned into a
  silent bug for contract-creator-of-contract patterns.)
- **ApplyEvmCallTx now transfers `payload.value` from sender to
  recipient** before invoking evmone. evmone exposes the value
  via the CALLVALUE opcode but does NOT move funds itself — by
  spec, that's the transaction harness's job. Every outer CALL
  with value > 0 was previously leaving the recipient under-
  funded by exactly that amount, breaking ALL value-bearing
  transactions (huge silent class of failures).
- **ApplyEvmDeployTx similarly transfers the new `payload.value`
  field** from sender to the deployed contract, and pre-seeds
  the contract record with nonce=1 before init runs. This makes
  inner CREATEs from within the constructor work (their
  CallCreate path requires the constructing contract's account
  to exist).
- **Standard Ethereum precompiles 0x01..0x04** (ECRECOVER, SHA256,
  RIPEMD160, IDENTITY) are now dispatched by CEvmHost::call().
  Previously these CALLs fell through to empty bytecode and
  returned zero output, silently corrupting any contract that
  signature-verified, hashed, or memcopied. The bn128 /
  blake2f / KZG primitives (0x05..0x0a) are still pending — port
  of bignum / pairing crypto is a separate follow-up.
- **EIP-2681 nonce overflow guard**: a sender whose nonce is
  already at 2^64-1 can no longer perform CREATE / CREATE2; the
  attempt fails before the (overflowing) nonce bump.
- **EIP-6780 SELFDESTRUCT semantics**: CEvmHost tracks the set of
  addresses CREATEd in the current tx; selfdestructing only
  deletes the account when its address is in that set
  (`exec.selfdestructs ∩ exec.sameTxCreated`). Pre-existing
  contracts that SELFDESTRUCT just transfer balance and remain
  in state.
- **MODEXP precompile (0x05)** via boost::multiprecision::powm.
  EIP-198 layout, EIP-2565 gas (Berlin+) with 64KB per-component
  sanity cap to defend against adversarial input sizes.
- **BLAKE2F precompile (0x09)**. Hand-rolled Blake2b F compression
  function per RFC 7693 / EIP-152: 213-byte input layout (rounds,
  h, m, t, f), 64-byte output, gas = rounds (1 per round).
- **Failed CREATE now deletes the pre-seeded account record** and
  refunds any value transferred. Previously ApplyEvmDeployTx left
  the seed in place after EVMC_REVERT / OOG / etc., so the
  fixture's post-state (which expects no account at the CREATE
  address on failure) would show our spurious `nonce=1` placeholder.
- **bn128 BN_ADD (0x06) and BN_MUL (0x07)** implemented with
  Boost.Multiprecision. Field arithmetic over Fp where p is the
  BN254 prime, affine point ops, validation that input points lie
  on y² = x³ + 3. Unlocked ~1000 fixtures wholesale (stZeroKnowledge2
  jumped from 11% to 99% pass).
- **EIP-7610 (Cancun) storage-collision check** added to CREATE
  paths. An account with non-empty storage is now an occupied
  collision target even when its code is empty and nonce is zero,
  matching the post-Cancun semantics.

### Remaining failure breakdown (as of v14)

9443 failures across the broader fixture set:

| Count | Category | Notes |
|---|---|---|
| 8645 | balance | small per-fixture drifts; remaining gas-accounting edges (BN_PAIRING / KZG-using fixtures + long-tail SSTORE/memory metering) |
| ~625 | storage | downstream of wrong gas → wrong control flow |
| 122 | nonce | CREATE/CREATE2 corner cases beyond EIP-2681 / EIP-6780 / EIP-7610 |
| 43 | address | account expected to exist in post but missing |
| 8 | expected | postStateHash mismatch (state slightly off; MPT computation itself is correct per unit tests) |

Per-suite pass rates as of v14 (selected):

| Suite | Pass | Total | Rate |
|---|---|---|---|
| stLogTests | 46 | 46 | 100% |
| stCallDelegateCodesHomestead | 58 | 58 | 100% |
| stZeroKnowledge2 | 513 | 519 | 99% |
| stArgsZeroOneBalance | 94 | 96 | 98% |
| stCallCodes | 81 | 86 | 94% |
| stStaticCall | 425 | 478 | 89% |
| stPreCompiledContracts2 | 216 | 248 | 87% |
| stBadOpcode | 3306 | 4251 | 78% |
| stReturnDataTest | 202 | 273 | 74% |
| stRevertTest | 212 | 271 | 78% |
| stZeroKnowledge | 661 | 944 | 70% |
| stPreCompiledContracts | 623 | 960 | 65% |
| stMemoryTest | 274 | 578 | 47% |
| stSStoreTest | 155 | 475 | 33% |

Likely follow-ups, in ROI order:

1. **Standard Ethereum precompiles 0x05–0x0a** (MODEXP, BN_ADD,
   BN_MUL, BN_PAIRING, BLAKE2F, KZG_POINT_EVALUATION). The
   stZeroKnowledge and stPreCompiledContracts suites are dominated
   by these. Implementing them requires bignum modular exponentiation
   (libgmp or hand-rolled), bn128 elliptic-curve arithmetic
   (libff, libbn128, or evmone's port), Blake2f compression, and
   KZG point evaluation (BLS12-381 + KZG). Mature C/C++
   implementations exist; vendoring evmone's `test/state/precompiles_*`
   is probably the cleanest unlock — would clear ~1500 fixtures.
2. **Long-tail gas accounting** in stSStoreTest / stMemoryTest /
   stRevertTest. Each suite shows a per-fixture small balance drift
   pattern. Probably one or two metering bugs per suite (memory
   expansion charge, SSTORE refund schedule edge cases, MSTORE
   memory expansion).
3. **InvalidBlocks-style fixtures** (1428 currently skipped). These
   test consensus validation of malformed blocks. Implementing
   the validation rules (header gas limit, timestamp ordering,
   trie roots) would let us assert "block rejected, state == pre".
   Lower ROI than precompiles.
