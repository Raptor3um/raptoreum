# TODOS — RTM-EVM Integration

> Branch: `feat/evm-integration`. Plan: `docs/evm/PLAN.md`. Proposal to core team: `docs/evm/PROPOSAL-FOR-CORE-TEAM.md`.
> Last updated: 2026-05-11 (post core-team acceptance + Phase 1.2/1.3).

**Priority levels:**
- **P0** — blocker, must resolve before meaningful progress.
- **P1** — current phase deliverables.
- **P2** — next phase.
- **P3** — backlog / per-phase findings.

---

## ★ Core team acceptance (2026-05-11)

Raptoreum core team **accepted the EVM integration proposal** with the following constraints:

1. **Smart Assets (existing) stay isolated.** Current Smart Assets (tx types 8/9/10) keep working as-is, UTXO-only, with **no EVM exposure**. Owners/users of services built on existing Smart Assets must never be exposed to Solidity bugs.
2. **A new asset class** for EVM-native tokens. Designed for DeFi composability from day 1, with its own tx types reserved (14/15/16).
3. **Optional opt-in wrap/unwrap** for Smart Assets that *want* EVM exposure (post-launch, voluntary per asset).
4. **No exotic signature schemes.** All "AI thing" / oracle integrations must use the existing crypto stack: LLMQ BLS threshold signatures (the "dual consensus" leg alongside PoW) or secp256k1. No zk-SNARKs, no new BLS variants, no exotic curves.
5. **Serialization changes acknowledged** — they expect new tx types and header fields, no objection.

**Consequence: D4 is revised.** See "Decisions (revised)" section below. The original D4-A (bidirectional mirror) is dropped in favor of a three-class asset model.

P0 blockers status:
- ✅ **Strategic alignment with public roadmap** — RESOLVED. Team accepts EVM-native interpretation.
- ☐ **CQ4 — Foundation legal entity** — still open. Required for Phase 6 audit contracts + bug bounty.

---

## P0 — Remaining Blockers

### CQ4 — Foundation legal entity

**What:** Set up legal foundation in a crypto-friendly jurisdiction (Switzerland, Singapore, Cayman).

**Why:** Phase 6 funding pipeline is blocked without a legal entity:
- Bug bounty pool ($1M) needs entity to hold and disburse.
- External audits ($400-500k total) need a contracting party.
- Grants to early builders ($300-500k) need a payor.

**Owner:** Project leadership.

**Status:** ☐ Not started. Lead time 3-6 months for new entity; existing Raptoreum Foundation may already cover this — coordinate.

---

## Phase 0 — DONE (tagged `phase-0-complete`)

All deliverables completed and validated 2026-05-11.

- ✅ `docs/evm/PLAN.md` + `docs/evm/PROPOSAL-FOR-CORE-TEAM.md` + `docs/evm/PHASE-0-HANDOFF.md` (commit `b3b750b90`).
- ✅ `depends/packages/evmone.mk` (commit `b3b750b90`; manual install workaround documented).
- ✅ `src/evm/smoke.{h,cpp}` + `src/test/evm_smoke_tests.cpp` + `src/rpc/ethereum.cpp` (commit `b3b750b90`).
- ✅ Build wiring in `src/Makefile.am`, `src/Makefile.test.include`, `src/rpc/register.h` (commit `b3b750b90`).
- ✅ Build clean inside Ubuntu 22.04 + GCC 11.4 container (2026-05-11).
- ✅ Smoke tests **3/3 pass**, 39 assertions (2026-05-11).
- ✅ Manual RPC test: ADD+RETURN returns `…0009`, KECCAK256("") matches canonical constant `c5d2460186…85a470` (2026-05-11).
- ☐ Run Ethereum tests/ suite at 100% (T1) — recommended Phase 1.0 confirmation, not blocking.
- ✅ Tag `phase-0-complete` (commit `b3b750b90`).

---

## Phase 1.0 — Build hygiene fixes (cleanup of Phase 0 workarounds)

- ✅ **Issue 1** — Add top-level `.gitattributes` enforcing LF on build-critical files. Commit `28ed1c0c2`.
- ☐ **Issue 2** — Two options to fix Hunter+HTTPS:
  - (A) `depends/packages/cmake.mk`: `-DCMAKE_USE_OPENSSL=ON`, add openssl as dep.
  - (B) Vendor `evmc`, `intx`, `ethash` as separate `depends/packages/{evmc,intx,ethash}.mk`; patch evmone to disable Hunter. **Recommended** (offline reproducibility, Guix compat).
