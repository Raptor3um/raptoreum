# TODOS — RTM-EVM Integration

> Branch: `feat/evm-integration`. Source plan: `~/.claude/plans/lo-que-hay-en-cheeky-dove.md` (to be mirrored to `docs/evm/PLAN.md`).
> Last updated: 2026-05-09 (post `/plan-eng-review`, decisions D1-D6 frozen).

**Priority levels:**
- **P0** — blocker, must resolve before meaningful progress.
- **P1** — current phase deliverables.
- **P2** — next phase (Phase 1 after Phase 0 success).
- **P3** — backlog / per-phase findings to address.

---

## P0 — Blockers

### CQ4 — Foundation legal entity

**What:** Set up legal foundation in a crypto-friendly jurisdiction (Switzerland, Singapore, Cayman).

**Why:** Phase 6 funding pipeline is blocked without a legal entity:
- Bug bounty pool ($1M) needs entity to hold and disburse.
- External audits ($400-500k total) need a contracting party.
- Grants to early builders ($300-500k) need a payor.

**Pros of resolving early:** unlocks Phase 6 pre-work (audit firm scheduling) which has long lead times.

**Cons of delay:** Phase 6 cannot start without it; total timeline slips proportionally.

**Owner:** Project leadership (NOT engineering).

**Status:** ☐ Not started.

**Dependencies:** Coordinate with existing Raptoreum Foundation if it exists; if not, create one.

---

### Strategic alignment with public roadmap

**What:** Reconcile this branch's EVM-only design with the public README's commitment to *"smart contracts in 4 major programming languages as opposed to the situation with Ethereum being limited to Solidity."*

**Why:** Public roadmap declared multi-language VM. This branch interprets that as "EVM ecosystem languages (Solidity, Vyper, Yul, Fe)" but the original wording suggested non-EVM (likely WASM-based). Mismatch will produce community pushback at activation, possibly fatal.

**Pros of resolving now:** clean public messaging from day 1; foundation buy-in pre-mainnet.

**Cons of delay:** discovered late = risk of community veto right before mainnet activation, after $4-6M spent.

**Owner:** Project leadership + technical lead.

**Status:** ☐ Not started.

**Possible resolutions:**
- **(a)** Update README and public messaging to reflect EVM-as-VM choice.
- **(b)** Frame this branch as Phase 1 of a multi-VM strategy (EVM first, WASM in v2).
- **(c)** Switch design to WASM-first (drops 90% of current plan; near-total rework).

**Recommended:** (b) — preserves momentum while honoring original commitment.

---

## P1 — Phase 0 deliverables (next session)

**Spike scope:** validate that `evmone` compiles and links into `raptoreumd`. **Zero consensus changes.** Deliverables in order of implementation:

- ☐ Mirror plan to `docs/evm/PLAN.md` so the design doc lives in the repo (currently only at `~/.claude/plans/`).
- ☐ Mirror `/plan-eng-review` decisions to `docs/evm/REVIEW.md`.
- ☐ `depends/packages/evmone.mk` — pin specific evmone commit, configure cross-compile for all supported HOSTs.
- ☐ `depends/packages/intx.mk` (evmone dep) and any transitive deps.
- ☐ Update `depends/packages.mk` to include the new packages.
- ☐ `src/evm/.gitignore` — empty placeholder so directory exists in git.
- ☐ `src/evm/smoke.{h,cpp}` — minimal entry point: `EvmSmokeExecute(bytecode, calldata) → (return_data, gas_used)`. No consensus impact.
- ☐ `src/test/evm_smoke_tests.cpp` — Boost test cases:
  - **Test 1:** empty contract returns successfully with zero return data.
  - **Test 2:** `6005600401` (PUSH1 5, PUSH1 4, ADD, STOP) leaves 9 on stack.
  - **Test 3:** KECCAK256 of "abc" matches expected hash `4e03657a...b41ad`.
