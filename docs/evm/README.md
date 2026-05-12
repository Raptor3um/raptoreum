# EVM integration — documentation index

Native EVM integration in Raptoreum. 31 commits on
`feat/evm-integration` (`JSanchezFDZ/raptoreum`), 5 phases shipped,
3 phases remaining.

## Read this first

If you're a maintainer / reviewer landing here for the first time:

1. [`STATUS.md`](STATUS.md) — **What's actually shipped.** Phase-
   by-phase map with commit refs and the test surface. The
   one-screen "is this ready?" view.
2. [`QUESTIONS.md`](QUESTIONS.md) — Decisions still owed by the
   core team. Resolving these unblocks Phase 5.4 / 6 / 7.
3. [`FUP.md`](FUP.md) — Follow-up register, prioritised by what
   gates testnet vs. mainnet vs. post-launch.

## Reference

| File | Purpose |
|---|---|
| [`STATUS.md`](STATUS.md) | Phase-by-phase state with commit refs |
| [`RPC.md`](RPC.md) | All 25 RPC methods with examples |
| [`PRECOMPILES.md`](PRECOMPILES.md) | 4 RTM-native precompiles + Solidity interfaces + selectors |
| [`FUP.md`](FUP.md) | Deferred follow-ups, prioritised |
| [`QUESTIONS.md`](QUESTIONS.md) | Open questions for the core team |
| [`BUILD.md`](BUILD.md) | Build / run / test guide |
| [`PLAN.md`](PLAN.md) | Original strategic plan (frozen — kept as historical reference) |
| [`PROPOSAL-FOR-CORE-TEAM.md`](PROPOSAL-FOR-CORE-TEAM.md) | Formal acceptance record (D1–D7 decisions) |
| [`PHASE-0-HANDOFF.md`](PHASE-0-HANDOFF.md) | Phase 0 spike handoff (historical) |
| [`BRANCH-GUIDE.md`](BRANCH-GUIDE.md) | Branch / dev conventions |

## TL;DR for reviewers

**What works today on regtest** (UPDATE_EVM activated at height 0):

- A MetaMask client points at `http://localhost:8545`, sees chain
  id `7375`, can read account state, simulate contract calls, and
  browse blocks via the standard `eth_*` namespace.
- A wallet-signed EIP-1559 transaction submitted via
  `eth_sendRawTransaction` is decoded, sender-recovered via
  secp256k1, wrapped as `TRANSACTION_EVM_CALL` (or
  `TRANSACTION_EVM_DEPLOY`), and enters the mempool. Receipts are
  generated during `ConnectTip` and looked up by either the
  Ethereum hash (keccak of wire bytes) or the Raptoreum wrapper
  hash via a cross-index.
- Solidity contracts can call the four RTM-native precompiles —
  ChainLocks, Masternode Registry, Smart Asset ERC-20 (read-only),
  LLMQ Oracle (sync verify).
- The C++ EIP-1559 signing primitives are byte-for-byte identical
  to Python `eth-account` on the canonical
  `0x4646…4646` test key.

**What's deliberately deferred**:

- The D2 header fields (`stateRoot`, `receiptsRoot`,
  `transactionsRoot`, `chainLocksCommit`) are not yet in
  `CBlockHeader`. Without them, base-fee dynamics stay at 0 and
  the coinbase-tip output verification is unenforced. **Mainnet
  activation depends on this** (FUP-2.1 → FUP-2.3).
- Wallet-keystore HD integration awaits Q-A2 (and the new Q-A4) —
  whether EVM keys live at `m/44'/60'` or under the RTM coin type.
- Qt UI for deploy/call is a separate session (no shared
  technical risk, just Qt + MOC overhead).
- The Smart Asset ↔ EVM bidirectional balance mirror (D4-revised
  "T-mirror") is the highest-risk integration piece still ahead.
  Without it the ERC-20 precompile's `transfer`/`approve` paths
  stay unimplemented (read surface is live).

**Test surface**: 11 EVM unit-test suites, all green, every commit.
End-to-end live-validated against a regtest daemon for each RPC
method.

## Operating model

- All public artifacts (commits, PRs, docs, comments) authored
  solely by JSanchezFDZ. No third-party attribution.
- Phase-by-phase commits with substantive bodies (the implementation
  notes for each phase live in the commit messages, not just here).
- One-fork model: changes accumulate on
  `JSanchezFDZ/raptoreum:feat/evm-integration` and PR upstream
  when ready for review.

## Status snapshot

Last updated 2026-05-12.

- ✅ Phase 0 — evmone spike validated
- ✅ Phase 1 — AAL (tx types, opcodes, activation gate)
- ✅ Phase 2 — Full EVM execution pipeline (state → host → apply →
  process → ConnectBlock wiring → reorg journal → worker pool)
- ✅ Phase 3 — JSON-RPC `eth_*` namespace (22 methods + 8545 listener)
- ✅ Phase 4 — All four RTM-native precompiles
- ✅ Phase 5 — C++ EIP-1559 signing primitives + 3 RPC (HD wallet +
  Qt UI deferred)
- ☐ Phase 6 — Testnet pública + paid audits
- ☐ Phase 7 — Mainnet activation
- ☐ Phase 8 — Ecosystem launch
