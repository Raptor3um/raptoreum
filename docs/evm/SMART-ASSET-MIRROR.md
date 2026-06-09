# D4 — Smart-Asset bidirectional mirror

> The flagship differentiator: every Raptoreum Smart Asset is callable as a
> standard ERC-20 token from Solidity / MetaMask, and units move **both
> ways** across the UTXO ↔ EVM boundary, supply-conserving and reorg-safe.

This is the design-of-record for the wrap/unwrap bridge and the EVM-side
ERC-20 ledger. It complements [`PRECOMPILES.md`](PRECOMPILES.md) (the
read/write precompile surface) and [`RPC.md`](RPC.md) (the wallet RPCs).

---

## 1. The two ledgers

A Smart Asset lives on the **UTXO side** as an amount carried in
asset-script outputs (`OP_ASSET_ID … OP_DROP`), indexed per address in
`CAssetsCache::mapAssetAddressAmount`. The asset's total is
`circulatingSupply` (bumped only by `MINT`).

The mirror adds an **EVM side**: an ERC-20 ledger held in the per-asset
precompile's own storage trie, at the deterministic address

```
0xA55E70_0000000000 || hash160(assetId)[8:20]
```

with a Solidity-compatible layout (so standard tooling can inspect it):

| Slot | Field |
|---|---|
| 0 | `mapping(address => uint256) balanceOf` |
| 1 | `mapping(address => mapping(address => uint256)) allowance` |
| 2 | `uint256 wrappedSupply` ( == ERC-20 `totalSupply`) |

`evm/asset_ledger.{h,cpp}` is the **single source of truth** for this
layout. Both the precompile read/write surface and the wrap/unwrap apply
path compute slots through it, so a balance credited by `wrap` is the exact
slot `balanceOf()` reads — the mirror cannot drift.

Amounts are the asset's native consensus unit `nAmount`, which is
**COIN-scaled (1e8) for every asset** regardless of its `decimalPoint`
(`decimalPoint` only restricts which fractional amounts are valid, not the
scale). The wrapped token therefore reports `decimals() == 8` uniformly, so
`balanceOf / 10**8` shows the correct whole-unit value in any ERC-20 client.

---

## 2. The conservation invariant

The single law the whole bridge maintains, per asset:

```
sum(UTXO asset balances) + EVM wrappedSupply == circulatingSupply   (constant)
```

`circulatingSupply` is **never touched** by wrap/unwrap — the units just
change which side they live on. And on the EVM side the ERC-20 invariant
`sum(balanceOf) == wrappedSupply` holds at all times.

There is **no vault, no custody account, and no coinbase asset channel.**
Wrap burns units from the UTXO side; unwrap mints them back. The mechanism
is the per-tx asset-conservation exemption (below).

---

## 3. How wrap / unwrap move units

Raptoreum enforces per-transaction asset conservation in
`tx_verify.cpp::checkAssetsOutputs` — for each `assetId`, `sum(inputs) ==
sum(outputs)` — **except** for `MINT`, which may create outputs without
inputs. The mirror adds `WRAP` and `UNWRAP` to that exemption, and each
re-imposes a **constrained** conservation in its own `CheckSpecialTx`
handler so the exemption can't be abused:

- **WRAP** (`TRANSACTION_WRAP_ASSET = 17`): the tx spends the user's asset
  UTXOs and re-outputs `amount` FEWER units than it consumes. Those `amount`
  units are burned from the UTXO side. `CheckWrapAssetTx` requires: for the
  declared `assetId`, `sum(asset inputs) − sum(asset outputs) == amount`,
  and **every other asset balanced**. `ApplyWrapAssetTx` then credits
  `balanceOf[evmRecipient] += amount` and `wrappedSupply += amount`.

- **UNWRAP** (`TRANSACTION_UNWRAP_ASSET = 18`): the reverse. The tx mints
  `amount` units as ordinary asset outputs (no asset inputs).
  `CheckUnwrapAssetTx` requires: for `assetId`, `sum(outputs) − sum(inputs)
  == amount`, every other asset balanced. `ApplyUnwrapAssetTx` debits the
  EVM ledger — `balanceOf[evmSender] ≥ amount`, then `balanceOf -= amount`
  and `wrappedSupply -= amount`. **This debit is the authorization** for the
  UTXO mint: if the sender does not hold the wrapped units the apply fails,
  and the whole block is rejected, so the minted output can never stand
  without its matching EVM burn.

Both txs carry no gas / EIP-1559 fields (like `FUND`); the only cost is the
ordinary UTXO miner fee.

### Atomicity & reorg safety

The EVM-side credit/debit runs in `ProcessEvmTransactionsInBlock` during
`ConnectBlock`, so it lands in the v3 `evmStateRoot` commitment and is
captured by the EVM reorg undo journal (`CEvmStateUndo`). The UTXO-side
burn/mint reverts via the normal `UpdateCoins` input-restore / output-
removal path in `DisconnectBlock`. A failed EVM apply makes
`ProcessEvmTransactionsInBlock` return `ok=false`, rejecting the entire
block — UTXO and EVM effects are all-or-nothing at block granularity.

---

