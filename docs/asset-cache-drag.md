# The per-ATMP asset-cache copy

## The problem

`AcceptToMemoryPoolWorker` runs, for every transaction entering the mempool:

```cpp
CAssetsCache assetsCache = *passetsCache.get();
```

`CAssetsCache`'s copy constructor deep-copies three `std::map`s holding one entry per
confirmed asset, so the cost is O(confirmed assets) — several heap allocations per asset,
paid on every acceptance.

That copy has exactly one consumer: `CheckSpecialTx`. It returns at its first line for any
`nVersion != 3 || nType == TRANSACTION_NORMAL` — every ordinary payment — without
dereferencing the cache. Only `NEW_ASSET`, `UPDATE_ASSET` and `MINT_ASSET` ever read it.
So every payment pays an O(assets) copy it never uses.

The in-memory cache is capped at `MAX_CACHE_ASSETS_SIZE` (2500) by `LoadAssets` at startup
and grows from there over uptime (`Flush` adds without eviction). Any chain holding more
than 2500 assets puts every restarted node at that floor.

## Cost

Measured on one core of an x86-64 server CPU, `-O2`:

| in-memory assets | copy cost per transaction |
|---:|---:|
| 0 | ~25 ns |
| 1,000 | ~0.2 ms |
| 2,500 | ~1.2–3 ms |
| 10,000 | ~6 ms |

Mainnet's asset database currently holds 3,439 assets, so a restarted node sits at the
2,500 floor. There the copy alone caps single-thread mempool acceptance at a few hundred
transactions per second, for every transaction type — it is invisible today only because
mainnet load is far below that ceiling.

Two independent methods agree: a microbenchmark of the copy (`bench/assets_cache_copy.cpp`)
and a differential timing against a running node (`sendrawtransaction` for a transaction
that reaches the copy, minus one rejected before it). The exact figure varies with allocator
state — a freshly started process pays ~1.2 ms, a long-running one 2–3 ms.

## The fix

Construct the copy only for the three asset transaction types and pass `nullptr` otherwise.
`CheckSpecialTx` dereferences the pointer only for those types, so acceptance decisions are
unchanged for every transaction; the change only elides a computation that was never read.

Before/after on regtest at 2,500 in-memory assets, two builds differing only in this change,
measured with the differential above:

| normal payment | copy-path cost |
|---|---|
| before | 1.16 ms |
| after | 0.02 ms |

Payment acceptance becomes independent of the confirmed-asset count and of node uptime.
Asset transactions still receive the copy and are unaffected; `feature_assets.py` passes
against the patched build.
