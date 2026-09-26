# Transaction decoupling: private-network implementation specification

Status: implementation specification for an experimental regtest release. Public-network activation is not part of this change.

Base: `13b93da4cfe6502448657ddb8dd50a4b33467fae`. Issues #412–#416 and the supplied September discussion motivate the requirements below; their drafts do not constitute an approved consensus protocol.

## Deliverable and scope

Implement the complete path from an ordinary transaction through quorum prevalidation, a bounded buspool, a mixed mining template, a mined block represented by bodies and references, body recovery, canonical block validation/storage, and fresh replay. An additional regtest consensus experiment delegates script execution using certificates committed in the block. Merely adding a cache or a `vtxids` field does not complete the deliverable.

There are two explicit modes:

1. **Transport mode:** reference encoding and buspool retention preserve all current validation rules. Eligibility may use an existing InstantSend lock after local transaction validation. A reference or lock never exempts a transaction from current consensus checks.
2. **Delegated-script mode:** an explicitly activated regtest rule permits a certificate to replace script execution for narrowly eligible transactions. Every other contextual rule still runs. This introduces additional trust in the selected quorum. A threshold capable of certifying invalid scripts can make this mode accept them; the implementation and its tests must expose that distinction.

The experiment covers ordinary payments with confirmed, non-asset, non-future inputs. Special transactions, assets, future inputs, unconfirmed-parent transactions and any transaction lacking an eligible certificate continue through full validation. They remain supported in mixed blocks. A future expansion of delegated eligibility needs separate tests and rules; it is not silently inferred from ordinary-payment tests.

Do not increase transaction or block limits in this implementation. Removing a bound is a separate resource/consensus change, not a consequence of encoding bodies as references. In particular, `ContextualCheckTransaction` currently enforces **100,000 bytes with DIP0001 enabled**, in addition to the relaxed 1 MB check in `CheckTransaction`. The earlier preparation document described the 100 KB bound too narrowly as policy; this specification corrects that statement.

## Activation and compatibility

- Experimental generation, RPC extensions and P2P negotiation require `-txdecoupling=1` on regtest. Reject that option on other networks rather than silently enabling any subset.
- Delegated-script consensus additionally requires `-txdecouplingheight=N`, regtest only, default disabled. All nodes in the private network use the same activation height. The rule is a consensus parameter, not a per-request validation preference.
- Preserve the ordinary coinbase payload version 2. Reserve experimental payload version `0x8001` locally for a certificate manifest; accept it only when the regtest rule is active. Do not consume version 3, which another unmerged proposal already uses. This experiment does not reserve a public deployment/version number.
- Do not reuse `VERSIONBITS_TOP_BITS`. The 80-byte header remains unchanged; transport format is negotiated, and the experimental coinbase payload commits delegated validation evidence through the existing merkle root.
- Ordinary GBT and `submitblock` retain their interface. Peers without transport negotiation receive complete blocks or existing compact blocks.
- Transport-only blocks remain valid under existing rules. A block using the delegated consensus extension is intentionally incompatible with nodes lacking that rule; compatibility tests must not label that as an accidental transport regression.

## One candidate graph, two transaction sources

Retain `CTxMemPool` as the common candidate graph for dependencies, outpoint conflicts, CPFP, fees, sigops and special-transaction indexes. A buspool manager owns a separately bounded collection of transaction bodies, certificates, context and retention metadata. Bodies are shared through `CTransactionRef`; no second raw-body copy is required.

Promotion does not call mempool removal/reinsertion. Removal currently notifies InstantSend and can delete locks, as well as dismantling graph indexes. A candidate is classified as ordinary or bus-eligible by its current verified record. Expired certificates demote eligibility without turning retained bodies into candidates.

Admission from either network source runs the common admission rules. Transport mode always performs ordinary admission. Delegated mode may omit only its two script-execution checks after validating an eligible certificate against the local view. Record delegated provenance separately; never populate local script/ECDSA-success caches from a certificate. Signers always execute scripts locally, even when a certificate was received first.

