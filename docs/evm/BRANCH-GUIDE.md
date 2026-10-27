# EVM Integration Branch Guide

> **Branch:** `feat/evm-integration` — native EVM hard-fork (Camino C of the design plan).
> **Scope:** consensus changes, new tx types, new opcodes, EVM execution, `eth_*` RPC, four RTM-native precompiles.
> **Plan:** `docs/evm/PLAN.md` (to be mirrored from local design doc; see TODOS.md → P1).

This file orients contributors working on this branch. It does **not** exist on `master`.

---

## Project context

Raptoreum is a Dash-derivative Bitcoin Core fork with two distinguishing native features:

- **Smart Assets** (`src/assets/`) — native asset creation via special transaction types (no VM required).
- **GhostRider PoW** — CPU-only, ASIC-resistant mining algorithm.

Inherited from Dash: masternodes, **LLMQ** (`src/llmq/`), ChainLocks, InstantSend, deterministic masternode list (`src/evo/`).

**This branch adds:** an EVM execution layer integrated natively into the mainchain (not a sidechain), with state trie, account model, `eth_*` JSON-RPC namespace, and four RTM-specific precompiles exposing Smart Assets, LLMQ threshold signing, ChainLocks, and the masternode registry to Solidity contracts.

---

## Strategic note (must resolve before mainnet)

The public README declares the smart-contract goal as a *"VM protocol that would allow for smart contracts in 4 major programming languages as opposed to the situation with Ethereum being limited to Solidity."*

This branch's design (Camino C, EVM-native) interprets "4 languages" as **the EVM ecosystem** (Solidity, Vyper, Yul, Fe) rather than a multi-VM (WASM + others) approach. This interpretation is non-trivial and requires Foundation/community alignment before mainnet activation. See [TODOS.md](TODOS.md) → P0.

---

## Build environment

Bitcoin-Core-style autotools build. Standard commands on Linux / WSL2 / MSYS2:

```bash
./autogen.sh
./configure --prefix=/usr/local --enable-debug --enable-tests
make -j$(nproc)
src/test/test_raptoreum                              # unit tests
src/test/test_raptoreum --run_test=evm_smoke_tests   # EVM-specific suite
make check                                           # full test suite
```

Cross-compile via the `depends/` system:

```bash
cd depends && make HOST=x86_64-w64-mingw32 -j$(nproc)
cd .. && ./autogen.sh && CONFIG_SITE=$PWD/depends/x86_64-w64-mingw32/share/config.site ./configure --prefix=/
make -j$(nproc)
```

Reproducible builds use Guix (see `contrib/guix/`). When adding `evmone` and other EVM dependencies, the Guix manifest must be updated alongside `depends/packages/*.mk`. See TODOS.md → CQ3.

---

## Code style

Bitcoin Core conventions, strictly enforced:

- **C++17.** No exceptions in consensus code (return-codes only).
- **Format with `clang-format`** using `src/.clang-format`.
- Run `contrib/devtools/clang-format-diff.py` on changes before commit.
- Avoid `auto` for consensus-critical types — explicit is better.
- **Locks:** every shared resource has `EXCLUSIVE_LOCKS_REQUIRED(...)` annotation.
- `DEBUG_LOCKORDER` asserts must be respected; never invert lock order.
- **Consensus changes** are gated behind activation predicates (`Updates().IsEvmActive(height)`); never unconditional.

---

## Where things live

| Area | Path | Status |
|---|---|---|
| New EVM tx types | `src/evm/evmtx.{h,cpp}` | to create (Phase 1) |
| EVM state cache | `src/evm/evmstatecache.{h,cpp}` | to create (Phase 2) |
| EVM state DB | `src/evm/evmstatedb.{h,cpp}` | to create (Phase 2) |
| EVM host (evmc binding) | `src/evm/host.{h,cpp}` | to create (Phase 2) |
| Apply EVM tx | `src/evm/apply.{h,cpp}` | to create (Phase 2) |
| Precompiles | `src/evm/precompiles/*.{h,cpp}` | to create (Phase 4) |
| `eth_*` RPC | `src/rpc/ethereum.cpp` | to create (Phase 3) |
| EVM dependency | `depends/packages/evmone.mk` | to create (Phase 0) |
| Existing assets framework | `src/assets/` | reuse / model |
| Existing LLMQ | `src/llmq/` | reuse for precompiles |
| Existing DMN list | `src/evo/` | reuse for precompiles |
| Existing tx framework | `src/evo/specialtx.{h,cpp}` | extend dispatcher |
| Validation pipeline | `src/validation.cpp` | extend `ConnectBlock` |

---

## Conventions for adding new tx types

Follow the pattern in `src/evo/specialtx.cpp:18-56`. Steps:

1. Add enum entry to `src/primitives/transaction.h:16-28`. Reserved IDs:
   - `TRANSACTION_EVM_DEPLOY = 11`
   - `TRANSACTION_EVM_CALL = 12`
   - `TRANSACTION_EVM_SPEND = 13`
2. Define payload struct with `SERIALIZE_METHODS` macro in `src/evm/evmtx.h`.
3. Add `Check<TxName>Tx()` validation function in `src/evm/evmtx.cpp`.
4. Wire dispatcher cases in `CheckSpecialTx()`, `ProcessSpecialTx()`, `UndoSpecialTx()`.
5. Add fee handling case in `src/consensus/tx_verify.cpp:50-94`.
6. Gate behind `Updates().IsEvmActive(pindexPrev->nHeight)` until activation height.

Reference precedent: Smart Assets added `TRANSACTION_NEW_ASSET=8`, `_UPDATE_ASSET=9`, `_MINT_ASSET=10` via the same pattern. See `src/evo/providertx.cpp:125-219`.

---

## Conventions for adding new opcodes

