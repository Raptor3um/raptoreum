# Follow-up register (FUP)

Open work items deliberately deferred from the implemented phases.
Each item has a phase tag, a brief description, the code reference
where the deferral is marked, and a priority bucket.

**Priorities:**

- 🚧 **mainnet-blocking** — must land before mainnet activation
- ⚠️ **before-testnet** — should land in time for the public testnet
- 💡 **post-launch** — perf/UX optimisation; can ship after mainnet
- 📐 **calibration** — empirical tuning; needs workload data first

The PRE-mainnet items in particular gate Phase 6 (testnet + audits).

---

## Phase 2 (execution engine) follow-ups

| ID | Priority | Item | Reference |
|---|---|---|---|
| FUP-2.1 | 🚧 | **D2 header fields** — add `stateRoot`, `receiptsRoot`, `transactionsRoot`, `chainLocksCommit` to `CBlockHeader`. Coordinated hard-fork. | `src/primitives/block.h` |
| FUP-2.2 | 🚧 | **EIP-1559 base-fee dynamics.** Currently hardcoded to 0 in `ConnectBlock`. Activates with FUP-2.1. | `src/validation.cpp::ConnectBlock` |
| FUP-2.3 | 🚧 | **Coinbase tip output verification.** Miners must credit `totalCoinbaseTip` to an output; consensus rule verifies. Requires `miner.cpp` work + activation rule. | `src/miner.cpp`, `src/validation.cpp` |
| FUP-2.4 | 💡 | **True parallel EVM execution** with read/write-set tracking + OCC + retry. Phase 2.5 ships parallel pre-flight + serial execute. | `src/evm/parallel.cpp` |
| FUP-2.5 | 💡 | **Release `cs_main` during EVM execution** (Qtum mitigation). Requires `ConnectBlock` refactor. | `src/validation.cpp::ConnectBlock` |
| FUP-2.6 | ⚠️ | **Functional/integration test** that runs a regtest, deploys + calls a contract, reorgs across the EVM tx, asserts state restoration. | `test/functional/` |
| FUP-2.7 | ⚠️ | **Sandbox EVM caches** in `TestBlockValidity` (≈L4436) + `VerifyDB` (≈L4934). Currently pass `evmStateCache=nullptr`. | `src/validation.cpp` |
| FUP-2.8 | 📐 | **Block-gas-limit calibration**. Currently hardcoded 30M; tune to actual ConnectBlock latency budget. | `src/validation.cpp::ConnectBlock` |

## Phase 3 (RPC namespace) follow-ups

| ID | Priority | Item | Reference |
|---|---|---|---|
| FUP-3.1 | ⚠️ | **Receipt-pre-funding via wallet.** Today the end-to-end "send → mine → getReceipt" path needs an EVM-balance sender; the wallet integration of Phase 5 unlocks this. | — |
| FUP-3.2 | 💡 | **Non-empty access lists** in `eth_sendRawTransaction`. Currently rejected; needs warm-slot wiring to evmone. | `src/evm/rawtx.cpp::DecodeEip1559` |
| FUP-3.3 | 💡 | **Historical block-tag** support for state queries (`eth_getBalance @ 0x100` etc.). Needs the receipt indexer + state-snapshot replay. | `src/rpc/ethereum.cpp::RequireLatestBlockTag` |
| FUP-3.4 | 💡 | **`eth_getTransactionByHash` rich fields** (value, input, gas, gasPrice). Currently sourced from the receipt; need txindex integration. | `src/rpc/ethereum.cpp::eth_getTransactionByHash` |
| FUP-3.5 | 💡 | **WebSocket / `eth_subscribe`** for `newHeads` and `logs`. Needs a second long-lived transport on the listener. | `src/httpserver.cpp` |
| FUP-3.6 | 📐 | **`eth_estimateGas` binary search** to find minimum-gas-that-succeeds. Today single-attempt at the supplied limit. | `src/rpc/ethereum.cpp::eth_estimateGas` |

## Phase 4 (precompiles) follow-ups

