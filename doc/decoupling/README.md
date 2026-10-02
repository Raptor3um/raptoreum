# Private transaction decoupling experiment

This branch adds optional reference-based block transport and an independently activated quorum script-certificate rule on regtest. Mainnet and testnet reject the experimental activation options. Read the [protocol specification](specification.md) for the exact consensus and resource boundaries.

## Two modes

| Configuration | Behavior |
|---|---|
| Default | Existing block, transaction and mining interfaces |
| `-regtest -txdecoupling=1` | Bounded transaction retention, negotiated reference transport and opt-in mining presentation; ordinary validation remains authoritative |
| Add `-txdecouplingheight=N` | Positive certificates can authorize the specified script checks from height N; all private-network nodes must share the activation height |

Delegated mode changes the trust model. A compromised signing threshold can authorize invalid scripts in an otherwise valid transaction. Proof of work still orders and commits the resulting block, but does not replace the omitted script checks. Amounts, input spending, maturity, sequence rules, special transactions, assets, sigops and block limits remain enforced. Smartnodes check scripts locally before voting.

Certificates bind a transaction to one parent block, its local prevouts and exact verification flags. They expire as authorization when the parent changes. Negative certificates are diagnostic and do not permanently reject transactions. An InstantSend lock and a script certificate serve different purposes.

## Retention and history

The mempool remains the single candidate/dependency graph. The buspool retains shared transaction bodies and certificates without removing/reinserting candidates. `getbuspoolentry` distinguishes a retained body, a candidate, local script validation and current reference eligibility.

Canonical full blocks remain stored on disk. Reference transport requires missing bodies to be supplied before normal block processing. Missing data is retryable; it does not make a header consensus-invalid. Cold sync and replay use committed public quorum history and full blocks, without requiring a buspool database or local signing shares. Delegated mode rejects pruning because its rule evaluation requires historical voting data. No snapshot trust model is introduced.

The existing 100,000-byte contextual transaction bound and 2,000,000-byte block bound remain. The experimental coinbase payload fits at most 41 certificates. Removing those bounds is outside this experiment.

## RPC workflow

The functional scenarios below set up the real quorum and required public commitments. For an already configured private network:

1. Call `requesttxvalidation "transaction_hex"` on quorum members. This validates locally and requests a vote; it does not admit the transaction. `status=abstain` means the required context was unavailable.
2. Query `gettxcertificate "txid"` on a participating member. Require `recovered=true` and `positive=true` before using its encoded proof.
3. Call `submitbuspooltransaction "transaction_hex" "certificate_hex"` to admit the body through the common admission rules. Negotiated peers can relay the same body/proof by certificate hash.
4. Request an opt-in template with `getblocktemplate '{"capabilities":["decoupled-v1"]}'`. If references are selected, the response adds `vtxids`, aligned `vtxidmetadata`, `workid` and `expires`.
5. Retrieve retained bodies with `getdecoupledblocktransactions "workid" '[1,2]'`, using the returned canonical indexes. Submit the versioned reference encoding with `submitdecoupledblock "encoded_block_hex" "workid"`, or submit the complete block through `submitblock`.

Indexes and dependencies always refer to the complete block, with coinbase at zero. A certificate manifest binds these positions; obey the returned `mutable` contract. The ordinary template omits candidates that have only delegated script provenance. Without selected references, even an opt-in call returns the ordinary schema.

Work leases retain complete bodies for up to 600 seconds, subject to eight-template/32-MiB limits. Polling an unchanged template returns the same `workid` and its remaining lifetime in `expires`. An expired workid can no longer retrieve its bodies. Submission can still reconstruct from current local sources; otherwise it returns `status=incomplete` with exact missing positions and IDs. A lease does not make a stale-parent block valid.

Fee ranking and `-blockmintxfee` continue to use transaction-body bytes. Manifest bytes count toward block and payload capacity as coinbase overhead.

## Resource limits

`-buspoolmaxcount` defaults to 10,000 and accepts 1..100,000. `-buspoolmaxbytes` defaults to 67,108,864 and accepts 1..1,073,741,824. Both require the experimental flag. Use `getbuspoolinfo` to inspect accounted retention and signing requests.

Peer reconstructions are bounded to two/8 MiB per peer and sixteen/64 MiB globally, including held bodies after cache eviction. Pending certificate announcements follow the buspool bounds, with at most one eighth per peer. `submitdecoupledblock` may use up to 64 MiB per call, enough for the most expensive valid block. Missing-body recovery uses the existing block download queue and full-block fallback. These are resource ceilings, not throughput promises.

## Running the checks

After building the node and installing the repository's functional-test prerequisites, run from the repository root:

```sh
python3 test/functional/feature_decoupled_mining.py
python3 test/functional/feature_llmq_txvalidation.py
python3 test/functional/p2p_decoupledblocks.py
python3 test/functional/p2p_compactblocks.py
```

The quorum scenario exercises real five-member, threshold-three DKG, opposite-vote persistence, mixed mining, lease expiry/eviction, delegated-script trust and historical replay. The P2P scenario exercises negotiation, missing/wrong bodies, quotas, fallback and certificate relay. Each command must complete successfully; reaching an intermediate stage is not a passing result.

Run `feature_decoupling_profile.py` alone for repeated ordinary/complex-payment and asset samples. It records offered/admitted/relayed/mined counts, bytes, stage times, resource snapshots and the executable hash. Its local RPC samples include polling and RPC overhead and do not establish network saturation throughput.

Add `--raptoreumd-arg=-txdecoupling=1` to enable transport on every node, or `--profile-transport-node=0` to enable it on one node only. The profile ends by asserting that all nodes share the best block, UTXO set and asset state, so the mixed form checks that enabled and ordinary nodes agree. Reference blocks are exchanged only when both peers enable transport.
