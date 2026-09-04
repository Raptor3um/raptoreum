# EVM Integration — Bank-Grade Findings (independently verified)

Status date: 2026-07-04
Branch: `feat/evm-integration`
Author: JSanchezFDZ (Unknown Gravity, external contributor)
Audience: Raptoreum core team (internal review)

## How to read this document

Every finding below was verified by **reading the actual code path end to end**,
not by static heuristics or a test that happened to fail. Each one cites the
exact `file:line` evidence so a reviewer can reproduce the conclusion in minutes.

### The single most important caveat: nothing here is live-exploitable today

All EVM consensus code is gated behind `IsEvmActive(pindexPrev)` (and, for the
header commitment, `IsEvmCommitActive`). The `EVM` / `EVM_COMMIT` deployments are
**force-active at height 0 on regtest ONLY** and are **unregistered on mainnet and
testnet**. On any shared network today, every EVM-typed transaction is rejected at
`CheckEvmCommon` with `evm-not-activated` (see `src/evm/evmtx.cpp:189`).

Therefore:

- **None of these findings is a live-network vulnerability right now.**
- **Every CRITICAL/HIGH finding is a hard blocker that MUST be fixed before the
  `EVM` deployment is scheduled to activate on _any_ shared network — testnet
  included.** The moment the gate opens on a network where a non-cooperating node
  exists, EVM-01 becomes theft-of-funds.

This is exactly the "fix it before you turn it on" window. It is the cheapest
possible time to fix EVM-01 and EVM-02: because no EVM transaction has ever been
committed on mainnet or testnet, changing the EVM payload serialization has **zero
backward-compatibility cost** — there is nothing to fork away from.

## Severity summary

| ID | Severity | Title | Gating | Status |
|----|----------|-------|--------|--------|
| EVM-01 | **CRITICAL** | Consensus trusts an unauthenticated `senderHash`; no signature is verified in-block | regtest-only | Verified — fix designed, **not** applied (needs core-team decision) |
| EVM-02 | **HIGH** | Chain-id split: ingest/signing uses 7375/7374, execution hardcodes 7373 | regtest+testnet | Verified — fix designed, not applied |
| EVM-05 | **HIGH** | EVM state is not rolled back on the `VerifyDB` / `RollbackBlock` disconnect paths | regtest-only | Verified — fix designed, not applied |
| EVM-06 | MEDIUM (by design) | `value` is `uint64_t`; values ≥ 2⁶⁴ wei are unrepresentable | regtest-only | Known/by-design — documented |
| DET-01 | UNVERIFIED | Cross-node determinism of consensus-state-reading precompiles (masternodes/chainlocks) not independently confirmed | Phase 4 | Open — needs dedicated review |
| FUP-X.2 | LOW | `--disable-wallet` build broke on `HELP_REQUIRING_PASSPHRASE` in EVM RPC help | build-only | **FIXED this session** |

---

## EVM-01 — CRITICAL — Consensus trusts an unauthenticated `senderHash`

### Claim

An EVM transaction's Ethereum signature is verified **only** at RPC ingest and is
then **discarded**. The consensus object that is serialized into the block carries
a plain, unauthenticated `senderHash` (for DEPLOY/CALL) / `fromAddress` (for SPEND)
and no signature. No consensus code path re-derives or verifies the sender.
Therefore any party who can place a transaction in a block (or relay one into the
mempool) can **spend from, and act as, any EVM account**.

### Evidence — the complete chain, verified line by line

**1. The signature is dropped at ingest.** `eth_sendRawTransaction`
(`src/rpc/ethereum.cpp:1504-1550`) decodes the signed wire bytes, recovers the
sender, and builds the payload with `payload.senderHash = <recovered address>`.
It serializes **only** the payload into `mtx.vExtraPayload`
(`ethereum.cpp:1530-1532`, `:1547-1549`). The raw signed `wire` bytes are used
solely to compute the eth-hash for the cross-index (`ethereum.cpp:1565`) and are
then **not stored anywhere in the consensus transaction**.

