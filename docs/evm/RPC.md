# EVM JSON-RPC reference

25 RPC methods across three namespaces. All live on the active
`raptoreumd` RPC port AND on the dedicated Ethereum-style port
(default 8545; configurable via `-evmrpcport`; disable with
`-evmrpcport=0`).

| Namespace | Count | Purpose |
|---|---|---|
| `eth_*`, `net_*`, `web3_*` | 22 | Ethereum-spec compatibility for MetaMask / ethers.js / viem / web3.js |
| `evm_*` | 3 | Server-side signing primitives + Phase 0 smoke probe |

Chain IDs (EIP-155): **7373** mainnet, **7374** testnet, **7375**
regtest. Until UPDATE_EVM activates on each network the methods
respond but underlying state lookups return empty/zero values.

## Method index

### State queries

| Method | Purpose |
|---|---|
| [`eth_chainId`](#eth_chainid) | Active chain id |
| [`eth_blockNumber`](#eth_blocknumber) | Active chain tip height |
| [`eth_gasPrice`](#eth_gasprice) | Suggested gas price (next-block base fee + 1-gwei tip) |
| [`eth_getBalance`](#eth_getbalance) | EVM account balance in weis |
| [`eth_getTransactionCount`](#eth_gettransactioncount) | EVM account nonce |
| [`eth_getCode`](#eth_getcode) | Runtime bytecode |
| [`eth_getStorageAt`](#eth_getstorageat) | Single storage slot |

### Execution probes

| Method | Purpose |
|---|---|
| [`eth_call`](#eth_call) | Read-only EVM execution against a state snapshot |
| [`eth_estimateGas`](#eth_estimategas) | Complete tx gas limit (intrinsic + binary-searched execution) |

### Block exploration

| Method | Purpose |
|---|---|
| [`eth_getBlockByNumber`](#eth_getblockbynumber) | Block by height (or tag) |
| [`eth_getBlockByHash`](#eth_getblockbyhash) | Block by hash |
| [`eth_getBlockTransactionCountByNumber`](#eth_getblocktransactioncountbynumber) | Tx count |
| [`eth_getBlockTransactionCountByHash`](#eth_getblocktransactioncountbyhash) | Tx count |

### Metadata

| Method | Purpose |
|---|---|
| [`eth_protocolVersion`](#eth_protocolversion) | "0x41" |
| [`eth_syncing`](#eth_syncing) | false / sync status object |
| [`eth_accounts`](#eth_accounts) | [] (wallet integration pending) |
| [`eth_coinbase`](#eth_coinbase) | 0x000…000 |
| [`eth_mining`](#eth_mining) | false |
| [`eth_hashrate`](#eth_hashrate) | "0x0" |
| [`eth_maxPriorityFeePerGas`](#eth_maxpriorityfeepergas) | Suggested priority tip (1 gwei) |
| [`net_version`](#net_version) | Decimal-string chain id |
| [`net_listening`](#net_listening) | P2P listening flag |
| [`net_peerCount`](#net_peercount) | Peer count |
| [`web3_clientVersion`](#web3_clientversion) | "Raptoreum/<ver>/EVM" |

### Write + receipts/logs

| Method | Purpose |
|---|---|
| [`eth_sendRawTransaction`](#eth_sendrawtransaction) | EIP-1559 or legacy signed tx |
| [`eth_getTransactionReceipt`](#eth_gettransactionreceipt) | Receipt by Ethereum hash |
| [`eth_getTransactionByHash`](#eth_gettransactionbyhash) | Tx body by Ethereum hash |
| [`eth_getLogs`](#eth_getlogs) | Filtered log query |

### Signing primitives (`evm_*`)

| Method | Purpose |
|---|---|
| [`evm_keyToAddress`](#evm_keytoaddress) | Derive EVM address from a private key |
| [`evm_signTransaction`](#evm_signtransaction) | Sign EIP-1559 or legacy (type 0x0) tx, return wire bytes |
| [`evm_sendTransaction`](#evm_sendtransaction) | Sign + submit in one call |
| [`evm_executeReadOnly`](#evm_executereadonly) | Phase 0 bytecode smoke probe (developer tool) |

---

## State queries

### `eth_chainId`

Returns the active EVM chain id.

```
$ raptoreum-cli eth_chainId
0x1ccf   # 7375 (regtest)
```

### `eth_blockNumber`

Returns the active chain tip height as a `0x` quantity.

### `eth_gasPrice`

Returns the suggested gas price in weis: the next block's EIP-1559 base
fee (the committed D2 base-fee recurrence applied to the tip's
`evmBaseFee`/`evmGasUsed`) plus a suggested 1-gwei priority tip. Returns
`0x0` on networks where `EVM_COMMIT` is not yet active.

### `eth_getBalance(address, block)`

Returns the EVM account balance.

```
$ raptoreum-cli eth_getBalance 0x9d8a62f656a8d1615c1294fd71e9cfb3e4855a4f latest
0x0
```

Only `latest`/`pending` block tags are supported today. Historical
queries land with the log/receipt indexer follow-up.

### `eth_getTransactionCount(address, block)`

Returns the EVM account's nonce.

### `eth_getCode(address, block)`

Returns the contract's runtime bytecode (or `0x` for EOAs / empty
accounts).

### `eth_getStorageAt(address, slot, block)`

Returns the value of a 32-byte storage slot (or the canonical zero
word on miss).

---

## Execution probes

### `eth_call(callObject, block)`

Runs an EVM call against a snapshot of the current state and
returns the contract's RETURN bytes. State changes are discarded
(read-only).

```
$ raptoreum-cli eth_call \
    '{"to":"0x000…0a03","data":"0x2a433be9"}' latest
0x000…000   # ChainLocks::latestChainLockedHeight() returns 0
```

REVERT surfaces as a JSON-RPC error with the revert bytes attached;
wallets parse those into `Error(string)` messages.

### `eth_estimateGas(callObject, block)`

Returns a complete transaction gas limit: the intrinsic gas (21000 +
EIP-2028 calldata cost) plus the smallest execution-gas limit at which
the call still succeeds, found by binary search (so the 63/64 nested-call
gas-forwarding rule is honoured). Suitable for sizing a real tx's
`gasLimit` directly — it will not under-fund.

---

## Block exploration

### `eth_getBlockByNumber(tag, fullTx)`

Returns the block as an Ethereum-shaped object. `fullTx=true`
returns transactions[] as objects; `false` returns just hashes.

Fields without a Raptoreum analog today (`stateRoot`, `receiptsRoot`,
`logsBloom`, `sha3Uncles`, `miner`) return canonical zeros — D2
hard fork populates them.

### `eth_getBlockByHash(blockHash, fullTx)`

Same as above, indexed by hash.

### `eth_getBlockTransactionCountBy{Number,Hash}`

Just the tx count for a block.

---

## Metadata

### `eth_protocolVersion`

Returns `0x41` — the conventional value modern clients return.

### `eth_syncing`

Returns `false` at the tip, otherwise an object with `startingBlock`
/ `currentBlock` / `highestBlock`.

### `eth_accounts`

Returns `[]` until wallet-keystore integration lands.

### `eth_coinbase`

Returns `0x000…000` until wallet integration sources the coinbase
EVM address.

### `eth_mining` / `eth_hashrate`

Always `false` / `0x0`. Raptoreum PoW + masternode consensus is
reported on the UTXO side, not via the EVM RPC.

### `eth_maxPriorityFeePerGas`

Returns the suggested EIP-1559 priority fee (tip) in weis — a non-zero
default of 1 gwei so wallets don't build a zero-tip tx the mempool drops.

### `net_version`

Returns the active chain id as a **decimal string** ("7373") — required
form per the `net_` spec, NOT 0x-hex.

### `net_listening`

True when the P2P listener is up.

### `net_peerCount`

0x-prefixed peer count.

### `web3_clientVersion`

`"Raptoreum/<full-version>/EVM"`.

---

## Write path

### `eth_sendRawTransaction(signedTx)`

Accepts an EIP-1559 (type 0x02) or legacy EIP-155 signed transaction.
The handler:

1. Decodes the RLP envelope.
2. Verifies the EIP-155 chain id matches the active network.
3. Recovers the EVM sender via `secp256k1_ecdsa_recover`.
4. Wraps as `TRANSACTION_EVM_DEPLOY` (if `to` was empty) or
   `TRANSACTION_EVM_CALL`.
5. Registers the `ethHash ↔ rtmHash` cross-index entries.
6. Broadcasts through the standard mempool path.

Returns the Ethereum tx hash (keccak256 of the original wire bytes).

```
$ raptoreum-cli eth_sendRawTransaction "0x02f864821ccf80…"
0x07b187941c7917d132a703ed1063b9278cf686bfb95be605dc6f4da40870346b
```

---

## Receipts / logs

### `eth_getTransactionReceipt(txHash)`

Returns the receipt for an Ethereum tx hash (or `null` on miss).

Looks up rtmHash via the cross-index, loads `CEvmReceipt` by rtmHash,
formats as the Ethereum-shaped receipt object: `transactionHash`,
`blockHash`, `blockNumber`, `transactionIndex`, `from`, `to` (or
`contractAddress`), `cumulativeGasUsed`, `gasUsed`,
`effectiveGasPrice`, `status`, `logs[]`, `logsBloom` (zero).

Accepts the rtm-hash directly as a fallback for internal tooling.

### `eth_getTransactionByHash(txHash)`

Full Ethereum transaction object: loads the wrapper tx from its block
(`blockHash`+`txIndex`) and projects `from`/`to`/`value`/`input`/`gas`/
`nonce`/`maxFeePerGas`/`chainId` (degrades to a receipt-only object if the
block is unreadable). The returned `hash` uses the same identity as the
block listing and the receipt — every EVM tx is keyed by its keccak
`ethTxHash` (or, absent a cross-index entry, the forward rtm hash) — so
block ↔ receipt ↔ getTransactionByHash all cross-reference.

### `eth_getLogs(filter)`

Filters logs by block range (or a single block via `blockHash`) +
address + topics (per-position OR of arrays per Ethereum spec).

```
$ raptoreum-cli eth_getLogs '{
    "fromBlock": "0x0",
    "toBlock":   "latest",
    "address":   "0xCAFE…",
    "topics": [
      "0xddf252ad…",
      null,
      ["0xabc…", "0xdef…"]
    ]
  }'
```

Contract behaviour (indexer-facing):

- **`blockHash`** (EIP-234) restricts the query to one block and is
  **mutually exclusive** with `fromBlock`/`toBlock` — passing both is a
  clean error, not a silently-ignored field. The block must be in the
  canonical chain; a reorged-out hash returns an explicit error (the
  indexer's roll-back signal), never stale logs.
- **Ordering** is `(blockNumber, transactionIndex, logIndex)`, and
  `logIndex` is the log's position **within its block** (cumulative
  across the block's transactions), per the spec — not per-receipt.
- **Range cap:** 10000 blocks per call. An oversized *requested* numeric
  range fails fast with an explicit error **before** scanning, rather
  than letting the call grind toward a client timeout. A numeric
  `toBlock` past the tip clamps to the tip (geth-style).
- After a reorg, `DisconnectTip` + reconnect update results
  consistently: an orphaned block's logs disappear from range queries
  and error by hash; on reconnect the same logs are served again.

---

## Signing primitives (`evm_*`)

### `evm_keyToAddress(privKey)`

Derives the 20-byte EVM address from a 32-byte secp256k1 private
key (hex, with or without 0x prefix).

```
$ raptoreum-cli evm_keyToAddress 0x4646464646464646464646464646464646464646464646464646464646464646
0x9d8a62f656a8d1615c1294fd71e9cfb3e4855a4f
```

### `evm_signTransaction(privKey, callObject)`

Signs a transaction with the supplied private key and returns the wire
bytes (0x-prefixed). Defaults to EIP-1559 (type 0x2), byte-for-byte
identical to `Account.sign_transaction(...)` from Python `eth-account`.
Pass `"type":"0x0"` in the call object to sign a **legacy EIP-155** tx
instead (for older tooling / hardware wallets); the chain id must be
non-zero (EIP-155 replay protection is mandatory). `evm_sendTransaction`
accepts the same `type` switch.

```
$ raptoreum-cli evm_signTransaction 0x4646… \
    '{"nonce":"0x0","gas":"0x5208","maxFeePerGas":"0x64",
      "maxPriorityFeePerGas":"0x1","to":"0x35…35","value":"0x0","data":"0x"}'
0x02f864821ccf80016482520894…
```

### `evm_sendTransaction(privKey, callObject)`

Sign + submit in one call. Same call-object shape as above; returns
the Ethereum tx hash. Internally re-dispatches through
`eth_sendRawTransaction` so all consensus carve-outs +
cross-index registration apply uniformly.

### `evm_executeReadOnly(bytecode_hex, calldata_hex, gas_limit)`

Phase 0 developer probe — runs EVM bytecode against an empty world
state and returns `{ status_code, gas_used, return_data }`. Not
consensus-relevant; useful for opcode-level debugging.

---

## Value & asset bridges (`evo` namespace, wallet)

These build and submit the consensus special transactions that move value
across the UTXO ↔ EVM boundary. All require an unlocked wallet.

### `evm_fund(evmaddress, amount, fundaddress[, submit])`

Move RTM from the UTXO side into an EVM account (the AAL funding bridge).
Spends wallet UTXOs at `fundaddress` and credits `evmaddress` by `amount`
RTM. The amount leaves the UTXO money supply and reappears as EVM balance
(supply-conserving). `submit` defaults to `true`; pass `false` to get the
signed hex back instead of broadcasting.

```
$ raptoreum-cli evm_fund 0x000000000000000000000000000000000000000a 1.5 "RtmFundAddr.."
```

### `wrap_asset(assetid, amount, evmrecipient[, changeaddress][, submit])`

Wrap Smart Asset units into the EVM-side ERC-20 ledger (D4 mirror, UTXO →
EVM). Spends the wallet's UTXOs holding `assetid`, burns `amount` units from
the UTXO side, and credits them to the EVM account `evmrecipient` as the
asset's wrapped ERC-20 token. `changeaddress` (optional) receives the RTM
and asset change. See [`SMART-ASSET-MIRROR.md`](SMART-ASSET-MIRROR.md).

```
$ raptoreum-cli wrap_asset "GOLD" 10 0x000000000000000000000000000000000000aaaa
```

### `unwrap_asset(assetid, amount, evmsender, rtmrecipient, fundaddress[, submit])`

Unwrap from the EVM-side ledger back to the UTXO side (EVM → UTXO). Debits
`amount` wrapped units from `evmsender` and mints them back as ordinary
asset units to `rtmrecipient`; `fundaddress` supplies the RTM network fee.
The EVM-side debit is enforced at block connection — a shortfall rejects the
whole block, so the minted UTXO output can never stand without its EVM burn.

```
$ raptoreum-cli unwrap_asset "GOLD" 5 0x..aaaa "RtmRecipient.." "RtmFeeAddr.."
```

### `get_asset_evm_address(assetid)`

Return the deterministic EVM precompile address at which a Smart Asset is
callable as an ERC-20 (`0xA55E70…||hash160(assetId)[8:20]`). Add it to
MetaMask / a Solidity contract as the token's contract address. Read-only.

```
$ raptoreum-cli get_asset_evm_address "GOLD"
0xa55e700000000000d3e7a239ba904ee463beb255
```

---

## Notes

- **Authentication.** Both ports inherit the same `-rpcuser` /
  `-rpcpassword` config. Treat the 8545 listener as equally
  privileged.
- **Hex conventions.** Quantities follow the Ethereum spec
  (minimal-length, leading-zero-stripped: "0x0", "0xa", "0x10").
  Data is preserved at natural byte length ("0x" for empty).
- **Address bytes.** Addresses are 0x-prefixed 20-byte hex, no
  EIP-55 checksum requirement (we accept all-lowercase).
- **Conversion table** for CLI clients lives in
  `src/rpc/client.cpp`; if you add a method that takes a JSON
  object or boolean, register it there too.