On a tip change, certificates for another parent cease to authorize delegation. Before such a candidate is mined or relayed again, locally revalidate its scripts or obtain a certificate for the new parent. Invalid entries are removed through the existing conflict/dependency machinery. Retained immutable bodies can still serve reconstruction by txid, independently of candidate eligibility.

## Validation certificate

Use the existing DKG, signing-share distribution and recovered-signature engine. `AsyncSignIfMember` receives the selected quorum hash explicitly; signing eligibility is not transaction validation.

A version-1 certificate contains the transaction ID, parent block hash, ordered-prevout digest, exact consensus and standard-policy script flags, quorum type/hash, result and BLS signature. Use a new domain `rtm-txvalidation-v1` and the network genesis hash. Canonical serialization must determine:

```text
requestId = H(domain, genesisHash, parentHash, txid)
messageHash = H(version, parentHash, txid, prevoutsDigest,
                consensusFlags, policyFlags, result)
signatureHash = existing BuildSignHash(type, quorumHash, requestId, messageHash)
```

The receiver recomputes the prevout digest from its own coins view. Commit to each ordered outpoint and the complete effective coin: amount, script, height, coinbase and future/context fields. Do not trust peer-supplied prevout contents. A positive certificate is valid only for the named parent and its immediate successor under those exact flags.

Version 1 permits only the regtest quorum type `LLMQ_5_60` (100). The selection anchor is the exact parent's ancestor at `parent.height - 8`; no ancestor means no eligible quorum. Take the two most recently mined non-null commitments of that type at or before that anchor. Score each with `H(type, quorumHash, requestId)`, using existing `uint256` ordering; choose the smallest score, breaking a score tie by quorum hash. Fewer than two available commitments use the available set; an empty set means abstention. The quorum size/threshold remain the configured regtest DKG parameters and must be recorded in evidence.

Selection and verification require the public EvoDB state corresponding to the exact parent, as already required at block connection. Check that state before special-transaction processing mutates it, verify every commitment's mined block and quorum-base block are ancestors of the anchor, and use its committed public key directly. Do not use current-tip quorum caches, local recovered-signature history or retained private shares for historical acceptance. A caller with another branch's state must first establish the correct connected state; it cannot declare a certificate invalid from unrelated state. Test competing branches at equal height, alternative validly signed quorums and the disallowed second test type.

Certificate v1 freezes its policy flags as the explicit script-flag bit set in this base revision, not a future value of `STANDARD_SCRIPT_VERIFY_FLAGS`. Its consensus flags are those of the parent's immediate successor. Calculate them with the same rule helper used for block connection and compare them with the actual block's flags. Predict successor activation from the parent's ancestry without leaving temporary block-index pointers in update caches. For time-dependent BIP16 flags, require the parent's median time plus one already to be past the historical switch; otherwise omit delegation. Cover the V17 transition and reorg in tests.

The signer verifies transaction structure, contextual eligibility, inputs, amounts, finality/sequence constraints and both script-flag sets before a positive vote. Existing ATMP `fDryRun` returns before scripts and cannot be used for this purpose. A deterministic script/consensus failure can produce a negative result for the same statement; missing bodies, missing context or unavailable quorums mean abstention. A negative vote/certificate is diagnostic: it cannot permanently invalidate a transaction or block on peers, because full validation remains authoritative outside a positive delegated statement. Request-ID vote persistence prevents signing both results for one context.

Script validity is not an outpoint lock. Keep existing InstantSend conflict handling and local UTXO spending checks. Two otherwise valid transactions that spend the same output cannot both connect merely because each has a script certificate.

## Canonical commitment and verification

The experimental coinbase payload extends the existing height, smartnode root and quorum root with a sorted certificate manifest indexed by canonical transaction position. Position zero cannot be certified. Reject duplicates, out-of-range positions, ineligible transactions, mismatched txids/contexts and invalid signatures.