- ☐ **Issue 3** — Add `#ifdef ENABLE_WALLET` guards in `src/assets/assets.cpp`, `src/rpc/rpcassets.cpp`, etc. Fix `#ifdef USE_NATPMP` → `#if USE_NATPMP` in `src/mapport.cpp` (or `configure.ac` to `#undef` on disable).
- ✅ **Issue 4** — Fixed `NATPMP_LIBS` substitution + inverted conditional + typo in `configure.ac`. Commit `158621864`.

---

## Phase 1.1 — Scaffolding (DONE)

Commit `cd7c7d24a`.

- ✅ `TRANSACTION_EVM_DEPLOY=11`, `_CALL=12`, `_SPEND=13` added to `src/primitives/transaction.h`.
- ✅ `src/evm/evmtx.{h,cpp}` — three payload structs with `SERIALIZE_METHODS` + EIP-1559 fees + size limits + `Check*Tx()` structural validation.
- ✅ Dispatcher wiring in `src/evo/specialtx.cpp` (CheckSpecialTx / ProcessSpecialTx / UndoSpecialTx).
- ✅ Opcodes `OP_EVMCREATE=0xbd`, `OP_EVMCALL=0xbe`, `OP_EVMSPEND=0xbf` in `src/script/script.h` + `GetOpName()` in `script.cpp`.
- ✅ All EVM txs currently rejected with "evm-not-activated" — safe default.

---

## Phase 1.2 — Unit tests (DONE)

Commit `92c3498dd`.

- ✅ `src/test/evm_evmtx_tests.cpp` — 12 test cases, 65 assertions.
  - Round-trip serialization for the 3 payload structs.
  - `GetTxPayload<T>()` from a `CTransaction` with `vExtraPayload` set.
  - Defaults / size limits / tx type and opcode value stability.

---

## Phase 1.3 — Script interpreter gating (DONE)

Commit `92c3498dd`.

- ✅ `SCRIPT_ENABLE_EVM_OPCODES = (1U << 16)` in `src/script/interpreter.h`.
- ✅ Cases in `src/script/interpreter.cpp` `EvalScript()`: opcodes return `SCRIPT_ERR_BAD_OPCODE` without flag, no-op with flag.
- ✅ Two gating tests in `evm_evmtx_tests.cpp` validating both halves.

---

## Phase 1.4 — Activation gating (NEXT — pending core team consensus on activation height)

- ☐ Add `EUpdate::EVM` enum entry to `src/update/update.h`.
- ☐ Add UPDATE_EVM activation params to `src/chainparams.cpp` per network (mainnet/testnet/regtest/devnet). Hard-fork model, NOT BIP9 (per D2).
- ☐ Replace `evm::IsEvmActive()` stub in `src/evm/evmtx.cpp` with `Updates().IsActive(EUpdate::EVM, pindexPrev)`.
- ☐ Wire `SCRIPT_ENABLE_EVM_OPCODES` into `STANDARD_SCRIPT_VERIFY_FLAGS` after activation height.
- ☐ Functional test `test/functional/feature_evm_activation.py` — regtest activation gating.

---

## Reserved transaction types and opcodes

Reserving slots now so future features don't collide. Implementation lands per phase.

| ID | nType | Tx type | Phase | Status |
|---|---|---|---|---|
| Existing | 8 | TRANSACTION_NEW_ASSET | — | Unchanged (Smart Assets, isolated from EVM) |
| Existing | 9 | TRANSACTION_UPDATE_ASSET | — | Unchanged |
| Existing | 10 | TRANSACTION_MINT_ASSET | — | Unchanged |
| Phase 1.1 | 11 | TRANSACTION_EVM_DEPLOY | 1.1 | ✅ implemented |
| Phase 1.1 | 12 | TRANSACTION_EVM_CALL | 1.1 | ✅ implemented |
| Phase 1.1 | 13 | TRANSACTION_EVM_SPEND | 1.1 | ✅ implemented |
| **NEW** | 14 | TRANSACTION_NEW_EVM_ASSET | Phase 4 | ☐ reserved (revised D4) |
| **NEW** | 15 | TRANSACTION_UPDATE_EVM_ASSET | Phase 4 | ☐ reserved |
| **NEW** | 16 | TRANSACTION_MINT_EVM_ASSET | Phase 4 | ☐ reserved |
| **NEW** | 17 | TRANSACTION_WRAP_ASSET | Phase 5+ | ☐ reserved (opt-in Smart Asset → wrapped ERC-20) |
| **NEW** | 18 | TRANSACTION_UNWRAP_ASSET | Phase 5+ | ☐ reserved |

| Opcode | Value | Phase | Status |
|---|---|---|---|
| OP_ASSET_ID | 0xbc | existing | unchanged |
| OP_EVMCREATE | 0xbd | 1.1 | ✅ implemented |
| OP_EVMCALL | 0xbe | 1.1 | ✅ implemented |
| OP_EVMSPEND | 0xbf | 1.1 | ✅ implemented |