**2. The consensus payload has no signature field.** In `src/evm/evmtx.h`:
- `CEvmDeployTx::SERIALIZE_METHODS` serializes
  `nVersion, code, gasLimit, maxFeePerGas, maxPriorityFeePerGas, senderHash, nonce`
  — **no r/s/v**.
- `CEvmCallTx::SERIALIZE_METHODS` serializes
  `nVersion, toAddress, value, data, gasLimit, maxFeePerGas, maxPriorityFeePerGas, senderHash, nonce`
  — **no r/s/v**.
- `CEvmSpendTx::SERIALIZE_METHODS` serializes
  `nVersion, fromAddress, amount, outputScript, gasLimit, ...` — **no r/s/v**.

**3. Block validation does not verify the sender.** The consensus `Check*`
functions (`src/evm/evmtx.cpp`) validate only structure:
- `CheckEvmDeployTx` (`:206-226`) → `CheckEvmCommon` + code size.
- `CheckEvmCallTx` (`:228-245`) → `CheckEvmCommon` + calldata size.
- `CheckEvmSpendTx` (`:247-267`) → `CheckEvmCommon` + amount>0 + non-empty script.
- `CheckEvmCommon` (`:182-202`) checks activation, version, `gasLimit != 0`,
  `maxPriorityFeePerGas <= maxFeePerGas`. **No signature recovery. No
  authorization of `senderHash`/`fromAddress`.**

**4. Apply debits the claimed account with no authorization.**
`PreFlightCallOrDeploy` (`src/evm/process.cpp:130-169`) loads the account at
`EvmAddrFromUint256(senderHash)`, checks only `nonce == payloadNonce` and
`balance >= upfrontCost`, then **debits the balance and increments the nonce**.
`ProcessEvmCallTx` (`:247`), `ProcessEvmDeployTx` (`:309`), and `ProcessEvmSpendTx`
(`:368`, using `payload.fromAddress`) all feed the payload-supplied address in as
the authority. The nonce provides only replay protection, not authorization — an
attacker simply reads the victim's current nonce.

### Exploit scenario (once the gate is open on a shared network)

1. Attacker reads the victim's EVM nonce (`eth_getTransactionCount`).
2. Attacker hand-builds a `TRANSACTION_EVM_SPEND` with `fromAddress = victim`,
   `amount = victim's balance`, `outputScript = attacker's address`, and the
   victim's nonce. No signature is needed — none is checked.
3. The tx passes `CheckEvmSpendTx` (structure-only) at mempool relay and block
   validation.
4. At `ConnectBlock`, `ProcessEvmSpendTx` debits the victim's EVM balance and the
   block mints a UTXO output to the attacker's script.
5. Result: **direct, unauthenticated extraction of any EVM account's balance into
   a UTXO the attacker controls.** The CALL variant additionally lets the attacker
   execute arbitrary calls _as_ the victim (drain ERC-20 mirror balances, etc.).

### Fix design (recommended, not yet applied)

The senderHash must become a **verified-derived** value, never trusted. Two
coherent options; recommend Option A because it reuses the existing, tested
decode+recover path and introduces **no new signature scheme** (directly
addressing the core team's "dual consensus, no weird sigs" constraint — this is
bog-standard EIP-155/EIP-1559 ECDSA that already lives in `src/evm/signing.*`
and `src/evm/rawtx.*`).

**Option A — carry the standard signed envelope, re-verify in `Check`:**
- Add one field to the DEPLOY/CALL payloads: the original signed Ethereum wire
  bytes (already computed at ingest as `wire`). Bump `EVM_TX_PAYLOAD_VERSION`.
- In `CheckEvmDeployTx`/`CheckEvmCallTx`, call the existing
  `evm::DecodeRawEthTx(payload.signedTx, ActiveEvmChainId(), decoded)` and assert
  `decoded.sender == payload.senderHash` **and** that the decoded fields
  (nonce/to/value/data/gas/fees) match the payload. Recovery + EIP-155 chain-id
  enforcement already exist in `rawtx.cpp:100-103,176-178`.