Preserve the existing 10 KB extra-payload bound. Include only certificates that fit the manifest; other candidates use ordinary script validation. Mining accounts for this payload before final block-size/fee selection and performs `TestBlockValidity` with the same rules used to connect the block.

At block connection:

```text
no certificate for position -> existing script checks
certificate present but invalid or negative -> reject the block
valid eligible positive certificate -> omit only that transaction's scripts
```

Do not disable the global `fScriptChecks`: it also controls special-transaction signature checks. Preserve structural/contextual validation, merkle/size/sigops limits, input existence and spending, amounts/fees, maturity, sequence/future restrictions, asset/special rules, miner/founder/smartnode payments and state updates. `fJustCheck` does not exempt certificates.

Persist the full reconstructed `CBlock`, including the certificate-bearing coinbase. Existing block/undo/index storage remains canonical. A new node and `-reindex` must verify the same certificates from public historical commitments, without previously participating in the buspool. No local cache is an authority for historical acceptance.

## Mixed block encoding

Introduce an explicitly versioned `CDecoupledBlock` transport/RPC object. Keep `CBlock` serialization unchanged.

```text
version: uint16, exactly 1
header: ordinary CBlockHeader
vtx: full transaction bodies in their canonical relative order
vtxids: sorted (uint16 canonicalIndex, full txid) pairs
```

Bodies fill positions not named by `vtxids`. Total count equals the sum of both vectors. Coinbase is always a full body at position zero. Reject duplicate/out-of-range positions, duplicate references, reference/body collisions and a missing coinbase. Preserve parent-before-child ordering and all full-block validation.

This encoding supports at most 65,535 positions; use ordinary full-block transport otherwise. Validate vector counts before allocation, and reconstructed bytes before acceptance. Encoding limits are not an increase in consensus limits.

Reconstruction resolves bodies from the common graph, buspool retention and the existing compact-block extra cache. Any body with the exact requested txid can supply data; it need not satisfy mempool policy. Check every supplied body against the expected position and txid. Hold shared references while reconstructing so cache eviction cannot invalidate live work. Produce `CBlock` with `fChecked=false` and use its actual parent context.

## Peer protocol and availability

Negotiate `senddblock(announce, version=1)` only between enabled experimental peers. Add a distinct inventory/getdata type and `dblock` response. Reuse existing header admission, block availability, in-flight ownership, timeouts and full-block fallback. Do not create a second independent block-download scheduler.

Use `GETBLOCKTXN`/`BLOCKTXN` for missing canonical positions. A response must satisfy expected txids before completion. Failed encoding, wrong bodies, missing data, timeouts and local quota eviction trigger fallback or remain pending; none permanently marks the header consensus-invalid. A complete reconstructed block alone enters `ProcessNewBlock`.

Certificate/body announcements have their own negotiated message/inventory identity, keyed by the certificate hash so a new-parent certificate is not confused with an older one. Bound pending announcements by the buspool budget and rate-limit expensive repeated invalid proofs using the existing peer misbehavior/request mechanisms. Legacy peers receive ordinary transactions.

Keep historical download/IBD on full canonical blocks. When all sources withhold a body, report an incomplete block and preserve bounded progress; no certificate can manufacture availability.

## Mining RPC contract

GBT opt-in capability is `decoupled-v1`. Without it, or when the selected template has no references, return the existing schema and all transaction bodies.

For a mixed template, return ordinary bodies in `transactions`, full buspool IDs in `vtxids`, and corresponding `vtxidmetadata` with canonical `index`, `depends`, fee, special fee, sigops and serialized size. All indexes/dependencies use the complete block order, coinbase zero. Never reinterpret dependency indexes as positions in a reduced vector.