---

## Serialization changes summary (for core team visibility)

Per their note: *"there needs to be some serialization changes I suspect"*. Full list:

**New transaction types** (extending the existing enum in `src/primitives/transaction.h`):
- 11/12/13 — EVM execution operations (DONE).
- 14/15/16 — EVM-native asset operations (Phase 4 — revised D4).
- 17/18 — wrap/unwrap bridge for Smart Assets (Phase 5+, optional opt-in per asset).

**New script opcodes** (in `src/script/script.h`):
- 0xbd/0xbe/0xbf — EVM marker opcodes (DONE).

**`CBlockHeader` field additions** (Phase 2 hard-fork — per D2 and D3):
- `stateRoot` (uint256, 32 bytes) — Merkle Patricia Trie root of EVM account state.
- `receiptsRoot` (uint256, 32 bytes) — Merkle Patricia Trie root of EVM tx receipts.
- `transactionsRoot` (uint256, 32 bytes) — root over EVM tx RLP encodings.
- `chainLocksCommit` (32-128 bytes, structure TBD) — committed list of ChainLocks/IsLocks observed by the miner. Required for deterministic precompile reads.

**New activation flag** (per `src/update/update.h`):
- `EUpdate::EVM` — hard-fork activation height per network.

**No new signature schemes** introduced. All cryptography stays within the existing stack:
- secp256k1 (transaction signatures, EVM ECDSA recovery via existing infrastructure).
- BLS threshold (LLMQ — used for ChainLocks commitments, oracle precompile signatures).

---

## Decisions (revised per core team acceptance 2026-05-11)

| ID | Topic | Decision | Risk |
|---|---|---|---|
| **D1** | EVM execution lock model | Worker pool + serialized merge, modeled on `src/checkqueue.h`. Never run EVM inside `cs_main`. | LOW |
| **D2** | `CBlockHeader` fields | Add `stateRoot` + `receiptsRoot` + `transactionsRoot`. Hard-fork activation, NOT BIP9 soft-fork. | MEDIUM (coordination) |
| **D3** | LLMQ/ChainLock determinism | Commitment of locks in header (`chainLocksCommit` field). Precompile reads from header, not LLMQ runtime. | MEDIUM |
| **D4 (REVISED)** | Smart Assets ↔ EVM | **Three asset classes**: (1) existing Smart Assets stay isolated UTXO-only; (2) new EVM-native asset class (tx types 14/15/16) lives in EVM trie from creation; (3) optional opt-in wrap/unwrap bridge (17/18) for Smart Assets that explicitly want EVM exposure. **No bidirectional mirror.** | LOW |
| **D5** | Tx ordering | Fee-priority v1 (Ethereum-compatible). LLMQ-PBS commit-reveal deferred to v2 hard fork. | LOW |
| **D6** | EVM target hard fork | Cancun (PUSH0, MCOPY, transient storage). Blob txs (EIP-4844) excluded. Prague upgrade path documented. | LOW |
| **D7 (NEW)** | Cryptographic surface | No new signature schemes. All signing via existing secp256k1 (txs) and LLMQ BLS threshold (oracles, ChainLocks). Applies to all current and future "AI thing" / oracle integrations per core team constraint. | LOW |

**Why D4 revision is better than original D4-A:**

| Dimension | Original D4-A (mirror) | Revised D4 (three classes) |
|---|---|---|
| Risk to existing assets | Medium (consensus bug could cross-contaminate) | **Zero** (Smart Assets fully isolated) |
| Consensus complexity | High (reconciliation pass, allowance mapping across namespaces) | **Low** (each class self-contained) |
| Test surface | T-mirror fuzz (10000 sequences, mandatory) | Standard ERC-20 tests for class 2 only |
| Backward compat | Behavior change for existing assets | **100% unchanged for existing assets** |
| Public narrative | "Smart Assets are ERC-20 natively" — overpromised | **"Choose your risk profile"** — actually a better story |
| Foundation risk profile | High — every Smart Asset becomes a DeFi-exposed surface | Low — opt-in only |

---

## Failure modes — must have test+handling before testnet pública (Phase 6)

Updated after D4 revision (T-mirror removed):

1. **EVM execution race in worker pool** → silent state divergence. Mitigation: T2 (state divergence harness).
2. ~~**Smart Asset mirror sync inconsistency post-reorg**~~ — REMOVED (no mirror, no risk).
3. **LLMQ async signature pending forever** → contract gas drained. Mitigation: A7 (gas escrow + timeout).
4. **ChainLock commitment omitted by malicious miner** → contracts fail. Mitigation: header validation rejects.
5. **EIP-1559 base fee swing 100x** → "tx never confirms" UX. Mitigation: A9 + load tests.
6. **NEW: Wrap/unwrap allowance race** (Phase 5+, when 17/18 lands) → wrapped tokens minted without matching lock. Mitigation: atomic locking in single tx.

