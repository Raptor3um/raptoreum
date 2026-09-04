# Open questions for the core team

Decisions or clarifications still owed by the core team. Each
question references the design context, the current default, and
the work it blocks.

The three original `Q-A*` questions from the
[`PROPOSAL-FOR-CORE-TEAM.md`](PROPOSAL-FOR-CORE-TEAM.md) are
restated here together with new ones surfaced during Phases 2–5.

> **2026-06-10 update.** Each question now carries a **Proposed
> answer** so the core team can ratify (👍 on the PR thread is
> enough) or amend, instead of designing from scratch. Several were
> effectively answered *by the implementation* since the questions
> were written — those are marked **answered-by-implementation** and
> only need ratification. Review process note: per the project
> decision, security review is performed by the Raptoreum core team
> (no external audit firm will be engaged); the review entry point is
> [`REVIEW-GUIDE.md`](REVIEW-GUIDE.md).

---

## Q-A1 — "The AI thing"

**Context.** Surfaced during the original proposal review. The core
team mentioned an "AI thing" without specifying what they were
referring to. We need a precise problem statement before we can
take a position.

**Currently blocking.** Nothing critical, but the question deserves
a clear answer in writing.

**Want from core team.** What system / feature / external dependency
are they referring to?

**RESOLVED (out-of-band, 2026-05).** The core team clarified that
"AI" was a typo — the subject is the **EVM itself**, and the real
requirement behind the remark is that the EVM integration must stay
**within the existing dual-consensus model and signature scheme**
("no weird sigs") and avoid invasive serialization changes. The
shipped design satisfies all three:

- EVM transactions ride **standard special-tx wrappers** validated by
  the existing `CheckSpecialTx` machinery — no new consensus lane.
- The only curve anywhere is **secp256k1** (EVM sender recovery uses
  the same curve RTM already runs); no new signature schemes touch
  the protocol layer. If the protocol later migrates its own
  signatures (e.g. post-quantum), the wrapper design decouples that
  from the EVM layer entirely.
- **No existing type's serialization changed**: the new payloads are
  additive special-tx types, and the coinbase commitment is a
  versioned, additive `CCbTx` v3.

We consider this closed unless the core team raises a specific
residual.

---

## Q-A2 — Wrap / unwrap (tx types 17/18)

**Context.** The plan reserved transaction types 17 and 18 for
wrapping a Smart Asset into an EVM-side balance (and back). D4-
revised left two viable paths:

- **v1 — strict** wrap/unwrap with an opt-in, explicit user action
  per asset transfer between the UTXO and EVM sides.
- **v2 — implicit** wrap-on-touch where balances cross the bridge
  on first use of a Smart Asset inside an EVM contract.

**Currently blocking.** ~~Phase 4.1 ERC-20 precompile semantics; the
wallet HD-path decision.~~ No longer blocking — see below.

**Want from core team.** Pick v1 or v2 for the initial mainnet.

**Proposed answer (answered-by-implementation — ratify).** **v1
(strict)** is implemented, tested, and proven e2e: explicit
`TRANSACTION_WRAP_ASSET`/`UNWRAP_ASSET` with a constrained
conservation binding, wallet RPCs, and the full EVM-side ERC-20
surface (`transfer`/`approve`/`transferFrom` live on the wrapped
ledger). Design-of-record: [`SMART-ASSET-MIRROR.md`](SMART-ASSET-MIRROR.md).
We recommend ratifying v1 for initial mainnet: its consensus surface
is small and auditable (one exemption + one constraint), whereas v2's
wrap-on-touch would put bridge mutations inside EVM execution
semantics. v2 can be revisited post-launch as UX sugar built ON TOP
of v1 (a wallet/dApp auto-wrap flow needs no new consensus).