## 4. Code map

| Concern | File |
|---|---|
| Ledger layout + credit/debit (single source) | `src/evm/asset_ledger.{h,cpp}` |
| ERC-20 read/write precompile surface | `src/evm/precompile_asset_erc20.cpp` |
| Payloads `CWrapAssetTx` / `CUnwrapAssetTx` | `src/evm/evmtx.h` |
| Structural validation (constrained delta) | `src/evm/evmtx.cpp` (`CheckWrap/UnwrapAssetTx`) |
| EVM apply (credit / debit) | `src/evm/apply.cpp` (`ApplyWrap/UnwrapAssetTx`) |
| Process wrappers (snapshot, block-reject on fail) | `src/evm/process.cpp` |
| ConnectBlock dispatch | `src/evm/connectblock.cpp` |
| Consensus wiring (dispatch, exemption, mempool, tx shape) | `specialtx.cpp`, `tx_verify.cpp`, `tx_check.cpp`, `validation.cpp` |
| Wallet RPCs | `src/rpc/rpcevo.cpp` (`wrap_asset` / `unwrap_asset` / `get_asset_evm_address`) |

---

## 5. Test surface

| Suite | Locks |
|---|---|
| `evm_asset_erc20_tests` | ERC-20 ledger read/write surface; `decimals()==8` |
| `evm_asset_wrap_tests` | Apply-level credit/debit; the mirror (wrap credit == precompile read); over-debit refusal |
| `evm_asset_mirror_convergence_tests` | T-mirror gate: 2000-op randomized sequence, both invariants after every op |
| `evm_wrap_consensus_tests` | `CheckWrap/UnwrapAssetTx` constrained-delta binding (incl. "silently burn another asset" attack) |

All four are in the Linux `evm-consensus-gate` CI job (hard gate).

---

## 6. End-to-end usage (live regtest, validated)

```bash
# regtest prerequisites: EVM is force-active at height 0, but Smart Assets
# gate on the ROUND_VOTING update (vote-based) and asset fees on SPORK_22.
raptoreumd -regtest -daemon \
  -sporkkey=cVpnZj4dZvRXmBf7Jze1GjpLQb25iKP92GDXUsKdUJTXhXRo2RFA
CLI="raptoreum-cli -regtest -rpcwallet=w"
$CLI createwallet w
MINER=$($CLI getnewaddress)
$CLI generatetoaddress 550 "$MINER"     # vote in ROUND_VOTING (assets active)
$CLI spork SPORK_22_SPECIAL_TX_FEE 256  # enable asset fees (byte1 = fee = 1)

# create + mint an asset
ASSET=$($CLI createasset '{"name":"GOLD","is_root":true,"updatable":true,
  "isunique":false,"maxMintCount":10,"decimalpoint":8,"referenceHash":"",
  "type":0,"targetAddress":"'$($CLI getnewaddress)'","issueFrequency":0,
  "amount":1000,"ownerAddress":"'$($CLI getnewaddress)'"}' | jq -r .txid)
$CLI generatetoaddress 2 "$MINER"
$CLI mintasset "$ASSET"; $CLI generatetoaddress 2 "$MINER"

# wrap 10 GOLD into the EVM ledger of EVM account 0x..aaaa
$CLI wrap_asset "$ASSET" 10 0x000000000000000000000000000000000000aaaa
$CLI generatetoaddress 2 "$MINER"

# read it back as an ERC-20: balanceOf -> 10 * 1e8 = 0x3b9aca00
PRECOMP=$($CLI get_asset_evm_address "$ASSET")
$CLI eth_call '{"to":"'$PRECOMP'","data":"0x70a08231'\
'000000000000000000000000000000000000000000000000000000000000aaaa"}' latest

# unwrap 5 back to a UTXO address; balanceOf -> 5 * 1e8 = 0x1dcd6500,
# and the recipient receives 5 GOLD on the UTXO side
$CLI unwrap_asset "$ASSET" 5 0x000000000000000000000000000000000000aaaa \
  "$($CLI getnewaddress)" "$MINER"
$CLI generatetoaddress 2 "$MINER"
```

Proven on a live regtest daemon: wrap → `balanceOf` = 10·1e8 → unwrap →
`balanceOf` = 5·1e8, `totalSupply` = 5·1e8, recipient UTXO credited 5 GOLD;
supply conserved (1000 = UTXO 995 + EVM 5); the EVM ledger persists across a
daemon restart.

---

## 7. Follow-ups

- **Activation:** wrap/unwrap gate on `IsEvmActive`, force-active only on
  regtest. Testnet/mainnet activation lands with the EVM hard-fork RIP vote.
- **Reverse index:** address→assetId resolution in the precompile is an
  O(N) scan over `mapAsset`; a `hash160 → assetId` index in `CAssetsCache`
  is a tracked optimization for large asset populations.
- **NFTs:** unique (NFT) assets are rejected by wrap/unwrap for now; a
  per-token mirror is a separate design.
- **Fee accounting:** the `unwrap_asset` wallet RPC sizes the fee before
  appending the mint output, a negligible underestimate on regtest; a tight
  fee pass is a minor wallet follow-up.