- **Crucial interaction with EVM-02:** recovery must use the chain-id the tx was
  _signed_ with (`ActiveEvmChainId()`), which is why EVM-02 must be fixed in the
  same batch — otherwise recovery on regtest/testnet uses the wrong id.

**SPEND / FUND / WRAP / UNWRAP are a separate, open design question.** These are
**not** created from an Ethereum transaction (they are wallet-authored special
txs), so they have no eth signature to carry. Today they are authorized only by
the wallet that builds them — there is **no consensus-level proof** that the
builder controls `fromAddress`. They need their own authorization model, e.g. an
secp256k1 signature by the EVM account key over the payload, verified in `Check`.
This is the part that genuinely needs a core-team design decision before coding,
because it defines a new (small, standard-ECDSA) authorization envelope for the
bridge transactions.

**Why this is not applied autonomously:** it changes consensus serialization and
introduces in-consensus signature verification. Per the core-team contact's
stated constraints (keep EVM in the "no weird sigs / no invasive serialization"
box) this is a decision the team must ratify, not a change to land unilaterally.
The design above is ready to implement on green-light.

---

## EVM-02 — HIGH — Chain-id split between signing and execution

### Claim

The chain-id used to validate a transaction's EIP-155 signature at ingest differs
from the chain-id the EVM reports at execution (`CHAINID` opcode / `block.chainid`)
on every non-mainnet network.

### Evidence

- Ingest/signing id — `ActiveEvmChainId()` (`src/rpc/ethereum.cpp:311-318`):
  `main → 7373`, `test → 7374`, `regtest → 7375`. `eth_sendRawTransaction` decodes
  with this id (`ethereum.cpp:1505-1507`); a mismatch is rejected in
  `rawtx.cpp:102,177`.
- Execution id — **hardcoded** `7373`:
  - `src/evm/connectblock.cpp:263` → `mctx.chainId = 7373;`
  - `src/validation.cpp:2597` → `evmCtx.chainId = 7373; // TODO: parameterize`

So on **regtest** a user must sign for `7375` (or ingest rejects the tx), but the
executing context reports `7373`; on **testnet**, sign `7374` / execute `7373`.
Only mainnet is internally consistent (`7373 == 7373`).

Note the unit tests already build their execution context with the regtest id
(`c.chainId = 7375` in `evm_connectblock_tests.cpp:48`, `evm_process_tests.cpp:43`,
etc.), so production (`connectblock.cpp`) and the tests **disagree**; the tests
don't catch it because they never exercise the hardcoded production path.

### Impact

- Any contract that reads `block.chainid` (replay guards, EIP-712 domain
  separators, cross-chain checks) sees the wrong network id on regtest/testnet.
- Directly blocks the EVM-01 Option A fix (signature recovery must use the signing
  id, not the execution id).

### Fix design

Implement FUP-1: add `evmChainId` to `Consensus::Params`, set it per network
(7373/7374/7375), and read it in `connectblock.cpp:263`, `validation.cpp:2597`,
**and** `ActiveEvmChainId()` so a single source of truth feeds signing, ingest,
and execution. Consensus-affecting on regtest/testnet only; mainnet value is
unchanged. Land it together with EVM-01.

---

## EVM-05 — HIGH — EVM state not undone on VerifyDB / RollbackBlock

### Claim

EVM state changes are rolled back **only** on the live-reorg path
(`DisconnectTip`). The `VerifyDB` and `RollbackBlock` code paths call
`DisconnectBlock` directly, which reverts UTXO and asset state but **not** EVM
state — so a `-checkblocks`-style verify (or a rollback) over a range that
contains EVM transactions leaves the EVM state DB inconsistent.

### Evidence

- `DisconnectTip` applies the EVM undo: reads the undo stream and calls
  `evm::ApplyUndoToDB(evmUndo, *pevmstatedb)` (`src/validation.cpp:3316-3319`),
  paired with `BuildUndoFromCache` on connect (`:3495-3503`).
