# EVM Integration — Bank-Grade Findings (independently verified)

Status date: 2026-10-03
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

This is the "fix it before you turn it on" window. Public activation remains
blocked by the unresolved findings. An authorization change still needs explicit
core-team ratification under the serialization constraints in `QUESTIONS.md`;
the inactive public deployments are not permission to choose a new protocol.

## Severity summary

| ID | Severity | Title | Gating | Status |
|----|----------|-------|--------|--------|
| EVM-01 | **CRITICAL** | Consensus trusts an unauthenticated `senderHash`; no signature is verified in-block | regtest-only | Verified — authorization decision pending; correction not applied |
| EVM-02 | **HIGH** | Chain-id split between ingest/signing and execution | regtest+testnet | Corrected — shared consensus parameter and regression coverage |
| EVM-05 | **HIGH** | EVM verification/recovery paths omit state execution and undo | regtest-only | Integration gaps verified; correction not applied |
| EVM-06 | MEDIUM (by design) | `value` is `uint64_t`; values ≥ 2⁶⁴ wei are unrepresentable | regtest-only | Known/by-design — documented |
| DET-01 | UNVERIFIED | Cross-node determinism of consensus-state-reading precompiles (masternodes/chainlocks) not independently confirmed | Phase 4 | Open — needs dedicated review |
| FUP-X.2 | LOW | `--disable-wallet` build broke on `HELP_REQUIRING_PASSPHRASE` in EVM RPC help | build-only | **FIXED this session** |

---

## EVM-01 — CRITICAL — Consensus trusts an unauthenticated `senderHash`

### Claim

For CALL/DEPLOY, the Ethereum signature is verified **only** at RPC ingest and is
then **discarded**. The consensus object that is serialized into the block carries
a plain, unauthenticated `senderHash` (for DEPLOY/CALL) / `fromAddress` (for SPEND)
and no signature. No consensus code path re-derives or verifies the sender.
Therefore any party who can place a transaction in a block (or relay one into the
mempool) can **spend from, and act as, any EVM account**.

### Evidence — the complete chain, verified line by line

**1. The signature is dropped at ingest.** `eth_sendRawTransaction`
(`src/rpc/ethereum.cpp:1489-1534`) decodes the signed wire bytes, recovers the
sender, and builds the payload with `payload.senderHash = <recovered address>`.
It serializes **only** the payload into `mtx.vExtraPayload`
(`ethereum.cpp:1515-1517`, `:1532-1534`). The raw signed `wire` bytes are used
solely to compute the eth-hash for the cross-index (`ethereum.cpp:1550`) and are
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

### Authorization proposal (not ratified or applied)

The senderHash must become a **verified-derived** value, never trusted. The
existing Option A proposal below reuses the decode+recover path and the
EIP-155/EIP-1559 ECDSA implementation in `src/evm/signing.*` and
`src/evm/rawtx.*`. Its serialization change still needs explicit ratification;
reusing the signature scheme does not settle that separate constraint.

**Option A — carry the standard signed envelope, re-verify in `Check`:**
- Add one field to the DEPLOY/CALL payloads: the original signed Ethereum wire
  bytes (already computed at ingest as `wire`). Bump `EVM_TX_PAYLOAD_VERSION`.
- In `CheckEvmDeployTx`/`CheckEvmCallTx`, call the existing
  `evm::DecodeRawEthTx(payload.signedTx, ActiveEvmChainId(), decoded)` and assert
  `decoded.sender == payload.senderHash` **and** that the decoded fields
  (nonce/to/value/data/gas/fees) match the payload. Recovery + EIP-155 chain-id
  enforcement already exist in `rawtx.cpp:100-103,176-178`.
- Recovery must use the same network chain-id as ingest and execution. The
  bounded EVM-02 correction establishes that source of truth independently;
  it does not supply or approve the missing authorization proof.

**Outgoing EVM debits (SPEND / UNWRAP) require a separate authorization decision.**
These wallet-authored special transactions have no signed Ethereum envelope,
and claiming an EVM source address is not proof that the builder controls it.
Their authorization model needs explicit core-team ratification before coding.
The incoming FUND / WRAP paths are different: they spend/burn UTXO inputs whose
ordinary script authorization remains authoritative, and name an EVM recipient.
They must not be described as unauthenticated debits of that recipient account.

**Why this is not applied autonomously:** it changes consensus serialization and
introduces in-consensus signature verification. Per the core-team contact's
stated constraints (keep EVM in the "no weird sigs / no invasive serialization"
box) this is a decision the team must ratify, not a change to land unilaterally.
The options above remain proposals; the draft does not select or implement a
new authorization format.