| ID | Priority | Item | Reference |
|---|---|---|---|
| FUP-4.1 | ⚠️ | **Hash160 → assetId index** in `CAssetsCache`. Replaces the linear scan in the Asset ERC-20 resolver. | `src/evm/precompile_asset_erc20.cpp::ResolveAssetIdFromAddress` |
| FUP-4.2 | 🚧 | **Bidirectional Smart Asset ↔ EVM balance mirror** (D4-revised "T-mirror" gate). Enables `transfer`/`transferFrom`/`approve`/`allowance` on the ERC-20 precompile. | `src/evm/precompile_asset_erc20.cpp::balanceOf` |
| FUP-4.3 | ⚠️ | **Async `requestSignature`** with gas escrow + timeout per A7. State-mutating; needs Phase 5 wallet wiring. | `src/evm/precompile_llmq_oracle.cpp` |
| FUP-4.4 | 📐 | **Gas-cost calibration** for the precompiles (currently flat 5000–8000 per call). | All four `src/evm/precompile_*.cpp` |
| FUP-4.5 | 💡 | **IPv6 support** in `Masternode.ip` field. Currently IPv4 only. | `src/evm/precompile_masternodes.cpp::EncodeMasternodeStruct` |
| FUP-4.6 | 💡 | **Per-asset `symbol` / ticker** field separate from `name` in `CAssetMetaData`. Today `symbol()` returns `name`. | `src/assets/assets.h::CAssetMetaData` |
| FUP-4.7 | 💡 | **`latestChainLockedHeight` exact water-line.** Currently a 256-block reverse scan from tip. Track the latest locked-height directly in `CChainLocksHandler`. | `src/evm/precompile_chainlocks.cpp` |

## Phase 5 (wallet) follow-ups

| ID | Priority | Item | Reference |
|---|---|---|---|
| FUP-5.1 | 🚧 | **Wallet-keystore HD-path integration.** Awaits Q-A2 resolution (Ethereum-standard `m/44'/60'` vs RTM subtree `m/44'/10226'/0'/1/i`). | — |
| FUP-5.2 | ⚠️ | **Qt UI** — `EvmContractDialog` for deploy/call, transaction-record types `DeployContract` / `CallContract` / `EvmInternalTransfer` / `EvmToUtxo`. | `src/qt/transactionrecord.h` |
| FUP-5.3 | 💡 | **Wallet RPC `evm_deploycontract` / `evm_sendcall` / `evm_spendtoutxo`** that wraps the Phase 5 signing primitives in a "give me a key from the wallet" flow. | — |
| FUP-5.4 | 💡 | **Legacy EIP-155 signing** in `evm_signTransaction` (currently EIP-1559 only). | `src/evm/signing.cpp` |

## Cross-cutting follow-ups

| ID | Priority | Item |
|---|---|---|
| FUP-X.1 | 🚧 | **Vendor evmone/intx/ethash** per Issue 2 from the core-team acceptance — replace the depends/evmone manual build with proper depends/-managed builds for deterministic builds (Guix). |
| FUP-X.2 | 🚧 | **`#ifdef ENABLE_WALLET` guards** per Issue 3 — currently `--disable-wallet` doesn't compile cleanly with the EVM path. Workaround today: always pass `--enable-wallet`. |
| FUP-X.3 | 🚧 | **CQ4 Foundation legal entity** (from the original engineering-review register). Phase 6 bug-bounty pool + audit payments need an entity. |
| FUP-X.4 | ⚠️ | **T1 — Ethereum tests suite** (10k+ vectors) integrated into CI. Catches evmone version drift + consensus regressions cheaply. |
| FUP-X.5 | ⚠️ | **T2 — State-divergence harness.** Two nodes in regtest comparing `stateRoot` after every block; abort CI on divergence. (Requires FUP-2.1.) |
| FUP-X.6 | ⚠️ | **T3 — Reorg fuzzing.** Property-based: for any sequence of blocks with random reorgs, replay produces identical state. Catches undo-journal bugs (A10). |
| FUP-X.7 | 💡 | **T-mirror** — 10000 sequences of `(Smart Asset tx, EVM tx mutating same asset)` converge across nodes. Acceptance gate for FUP-4.2. |
| FUP-X.8 | 📐 | **State-pruning design** (A12). Measure crowed-out disk first; design later. |

---

## Open consensus rules to formalise

These aren't "follow-ups" — they're decisions that need to be
codified before testnet:

- **Block gas limit** (currently hardcoded 30M; chainparams field).
- **EIP-1559 base-fee dynamics** (factor, target gas, ceiling).
- **Per-asset gas-fee model** (Q-A3 to the core team — reuse the ~5
  RTM asset-creation fee or charge separately?).
- **Wrap/unwrap (tx types 17/18)** v1 or strict v2 (Q-A2 to the
  core team).
- **Activation height** per network (mainnet / testnet / regtest
  already at 0).

See [`QUESTIONS.md`](QUESTIONS.md) for the questions still open
with the core team.
