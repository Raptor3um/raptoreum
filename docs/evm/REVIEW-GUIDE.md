# Reviewer's guide — how to review the EVM integration branch

> Audience: the Raptoreum core team reviewing
> [PR #443](https://github.com/Raptor3um/raptoreum/pull/443)
> (`feat/evm-integration` → `develop`).
>
> The branch is large (500+ commits). This guide exists so review effort
> goes where the risk is: it gives a subsystem-ordered reading path, ranks
> the consensus-critical hot-spots with concrete "try to break this"
> prompts, and lists exactly how to reproduce every verification claim
> locally. Reviewing commit-by-commit is NOT recommended — review by
> subsystem and use the commit bodies as the rationale record.

---

## 0. The single most important property

**Merging this branch changes NOTHING on mainnet or testnet.**

Every consensus-visible behavior is gated on `Updates().IsEvmActive()` /
`Updates().IsEvmCommitActive()` (`EUpdate::EVM`, `EUpdate::EVM_COMMIT`).
Those updates are registered **only for regtest** (force-active at height
0, `src/chainparams.cpp`, `CRegTestParams`). On mainnet/testnet they are
unregistered, so:

- the new tx types (11/12/13/17/18/19) are rejected (`evm-not-activated`),
- the coinbase stays v2 (`CheckCbTx` only requires v3 when the gate is on),
- no EVM execution runs in `ConnectBlock`,
- the conservation exemption for wrap/unwrap never triggers (those tx
  types can't enter a block).

Verifying this gating is review item #1 — see hot-spot H1 below. Once
satisfied, the rest of the review is about regtest-and-later correctness,
not about live-network risk.

---

## 1. Suggested review order

Read the docs first — they are current and they cite the code:

| Step | Read | Purpose |
|---|---|---|
| 1 | [`STATUS.md`](STATUS.md) | What shipped, per phase, with commit refs |
| 2 | [`PROPOSAL-FOR-CORE-TEAM.md`](PROPOSAL-FOR-CORE-TEAM.md) | The accepted decisions D1–D7 this implements |
| 3 | [`SMART-ASSET-MIRROR.md`](SMART-ASSET-MIRROR.md) | Design-of-record for the highest-risk piece (D4) |
| 4 | This file, then code by subsystem (below) | — |
| 5 | [`QUESTIONS.md`](QUESTIONS.md) | Decisions we still owe YOU — several now have proposed answers awaiting ratification |

Then review code in this order (risk-descending):

1. **Consensus carve-outs + tx validation** — `src/consensus/tx_verify.cpp`,
   `tx_check.cpp`, `src/validation.cpp` (type allow-list), `src/evo/specialtx.cpp`.
2. **D4 mirror** — `src/evm/evmtx.{h,cpp}` (payloads + `CheckWrap/UnwrapAssetTx`),
   `src/evm/asset_ledger.{h,cpp}`, `src/evm/apply.cpp` (`ApplyWrap/UnwrapAssetTx`),
   `src/evm/precompile_asset_erc20.cpp`.
3. **D2 commitment + bridges** — `src/evo/cbtx.{h,cpp}` (v3 fields),
   `src/evm/connectblock.{h,cpp}` (`ComputeCoinbaseEvmCommitment`,
   `CheckCoinbaseRealisesSpendCredits`), `src/miner.cpp` (v3 coinbase),
   the EVM block in `src/validation.cpp::ConnectBlock`.
4. **EVM execution core** — `src/evm/{state_cache,host,apply,process}.cpp`,
   `src/evm/undo.{h,cpp}` (reorg journal), `src/evm/mpt.cpp` (state root).
   Note: this layer is the one validated by the official Ethereum test
   suite (Capa B, ~99.95% of applicable Cancun) — lean on that.
5. **RPC surface** — `src/rpc/ethereum.cpp` (eth_* namespace, raw-tx ingest),
   `src/rpc/rpcevo.cpp` (`evm_fund`/`wrap_asset`/`unwrap_asset`),
   `src/evm/rawtx.cpp` (decode + sender recovery), `src/evm/signing.cpp`.
6. **Precompiles** — `src/evm/precompiles.cpp` (dispatch + ABI),
   `precompile_{chainlocks,llmq_oracle,masternodes}.cpp`.

---

## 2. Consensus hot-spots (ranked) — what to try to break

### H1 — Activation gating (mainnet safety)

*Files:* `src/chainparams.cpp`, every `Updates().IsEvmActive` /
`IsEvmCommitActive` call site.

**Attack prompts.** Find any path where an EVM-typed tx, a v3 coinbase, or
an EVM state mutation can occur with the gates off. Check the mempool
path (`validation.cpp` type allow-list), block path (`CheckSpecialTx`),
fee path (`checkSpecialTxFee`), and `CheckCbTx`'s version floor. Confirm
`EUpdate::EVM`/`EVM_COMMIT` appear ONLY in the `CRegTestParams` section.

### H2 — The wrap/unwrap conservation exemption (D4)

*Files:* `tx_verify.cpp` (the `checkAssetsOutputs` exemption),
`evmtx.cpp::CheckConstrainedAssetDelta`.

This is the most novel consensus surface in the branch. The per-tx asset
input==output rule is **waived** for `WRAP`/`UNWRAP` (as it already is
for `MINT`), and each re-imposes a **constrained** delta: the declared
`assetId` must net exactly ∓`amount`, every other asset exactly 0.

**Attack prompts.**
- Can a WRAP tx burn (or an UNWRAP mint) units of an asset *other than*
  the declared one? (`evm_wrap_consensus_tests` includes this attack —
  check the test catches what you'd try.)
- Can the declared `amount` disagree with the actual delta in any path
  (`GatherAssetDeltas` reads spent coins from the *view* — is the view
  always the right one in mempool vs block context)?
- Unique/NFT assets are rejected — confirm `isUnique` can't slip through.
- Can a tx be BOTH (e.g. type WRAP with unwrap-shaped outputs)?

### H3 — Supply conservation across the boundary (FUND / SPEND / mirror)

*Files:* `tx_verify.cpp::checkSpecialTxFee` (FUND subtracts from
miner-claimable fee, `specialTxFee` left 0), `connectblock.cpp::
CheckCoinbaseRealisesSpendCredits`, `miner.cpp` (credit realisation),
`apply.cpp` (FUND credit / SPEND debit / wrap credit / unwrap debit).

**Attack prompts.**
- Can FUND value be claimed by the miner AND credited to the EVM side?
  (The design: `nFeeTotal -= fundSat` without raising `specialTxFee`.)
- Can a SPEND credit appear in coinbase without the EVM debit, or
  vice versa? (Multiset matching in `CheckCoinbaseRealisesSpendCredits`;
  block rejected on mismatch.)
- Can an unwrap mint stand if the EVM debit fails? (Process layer
  returns `preflightFailed` → whole block rejected — confirm there is no
  path that connects the block anyway.)
- Invariant to keep in mind: `UTXO asset balances + wrappedSupply ==
  circulatingSupply`; for RTM: UTXO supply + EVM balances constant.

### H4 — Miner == validator parity (D2)

*Files:* `evm/connectblock.cpp::ComputeCoinbaseEvmCommitment` (the ONE
shared helper), `miner.cpp`, the recompute-and-compare block in
`validation.cpp::ConnectBlock`.

**Attack prompts.** Any input to the commitment that the miner and a
validator could observe differently (clock: `evmExecTime` is pinned to
`max(parent MTP+1, header nTime)` — check both sides derive it from the
same data; base fee: parent's *committed* values, not local state).
Divergence here = wedged mining or chain split, so it deserves a careful
read even though `evm_d2_consensus_tests` mines through the production
miner+validator.

### H5 — Reorg safety (EVM undo journal)

*Files:* `evm/undo.{h,cpp}`, `ConnectTip`/`DisconnectTip` wiring in
`validation.cpp`.

**Attack prompts.** State written outside the cache→journal path (grep
for direct `pevmstatedb->Write` outside flush); the journal's coverage of
account create/delete/recreate and storage zero/restore (this is what
`evm_reorg_fuzz_tests` hammers with random deep reorgs — read the test's
model to see what's covered and what isn't).

### H6 — Raw-tx ingest (`eth_sendRawTransaction`)

*Files:* `src/evm/rawtx.cpp`, `rpc/ethereum.cpp`.

The one RPC that turns *external* bytes into a consensus object.
**Attack prompts:** sender recovery on malleated signatures (high-s?),
EIP-155 chain-id confusion, RLP edge cases (leading zeros, oversize
ints), the legacy-vs-1559 envelope switch. `eth_sendrawtx_roundtrip_tests`
covers tamper + wrong-chain cases; try to think of one it doesn't.

### H7 — Asset resolver self-correction (consensus-path performance index)

*Files:* `assets/assets.cpp::ResolveAssetIdByTag`,
`precompile_asset_erc20.cpp::ResolveAssetIdFromAddress`.

The reverse index is a **verified hint**: every hit is re-checked against
`mapAsset` and falls back to the authoritative scan, so the result is a
pure function of `mapAsset` by construction. **Attack prompt:** find any
return path whose answer depends on hint contents rather than `mapAsset`.
(`evm_asset_resolver_tests` asserts method==scan under 4000 random
mutations.)

### H8 — EVM execution semantics

Covered primarily by **Capa B** (official ethereum/tests, Cancun,
~99.95% of applicable vectors, CI-gated with a pass floor + a 10-case
classified allow-list — each residual is documented as harness-layer,
not consensus, in [`OFFICIAL-TESTS.md`](OFFICIAL-TESTS.md)). Spot-review
the allow-list classifications rather than re-deriving EVM semantics.

---

## 3. Reproducing the verification claims

### Unit suites (all green, 572 cases)

```bash
cd src && make -j$(nproc) && make -j$(nproc) test/test_raptoreum
LC_ALL=C.UTF-8 ./test/test_raptoreum                      # full suite
```

The consensus-critical subset (what CI's `evm-consensus-gate` job runs,
plus Capa B which needs the fixtures):

```bash
git clone --depth 1 --branch v14.0 --filter=blob:none --sparse \
  https://github.com/ethereum/tests.git ethereum-tests
(cd ethereum-tests && git sparse-checkout set BlockchainTests/GeneralStateTests)
EVM_OFFICIAL_TESTS_PATH="$PWD/ethereum-tests/BlockchainTests/GeneralStateTests" \
  ./test/test_raptoreum --run_test=evm_official_blockchaintest_tests,\
evm_reorg_fuzz_tests,evm_wrap_consensus_tests,evm_asset_wrap_tests,\
evm_asset_mirror_convergence_tests,evm_asset_erc20_tests,\
evm_asset_resolver_tests,evm_evmtx_validation_tests,evm_fund_tests,\
evm_spend_credit_tests,evm_d2_consensus_tests,cbtx_evm_commit_tests
```

### Live regtest e2e (the D4 mirror, end to end)

Follow [`SMART-ASSET-MIRROR.md`](SMART-ASSET-MIRROR.md) §6 verbatim —
create+mint an asset, `wrap_asset`, `eth_call balanceOf` at the address
from `get_asset_evm_address`, `unwrap_asset`, re-check both sides.
Regtest gotchas (already encoded in §6): assets gate on the vote-based
ROUND_VOTING update (~550 blocks), asset RPCs need
`spork SPORK_22_SPECIAL_TX_FEE 256` under the regtest spork key, and
`mintasset`/`wrap_asset` take the asset **txid**, not the name.

### Funded Ethereum-tooling e2e

`eth_funded_e2e_tests` does this in-process (FUND → EIP-155-signed
`eth_sendRawTransaction` → mine → receipt by eth hash → block↔receipt
hash cross-reference). For a live check, point MetaMask at
`http://localhost:8545` (chainId 7375) after `evm_fund`-ing an account.

---

## 4. Review checklist

- [ ] H1: gates verified; no consensus change with EVM/EVM_COMMIT unregistered
- [ ] H2: conservation exemption cannot mint/burn undeclared assets
- [ ] H3: every boundary crossing (FUND/SPEND/wrap/unwrap) conserves supply, both directions, including failure paths
- [ ] H4: commitment inputs identical for miner and validator
- [ ] H5: no EVM state write bypasses the undo journal
- [ ] H6: raw-tx decode rejects everything the tests reject, and you couldn't construct a new bypass
- [ ] H7: resolver result provably independent of hint contents
- [ ] H8: Capa B allow-list classifications are believable
- [ ] CI: `evm-consensus-gate` runs the suites above as a hard gate
- [ ] Docs match code (STATUS / RPC / PRECOMPILES / SMART-ASSET-MIRROR)
- [ ] [`QUESTIONS.md`](QUESTIONS.md): ratify or amend the proposed answers
- [ ] [`FUP.md`](FUP.md): agree the deferred-item priorities (esp. what gates testnet)

## 5. Known deliberate limitations (don't re-discover these)

- EIP-2930 access lists rejected at ingest (FUP-3.2 — gas accounting
  must be byte-identical across nodes; deferred for careful review).
- `value` is u64 weis (~18.44 RTM/tx ceiling) by payload design (FUP-3.9).
- Historical block-tag state queries unsupported (FUP-3.3).
- `TestBlockValidity`/`VerifyDB` skip EVM recompute (FUP-2.7 — rationale
  in the FUP entry: tautological for miner blocks, needs state snapshots
  for VerifyDB).
- NFT (unique) assets excluded from wrap/unwrap (FUP-4.9).
