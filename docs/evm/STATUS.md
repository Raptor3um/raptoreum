# EVM integration — implementation status

> Branch: `feat/evm-integration` on `JSanchezFDZ/raptoreum`
> 31 commits ahead of `upstream/master`. All pushed.
> Last updated: 2026-05-12.

This file maps each plan phase to the actual commits that shipped it,
plus the test surface and what's deliberately deferred. It's the
source of truth for "what's done vs. what's pending" — the strategic
narrative lives in [`PLAN.md`](PLAN.md) and the formal acceptance
record in [`PROPOSAL-FOR-CORE-TEAM.md`](PROPOSAL-FOR-CORE-TEAM.md).

## TL;DR

| Phase | Status | Highlights |
|---|---|---|
| **0** — evmone spike | ✅ shipped | Linkage validated; `evm_executeReadOnly` smoke RPC |
| **1** — AAL (account-abstraction layer) | ✅ shipped | Three new tx types + opcodes + activation gate |
| **2** — EVM execution | ✅ shipped | State primitives → evmc host → apply layer → process layer → ConnectBlock wiring → reorg journal → worker pool |
| **3** — JSON-RPC `eth_*` namespace | ✅ shipped | 22 ethereum-namespace methods; sendRawTransaction; receipts; logs; default 8545 listener |
| **4** — RTM-native precompiles | ✅ shipped | ChainLocks, Masternode Registry, Smart Assets ERC-20, LLMQ Oracle |
| **5** — Wallet integration | ✅ partial | C++ EIP-1559 signing primitives + 3 evm-namespace RPC; HD wallet + Qt UI deferred |
| **6** — Testnet + audits | ☐ pending | — |
| **7** — Mainnet activation | ☐ pending | — |
| **8** — Ecosystem launch | ☐ pending | — |

**Test surface:** 11 EVM unit-test suites, ~110 cases / >700 assertions
all green on every commit. End-to-end live-validated against a
regtest daemon for every RPC method (the canonical
`0x4646…4646` test key was used; results are byte-for-byte identical
to `eth-account`).

**Net new EVM-side code:** ~6000 LOC in `src/evm/` (16 files) plus
RPC, validation-pipeline, consensus carve-outs, and chainparams
wiring. See [`BUILD.md`](BUILD.md) for the file inventory and how
to compile.

---

## Phase 0 — evmone spike ✅

| Commit | Scope |
|---|---|
| `c3909e50d` | Phase 0 spike: vendored evmone, `evm_executeReadOnly` RPC, smoke tests |
| `51d38a437` | `.gitattributes` (LF line endings on build-critical files) |
| `f434a9470` | configure.ac NATPMP_LIBS fix |

**Verified:** evmone links cleanly into `raptoreumd`; bytecode
executes against an empty world; 3/3 smoke tests pass.

---

## Phase 1 — Account Abstraction Layer ✅

| Commit | Scope |
|---|---|
| `80c7e262c` | Phase 1.1: TX types 11/12/13 + opcodes 0xbd/0xbe/0xbf + validation stubs |
| `f34bcdf41` | Phase 1.2/1.3: serialization tests + `SCRIPT_ENABLE_EVM_OPCODES` gating |
| `0386419b4` | Core-team acceptance: D4 revised + D7 added |
| `a4f9b17b7` | Phase 1.4: activation gate wired to `UpdateManager` |
| `b853e7e8f` | Phase 1.5: validation-surface completion + ops helper |

**Three new transaction types** (active behind `Updates().IsEvmActive()`):

- `TRANSACTION_EVM_DEPLOY = 11` — contract creation
- `TRANSACTION_EVM_CALL = 12` — contract call
- `TRANSACTION_EVM_SPEND = 13` — EVM → UTXO move

**Three new opcodes** in `OP_NORMAL` script: `OP_EVMCREATE` (0xbd),
`OP_EVMCALL` (0xbe), `OP_EVMSPEND` (0xbf).

---

## Phase 2 — EVM execution ✅

| Sub-phase | Commit | Scope |
|---|---|---|
| 2.1 — state primitives | `a48d22313` | `CEvmAccount`, `CEvmStateDB`, `CEvmStateCache` |
| 2.2 — evmc host | `dad0d3069` | Full `evmc::Host` (14 overrides) over the cache |
| 2.3a — `ApplyEvmCallTx` | `ae8e6a292` | Single-call execution path |
| 2.3b — `ApplyEvmDeployTx` + Keccak/CREATE | `24b2a93aa` | Real Keccak-256 + CREATE address derivation |
| 2.3c — `ApplyEvmSpendTx` | `1eb006846` | EVM → UTXO move @ 10¹⁰ wei/satoshi |
| 2.3d — snapshot/revert + nested calls | `4a3e1d384` | Snapshot stack + nested CALL/DELEGATECALL/STATICCALL/CALLCODE + CREATE/CREATE2 |
| 2.4 — EIP-1559 process layer | `fb8dd3ec4` | Preflight + sender debit/refund + nonce + burn/tip split |
| 2.4e-1 — block iterator | `53891ae97` | `ProcessEvmTransactionsInBlock` |
| 2.4e-2 — ConnectBlock wiring | `90fb43f08` | `pevmstatedb` global + dispatch behind activation gate |
| 2.6-1 — reorg journal data | `9fcac2814` | `CEvmStateUndo` + `BuildUndoFromCache` + `ApplyUndoToDB` + DB persistence |
| 2.6-2 — reorg journal wiring | `c33d4f2d7` | `ConnectTip` writes undo + `DisconnectTip` applies + erases |
| 2.5 — worker pool (D1 MVP) | `26328f77e` | Parallel pre-flight + serial execute, byte-identical to serial |

