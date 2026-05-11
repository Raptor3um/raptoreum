# RTM-EVM Integration Proposal

> **Audience:** Raptoreum Core team and Foundation
> **Branch:** `feat/evm-integration`
> **Status:** Phase 0 spike in active validation. This document is a request for technical validation before any consensus-level work begins.
> **Date prepared:** 2026-05-11

---

## Table of contents

1. [Executive summary](#1-executive-summary)
2. [Strategic context](#2-strategic-context)
3. [Why EVM, why now, why native](#3-why-evm-why-now-why-native)
4. [Proposed architecture](#4-proposed-architecture)
5. [The four RTM-native precompiles](#5-the-four-rtm-native-precompiles)
6. [Design decisions D1–D6 (for validation)](#6-design-decisions-d1d6-for-validation)
7. [Phase roadmap and current status](#7-phase-roadmap-and-current-status)
8. [Known problems and proposed solutions](#8-known-problems-and-proposed-solutions)
9. [Open questions for the core team](#9-open-questions-for-the-core-team)
10. [Validation request and next steps](#10-validation-request-and-next-steps)

---

## 1. Executive summary

We propose to integrate the Ethereum Virtual Machine (EVM) natively into the Raptoreum mainchain via a coordinated hard-fork, enabling Solidity smart contracts on RTM while preserving the existing UTXO subsystem, Smart Assets, GhostRider PoW, and Dash-derived masternode/LLMQ infrastructure.

The approach (internally referred to as **"Camino C"** to distinguish it from a sidechain alternative) integrates the [evmone](https://github.com/ethereum/evmone) C++ EVM implementation through the standard [EVMC](https://github.com/ethereum/evmc) interface, adds three new special-transaction types and three new script opcodes for EVM operations, and exposes four RTM-specific precompiled contracts that give Solidity contracts native access to **Smart Assets** as ERC-20, the **LLMQ threshold-signing oracle**, **ChainLocks finality**, and the **deterministic masternode registry**.

The integration targets **Ethereum Cancun** (PUSH0, MCOPY, transient storage TLOAD/TSTORE) for maximum compatibility with Solidity 0.8.20+ tooling, while explicitly excluding EIP-4844 blob transactions.

A full engineering review identified six load-bearing design decisions (D1–D6 below) that should be ratified by the core team before Phase 1 (consensus code) begins. The most consequential are:

- **D2** — Adding `stateRoot`, `receiptsRoot`, and `transactionsRoot` to `CBlockHeader` requires a **hard-fork activation**, not a BIP9-style soft-fork. This is a more invasive coordination event than typical RTM upgrades.
- **D4** — The "Smart Assets as ERC-20 native" feature relies on a bidirectional state-mirror between the existing UTXO-based `CAssetsCache` and the new EVM state trie. This is the **highest-risk engineering decision** in the plan and includes a documented fallback to a `wrap/unwrap` model.
- A **strategic conflict** with the public README's commitment to *"smart contracts in 4 major programming languages as opposed to the situation with Ethereum being limited to Solidity"* must be resolved with the community before mainnet activation.

The full plan spans approximately 26 months across 8 phases, with a projected operational budget of ~$4–6M USD (engineering team of 6, three external audits, bug bounty, foundation overhead). Phase 0 (a 2-week spike to validate that `evmone` links cleanly into `raptoreumd`) is in active progress at the time of this document.

---

## 2. Strategic context

### 2.1 What Raptoreum is today

Raptoreum is a Dash-derivative Bitcoin Core fork (`src/` of the current `master` branch) with two distinguishing native features:

- **Smart Assets** (`src/assets/`) — native asset creation via special transaction types (`TRANSACTION_NEW_ASSET = 8`, `TRANSACTION_UPDATE_ASSET = 9`, `TRANSACTION_MINT_ASSET = 10`). No virtual machine required. Persistent state in `assetsdb` with prefix-keyed schema (`'A'` metadata, `'B'` name→txid, `'C'`/`'D'` balance indexes).
- **GhostRider PoW** — CPU-only, ASIC-resistant mining algorithm.

Inherited from Dash:

- **LLMQ** (`src/llmq/`) — Long-Living Masternode Quorums with BLS threshold signing.
- **ChainLocks** (`src/llmq/quorums_chainlocks.{h,cpp}`) — ~2-second probabilistic finality via LLMQ-signed block confirmations.
- **InstantSend** (`src/llmq/quorums_instantsend.{h,cpp}`) — ~3-second transaction-level locks.
- **Deterministic Masternode list** (`src/evo/deterministicmns.{h,cpp}`) — `CDeterministicMNManager` with `immer::map` of `CDeterministicMN` records.

### 2.2 What Raptoreum lacks

Smart Assets provide **issuance** primitives but **no programmability**. There is no virtual machine, no execution layer for arbitrary user-defined logic. As a consequence:

- No Automated Market Makers (Uniswap-style DEXes).
- No lending protocols (Aave-style markets).
- No on-chain governance beyond Dash-inherited proposal voting.
- No composable NFT marketplaces beyond static metadata.
- No bridges that can verify foreign-chain state.

In practice, this places Raptoreum in a structurally weaker market position than EVM-compatible chains for DeFi, NFT, RWA-tokenization, and cross-chain use cases — independent of any technical merits of the underlying chain.

### 2.3 The README commitment

The Raptoreum public README declares the smart-contract goal as:

> *"Integrating, developing and deploying a VM protocol that would allow for smart contracts in 4 major programming languages as opposed to the situation with Ethereum being limited to Solidity."*

This proposal interprets "4 languages" as **the EVM ecosystem**: Solidity, Vyper, Yul, Fe. Other reasonable interpretations exist (WASM with Rust/AssemblyScript/Go/C, or a fully custom multi-VM design like Polkadot). We acknowledge this interpretation requires explicit community/Foundation validation. See [§9 Question Q-1](#9-open-questions-for-the-core-team).

---

## 3. Why EVM, why now, why native

### 3.1 Why EVM specifically

Three factors made the EVM ecosystem the recommended target over WASM, MoveVM, or a custom VM:

1. **Developer pool.** Solidity developers outnumber developers in any competing smart-contract language by approximately one order of magnitude. Adoption-velocity for any new L1 is gated by builder availability.
2. **Audit ecosystem.** Trail of Bits, OpenZeppelin, ChainSecurity, Quantstamp, ConsenSys Diligence, and Spearbit all maintain mature Solidity audit practices. WASM/Move auditors are rarer and more expensive.
3. **Application portability.** A working `raptoreum-evm` chain can host forks of Uniswap V2/V3, Aave V2/V3, Curve, ERC-20/721/1155 standards, Chainlink-style oracles, and the OpenZeppelin contracts library *on day one*. Composability comes "for free" via standard ABIs.

The cost of choosing EVM is acceptance of EVM's well-known limitations: 256-bit word size inefficiency, MEV surface area, gas metering complexity, and the SELFDESTRUCT migration debate. None of these are blockers; all are well-understood by the audit and tooling community.

### 3.2 Why a hard-fork integration ("Camino C") rather than a sidechain ("Camino A")

A sidechain (EVM chain anchored to RTM via an LLMQ-federated two-way peg, modeled on RSK over Bitcoin) was considered and rejected for the following reasons:

- **Liquidity fragmentation.** RTM and wRTM would live in separate ledgers with imperfect price linkage.
- **Two security models.** A sidechain's consensus need not be Raptoreum's; user funds on the sidechain depend on a different validator set with different incentives.
- **Composability degradation.** Smart Assets on the mainchain would need to be wrapped to be visible to sidechain contracts, adding latency and risk.
- **Narrative dilution.** "RTM has smart contracts via a sidechain" is a weaker product story than "RTM has smart contracts natively."

The sidechain approach is preserved as a **documented fallback** if Phase 0 reveals fundamental incompatibilities (see [§8.1](#81-phase-0-validation-failure-modes)).

### 3.3 Why now

Three windows are closing:

1. **Cancun stability.** Ethereum's Cancun upgrade (March 2024) introduces transient storage (EIP-1153), MCOPY, and PUSH0 — all of which Solidity 0.8.20+ relies on by default. Starting on London or Shanghai means immediately playing catch-up; starting on Cancun means we match what tooling expects.
2. **Audit availability.** Tier-1 audit firms are scheduling 6–12 months out for L1-class engagements. Booking auditors during Phase 0 lets us land them at Phase 6 (testnet hardening).
3. **Cross-chain bridge maturity.** LayerZero v2, Wormhole v2, and Axelar have matured to the point where standard ETH/BSC/SOL bridge contracts can be deployed against a new EVM L1 with minimal custom integration work.

---

## 4. Proposed architecture

### 4.1 Single-binary, single-ledger, two subsystems

```
┌────────────────────────────────────────────────────────────────────────┐
│                    raptoreumd (single binary)                          │
│                                                                        │
│  ┌─────────────────────┐         ┌───────────────────────────────┐     │
│  │   UTXO subsystem    │         │   EVM subsystem (NEW)         │     │
│  │   (existing)        │         │                               │     │
│  │                     │  AAL    │                               │     │
│  │  CCoinsViewCache    │ <-----> │  CEvmStateCache               │     │
│  │  (chainstate/)      │         │  (evmstate/)                  │     │
│  │                     │         │                               │     │
│  │  - Coinbase/PoW     │         │  - Account state trie         │     │
│  │  - Smart Assets   <-┼-precompile 0a01─┐                       │     │
│  │  - DMN / LLMQ ----┼─precompiles 0a02-04                      │     │
│  │  - InstantSend      │         │  - Storage trie per contract  │     │
│  │  - ChainLocks       │         │  - Logs / receipts            │     │
│  └─────────────────────┘         └───────────────────────────────┘     │
│                                                                        │
│  ┌────────────────────────────────────────────────────────────────┐    │
│  │  ConnectBlock (validation.cpp ~line 2075) — atomic per block   │    │
│  │  ├─ Pass 1: UpdateCoins (UTXO + Smart Assets native txs)       │    │
│  │  └─ Pass 2: ApplyEvmTxBatch (parallel worker pool, then merge) │    │
│  └────────────────────────────────────────────────────────────────┘    │
│                                                                        │
│  ┌────────────────────────────────────────────────────────────────┐    │
│  │  Existing RPC namespace (getbalance, sendtoaddress, ...)       │    │
│  │  + NEW: eth_* JSON-RPC on second HTTP listener (port 8545)     │    │
│  └────────────────────────────────────────────────────────────────┘    │
└────────────────────────────────────────────────────────────────────────┘
```

### 4.2 Account Abstraction Layer (AAL)

Following the Qtum design pattern (the only widely-deployed production precedent for EVM-on-UTXO), three new opcodes and three new transaction types form the interface between the UTXO and account subsystems:

| Opcode | Value | Transaction type | Purpose |
|---|---|---|---|
| `OP_EVMCREATE` | `0xbd` | `TRANSACTION_EVM_DEPLOY = 11` | Deploy a new contract |
| `OP_EVMCALL` | `0xbe` | `TRANSACTION_EVM_CALL = 12` | Invoke an existing contract |
| `OP_EVMSPEND` | `0xbf` | `TRANSACTION_EVM_SPEND = 13` | Move RTM from an EVM account back into a UTXO |

Available script opcode slots after the last existing opcode `OP_ASSET_ID = 0xbc` are uncontested.

### 4.3 EVM execution model

EVM execution is performed by [evmone](https://github.com/ethereum/evmone) via the [EVMC](https://github.com/ethereum/evmc) ABI. A custom `evmc::Host` implementation (`src/evm/host.cpp`, Phase 2) wires the EVM state cache, account balances, masternode list, LLMQ signing, and ChainLock state into the EVM environment.

EVM execution **never runs synchronously inside `cs_main`** (see [§6 D1](#d1-evm-execution-model-and-cs_main-discipline)). Execution happens in a `CCheckQueue`-style worker pool; only the state merge is serialized under `cs_main`.

### 4.4 State trie and consensus over EVM state

EVM state is stored in a Merkle-Patricia trie under `evmstate/` (new datadir subdirectory). The trie root is committed to the block header (`stateRoot` field — see D2), making consensus over EVM state explicit and detectable across nodes.

### 4.5 Fee market

EIP-1559 base fee + tip model, with the base fee **burned** (deflationary pressure linked to network usage) and tips paid to miners. Calibration must be adjusted for Raptoreum's ~2-minute block time and higher variance vs. Ethereum's 12-second slots (see [§8.5](#85-eip-1559-calibration-for-pow-block-times)).

### 4.6 ChainID

Proposed (subject to chainlist.org registration):

| Network | ChainID | Hex |
|---|---|---|
| Mainnet | 7373 | `0x1ccd` |
| Testnet | 7374 | `0x1cce` |
| Regtest | 7375 | `0x1ccf` |
| Devnet | 7376 | `0x1cd0` |

EIP-155 replay protection mandatory across all networks.

---

## 5. The four RTM-native precompiles

These are the **strategic differentiation** vs. every other EVM L1. No other production EVM chain exposes this set natively to Solidity contracts.

### 5.1 Precompile `0x...0a01` — Smart Assets as ERC-20

A Solidity contract address deterministically derived from the Smart Asset's `assetId` exposes the standard ERC-20 interface:

```solidity
interface IRtmAsset {
    function name() external view returns (string memory);
    function symbol() external view returns (string memory);
    function decimals() external view returns (uint8);
    function totalSupply() external view returns (uint256);
    function balanceOf(address owner) external view returns (uint256);
    function transfer(address to, uint256 value) external returns (bool);
    function transferFrom(address from, address to, uint256 value) external returns (bool);
    function approve(address spender, uint256 value) external returns (bool);
    function allowance(address owner, address spender) external view returns (uint256);
    event Transfer(address indexed from, address indexed to, uint256 value);
    event Approval(address indexed owner, address indexed spender, uint256 value);
}
```

The address derivation reserves a high-bit prefix (e.g., `0xA55E70...`) so as to be deploy-collision-free. `balanceOf` reads from `CAssetsCache::mapAssetAddressAmount`. `transfer`/`transferFrom` mutate the asset cache through a deferred-effects queue applied at the end of the EVM transaction batch.

**Implication.** Any Uniswap V2 fork can list `RTM/<asset>` pairs and any `<asset>/<asset>` pair *without wrapping*. Asset creation cost is ~5 RTM on the UTXO side; deploying a comparable ERC-20 on Ethereum mainnet ranges $50–$500. This is the primary economic differentiator for RWA tokenization use cases.

This precompile carries the highest engineering risk in the plan; see [D4](#d4-smart-assets-as-erc-20-state-model) and [§8.2](#82-smart-assets-evm-mirror-divergence).

### 5.2 Precompile `0x...0a02` — LLMQ threshold-signed oracle

```solidity
interface ILlmqOracle {
    function requestSignature(uint8 llmqType, bytes32 id, bytes32 msgHash)
        external returns (bytes32 requestId);

    function getSignature(uint8 llmqType, bytes32 id)
        external view returns (bool available, bytes memory signature);

    function verifySignature(
        uint8 llmqType, uint32 signedAtHeight,
        bytes32 id, bytes32 msgHash, bytes calldata signature
    ) external view returns (bool);
}
```

Operations route to:

- `requestSignature` → `quorumSigningManager->AsyncSignIfMember(...)` in `src/llmq/quorums_signing.h:258`.
- `getSignature` → `quorumSigningManager->GetRecoveredSigForId(...)` (line 267).
- `verifySignature` → `CSigningManager::VerifyRecoveredSig(...)` static, thread-safe (line 282).

The model is **request-then-poll**: a contract emits `requestSignature`, the LLMQ signs over the next ~3 seconds (for `LLMQ_50_60`) or up to ~10 minutes (for `LLMQ_400_85`), and subsequent contract calls retrieve the result via `getSignature`. Gas accounting for pending requests is a Phase 4 design item (A7 in the engineering review) and is the source of the most plausible DoS vector against the LLMQ infrastructure.

**Implication.** A first-class threshold oracle, signed by ~4000 masternodes, baked into the chain — no Chainlink dependency, no third-party data feed contracts. Suitable for verifiable price feeds, randomness, and cross-chain attestations.

### 5.3 Precompile `0x...0a03` — ChainLocks

```solidity
interface IChainLocks {
    function isChainLocked(uint32 height, bytes32 blockHash) external view returns (bool);
    function isTxInstantLocked(bytes32 txid) external view returns (bool);
    function latestChainLockedHeight() external view returns (uint32);
}
```

**Important.** The naive implementation reads from the LLMQ runtime managers (which are per-node, time-dependent state). This **breaks consensus determinism** — see [D3](#d3-llmqchainlock-determinism). The corrected design reads from a `chainLocksCommit` field newly added to the block header, making the precompile a deterministic function of finalized chain data.

**Implication.** dApps see ~2-second finality (or whatever block-header commitment lag is). Bridges, DEX settlement, and centralized exchange deposits can treat RTM-EVM swaps as final orders of magnitude faster than Ethereum.

### 5.4 Precompile `0x...0a04` — Deterministic masternode registry

```solidity
interface IMasternodeRegistry {
    struct Masternode {
        bytes32 proTxHash;
        uint32 ip;
        uint16 port;
        bytes pubKeyOperator;     // BLS 48 bytes
        address payoutAddress;
        uint64 collateralAmount;
        bool isBanned;
    }

    function getCount() external view returns (uint256);
    function getByIndex(uint256 index) external view returns (Masternode memory);
    function getByProTxHash(bytes32 proTxHash) external view returns (Masternode memory);
    function isMasternode(bytes32 proTxHash) external view returns (bool);
}
```

Routes to `deterministicMNManager->GetListAtChainTip()` in `src/evo/deterministicmns.h:676`, iterating the underlying `immer::map<uint256, CDeterministicMNCPtr>`. Read-only and deterministic at any given block height.

**Implication.** On-chain governance contracts can natively address the masternode set. Service payment contracts can verify masternode identity. Treasury voting can weight by collateral. Possible composability with existing Dash-derived governance.

---

## 6. Design decisions D1–D6 (for validation)

These six decisions were surfaced during a structured engineering review (`/plan-eng-review`). Each is identified as load-bearing — incorrect resolution of any of them would either compromise consensus correctness, mainnet adoption, or both. We are asking the core team to ratify or amend each before Phase 1 (consensus code) begins.

### D1 — EVM execution model and `cs_main` discipline

**Problem.** Bitcoin Core's `cs_main` is the most contended lock in the codebase. Holding it during EVM execution (a potentially multi-millisecond, gas-bounded operation) replicates Qtum's well-known throughput limitation — peers ban on heartbeat timeout, RPC clients hang, mempool stalls. The original plan implicitly accepted this.

**Decision.** **Worker-pool execution with serialized merge**, modeled on `src/checkqueue.h` (Bitcoin Core's existing parallel script-verification queue). EVM transactions are decoded and validated in parallel worker threads operating on a consistent state snapshot, then results are merged into `CEvmStateCache` under `cs_main` in a single fast pass.

**Reversibility.** Low — concurrency model is foundational. Reversing this decision later requires a re-design of the entire validation pipeline.

**Cost.** Adds ~3–4 engineer-weeks to Phase 2.

### D2 — `CBlockHeader` fields and activation model

**Problem.** Standard EVM consensus requires `stateRoot`, `receiptsRoot`, and `transactionsRoot` in the block header to detect state divergence and to support light-client / bridge Merkle proofs. The current `CBlockHeader` (inherited from Bitcoin) has none of these.

**Decision.** Add `stateRoot` (32B), `receiptsRoot` (32B), and `transactionsRoot` (32B) to `CBlockHeader`. Activate via **hard-fork at a coordinated block height** (style of Ethereum's London/Shanghai/Cancun), not BIP9 version-bits. Activation requires:

- ≥90 days advance notice to operators.
- Release candidate published ≥60 days before activation.
- ≥95% masternode signaling during the signaling window.
- 30-day grace period between LockedIn and Active.
- Old nodes must be retired before activation height.

**Reversibility.** Zero — the block header structure is consensus-frozen once shipped.

### D3 — LLMQ/ChainLock determinism in precompiles

**Problem.** The naive implementation of `isChainLocked` / `getSignature` / `isTxInstantLocked` reads from per-node runtime state. Two honest nodes can return different results for the same query based on network propagation timing, **breaking consensus**.

**Decision.** Add a `chainLocksCommit` field to `CBlockHeader` (32–128B) containing the set of ChainLocks and IsLocks the miner observed at block construction time. Precompile reads from this header field, not the runtime LLMQ managers. Block validation verifies the committed locks are valid BLS signatures over the claimed blocks/txs.

**Reversibility.** Zero — depends on D2 header changes. Must be decided together.

**Coordination with D2.** Both decisions land in the same hard-fork; the header gains four new fields (`stateRoot`, `receiptsRoot`, `transactionsRoot`, `chainLocksCommit`) in a single coordinated event.

### D4 — Smart Assets as ERC-20: state model

**Problem.** The Smart Assets ERC-20 precompile (`0x...0a01`) requires writes to Smart Assets state from within EVM execution. Three approaches were considered:

- **(A) Bidirectional mirror.** Smart Assets state is mirrored into the EVM trie with a reconciliation pass; precompile reads/writes go through the mirror.
- **(B) Read-only precompile.** Precompile exposes `balanceOf`/`totalSupply` only; transfers must happen via UTXO-side `TRANSACTION_MINT_ASSET`. Mutation absent.
- **(C) Wrap/unwrap.** Assets explicitly bridged into the EVM as wrapped ERC-20 tokens (wETH pattern).

**Decision.** **(A) Bidirectional mirror** — preserves the "Smart Assets as ERC-20 native" product story. Mandatory technical safeguards:

1. Two-pass `ConnectBlock`: UTXO + Smart Assets txs in pass 1, EVM execution in pass 2, reconciliation in pass 3.
2. Allowance mapping lives in **EVM storage trie** of the precompile contract, never extending `assetsdb`.
3. Reentrancy guards (per-asset mutex during EVM execution).
4. **Mandatory fuzz test T-mirror** (10,000-sequence randomized fuzzing of `(Smart Asset tx, EVM mutation)` pairs across multiple nodes, asserting state convergence).
5. **Documented fallback.** If T-mirror cannot reach a clean pass rate after reasonable engineering effort during Phase 4, the plan falls back to option (C) wrap/unwrap. This is acceptable scope reduction, not project failure.

**Reversibility.** Medium — the wrap/unwrap fallback is available, but switching after activation requires a hard fork.

**Risk classification.** **HIGH.** This is the single most consequential engineering decision in the plan. Detailed design is the gating deliverable of Phase 4.1.

### D5 — Transaction ordering in blocks (consensus rule)

**Problem.** In the current UTXO model, intra-block transaction ordering is consensus-irrelevant (transactions reference outpoints; they commute). With EVM, ordering is consensus-critical (sandwich attacks, MEV). The choice between fee-priority, commit-reveal (LLMQ-PBS), and random ordering is a consensus rule frozen at activation.

**Decision.** **Fee-priority ordering** for v1 (standard EVM-compatible behavior; `effective_gas_price` descending). Document an LLMQ-PBS commit-reveal upgrade path for v2 as a *future* hard fork.

**Rationale.** Fee-priority maximizes tooling compatibility (MEV-boost, gas estimators, mempool watchers all assume this model). It accepts MEV in v1 as a known cost. LLMQ-PBS is a competitive differentiator but adds 6–12 weeks of consensus-critical work; v1 ships sooner this way.

**Reversibility.** Medium — orderings can be changed by future hard fork, but tooling will calcify around the v1 model.

### D6 — EVM target hard fork

**Decision.** **Ethereum Cancun** (PUSH0, MCOPY, transient storage TLOAD/TSTORE), with EIP-4844 blob transactions explicitly excluded. A documented upgrade mechanism allows future activation of Prague and Osaka without further architectural changes to the EVM layer.

**Rationale.** Solidity 0.8.20+ emits PUSH0 by default. Older targets (London, Shanghai) require workarounds. Prague is not yet stabilized on Ethereum mainnet.

**Reversibility.** Forward-compatible upgrade via soft-fork activation height.

---

## 7. Phase roadmap and current status

### 7.1 Phase 0 details

Phase 0's gating criteria:

- `depends/packages/evmone.mk` builds cleanly inside the standard `depends/` system.
- `raptoreumd` compiles with the new `src/evm/smoke.{h,cpp}` translation unit and links `libevmone.a`.
- Three smoke unit tests pass (`empty_contract_succeeds`, `add_then_return_yields_nine`, `keccak256_empty_matches_known_constant`).
- A new RPC command `evm_executeReadOnly(bytecode_hex, calldata_hex, [gas_limit])` returns expected results against the empty world state.
- **T1 (non-negotiable):** the upstream [Ethereum tests/](https://github.com/ethereum/tests) suite passes at 100% against the embedded evmone.

Phase 0 deliberately makes **zero consensus changes**. It exists to validate that the technical premise (link evmone into raptoreumd, call it correctly, get standard EVM semantics) is sound before any consensus code is written.

---

## 8. Known problems and proposed solutions

### 8.1 Phase 0 validation failure modes

| Failure | Probability | Proposed response |
|---|---|---|
| `evmc::MockedHost` not present in bundled evmc of pinned evmone version | Medium | Bump evmone to v0.13+ which ships modern evmc helpers. |
| `EVMC_CANCUN` constant not declared in pinned evmone | Low | Bump evmone version; verify upstream Cancun support. |
| `FetchContent` of intx/evmc/ethash blocked by corporate firewall | Medium | Vendor evmc, intx, ethash as separate `depends/packages/*.mk` files. Already noted in TODOs as a Phase 1 follow-up. |
| evmone pkg-config / cmake-config not generated correctly | Low | Hard-code `-levmone` in `Makefile.am` already in place; not blocking. |
| C++ ABI mismatch between system stdlib and evmone build | Low | Force GCC ≥9 in container; CI explicitly tests GCC 9, 11, 13. |
| Smoke tests link but Ethereum tests/ suite < 100% | Low | Indicates broken integration; root-cause via evmone's own state-test-runner before proceeding to Phase 1. |
| evmone cannot be made deterministic across (Linux x86-64, Linux ARM64, macOS) | Low | Forces consideration of Camino A (sidechain) as fallback. |

If Phase 0 cannot reach all gating criteria within 4 weeks (2× the budget): **re-evaluate Camino C** before committing further capital. The sidechain alternative (Camino A) remains a viable fallback that preserves most of the work invested in Phase 0.

### 8.2 Smart Assets / EVM mirror divergence

The bidirectional mirror (D4) is the highest source of consensus-bug risk. Concrete safeguards:

- **Pre-Phase-4 design document** describing the reconciliation pass with explicit ordering rules.
- **Mandatory T-mirror fuzz test** with 10,000+ randomized sequences asserting cross-node state convergence.
- **Documented fallback** to wrap/unwrap if T-mirror reveals fundamental ordering pathologies.
- **Allowance mapping isolation** — all ERC-20 allowance state lives in EVM storage of the precompile, never in `assetsdb`. This bounds the cross-namespace coupling.

### 8.3 Line endings / Windows developer environment

**Observed problem.** When the repository is cloned on Windows with the default `core.autocrlf=true` configuration, ~1258 files are converted from LF to CRLF. The `depends/` system fails immediately because shell scripts like `depends/config.guess` and `depends/gen_id` have CRLF shebangs (`#!/usr/bin/env bash\r\n`), and Linux's `/usr/bin/env` interprets `bash\r` as the binary name.

**Proposed solution.** Add a top-level `.gitattributes` file enforcing LF for build-critical files, then commit so that every fresh clone receives correct line endings:

```gitattributes
# Build-critical files must be LF regardless of platform
*.sh           text eol=lf
*.mk           text eol=lf
*.am           text eol=lf
*.ac           text eol=lf
*.in           text eol=lf
*.m4           text eol=lf
Makefile*      text eol=lf
configure*     text eol=lf
depends/config.guess    text eol=lf
depends/config.sub      text eol=lf
depends/gen_id          text eol=lf
build-aux/*             text eol=lf
share/*.sh              text eol=lf
contrib/devtools/*.py   text eol=lf
contrib/auto_gdb/*.py   text eol=lf
contrib/debian/rules    text eol=lf

# C++ source: tolerant of CRLF but normalize on commit
*.cpp          text
*.h            text

# Binary files
*.png          binary
*.ico          binary
*.icns         binary
```

This is **recommended for inclusion in this PR** because every new contributor on Windows otherwise repeats the same hour of debugging.

### 8.4 Reorg state revert mechanism

Ethereum-style state tries do not trivially snapshot. The original plan's reference to "CBlockUndo extension" understates the design effort required. The proposed approach is **journal-style incremental undo**: every state mutation produces a reverse-diff entry that is appended to `CBlockUndo`. `DisconnectBlock` replays these in reverse.

Detailed undo design is a Phase 2.6 deliverable. Testing target: reorg up to 100 blocks deep must produce bit-identical state vs. replaying from a checkpoint before the reorg.

### 8.5 EIP-1559 calibration for PoW block times

EIP-1559's base fee adjustment formula (1/8 of `BASE_FEE_MAX_CHANGE_DENOMINATOR`) is calibrated for Ethereum's ~12-second slot times. Raptoreum's ~2-minute targets with PoW variance will cause base-fee oscillation under load.

**Proposed solution.** Recalibrate the adjustment denominator to approximately `1/64` so that per-block change is proportionally smaller, smoothing the fee market over Raptoreum's longer block intervals. Exact value derived empirically on testnet (Phase 2.4 deliverable).

### 8.6 State pruning and disk growth

Without state pruning, full nodes will exceed 1 TB of disk within 2–3 years of EVM activation under realistic load. Geth's flat-state representation + pruning is the modern reference design.

**Proposed approach.** State pruning is **not a Phase 1–2 deliverable.** It is deferred to Phase 7 (after Phase 6 testnet generates real growth data). However, the state trie schema must be designed in Phase 2 with pruning in mind — specifically, snapshot capability and history independence must be present in the on-disk format from day 1.

### 8.7 Async oracle gas semantics (LLMQ precompile)

A naive `requestSignature` implementation allows contracts to enqueue unbounded pending requests, DoS-attacking the LLMQ infrastructure. Phase 4.2 design includes:

- **Gas reservation pattern** — fixed escrow per pending request, refunded on signature retrieval, burned on timeout.
- **Timeout** — request expires after N blocks (proposed N=4032, ≈7 days at 2-minute blocks). Expired requests are gas-burned and removed from the pending set.
- **Per-block cap** — maximum number of new signature requests per block, similar to gas limit.

### 8.8 BIP44 derivation conflict

EVM addresses are derived from `m/44'/60'/...` (Ethereum SLIP44 coin type 60). RTM existing wallets use coin type `10226`. A user importing an Ethereum-compatible seed will get *different* EVM addresses on RTM than on Ethereum mainnet.

**Proposed solution.** Wallet derives EVM addresses under `m/44'/10226'/0'/1/i` — a subtree under RTM's coin type with chain index 1 (`0` being the existing UTXO addresses). Document clearly in user-facing wallet UI. Provide migration tool for users who want to import an existing Ethereum-derived seed.

### 8.9 MEV in v1 (fee-priority ordering)

D5 commits to fee-priority ordering, which permits standard MEV (sandwich attacks, frontrunning). Mitigations for v1:

- **Application-level slippage protection** — well-documented in developer guides; standard Uniswap V2/V3 patterns work as-is.
- **Public mempool RPC** — `eth_subscribe newPendingTransactions` so MEV searchers and protection services operate normally.
- **Roadmap for v2 LLMQ-PBS** — commit publicly to commit-reveal ordering as a future hard fork (likely 12–18 months post-mainnet).

### 8.10 Reproducible / Guix builds

Bitcoin Core's reproducible build chain uses Guix for byte-identical binaries across machines. Adding `evmone` (and its transitive deps via `FetchContent`) to the build breaks this property in Phase 0.

**Proposed solution.** Phase 1 includes:

- Separate `depends/packages/{evmc,intx,ethash}.mk` files with pinned hashes.
- Guix manifest update covering all new dependencies.
- CI job verifying byte-identical builds across two reference machines.

This is **not blocking Phase 0** (the spike accepts the build-once-online cost) but **is blocking Phase 6** (audits require reproducible builds).

---

## 9. Open questions for the core team

### Q-1 — Base-fee burn vs. treasury allocation

EIP-1559 base fees can be:

- (a) Fully burned (Ethereum model) — maximum deflationary pressure, simplest narrative.
- (b) Split 50/50 between burn and masternode treasury — economic incentive for operators.
- (c) Fully routed to a community-managed treasury — funds future development but weakens "deflation" claim.

**Asked of:** Foundation tokenomics, community.

### Q-2 — Public testnet branding

The proposed Phase 6 public testnet (`evm-testnet.raptoreum.com`) sits adjacent to existing infrastructure. Approval and DNS coordination needed.

**Asked of:** Foundation infrastructure.

### Q-3 — `.gitattributes` PR

Should the `.gitattributes` fix from §8.3 be merged independently of the EVM work? It benefits all Windows contributors immediately and has zero consensus impact.

**Asked of:** Core maintainers.

### Q-4 — `chainId` registration

ChainID 7373 (mainnet) is proposed. Should the Foundation initiate registration on chainlist.org now, or wait until Phase 6 testnet stabilizes?

**Asked of:** Foundation operations.

---

*Proposal prepared on `feat/evm-integration` branch, Raptoreum mainnet commit base `900794ea9`. Ready for review by the Raptoreum core team and Foundation. Comments and amendments welcome via the standard PR process.*