An experimental response containing a certificate manifest must not advertise transaction-list mutation: its `mutable` contract permits only changes that preserve canonical transaction positions and the manifest (for example permitted time/nonce and coinbase extranonce changes). Removing or reordering a certified position requires rebuilding the manifest and revalidating, not merely changing the merkle root. Without opt-in, return complete bodies and the ordinary schema, but include a candidate admitted through delegation only after local script validation; otherwise omit it from that ordinary template.

The block assembler selects the shared graph and first produces a complete valid template. Classification, certification and payload capacity determine its reference presentation. Promotions/demotions update the existing template-change counter so caching/longpoll cannot serve stale eligibility.

Keep existing fee-rate ranking and `-blockmintxfee` calculations based on transaction-body bytes. Certificate manifests remain coinbase overhead for this policy; every manifest byte still counts toward the block and coinbase-payload limits. Delegation changes neither transaction fees nor special fees.

Retain complete templates under a bounded `workid` for 600 seconds. Return `workid` and `expires` only in the opt-in mixed response. Provide an explicit RPC to retrieve bodies by `workid` and canonical indexes.

`submitdecoupledblock(hex, workid)` reconstructs using retained templates and current body sources, then shares the normal full-block processing path. Missing data returns an explicit incomplete result with positions and txids, never success and never permanent block invalidity. A complete `submitblock` remains accepted through its existing path.

## Resource bounds and lifecycle

Initial experimental bounds, not throughput promises:

| Resource | Bound |
|---|---|
| Buspool records | 10,000 by default, plus 64 MiB including retained bodies/certificates/indexes |
| Pending reconstructions | Two per peer, sixteen globally |
| Reconstruction memory | 8 MiB per peer, 64 MiB globally |
| Retained templates | Eight, 32 MiB globally, 600-second expiry |
| Wire messages | Existing maximum and bounded element counts; full-block fallback when reference encoding does not fit |

Configuration must reject negative, zero where unusable, overflowing and unreasonably large values. Account for held references after eviction; do not hide memory in a lease or pending queue. Clear candidate eligibility on incompatible tip/rule context. ChainLock/mining cleanup may release acceleration data only after canonical block storage supplies history; it must not destroy required recovery data.

No persistent buspool database is required initially. Restart reconstructs its optimization state through normal admission/certificates while replay uses canonical blocks. This avoids inventing a second authoritative transaction store.

## Verification and completion

The implementation is complete only when the following evidence exists:

- A full, immutable preparatory baseline; final component/unit and functional results with exact binaries and failures retained.
- A runnable profile of ordinary/complex payments and assets, measuring admission, relay, mining/connection, bytes and resources separately. Repeated measurements record distribution and configuration; no conversion of isolated cache copies into TPS.
- Real smartnodes form DKG and positive/negative/abstention paths; no mocked certificate substitutes for the complete positive workflow.
- Mixed GBT, body retrieval, mined submission, reference P2P reconstruction and canonical replay work end to end. Legacy transport remains interoperable in transport mode.
- Independent corruption of every signed field, malformed encodings, spent/overspending/immature/sequence-invalid inputs, assets and excessive sigops are rejected appropriately. Certificate trust cannot waive those rules.
- Cold nodes, missing/withheld bodies, full fallback, quota eviction, template leases, restart, `-reindex`, `-reindex-chainstate`, rotation and reorg across activation reproduce the specified decisions.
- A controlled compromised-threshold test demonstrates the additional script trust in delegated mode. It must not be presented as equivalent security to full validation.
- Public networks reject the experimental options/payload; public deployment parameters stay unchanged.
- Independent source review checks correctness, lock/lifetime/resource behavior and test assertions. Every material finding has a fix and covering test or an explicitly unresolved status; unresolved acceptance defects prevent completion.

Native platforms, WAN emulation and public activation are recorded individually as tested or unavailable. Finite testing does not establish absence of all defects. The final report must distinguish implementation completion, measured environments and the separate decision to deploy a new consensus rule.
