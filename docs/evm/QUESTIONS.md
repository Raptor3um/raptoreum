# Open questions for the core team

Decisions or clarifications still owed by the core team. Each
question references the design context, the current default, and
the work it blocks.

The three original `Q-A*` questions from the
[`PROPOSAL-FOR-CORE-TEAM.md`](PROPOSAL-FOR-CORE-TEAM.md) are
restated here together with new ones surfaced during Phases 2–5.

---

## Q-A1 — "The AI thing"

**Context.** Surfaced during the original proposal review. The core
team mentioned an "AI thing" without specifying what they were
referring to. We need a precise problem statement before we can
take a position.

**Currently blocking.** Nothing critical, but the question deserves
a clear answer in writing.

**Want from core team.** What system / feature / external dependency
are they referring to? Is it a planned RTM-side integration? A
constraint from a third party? Once we know the actual subject we
can decide whether it intersects with the EVM work at all.

---

## Q-A2 — Wrap / unwrap (tx types 17/18)

**Context.** The plan reserved transaction types 17 and 18 for
wrapping a Smart Asset into an EVM-side balance (and back). D4-
revised left two viable paths:

- **v1 — strict** wrap/unwrap with an opt-in, explicit user action
  per asset transfer between the UTXO and EVM sides.
- **v2 — implicit** wrap-on-touch where balances cross the bridge
  on first use of a Smart Asset inside an EVM contract.

**Currently blocking.**

- Phase 4.1 ERC-20 precompile `balanceOf` / `transfer` / `allowance`
  semantics (today balanceOf returns 0; transfer not yet exposed).
- Wallet HD-path decision: if wrap/unwrap goes through tx types
  17/18 as standard "moves", the EVM key may not need to live in
  a different BIP44 coin_type from the UTXO key.

**Want from core team.** Pick v1 or v2 for the initial mainnet.
v2 is the better UX but its consensus surface is wider.

---

## Q-A3 — EVM Asset fee model

**Context.** Creating a Smart Asset today costs ~5 RTM (asset
issuance fee). EVM tokens (vanilla ERC-20 deployed inside the EVM
state) have no asset-issuance equivalent — just normal contract-
creation gas. The question: do we treat an EVM-deployed ERC-20 as
a "Smart Asset" for fee purposes (charge the 5-RTM equivalent in
the deploy gas), or leave EVM tokens fee-equivalent to vanilla
Ethereum?

**Currently blocking.** Nothing strictly. The default today is
fee-equivalent to vanilla Ethereum (no surcharge). But the
asymmetry may matter for incentive alignment of the asset
ecosystem.

**Want from core team.** Confirm "no surcharge for EVM-deployed
ERC-20s" is fine, or specify the surcharge mechanism.

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

**Want from core team.** Pick A or B. Recommend A unless there's a
specific reason against it (cross-chain identity is widely
considered a feature, not a bug).

---

## Q-A5 — Activation height per network (new — Phase 2.4e)

**Context.** Regtest activates UPDATE_EVM at height 0 today
(forced via `heightActivated=0` in chainparams). Mainnet and
testnet have no activation height registered — meaning EVM is
inert on those networks until a coordinated hard fork.

**Currently blocking.** Phase 6 testnet pública requires a testnet
activation height, ideally far enough in the future that early
operators can update before the cutoff. Phase 7 mainnet
activation needs a 90-day-comm-window cutover plan.

**Want from core team.**

- A target testnet activation block (suggested: testnet block N
  where N is chosen so all known infrastructure ops can update
  with 30+ days notice).
- A target mainnet activation block (suggested: at least 90 days
  after a v2.0.0-rc1 release that contains the EVM path).
- A signaling rule (BIP9-style with 80% MN signaling on top of
  miner signaling is the proposal default; confirm or revise).

---

## Q-A6 — D2 header field layout (new — Phase 2.4 / FUP-2.1)

**Context.** Decision D2 said: add `stateRoot`, `receiptsRoot`,
`transactionsRoot`, plus a fourth `chainLocksCommit` field (per
D3) to the block header. These are concretely 4 × 32-byte words =
128 extra bytes per header.

**Currently blocking.** FUP-2.1 (header fields) and therefore
FUP-2.2 (base-fee dynamics) and FUP-2.3 (coinbase tip
verification). All three gate mainnet.

**Want from core team.**

- Confirm the four-field layout.
- Confirm field ordering (suggested: `prevBlockHash`,
  `merkleRoot` (existing), `time`, `bits`, `nonce` (existing),
  THEN `stateRoot`, `receiptsRoot`, `transactionsRoot`,
  `chainLocksCommit`).
- Soft-fork bit-position for activation signaling (D2 said
  coordinated hard fork; confirm the activation mechanism still
  uses a version-bit gate).

---

## Q-A7 — Gas-price floor for non-EVM-fee mempool admission (new — Phase 3.5)

**Context.** The Phase 3.5 consensus carve-out exempts EVM-typed
txs from `minRelayTxFee` because their fee is the EIP-1559 gas
debit (on the EVM side), not a UTXO miner fee. With base-fee = 0
today, an EVM tx can theoretically be relayed with effectively
zero cost — opens a DoS vector via flooding zero-tip txs.

**Currently blocking.** Mainnet readiness. Today on regtest the
attack surface is moot; on mainnet a floor is needed.

**Want from core team.** Confirm "no UTXO-side minimum, but mempool
imposes a minimum effective gas price (e.g., 1 gwei equivalent in
weis) for EVM txs". Or pick an alternative model.

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

**Currently blocking.** Phase 4.2 full surface; Phase 6 audits
will catch this if unaddressed.

**Want from core team.** Endorsement of the gas-escrow + timeout
pattern (or an alternative), plus parameter choices: max-pending
sessions per contract, timeout duration in blocks, escrow
fee-per-session.

---

## Q-A9 — IPv6 in Masternode struct (new — Phase 4.4)

**Context.** The Masternode Registry precompile encodes `ip` as
`uint32` (IPv4 only). IPv6 needs `bytes16`. The struct layout
change is breaking for any contract that holds the type.

**Currently blocking.** Future MN IPv6 deployments. Not blocking
mainnet activation.

**Want from core team.** Confirm "v6 support is a forward-
incompatible v2 of the precompile that ships when MN IPv6 is
non-trivial in the network" — i.e., delay IPv6 in the precompile
until IPv6 MNs are >5% of the active list.

---

## Q-A10 — Bug-bounty pool authority (new — pre-Phase 6)

**Context.** The plan said $1M RTM locked in a multisig as a
bug-bounty pool. Who controls the multisig? Foundation board?
A 3-of-5 of core devs + community? Determines who can sign payouts
after a valid disclosure.

**Currently blocking.** Phase 6 launch. Auditors expect the bounty
program to be live when they're paid.

**Want from core team.** Multisig composition + payout authority
+ release criteria (ImmuneFi-tier severity tiers).

---

## How to answer

Reply on the upstream PR thread, or via a dedicated GitHub Issue
per question for traceability. Each "Want from core team" point
should map to a single decision; if the answer needs follow-up
work, the issue becomes the work-tracking ticket.
