# Testnet activation proposal — `EVM` + `EVM_COMMIT`

> Status: **PROPOSAL — requires core-team ratification** (this is the
> concrete answer to [`QUESTIONS.md`](QUESTIONS.md) Q-A5). Nothing in
> this document is wired into chainparams yet; the draft patch below is
> applied only after sign-off.

Today `EUpdate::EVM` and `EUpdate::EVM_COMMIT` are registered **only on
regtest** (force-active at height 0). Mainnet and testnet are inert: the
EVM tx types are rejected, the coinbase stays v2, no EVM code runs. This
document proposes how testnet goes live, and outlines mainnet.

---

## 1. Mechanism choice: forced height (testnet), RIP vote (mainnet)

Two activation mechanisms exist in `UpdateManager`:

| Mechanism | How | Pros | Cons |
|---|---|---|---|
| **Forced height** (`heightActivated = N`) | Deterministic cutover at block N | Predictable date for ops; zero vote-stall risk on a low-participation net; trivially testable | No rehearsal of vote machinery |
| **RIP vote** (bit + thresholds) | Miners + smartnodes signal; activates after threshold + grace | Rehearses mainnet governance | On a low-hashrate testnet a handful of non-updated miners can stall it indefinitely |

**Proposal: forced height for testnet, RIP vote for mainnet.**
The vote machinery itself is not the thing under test — it has already
activated V17, ROUND_VOTING and QUORUMS_200_8 on both networks. What
testnet must de-risk is the **EVM consensus surface** (D2 commitment,
bridges, mirror) under real multi-node conditions, and a deterministic
date maximises the time spent doing that instead of chasing signal
percentages.

## 2. Concrete parameters (testnet)

- **Updates:** register BOTH `EVM` and `EVM_COMMIT` at the **same**
  height. Activating them apart would create a window where EVM txs
  execute with no committed roots — the code tolerates it (regtest
  proved pre-v3 blocks connect), but the window has no testing value
  and splits the operational story.
- **Bits:** `EVM = 3`, `EVM_COMMIT = 4` (testnet bits 0–2 are taken by
  V17 / ROUND_VOTING / QUORUMS_200_8). Bits are still consumed by the
  registration even with a forced height, so they must not collide.
- **Activation height:** `N = current testnet tip at release time +
  21,600` (≈ 30 days at the 2-minute target spacing — 720 blocks/day).
  Pin the exact N in the release notes of the binary that ships it.
- **Round/voting params:** mirror the existing testnet entries
  (roundSize 1440, votingPeriod 7, maxRounds 365, grace 7,
  thresholds 85/85) — present for schema completeness; the forced
  `heightActivated` short-circuits them (`UpdateManager::State()`
  honours it unconditionally).

### Draft chainparams patch (apply only after ratification)

```cpp
// CTestNetParams — after the QUORUMS_200_8 registration:
updateManager.Add(
    Update(EUpdate::EVM, std::string("EVM (Phase 1+)"),
        /*bit=*/ 3, /*roundSize=*/ 1440, /*startHeight=*/ N,
        /*votingPeriod=*/ 7, /*votingMaxRounds=*/ 365,
        /*graceRounds=*/ 7, /*forcedUpdate=*/ false,
        VoteThreshold(85, 85, 1), VoteThreshold(0, 0, 1),
        /*failed=*/ false, /*heightActivated=*/ N));
updateManager.Add(
    Update(EUpdate::EVM_COMMIT, std::string("EVM commit (D2)"),
        /*bit=*/ 4, /*roundSize=*/ 1440, /*startHeight=*/ N,
        /*votingPeriod=*/ 7, /*votingMaxRounds=*/ 365,
        /*graceRounds=*/ 7, /*forcedUpdate=*/ false,
        VoteThreshold(85, 85, 1), VoteThreshold(0, 0, 1),
        /*failed=*/ false, /*heightActivated=*/ N));
```

(`N` = pinned activation height. Identical in both entries.)

## 3. Rollout plan (testnet)

1. **T−30d** — release the binary containing the registration (a
   testnet-only chainparams change; mainnet behavior untouched).
   Announce N + this document in the ops channels.
2. **T−30d → T** — node operators update. Non-updated nodes keep
   following the chain until N; at N they reject v3 coinbases — the
   standard hard-fork story, hence the 30-day window.
3. **T (height N)** — first v3 block: miner commits the EVM roots,
   validators recompute + enforce. EIP-1559 base fee starts at
   `kInitialEvmBaseFee` (1 gwei) and follows the recurrence.
4. **T → T+7d — smoke checklist** (each item already scripted for
   regtest; run against testnet):
   - [ ] v3 coinbase accepted; `getblock` shows the CbTx commitment
   - [ ] `evm_fund` → balance visible via `eth_getBalance`
   - [ ] EIP-155 signed deploy via `eth_sendRawTransaction` → receipt
         `status 0x1` by eth hash; block ↔ receipt hashes cross-reference
   - [ ] MetaMask connects to `:8545` (chainId **7374**), sends a tx
   - [ ] Smart Asset: create+mint → `wrap_asset` → `eth_call balanceOf`
         → `unwrap_asset` → UTXO balance restored; supply conserved
         ([`SMART-ASSET-MIRROR.md`](SMART-ASSET-MIRROR.md) §6)
   - [ ] Forced reorg across an EVM-tx block (invalidateblock /
         reconsiderblock) → state root re-converges
   - [ ] ≥2 independent nodes stay in consensus for the whole window
         (tip hash + `evmStateRoot` compared per block)
5. **Bug-bounty program goes live** scoped to the EVM surface
   (Q-A10), paying testnet findings at a reduced tier.

**Abort lever:** before N, shipping a release that re-registers the
updates with `failed=true` (or a later N) cleanly cancels/postpones —
no node that already updated misbehaves, since activation is read from
chainparams at runtime.

## 4. Mainnet (outline — separate ratification later)

- **Mechanism:** standard RIP vote (bit assignments to be checked
  against mainnet's register at proposal time), thresholds per the
  accepted proposal (miner + smartnode signaling; the original
  proposal default was 80% MN on top of miner signaling).
- **Preconditions:** core-team review of PR #443 complete (see
  [`REVIEW-GUIDE.md`](REVIEW-GUIDE.md)); testnet smoke checklist green
  for ≥30 consecutive days; the Q-A7 mempool gas floor implemented;
  bounty program live with no unresolved critical findings.
- **Comms:** ≥90 days between the rc release containing the mainnet
  registration and the earliest possible activation, per the original
  plan.

## 5. What this proposal deliberately does NOT include

- No mainnet parameters (separate decision after testnet evidence).
- No change to the regtest force-at-0 registration (tests depend on it).
- No `-evmrpcport` exposure changes: the 8545 listener stays
  loopback-bound by default on all networks; public testnet RPC
  endpoints are an infra decision, not a consensus one.