---

## P3 — Documented review findings (per-phase backlog)

### Architecture

- ☐ **A7** — Async LLMQ oracle gas/timeout semantics. Phase 4.2.
- ☐ **A8** — BIP44 derivation conflict. RTM uses `coin_type=10226`, Ethereum `60`. Consider `m/44'/10226'/0'/1/i` for EVM subtree. Phase 5.1.
- ☐ **A9** — EIP-1559 calibration for ~2-min PoW blocks. Recalibrate empirically; possible: `1/64` denominator. Phase 2.4.
- ☐ **A10** — Reorg state revert mechanism. Journal-style incremental undo (geth model). Phase 2.6.
- ☐ **A11** — Address collision prevention `0xA55E70...` prefix (now applies only to new EVM Assets per D4 revision). Phase 4.1.
- ☐ **A12** — State pruning design. Phase 7 (after testnet growth data).

### Code quality

- ☐ **CQ1** — Don't copy `assetsdb` 1:1 for EVM state trie. Use as schema model; trie correct from scratch. Phase 2.1.
- ✅ **CQ2** — `docs/evm/BRANCH-GUIDE.md` and `TODOS.md`. Done.
- ☐ **CQ3** — Guix integration for `evmone` + vendored deps. Phase 1.0 cleanup.
- ✅ **CQ4** — Foundation legal — bumped to P0 above. Strategic alignment ✅ resolved.

### Test gaps

- ☐ **T1** — Ethereum tests/ suite at 100%. Phase 1.0 recommended confirmation.
- ☐ **T2** — State divergence harness: 2-node regtest comparing `stateRoot` per block. Lane E, Phase 2 onward.
- ☐ **T3** — Reorg fuzzing (property-based). Phase 2.
- ☐ **T4** — `evmone` version pinning + runtime check. Phase 1.0 cleanup.
- ☐ **T5** — Address collision regression test (= A11). Phase 4.1.
- ~~**T-mirror**~~ — REMOVED (no mirror needed after D4 revision).

### Performance

- ☐ **P1-perf** — Re-evaluate ≤500GB target with testnet data. Phase 6.
- ☐ **P2-perf** — State pruning (= A12).
- ☐ **P3-perf** — `ConnectBlock` latency budget instrumented. Target 500ms p99 / 30M gas_limit / D1 worker pool. Phase 2.
- ☐ **P4-perf** — Mempool sizing analysis with EVM calldata. Phase 3.

---

## Open questions to ask the core team

These were not in the proposal's Q-1..Q-10 — they emerged from the acceptance:

- **Q-A1** What is "the AI thing" specifically? AI oracles? AI-driven valuation for RWA tokens? AI-governed treasury? Knowing this lets us validate that the LLMQ oracle precompile (`0x...0a02`) covers the use case, or flag where it doesn't.
- **Q-A2** Do they want the wrap/unwrap pattern for Smart Assets (tx types 17/18) included in v1, or strictly v2? Optional in scope vs. out of scope changes the Phase 5 roadmap.
- **Q-A3** EVM Assets (tx 14/15/16) — should they reuse the existing Smart Assets fee structure (≈5 RTM per asset creation), or have separate economics? Affects fee logic in `consensus/tx_verify.cpp` and the asset registry precompile.

---

## Lanes (worktree parallelization)

| Lane | Focus | Owner | Status |
|---|---|---|---|
| **A** | Consensus (Phase 1.4 → 2) | TBD | ready to start |
| **B** | RPC + tooling (Phase 3) | TBD | blocked by Lane A Phase 1.4 |
| **C** | EVM Asset class + precompiles (Phase 4) | TBD | blocked by Lane A Phase 2 |
| **D** | Wallet + Qt (Phase 5) | TBD | blocked by Lanes A, B |
| **E** | Test infra (T2 harness, CI/CD, Guix CQ3) | TBD | can start now |
| **F** | Legal/Foundation (CQ4) | Leadership | **P0 BLOCKER** for Phase 6 |

**Conflict flag:** Lane A (Phase 2) and Lane C (Phase 4) both touch `src/validation.cpp`. Coordinate via merge windows.

---

## How to update this file

1. Replace ☐ with ✅ when items complete; add `(YYYY-MM-DD, commit_hash)`.
2. Decisions are frozen unless a new review revisits them.
3. Add new findings as they emerge — don't silently lose context.
4. Never delete a P0 without documenting resolution.