Available slots after `OP_ASSET_ID = 0xbc`:

- `OP_EVMCREATE = 0xbd` — accompanies `TRANSACTION_EVM_DEPLOY`.
- `OP_EVMCALL = 0xbe` — accompanies `TRANSACTION_EVM_CALL`.
- `OP_EVMSPEND = 0xbf` — UTXO output from EVM account.

Edit `src/script/script.h` enum, add interpretation cases in `src/script/interpreter.cpp:270+` (`EvalScript()` switch). Soft-fork validation flag in `src/consensus/consensus.h` (e.g., `SCRIPT_VERIFY_EVM`).

---

## Locking and concurrency rules (CRITICAL — see D1)

The plan resolves the Qtum-style `cs_main` lock holding problem by using a **worker pool model** (modeled on `src/checkqueue.h`).

- EVM execution runs in worker pool; **never** synchronously inside `cs_main`.
- Only the merge of resulting state diffs into `CEvmStateCache` happens under `cs_main`.
- Never invoke `evmone_execute(...)` from a code path holding `cs_main`.

If a future change requires synchronous EVM execution under `cs_main`, that's a regression that MUST be flagged in code review. Such code must include a comment explaining the deviation and a benchmark proving worst-case `cs_main` hold time stays under 100ms.

---

## Testing requirements

This branch has a higher test bar than typical bitcoin-core changes:

| Tier | Requirement | Phase gate |
|---|---|---|
| Smoke | `src/test/test_raptoreum --run_test=evm_smoke_tests` passes (3/3 cases) | Phase 0 |
| Conformance | [Ethereum tests/ suite](https://github.com/ethereum/tests) passes at **100%** | Phase 0 |
| Divergence | 2-node regtest harness verifies identical `stateRoot` per block (T2) | Phase 1 |
| Reorg | Property-based fuzzing of (random tx batch, random reorg) → identical state (T3) | Phase 2 |
| Mirror | 10000-sequence fuzz of `(Smart Asset tx, EVM mutation)` pairs → convergent state (T-mirror) | Phase 4 |
| Adversarial | Reentrancy, gas griefing, ChainLock omission, base-fee swing tests | Phase 6 |

**No phase is "complete" until its tier of testing passes.** Specifically, Phase 0 is not complete with only the 3 smoke tests — the Ethereum tests/ suite at 100% is required.

---

## Activation model (revised by D2)

The plan's original "soft-fork BIP9" was incorrect for header structure changes. Activation is now **hard-fork coordinated**:

- `nEvmActivationHeight` fixed in `src/chainparams.cpp` per network (mainnet, testnet, regtest, devnet).
- ≥95% masternode signaling required during signaling window before activation height.
- ≥90 days advance notice to operators (release `v2.x.0-rc1` ≥60 days before activation).
- Old nodes will reject new blocks at activation height — **operators MUST upgrade**.
- Grace period of 30 days between LockedIn and Active.

Genesis state of the EVM trie is empty (`stateRoot = keccak256(rlp(empty trie))`).

---

## Frozen design decisions (D1-D7, revised 2026-05-11)

The core team accepted the proposal on 2026-05-11 with revisions to D4 and addition of D7. See [TODOS.md → Decisions](TODOS.md#decisions-revised-per-core-team-acceptance-2026-05-11) for the full table.

| ID | Topic | Decision |
|---|---|---|
| D1 | EVM execution model | Worker pool + serialized merge (NOT inline in `cs_main`) |
| D2 | `CBlockHeader` fields | Add `stateRoot`, `receiptsRoot`, `transactionsRoot` + hard-fork activation |
| D3 | LLMQ/ChainLock determinism | Commitment in header (`chainLocksCommit` field), NOT runtime read of LLMQ managers |
| **D4 (revised)** | **Three asset classes** | **(1) Smart Assets stay UTXO-only and isolated from EVM. (2) New EVM Assets (tx 14/15/16) for DeFi tokens. (3) Optional wrap/unwrap (tx 17/18) for Smart Assets that opt in. No bidirectional mirror.** |
| D5 | Tx ordering | Fee-priority v1; LLMQ-PBS deferred to v2 hard fork |
| D6 | EVM target | Cancun (PUSH0, MCOPY, transient storage); blob txs excluded |
| **D7 (new)** | **Cryptographic surface** | **No new signature schemes. All signing via existing secp256k1 (txs) and LLMQ BLS threshold (oracles, ChainLocks). No zk-SNARKs, no new BLS variants, no exotic curves.** |

---

## ChainID

| Network | ChainID (proposed) | Status |
|---|---|---|
| Mainnet | `7373` (`0x1ccd`) | Pending registration on chainlist.org |
| Testnet | `7374` (`0x1cce`) | — |
| Regtest | `7375` (`0x1ccf`) | — |
| Devnet | `7376` (`0x1cd0`) | — |

EIP-155 replay protection mandatory.

---

## Out of scope for this branch

- WASM / multi-language VMs (potential future v2; see strategic note above).
- L2 rollups / sidechains.
- Privacy via zk-SNARKs / shielded pools.
- LLMQ-PBS / proposer-builder separation (deferred to v2 hard fork per D5).
- EIP-4844 blob transactions (out of Cancun scope; only relevant if RTM hosts L2s).
- Migration of address format (separate UTXO base58 / EVM hex spaces stay).

---

## References

- Plan file (mirror to repo as part of Phase 0): `docs/evm/PLAN.md`.
- Bitcoin Core developer notes: `doc/developer-notes.md`.
- Dash documentation (LLMQ, masternodes): inherited via fork.
- evmone: <https://github.com/ethereum/evmone>
- EVMC interface: <https://github.com/ethereum/evmc>
- Ethereum tests: <https://github.com/ethereum/tests>