**EVM revision target:** Cancun (D6). PUSH0, MCOPY, TLOAD/TSTORE
(EIP-1153), KZG_POINT_EVALUATION precompile, EIP-2929 cold/warm
access lists. Blob txs (EIP-4844) excluded.

**Ethereum compatibility verified:**

- Keccak-256("") = `c5d2460186…85a470` (canonical)
- CREATE address derivation matches Ethereum yellow paper test
  vectors for `(sender=0x6ac7ea33…dbf0, nonce in 0..3)`
- ⇒ Solidity contracts deployed via our DEPLOY tx land at the same
  address they would on Ethereum mainnet for the same `(sender, nonce)`.

---

## Phase 3 — JSON-RPC `eth_*` namespace ✅

| Sub-phase | Commit | Methods |
|---|---|---|
| 3.1 — read-only state | `24b3f1e67` | chainId, blockNumber, gasPrice, getBalance, getTransactionCount, getCode, getStorageAt |
| 3.2 — execution probes | `db5ded10d` | eth_call, eth_estimateGas |
| 3.3 — block exploration | `285df40a5` | getBlockByNumber, getBlockByHash, getBlockTransactionCountByNumber/Hash |
| 3.4 — metadata | `06b244a6e` | eth_protocolVersion, syncing, accounts, coinbase, mining, hashrate, maxPriorityFeePerGas, net_version/listening/peerCount, web3_clientVersion |
| 3.5 — write path | `a6b635f4a` | eth_sendRawTransaction (RLP + EIP-1559/legacy + EIP-155 sig recovery via secp256k1 + consensus carve-outs) |
| 3.6 — receipts/logs | `57d3bbec4`, `2f6cd4982` | eth_getTransactionReceipt, eth_getTransactionByHash, eth_getLogs (with filter) |
| 3.7 — second listener | `45a3f445e` | `-evmrpcport` default 8545 |

**22 ethereum-namespace methods** total. See [`RPC.md`](RPC.md) for the
complete reference with examples.

**Cross-index** between Ethereum tx hash (keccak256 of wire bytes)
and Raptoreum wrapper hash (sha256d): forward + reverse direction
written at `eth_sendRawTransaction` submit time so dApps look up
receipts via the Ethereum hash they got back.

**Consensus carve-outs** for EVM-typed txs (vin/vout-empty,
nType allow-list, relay-fee skip) live in:
`src/consensus/tx_check.cpp`, `src/validation.cpp`, `src/chainparams.cpp`.

**MetaMask connects out of the box**: chainId 7373/7374/7375
(mainnet/testnet/regtest), 8545 listener default-on, 22 method
namespace covers everything MetaMask polls on connect.

---

## Phase 4 — RTM-native precompiles ✅

The differentiator. Four Solidity-callable contracts that expose
Raptoreum subsystems no other L1 EVM has.

| Address | Precompile | Commit |
|---|---|---|
| `0x…0a02` | LLMQ Oracle (BLS threshold sigs) | `26a6933b9` |
| `0x…0a03` | ChainLocks (2s finality probe) | `0454a95a9` |
| `0x…0a04` | Masternode Registry | `26a6933b9` |
| `0xA55E70…hash160(assetId)` | Smart Asset ERC-20 (one per asset) | `26a6933b9` |

See [`PRECOMPILES.md`](PRECOMPILES.md) for the Solidity interfaces,
selector tables, and per-method semantics.

---

## Phase 5 — Wallet integration ✅ (partial)

| Sub-phase | Commit | Scope |
|---|---|---|
| 5.1 — `EvmAddressForKey` | `fdd0846b6` | keccak256(uncompressed_pubkey[1..])[12..] |
| 5.2 — `SignEip1559Tx` | `fdd0846b6` | Full type-0x02 envelope, byte-identical to eth-account |
| 5.3 — RPC handlers | `fdd0846b6` | `evm_keyToAddress`, `evm_signTransaction`, `evm_sendTransaction` |

**Live parity check** vs. `eth-account` (Python) on the canonical
test key `0x4646…4646`:

- `evm_keyToAddress` → `0x9d8a62f656a8d1615c1294fd71e9cfb3e4855a4f`
  (eth-account: identical)
- `evm_signTransaction` wire bytes: byte-for-byte identical to
  `Account.sign_transaction(tx, key).raw_transaction`
- `evm_sendTransaction` → submitted; same Ethereum tx hash
  `0x07b187…0346b` lands in mempool

**Deferred to Phase 5.x follow-ups:**

