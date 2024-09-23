Benchmarking
============

Raptoreum Core has an internal benchmarking framework, with benchmarks
for cryptographic algorithms such as SHA1, SHA256, SHA512 and RIPEMD160. As well as the rolling bloom filter.

Running
---------------------
After compiling Raptoreum Core, the benchmarks can be run with:

    src/bench/bench_raptoreum

The output will look similar to:
```
|        ns/byte |            byte/s | error % | benchmark
|---------------:|------------------:|--------:|:-----------------------------
|          64.13 |     15,592,356.01 |    0.1% | `Base58CheckEncode`
|          24.56 |     40,722,672.68 |    0.2% | `Base58Decode`
...
```
Help
---------------------
`-?` will print a list of options and exit:

    src/bench/bench_raptoreum -?

Notes
---------------------
More benchmarks are needed for, in no particular order:
- Script Validation
- CCoinDBView caching
- Coins database
- Memory pool
- Wallet coin selection
