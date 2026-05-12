# RTM-native precompiles

Four Solidity-callable contracts at fixed/derived addresses that
surface Raptoreum subsystems no other L1 EVM has. These are the
**core differentiator** of the integration — the reason a dApp
would choose RTM over a generic EVM L1.

## Addresses

| Address | Precompile | Surface |
|---|---|---|
| `0x0000000000000000000000000000000000000a02` | LLMQ Oracle | Threshold-signed BLS messages |
| `0x0000000000000000000000000000000000000a03` | ChainLocks | 2-second finality probe |
| `0x0000000000000000000000000000000000000a04` | Masternode Registry | Composable governance / staking |
| `0xA55E700000000000_xxxxxxxxxxxxxxxx_xxxxxxxxxxxxxxxx` | Smart Asset ERC-20 | One per Smart Asset (suffix = hash160(assetId)) |

The reserved prefix `0xA55E70…` blocks user CREATE deployments per
the A11 rule.

Per-call gas cost (current calibration; FUP): 5000 weis for
ChainLocks / Masternodes / Smart Assets, 8000 weis for LLMQ Oracle
(heavier due to BLS verify).

---

## ChainLocks (0x…0a03)

```solidity
interface IChainLocks {
    function isChainLocked(uint32 height, bytes32 blockHash)
        external view returns (bool);
    function isTxInstantLocked(bytes32 txid)
        external view returns (bool);
    function latestChainLockedHeight()
        external view returns (uint32);
}
```

Selectors:

| Function | Selector |
|---|---|
| `isChainLocked(uint32,bytes32)` | `0x59d66e82` |
| `isTxInstantLocked(bytes32)` | `0x51c37046` |
| `latestChainLockedHeight()` | `0x2a433be9` |

Backed by `llmq::chainLocksHandler` and `llmq::quorumInstantSendManager`.
The latest-locked-height scan walks back up to 256 blocks from the
tip; refinement to track the exact locked-height water-line is a
follow-up.

**Use case.** Bridges, DEXes, and exchanges treat ChainLocked or
InstantLocked transactions as final in ~2 s instead of waiting
for probabilistic-finality confirmations. Saves user UX
substantially.

---

## Masternode Registry (0x…0a04)

```solidity
struct Masternode {
    bytes32 proTxHash;
    uint32  ip;             // IPv4 only for now; IPv6 is a follow-up
    uint16  port;
    bytes   pubKeyOperator; // BLS, 48 bytes
    uint64  collateralAmount;
    bool    isBanned;
}

interface IMasternodeRegistry {
    function getCount() external view returns (uint256);
    function isMasternode(bytes32 proTxHash)
        external view returns (bool);
    function getByProTxHash(bytes32 proTxHash)
        external view returns (Masternode memory);
    function getByIndex(uint256 index)
        external view returns (Masternode memory);
}
```

Selectors:

| Function | Selector |
|---|---|
| `getCount()` | `0xa87d942c` |
| `isMasternode(bytes32)` | `0x47aebab3` |
| `getByProTxHash(bytes32)` | `0x0402e163` |
| `getByIndex(uint256)` | `0x2d883a73` |

Backed by `deterministicMNManager->GetListAtChainTip()` under
`cs_main`. `getByIndex` walks the `immer::map` to find the Nth entry
(stable iteration order within a snapshot).

**Use case.** On-chain governance weighted by masternode collateral.
Treasury contracts that pay services to verified masternodes.
Delegated-staking primitives. Anything that benefits from the
deterministic ~4000-node registry being a vanilla view from
Solidity.

---

## LLMQ Oracle (0x…0a02)

```solidity
interface ILlmqOracle {
    // Returns true if a recovered (threshold-signed) signature
    // is on file for (llmqType, id).
    function hasSignature(uint8 llmqType, bytes32 id)
        external view returns (bool);

    // Returns (true, sig 96 bytes) if a recovered signature is
    // available; (false, "") otherwise.
    function getSignature(uint8 llmqType, bytes32 id)
        external view returns (bool, bytes memory);

    // Statically verifies a BLS threshold signature without
    // touching the signing manager's state. Useful for off-chain
    // verification flows and bridge-side gates.
    function verifySignature(uint8 llmqType, uint32 signedAtHeight,
                             bytes32 id, bytes32 msgHash,
                             bytes calldata signature)
        external view returns (bool);
}
```

Selectors:

| Function | Selector |
|---|---|
| `hasSignature(uint8,bytes32)` | `0x76f08249` |
| `getSignature(uint8,bytes32)` | `0x4e640cb2` |
| `verifySignature(uint8,uint32,bytes32,bytes32,bytes)` | `0x1805ce6b` |

LLMQType values (from `Consensus::LLMQType`):

| Value | LLMQ |
|---|---|
| `1` | `LLMQ_50_60` (InstantSend) |
| `2` | `LLMQ_400_60` |
| `3` | `LLMQ_400_85` (ChainLocks) |
| `4` | `LLMQ_100_67` |
| `100` | `LLMQ_5_60` (test) |
| `101` | `LLMQ_TEST_V17` (test) |