- Wallet-keystore HD-path integration. Requires Q-A2 resolution
  (Ethereum-standard `m/44'/60'` vs RTM-subtree
  `m/44'/10226'/0'/1/i`).
- Qt UI dialogs (deploy/call). Separate session — Qt + MOC.

---

## Post-Phase-5 — on-chain end-to-end (D2 + value bridges + D4 mirror) ✅

Shipped after the 2026-05-12 snapshot; makes the EVM fully usable on-chain.

| Area | Scope |
|---|---|
| **D2 header-commitment hard fork** | `CCbTx` v3 commits `evmStateRoot` / `evmReceiptsRoot` / `evmBaseFee` / `evmGasUsed` / `evmExecTime` in the coinbase special tx, RIP-vote-gated (`EUpdate::EVM_COMMIT`, force-active@0 on regtest). Miner==validator parity; EIP-1559 burn/tip live. |
| **UTXO → EVM funding** | `TRANSACTION_EVM_FUND = 19` + `evm_fund` RPC: supply-conserving (the funded amount leaves the UTXO miner-claimable fee and reappears as EVM balance). |
| **EVM → UTXO spend** | `TRANSACTION_EVM_SPEND` credits realized as coinbase outputs (`CheckCoinbaseRealisesSpendCredits`); funded e2e proven (`eth_sendRawTransaction` → mine → receipt status `0x1`). |
| **D4 Smart-Asset mirror** | Bidirectional wrap/unwrap bridge + EVM-side ERC-20 ledger. See [`SMART-ASSET-MIRROR.md`](SMART-ASSET-MIRROR.md). |

**D4 mirror** (`TRANSACTION_WRAP_ASSET = 17` / `TRANSACTION_UNWRAP_ASSET =
18`): every Smart Asset is callable as an ERC-20 at its per-asset precompile
address, and units move both ways across the UTXO ↔ EVM boundary,
supply-conserving (`UTXO balances + wrappedSupply == circulatingSupply`) and
reorg-safe. Wallet RPCs `wrap_asset` / `unwrap_asset` /
`get_asset_evm_address`. Proven end-to-end on a live regtest daemon
(create+mint → wrap → `eth_call balanceOf` → unwrap → verify). Consensus
suites (`evm_wrap_consensus_tests`, `evm_asset_wrap_tests`,
`evm_asset_mirror_convergence_tests`, `evm_asset_erc20_tests`) are hard CI
gates.

**Capa B (official Ethereum tests):** ~99.95% of applicable Cancun
(`evm_official_blockchaintest_tests`), CI-locked at a pass floor with a
classified non-fixable allow-list. See [`OFFICIAL-TESTS.md`](OFFICIAL-TESTS.md).

### Post-mirror hardening (correctness + completeness)

A wallet-compatibility + correctness pass over the `eth_*`/`evm_*` surface
and the asset resolver, each landed with a regression test. See
[`FUP.md`](FUP.md) for the full ✅-shipped register.

| Area | What changed |
|---|---|
| `eth_estimateGas` (FUP-3.6) | Returns a complete tx gas limit — intrinsic (21000 + EIP-2028 calldata) + binary-searched min execution gas (63/64 rule). Was execution-only, under-funding real txs. |
| EIP-1559 RPC surface | `eth_gasPrice` = next-block base fee + tip; `eth_maxPriorityFeePerGas` = 1 gwei; block object now carries `baseFeePerGas`/`gasUsed`/`stateRoot`/`receiptsRoot` from the CCbTx v3 commitment (were zero/missing); real `difficulty`/`totalDifficulty` (FUP-3.8). |
| Tx-hash identity (FUP-3.7) | EVM txs use one Ethereum identity across block listing / receipt / `eth_getTransactionByHash` — dApps can cross-reference. |
| `eth_getTransactionByHash` (FUP-3.4) | Full tx shape (value/input/gas/nonce/...) loaded from the block. |
| Legacy signing (FUP-5.4) | `evm::SignLegacyTx` + a `"type":"0x0"` path in `evm_signTransaction`/`evm_sendTransaction`. |
| Asset resolver (FUP-4.1) | `CAssetsCache::ResolveAssetIdByTag` — O(log N) **self-correcting** verified hint index (consensus-safe by construction) replacing the O(N) precompile scan. |
| `unwrap_asset` fee (FUP-4.8) | Reserves the appended mint output's size in the fee. |

---

## Outstanding work (not yet started)

- **Phase 6** — Testnet pública + security review by the Raptoreum
  core team (per the 2026-06 project decision, no external audit firm
  is engaged; [`REVIEW-GUIDE.md`](REVIEW-GUIDE.md) is the review entry
  point and [`TESTNET-ACTIVATION.md`](TESTNET-ACTIVATION.md) the
  activation proposal), plus the bug-bounty program (Q-A10).
- **Phase 7** — Mainnet activation height + 90-day comm windows.
- **Phase 8** — Ecosystem launch (RaptorSwap, lending fork, bridge,
  block explorer).

See [`FUP.md`](FUP.md) for the full register of
deferred work and [`QUESTIONS.md`](QUESTIONS.md) for the open
questions the core team still owes us.