- ☐ `src/rpc/ethereum.cpp` — single RPC command `evm_executeReadOnly(bytecode_hex, calldata_hex)` calling into smoke.
- ☐ `src/Makefile.am` — add `src/evm/` source files, link `libevmone.a` and `libintx.a`.
- ☐ `src/test/Makefile.test.include` — add `evm_smoke_tests` to test suite.
- ☐ `src/rpc/register.h` — add `RegisterEthereumRPCCommands()` declaration and call.
- ☐ `src/init.cpp` — wire registration in `RegisterAllCoreRPCCommands()`.
- ✅ Successfully run `./autogen.sh && ./configure && make -j$(nproc)` (2026-05-11; required Issue-3 and Issue-4 workarounds — see PHASE-0-HANDOFF.md).
- ✅ Successfully run `src/test/test_raptoreum --run_test=evm_smoke_tests` — **3/3 pass** (2026-05-11).
- ✅ Manual RPC test: ADD+RETURN returns `…0009`, KECCAK256("") matches canonical constant `c5d2460186…85a470` (2026-05-11).
- ☐ Run [Ethereum tests/ suite](https://github.com/ethereum/tests) at 100% — recommended in Phase 1.0, not blocking (T1).
- ☐ Update Guix manifest to include evmone (CQ3).
- ✅ Tag branch `phase-0-complete` (2026-05-11).

### Issues discovered during Phase 0 validation — must be fixed in Phase 1.0

These are documented in `docs/evm/PHASE-0-HANDOFF.md` § "Known issues":

- ☐ **Issue 1** — Add top-level `.gitattributes` enforcing LF for build-critical files (`.sh`, `.am`, `.mk`, `.m4`, etc.). Eliminates CRLF-shebang failures for Windows contributors.
- ☐ **Issue 2** — Two options to fix Hunter+HTTPS:
  - (A) `depends/packages/cmake.mk`: `-DCMAKE_USE_OPENSSL=ON`, add openssl as dep.
  - (B) Vendor `evmc`, `intx`, `ethash` as separate `depends/packages/{evmc,intx,ethash}.mk`; patch evmone to disable Hunter. **Recommended** for Guix.
- ☐ **Issue 3** — Add `#ifdef ENABLE_WALLET` guards in `src/assets/assets.cpp`, `src/rpc/rpcassets.cpp`, etc. Fix `#ifdef USE_NATPMP` → `#if USE_NATPMP` in `src/mapport.cpp` (or `configure.ac` to `#undef` on disable).
- ☐ **Issue 4** — Fix `NATPMP_LIBS` substitution in `configure.ac` line ~1347 so `-lnatpmp` is auto-populated.

**Phase 0 success criterion:** all checkboxes ✅. Phase 0 failure = re-evaluate Camino C before committing more capital.

---

## P2 — Phase 1 deliverables (Account Abstraction Layer scaffolding)

After Phase 0 success. New tx types and opcodes wired into consensus, gated behind activation predicate. **No execution yet.**

- ☐ Add `TRANSACTION_EVM_DEPLOY=11`, `_CALL=12`, `_SPEND=13` in `src/primitives/transaction.h:16-28`.
- ☐ Create `src/evm/evmtx.{h,cpp}`:
  - `struct CEvmDeployTx { ... }` with `SERIALIZE_METHODS`.
  - `struct CEvmCallTx { ... }` with `SERIALIZE_METHODS`.
  - `struct CEvmSpendTx { ... }` with `SERIALIZE_METHODS`.
- ☐ Validation functions in `src/evm/evmtx.cpp`:
  - `CheckEvmDeployTx(tx, pindexPrev, state)` — structure + sig + activation gate (no execution).
  - `CheckEvmCallTx(...)`.
  - `CheckEvmSpendTx(...)`.
- ☐ Wire dispatcher cases in `src/evo/specialtx.cpp:18-56` (`CheckSpecialTx`, `ProcessSpecialTx`, `UndoSpecialTx`).
- ☐ Fee handling in `src/consensus/tx_verify.cpp:50-94` for the new types.
- ☐ Add new opcodes `OP_EVMCREATE=0xbd`, `OP_EVMCALL=0xbe`, `OP_EVMSPEND=0xbf` in `src/script/script.h`.
- ☐ Add cases in `src/script/interpreter.cpp` `EvalScript()` (gated, no execution).
- ☐ `src/consensus/consensus.h` — add `SCRIPT_VERIFY_EVM` flag.
- ☐ `src/update/update.h` — add `UPDATE_EVM` enum entry.
- ☐ `src/chainparams.cpp` — add `nEvmActivationHeight` per network. Hard-fork activation params (per D2, NOT BIP9 version-bits).
- ☐ Tests:
  - `src/test/evmtx_serialization_tests.cpp` — round-trip serialization.
  - `src/test/evmtx_validation_tests.cpp` — malformed tx rejected.
  - `test/functional/feature_evm_activation.py` — regtest activation gating.
- ☐ State divergence harness (T2) — Lane E parallel work.

---

## P3 — Documented review findings (per-phase backlog)

Issues from `/plan-eng-review` 2026-05-09. Each is assigned to its target phase.

### Architecture

- ☐ **A7** — Async LLMQ oracle gas/timeout semantics. Without gas escrow + timeout, contracts can DoS the LLMQ system. Phase 4.2.
- ☐ **A8** — BIP44 derivation conflict. RTM uses `coin_type=10226`, Ethereum uses `60`. Document UX clearly. Consider `m/44'/10226'/0'/1/i` for EVM subtree. Phase 5.1.
- ☐ **A9** — EIP-1559 calibration for ~2-min PoW blocks. Adjustment factor `1/8` calibrated for 12s blocks. Recalibrate empirically; possible: `1/64`. Phase 2.4.
- ☐ **A10** — Reorg state revert mechanism. Implement journal-style incremental undo (geth model) with tests for reorgs up to 100 blocks deep. Phase 2.6.
- ☐ **A11** — Smart Asset → ERC-20 address collision. `0xA55E70...` prefix must be blocked from EVM deploys (validation rule in `CheckEvmDeployTx`). Phase 4.1.
- ☐ **A12** — State pruning design. Measure growth in testnet (Phase 6), formalize in Phase 7. Without pruning, full nodes ≥1TB in 2-3 years.

### Code quality

- ☐ **CQ1** — Section 2.1 of plan says "modelar 1:1 sobre `assetsdb.cpp`". Incorrect: EVM state is Merkle Patricia Trie, not flat KV. Use `assetsdb` only as schema model; trie correct from scratch. Phase 2.1.
- ✅ **CQ2** — `docs/evm/BRANCH-GUIDE.md` and `TODOS.md` (this file). Done.
- ☐ **CQ3** — Guix integration for `evmone`. Without this, supply-chain security regresses vs. upstream Bitcoin Core. Phase 0 alongside packaging.
- ☐ **CQ4** — Foundation legal (= P0 above).

### Test gaps

- ☐ **T1** — Ethereum tests/ suite at 100%. Phase 0 success criterion (already in P1).
- ☐ **T2** — State divergence harness: 2-node regtest comparing `stateRoot` per block (enabled by D2). Lane E, Phase 1 onward.
- ☐ **T3** — Reorg fuzzing (property-based). For any block sequence with random reorgs, replay produces identical state. Phase 2.
- ☐ **T4** — `evmone` version pinning + runtime check. Abort node startup on mismatch. Phase 0.
- ☐ **T5** — Address collision regression test (= A11). Phase 4.1.
- ☐ **T-mirror** — Smart Asset mirror divergence fuzzing. 10000-sequence fuzz of `(Smart Asset tx, EVM mutation)` pairs → convergent state. Phase 4.1, from D4.

### Performance

- ☐ **P1-perf** — Re-evaluate ≤500GB-in-5-years target with empirical testnet data. Phase 6.
- ☐ **P2-perf** — State pruning (= A12).
- ☐ **P3-perf** — `ConnectBlock` latency budget instrumented. Target: 500ms p99 with `block_gas_limit=30M` and worker pool of D1. Phase 2.
- ☐ **P4-perf** — Mempool sizing analysis with EVM calldata. Current 300MB likely insufficient; deploys are ~24KB each. Phase 3.

---

## Decisions (frozen)

From `/plan-eng-review` 2026-05-09. **These are not re-litigated without a new review.**

| ID | Topic | Decision | Risk |
|---|---|---|---|
| **D1** | EVM execution lock model | Worker pool + serialized merge, modeled on `src/checkqueue.h`. Never run EVM inside `cs_main`. | LOW |
| **D2** | `CBlockHeader` fields | Add `stateRoot` + `receiptsRoot` + `transactionsRoot`. Hard-fork activation, NOT BIP9 soft-fork. | MEDIUM (coordination) |
| **D3** | LLMQ/ChainLock determinism | Commitment of locks in header (`chainLocksCommit` field). Precompile reads from header, not LLMQ runtime. | MEDIUM |
| **D4** | Smart Assets ↔ EVM | Bidirectional mirror with reconciliation pass and reentrancy guards. Fallback if test T-mirror fails: wrap/unwrap pattern. | **HIGH** ⚠️ |
| **D5** | Tx ordering | Fee-priority v1 (Ethereum-compatible). LLMQ-PBS commit-reveal deferred to v2 hard fork. | LOW |
| **D6** | EVM target hard fork | Cancun (PUSH0, MCOPY, transient storage). Blob txs (EIP-4844) excluded. Prague upgrade path documented. | LOW |

⚠️ **D4 is the highest-risk decision.** Phase 4 design must include explicit ordering rules, allowance mapping in EVM trie (NOT extending `assetsdb`), reentrancy guards, and the T-mirror fuzz test as gating criterion. If T-mirror cannot pass after reasonable effort, fall back to D4-C (wrap/unwrap pattern).

---

## Failure modes — must have test+handling before testnet pública (Phase 6)

5 critical gaps identified by review. None blocks Phase 0; all block testnet pública:

1. **EVM execution race in worker pool** → silent state divergence. Mitigation: T2.
2. **Smart Asset mirror sync inconsistency post-reorg** → funds locked. Mitigation: T-mirror.
3. **LLMQ async signature pending forever** → contract gas drained. Mitigation: A7.
4. **ChainLock commitment omitted by malicious miner** → contracts fail. Mitigation: header validation rejects.
5. **EIP-1559 base fee swing 100x** → "tx never confirms" UX. Mitigation: A9 + load tests.

---

## Lanes (worktree parallelization)

| Lane | Focus | Owner | Earliest start | Status |
|---|---|---|---|---|
| **A** | Consensus (Phase 0 → 1 → 2) | TBD | Phase 0 in next session | not started |
| **B** | RPC + tooling (Phase 3) | TBD | After Lane A finishes Phase 1 | blocked |
| **C** | Precompiles (Phase 4) | TBD | After Lane A finishes Phase 2 | blocked |
| **D** | Wallet + Qt (Phase 5) | TBD | After Lanes A (Phase 1), B (Phase 3) | blocked |
| **E** | Test infra (T2 harness, CI/CD, Guix) | TBD | Day 1 | not started |
| **F** | Legal/funding (Foundation, audit firms) | Leadership | **Day 1 — currently P0 BLOCKER** | not started |

**Conflict flag:** Lane A (Phase 2) and Lane C (Phase 4) both touch `src/validation.cpp`. Coordinate via merge windows.

---

## How to update this file

When checking off an item:
1. Replace `☐` with ✅.
2. Add a parenthetical with commit hash and date: `✅ (2026-05-09, abc1234)`.
3. Move "frozen" entries (decisions, completed phases) into a separate section if it gets too long.
4. Add new findings as they emerge — don't silently lose context.
5. **Never delete a P0 without resolution.** If a P0 becomes irrelevant, document why before removing.