- `DisconnectBlock` itself (`:1669-1984`) reverts only UTXO + asset undo; it does
  **not** touch EVM state.
- `VerifyDB` disconnects via `DisconnectBlock` directly
  (`src/validation.cpp:5344`) — **no** `ApplyUndoToDB`.
- `RollbackBlock` likewise (`:5475`) — **no** `ApplyUndoToDB`.

At `nCheckLevel >= 3`, `VerifyDB` disconnects then rolls forward
(`RollforwardBlock → ProcessSpecialTxsInBlock`, `:5409`), which **re-applies** the
EVM txs on top of state that was never rolled back → double-debit / nonce skew →
the recomputed `evmStateRoot` diverges from the committed one
(`:2679-2697`), spuriously failing verification, or (worse, if run without the
commitment check active) silently corrupting the EVM state DB.

### Fix design

Move the EVM undo application out of `DisconnectTip` and into `DisconnectBlock`
(or a shared helper both call), so every disconnect path — live reorg, `VerifyDB`,
`RollbackBlock` — restores EVM state symmetrically with UTXO/asset state. Add a
regression test that runs `verifychain 4 <depth>` over a regtest chain containing
EVM txs and asserts a clean result.

---

## EVM-06 — MEDIUM (by design) — `value` is `uint64_t`

`CEvmCallTx.value` / the EVM value plumbing are `uint64_t`. Values ≥ 2⁶⁴ wei
(~18.45 RTM-equivalent at 1e18 wei) are unrepresentable and such a transaction
cannot be constructed through the native path. This is an accepted design bound
(documented in the T1 test analysis: `callWithHighValue` is the single Cancun
vector that fails for this reason). Flagged here for completeness so the core team
can decide whether the native value type should widen to `uint256` before any
mainnet activation; it is not a safety bug.

---

## DET-01 — UNVERIFIED — Determinism of consensus-state-reading precompiles

The Phase-4 precompiles that read live consensus state
(`precompile_masternodes.cpp`, `precompile_chainlocks.cpp`,
`precompile_llmq_oracle.cpp`) must return **byte-identical** results on every node
for a given block, or the EVM state root diverges. This was **not** independently
verified in this pass (the automated determinism check in the audit returned
unusable output). It is called out honestly as an open item rather than assumed
safe. Recommend a dedicated review: confirm each such precompile reads only state
that is fixed as of `pindexPrev` and never node-local/wall-clock/mempool data.

---

## Fixed this session

### FUP-X.2 — `--disable-wallet` build

`evm_fund` / `wrap_asset` / `unwrap_asset` referenced `HELP_REQUIRING_PASSPHRASE`
(defined in `wallet/rpcwallet.h`, included only under `ENABLE_WALLET`) inside their
`RPCHelpMan` help blocks, which are compiled unconditionally — breaking a
`--disable-wallet` build with "identifier not declared". Fixed by inlining a
wallet-independent copy of the literal (`EVM_HELP_REQUIRING_PASSPHRASE` in
`src/rpc/rpcevo.cpp`); the wallet-gated function bodies already throw
`RPC_METHOD_NOT_FOUND` under `--disable-wallet`, so wallet-less nodes
(mining/relay) now compile and expose the methods as unavailable.

---

## Recommended sequencing

1. **Ratify the authorization model (EVM-01 + EVM-02 together).** These are the
   blockers. Both touch consensus; land them as one reviewed batch. EVM-02 is a
   prerequisite for EVM-01's signature recovery.
2. **Symmetrize EVM undo (EVM-05).** Independent of 1; can land in parallel.
3. **Decide value width (EVM-06)** before mainnet, not before testnet.
4. **Dedicated determinism review (DET-01)** of the state-reading precompiles.
5. Only after 1–2 land and are tested: schedule the `EVM` deployment on testnet
   per `docs/evm/TESTNET-ACTIVATION.md`.

None of 1–4 is urgent in the sense of "live risk today" — all EVM paths are
inert on mainnet/testnet. All of 1–2 are non-negotiable before the gate opens
anywhere shared.