---

## EVM-02 — HIGH — Chain-id split between signing and execution (corrected)

### Original defect and reproduction

The RPC mapper used 7373 on mainnet, 7374 on testnet and 7375 on regtest, while
mining's `ComputeCoinbaseEvmCommitment` and validation's `ConnectBlock` hardcoded
7373. On regtest, a mined and connected contract therefore saw 7373 from
`CHAINID` even though `eth_chainId`, `eth_call` and signed-transaction ingest used
7375. Testnet execution would similarly disagree with its RPC id if activated.

The new `evm_d2_consensus_tests/chainid_agrees_between_rpc_mining_and_validation`
case reproduced the original defect: the block connected and both RPC checks
passed, but its persistent CHAINID storage failed the expected 7375 assertion.
It uses the production mining commitment helper and block-validation path,
rather than an execution context assembled by the test.

### Applied correction

`Consensus::Params::evmChainId` is the shared source for
`ActiveEvmChainId()` (`src/rpc/ethereum.cpp`), mining
(`src/evm/connectblock.cpp`) and validation (`src/validation.cpp`).
`src/chainparams.cpp` selects 7374 for testnet and 7375 for regtest; the default
7373 preserves mainnet and the existing devnet fallback. The
`chainid_network_parameters` regression pins the three network values.

This bounded correction changes CHAINID execution on regtest and on testnet if
activated; mainnet's value is unchanged. It neither adds an authorization proof
nor changes payload serialization or the activation gates. EVM-01 remains a
separate blocking decision before any shared-network activation.
The execution regression uses a fresh regtest fixture; it does not establish
compatibility with historical regtest EVM blocks or state databases.

---

## EVM-05 — HIGH — EVM verification and recovery omit state handling

### Confirmed implementation gaps

`DisconnectBlock` accepts an EVM cache but explicitly ignores it
(`src/validation.cpp:1669-1679`). The persistent EVM undo is applied only in
`DisconnectTip` (`:3310-3322`), after the ordinary UTXO/asset disconnect.

`VerifyDB` performs memory-only disconnects with a temporary UTXO view and asset
cache (`:5289-5297`, `:5343`). Its level-4 reconnect calls `ConnectBlock` without
an EVM cache (`:5384`); the optional cache defaults to null
(`src/validation.h:772`), and the execution/commitment checks at
`src/validation.cpp:2582` are therefore skipped. This path neither rolls back
nor re-executes EVM state. The previous description of VerifyDB double-debiting
EVM balances was not supported by this call path and must not be used as an
established reproduction.

Crash recovery similarly disconnects via `DisconnectBlock` (`:5474`) and calls
`RollforwardBlock` (`:5494`). The latter applies ordinary special-transaction and
UTXO/asset processing (`:5408`) but does not invoke the EVM block-execution
pipeline. The EVM consistency consequences need an end-to-end crash/replay test;
the missing integration is confirmed by inspection, not by a new daemon run.

### Required correction and validation

Verification must use an isolated EVM state view that can apply the existing
undo and replay the selected blocks without flushing persistent state, deleting
undo records or writing receipts. Successful verification and every early-exit
path must leave persistent accounts, storage, code, receipts and undo unchanged.

Recovery needs explicit EVM rollback/rollforward integration and a regression
that reconstructs the expected state after interrupted persistence. Keep the
working live-reorg path covered separately.

Do not simply move `ApplyUndoToDB` into `DisconnectBlock`: that helper writes the
persistent database (`src/evm/undo.cpp:79`), while VerifyDB's disconnect is
intentionally memory-only. This work changes state lifecycle handling, not the
transaction authorization envelope, but requires dedicated tests before it can
be considered fixed.

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

1. **Ratify and implement the authorization model (EVM-01).** EVM-02 now supplies
   the shared chain-id; it does not authorize a sender or settle the protocol
   decision. Review CALL/DEPLOY and outgoing SPEND/UNWRAP under the existing
   constraints before implementation.
2. **Integrate isolated verification and crash recovery (EVM-05).** Independent of 1; requires state-lifecycle tests before completion.
3. **Decide value width (EVM-06)** before mainnet, not before testnet.
4. **Dedicated determinism review (DET-01)** of the state-reading precompiles.
5. Only after 1–2 land and are tested: schedule the `EVM` deployment on testnet
   per `docs/evm/TESTNET-ACTIVATION.md`.

None of 1–4 is urgent in the sense of "live risk today" — all EVM paths are
inert on mainnet/testnet. All of 1–2 are non-negotiable before the gate opens
anywhere shared.