Unknown `llmqType` values are rejected explicitly so a dApp can't
silently probe a non-existent quorum and get false back.

Backed by `llmq::quorumSigningManager` (`HasRecoveredSigForId` /
`GetRecoveredSigForId`) plus the static
`CSigningManager::VerifyRecoveredSig`.

**No `requestSignature` exposed yet.** Triggering an async LLMQ
signing session writes signing state and isn't safe inside a
read-only EVM call. It needs the A7 gas-escrow + timeout pattern,
landing alongside Phase 5 wallet wiring.

**Use case.** The Chainlink-killer piece — feeds of prices signed
off-chain by quorum-elected masternodes, randomness verifiable by
the chain itself, attestations for cross-chain bridges. ~4000-node
threshold security replaces the ~30-node Chainlink decentralised
oracle network at zero relayer-trust cost.

---

## Smart Asset ERC-20 (0xA55E70…)

One precompile address per Smart Asset, derived deterministically:

```
precompile_address = 0xA55E700000000000 || hash160(assetId)[0..12]
```

The high 8 bytes spell `A55E70…` (the asset prefix) and reserve the
address range (per A11 collision rule). The low 12 bytes are
hash160(assetId) truncated to fit. Truncation collisions are
extraordinarily unlikely for the asset population sizes we expect;
a follow-up adds a longer suffix path.

```solidity
interface IRtmAsset {
    function name()                   external view returns (string memory);
    function symbol()                 external view returns (string memory);
    function decimals()               external view returns (uint8);
    function totalSupply()            external view returns (uint256);
    function balanceOf(address owner) external view returns (uint256);
}
```

Selectors:

| Function | Selector |
|---|---|
| `name()` | `0x06fdde03` |
| `symbol()` | `0x95d89b41` |
| `decimals()` | `0x313ce567` |
| `totalSupply()` | `0x18160ddd` |
| `balanceOf(address)` | `0x70a08231` |

Backed by `passetsCache->GetAssetMetaData()`. Resolution from the
precompile address to the `assetId` is a linear scan over the
current asset list — fine for the small asset populations of
regtest/testnet/early-mainnet. The hash160 → assetId index lives
in a follow-up.

**MVP scope: read-only.** `transfer`, `transferFrom`, `approve`,
`allowance` arrive together with the D4-revised bidirectional
Smart Asset ↔ EVM balance mirror (the "T-mirror" test gate). The
metadata reads are enough for MetaMask "Add Token" to work and
for ERC-20-aware dApps to display the asset before any transfers
cross the bridge.

**Use case.** Tokenisation of RWA / real-world assets. Smart Assets
on RTM cost ~$0.10 per creation (vs. $50–500 on Ethereum to deploy
an ERC-20). Pair them with `RaptorSwap` (Uniswap V2 fork) and you
get a one-click on-chain DEX listing for any tokenised asset.

---

## Calling precompiles from Solidity

For static-address precompiles (ChainLocks, MN Registry, LLMQ
Oracle):

```solidity
IChainLocks cl = IChainLocks(0x0000000000000000000000000000000000000a03);
bool finalised = cl.isTxInstantLocked(txid);
```

For Smart Asset ERC-20, address derivation lives in the dApp's
deployment script:

```solidity
function assetIdToErc20Address(string memory assetId)
    pure returns (address)
{
    bytes20 h = ripemd160(abi.encodePacked(sha256(bytes(assetId))));
    // Layout: 0xA55E700000000000_<12 low bytes of h>
    bytes memory suffix = new bytes(12);
    for (uint i = 0; i < 12; ++i) {
        suffix[i] = h[8 + i];
    }
    bytes memory addrBytes = abi.encodePacked(
        bytes8(0xA55E700000000000),
        suffix
    );
    return address(bytes20(addrBytes));
}
```

(The `@raptoreum/evm-sdk` package ships this helper for JS/TS.)

---

## Calling precompiles via `eth_call`

```
$ raptoreum-cli eth_call \
    '{"to":"0x0000000000000000000000000000000000000a04",
      "data":"0xa87d942c"}' latest
0x0000000000000000000000000000000000000000000000000000000000000000
# MN getCount() == 0 on regtest
```

For multi-arg functions, ABI-encode args manually or use `cast
calldata "fn(...)"` from foundry, or `web3.eth.abi.encodeFunctionCall`
from web3.js. The CLI accepts the resulting hex via the `data` field
of the call object.

---

## Gas costs

Current (calibration-pending) per-call gas charges:

| Precompile | Gas |
|---|---|
| ChainLocks | 5000 |
| Masternode Registry | 5000 |
| Smart Asset ERC-20 | 5000 |
| LLMQ Oracle | 8000 (BLS verify is heavier) |

These are flat per call regardless of which method. Real cost-model
calibration based on measured CPU time is a Phase 6 follow-up.