The core team's out-of-band requirement that some assets must be able
to remain **fully outside the EVM** ("a second asset class…", "leave
the door open to some not being EVMable") is satisfied by v1's
default: an asset never touches the EVM unless a holder explicitly
wraps units. We additionally propose hardening that into a
consensus-level guarantee for issuers of regulated/sensitive assets:
a **per-asset EVM opt-out flag** in the asset metadata — when set,
`CheckWrapAssetTx` rejects any wrap of that asset, so it can never
acquire Solidity-risk exposure regardless of what holders do. Small
change inside the already-reviewed wrap surface; needs core-team
ratification of the metadata-field addition (serialization
versioning). Tracked as **FUP-4.10**.

---

## Q-A3 — EVM Asset fee model

**Context.** Creating a Smart Asset today costs ~5 RTM (asset
issuance fee). EVM tokens (vanilla ERC-20 deployed inside the EVM
state) have no asset-issuance equivalent — just normal contract-
creation gas. The question: do we treat an EVM-deployed ERC-20 as
a "Smart Asset" for fee purposes (charge the 5-RTM equivalent in
the deploy gas), or leave EVM tokens fee-equivalent to vanilla
Ethereum?

**Currently blocking.** Nothing strictly.

**Want from core team.** Confirm "no surcharge for EVM-deployed
ERC-20s" is fine, or specify the surcharge mechanism.

**Proposed answer (ratify the default).** **No surcharge.** A
vanilla ERC-20 inside the EVM is not a Smart Asset — it gets none of
the native benefits (no UTXO-side existence, no native precompile
mirror, no wallet/asset-index integration). Charging extra would
only push generic-token deployers to other EVM chains while not
protecting anything: the Smart-Asset value proposition (native +
mirrored, ~$0.10 issuance vs $50–500 for a bare ERC-20 elsewhere)
already differentiates on merit. Revisit with usage data if
EVM-token spam becomes a real resource problem (the gas market
already prices state growth).

---

## Q-A4 — HD derivation path for EVM keys (new — Phase 5)

**Context.** Surfaced during the Phase 5 wallet signing work. The
wallet currently manages secp256k1 keys under
`m/44'/10226'/0'/0/i` (RTM coin_type). EVM signatures can use the
same private key (secp256k1 is the same curve) but the address
derivation is different. We have two options:

- **Path A — Ethereum-standard `m/44'/60'/0'/0/i`.** Same path
  every Ethereum-aware HD wallet (MetaMask, Trezor, Ledger) uses.
  Means a single recovery seed produces the SAME address on RTM
  and on Ethereum L1 — useful for cross-chain identity, dangerous
  if the user assumes the funds are isolated. Backwards-compatible
  with hardware-wallet flows.
- **Path B — RTM subtree `m/44'/10226'/0'/1/i`** (note `/1/` instead
  of `/0/` for the change-from-receiving distinction). Isolates EVM
  keys under the RTM coin type. Cross-wallet UX harder (Ethereum
  wallets don't know about the path).

**Currently blocking.** Phase 5.4 — wallet-keystore HD
integration. The signing primitives (Phase 5.1–5.3) work either
way; only the key-derivation pipeline depends on this.

**Want from core team.** Pick A or B.

**Proposed answer.** **Path A (`m/44'/60'/0'/0/i`).** The decisive
argument is recoverability: a user's seed phrase imported into ANY
Ethereum wallet (MetaMask, Ledger, Trezor) must find their RTM-EVM
funds — with Path B it silently wouldn't, which is a foreseeable
funds-loss support disaster. Cross-chain address reuse is standard
across the EVM ecosystem and widely treated as a feature. Mitigate
the confusion risk in the wallet UI (label the EVM account "RTM EVM
— chainId 7373"). The signing primitives are path-agnostic, so this
can be ratified independently of the PR review.

---

## Q-A5 — Activation height per network (new — Phase 2.4e)

**Context.** Regtest activates UPDATE_EVM at height 0 today
(forced via `heightActivated=0` in chainparams). Mainnet and
testnet have no activation height registered — meaning EVM is
inert on those networks until a coordinated hard fork.

**Currently blocking.** Phase 6 testnet pública requires a testnet
activation height; mainnet activation needs a comm-window plan.

**Want from core team.** Target testnet + mainnet activation
parameters and a signaling rule.

**Proposed answer.** Full concrete proposal (mechanism, bits,
heights formula, rollout + post-activation smoke checklist, draft
chainparams patch) now lives in
[`TESTNET-ACTIVATION.md`](TESTNET-ACTIVATION.md). Summary: testnet
activates **both** `EVM` and `EVM_COMMIT` at one **forced height**
(current tip + ~30 days; deterministic, no vote-stall risk on a
low-participation net), bits 3/4; mainnet activates later via the
standard RIP miner+smartnode vote after a 90-day comm window, with
parameters mirrored from the testnet outcome.

---

## Q-A6 — D2 header field layout (new — Phase 2.4 / FUP-2.1)

**Context.** Decision D2 said: add `stateRoot`, `receiptsRoot`,
`transactionsRoot`, plus a fourth `chainLocksCommit` field (per
D3) to the block header — 4 × 32-byte words = 128 extra bytes per
header.

**Currently blocking.** ~~FUP-2.1/2.2/2.3 — all three gate
mainnet.~~ No longer blocking — see below.

**Want from core team.** Confirm layout / ordering / activation
mechanism.

**Proposed answer (answered-by-implementation — ratify the
revision).** The commitment was implemented in the **coinbase
special transaction (`CCbTx` v3)** instead of the 80-byte header:
`evmStateRoot`, `evmReceiptsRoot`, `evmBaseFee`, `evmGasUsed`,
`evmExecTime`, gated by `EUpdate::EVM_COMMIT`. Rationale:

- **Same security.** The CbTx is committed by the header's merkle
  root, so the EVM roots are exactly as immutable as a header field;
  every validator recomputes and enforces them in `ConnectBlock`.
- **No header break.** The 80-byte header is untouched — no impact
  on mining hardware, stratum, SPV parsers, or explorers.
- **Precedent.** This is the established Dash/Raptoreum pattern for
  committed roots (the CbTx already commits the MN-list and quorum
  merkle roots the same way).
- The originally-proposed `transactionsRoot` is redundant (the
  header merkle root already commits the txs) and `chainLocksCommit`
  remains served by the existing ChainLocks system; neither is
  carried in v3.

We ask the core team to ratify CCbTx-v3-commitment as the fulfilment
of D2's intent, closing FUP-2.1/2.2/2.3 (all three shipped on this
mechanism; base-fee dynamics and coinbase-tip verification are live
and regression-locked).

---

## Q-A7 — Gas-price floor for non-EVM-fee mempool admission (new — Phase 3.5)

**Context.** The Phase 3.5 consensus carve-out exempts EVM-typed
txs from `minRelayTxFee` because their fee is the EIP-1559 gas
debit (on the EVM side), not a UTXO miner fee. ~~With base-fee = 0
today~~ (base-fee dynamics are now live under `EVM_COMMIT`), a
zero-tip tx is still relayable at near-zero cost when the base fee
has decayed — a flooding DoS vector.

**Currently blocking.** Mainnet readiness.

**Want from core team.** Confirm a mempool-side minimum effective
gas price for EVM txs.

**Proposed answer.** Confirm: **mempool admission requires
`effectiveGasPrice ≥ 1 gwei`** (i.e. `min(maxFeePerGas, baseFee +
maxPriorityFeePerGas)` at the current tip) for EVM-typed txs, as a
policy (not consensus) rule — symmetric with `minRelayTxFee` on the
UTXO side and matching what `eth_gasPrice`/`eth_maxPriorityFeePerGas`
already advertise (next-block base fee + 1-gwei tip). Policy-only
means miners can still include lower-priced txs they mined
themselves, and the constant can be tuned without a fork. We'll
implement it as a follow-up once ratified (small, test-first,
mempool-only change).

---

## Q-A8 — `LLMQ` async requestSignature gas / timeout (new — Phase 4.2)

**Context.** The LLMQ Oracle precompile exposes the sync surface
(`hasSignature`, `getSignature`, `verifySignature`) but
deliberately does NOT expose `requestSignature`. Adding
requestSignature means a contract can trigger an async LLMQ
signing session. Two open design points:

- **Gas escrow.** The caller should pre-pay enough gas for an
  eventual `getSignature` callback. Otherwise an attacker spams
  signing sessions for free, exhausting LLMQ-handler capacity (A7
  from the original review).
- **Timeout.** A signing session should either complete or
  expire. The contract should be able to ask "did this session
  time out?" and refund whatever escrow it staged.

**Currently blocking.** Phase 4.2 full surface; the core team's
security review will catch this if unaddressed.

**Want from core team.** Endorsement of the gas-escrow + timeout
pattern (or an alternative), plus parameter choices.

**Proposed answer.** Endorse gas-escrow + timeout with these
starting parameters (all tunable pre-activation):
**max 4 pending sessions per calling contract** (cheap to track,
enough for real oracles, caps the per-contract amplification);
**timeout 30 blocks (~1 h)** after which the session is queryable as
`expired` and the escrow refundable to the caller;
**escrow = 100,000 gas-equivalent at the request's effective gas
price**, charged at request time, consumed by the callback or
refunded on expiry. Defer the implementation until after the initial
mainnet activation — the sync surface is enough for v1 dApps, and
this is the one precompile feature that touches LLMQ capacity, so it
deserves its own focused review cycle.

---

## Q-A9 — IPv6 in Masternode struct (new — Phase 4.4)

**Context.** The Masternode Registry precompile encodes `ip` as
`uint32` (IPv4 only). IPv6 needs `bytes16`. The struct layout
change is breaking for any contract that holds the type.

**Currently blocking.** Future MN IPv6 deployments. Not blocking
mainnet activation.

**Want from core team.** Confirm the deferral.

**Proposed answer (ratify the default).** Confirm: IPv6 ships as a
**v2 precompile at a new address** (additive, non-breaking — v1
keeps serving `uint32` IPv4, returning 0 for IPv6-only MNs) when
IPv6 masternodes exceed ~5% of the active list. Changing the v1
struct in place would silently break deployed contracts; a parallel
v2 address never can.

---

## Q-A10 — Bug-bounty pool authority (new — pre-Phase 6)

**Context.** The plan said $1M RTM locked in a multisig as a
bug-bounty pool. Who controls the multisig? Determines who can sign
payouts after a valid disclosure.

**Currently blocking.** Phase 6 launch.

**Want from core team.** Multisig composition + payout authority
+ release criteria.

**Proposed answer (reframed for internal-review model).** With the
decision that security review is performed by the core team rather
than an external firm, the bounty program becomes MORE important —
it is now the only paid adversarial pressure on the consensus code.
Proposal: **3-of-5 multisig held entirely by Raptoreum core team +
community members** (Unknown Gravity deliberately excluded — we
wrote the code under review, so we must not control payout
judgments); severity tiers per the ImmuneFi standard (critical =
consensus failure / supply inflation / theft; high = node crash /
DoS; etc.); the program goes live **with the public testnet**, scoped
first to the EVM surface, paying testnet findings at a reduced tier.
Pool size and tier amounts are the core team's call.

---

## How to answer

Reply on the upstream PR thread, or via a dedicated GitHub Issue
per question for traceability. Each "Proposed answer" is written so
a single 👍 ratifies it; anything amended becomes the work-tracking
ticket. The ones that gate the next milestone (public testnet) are
**Q-A5** (activation) and **Q-A7** (mempool floor); the rest can be
ratified asynchronously.
