# Build + run + test guide

## Prerequisites

- Linux x86_64 (or a Docker container; see below)
- GCC 11+ with C++17
- The standard Raptoreum build deps (autotools, libtool, pkg-config,
  boost, BDB 4.8 or `--with-incompatible-bdb`, libevent, secp256k1
  submodule)
- `evmone` (shipped in `depends/`)

## File inventory (EVM module)

```
src/evm/
├── account.{h,cpp}              # CEvmAccount + canonical empties
├── apply.{h,cpp}                # ApplyEvm{Call,Deploy,Spend}Tx
├── balance.{h,cpp}              # uint256 BE arithmetic helpers
├── connectblock.{h,cpp}         # ProcessEvmTransactionsInBlock (block-level iterator)
├── evmtx.{h,cpp}                # CEvm{Call,Deploy,Spend}Tx payload structs + Check*
├── hashing.{h,cpp}              # Keccak256 + ContractAddressFromCreate{,2}
├── host.{h,cpp}                 # CEvmHost (evmc::Host) + nested CALL/CREATE
├── parallel.{h,cpp}             # ParallelProcessEvmTransactionsInBlock (D1 MVP)
├── precompiles.{h,cpp}          # Precompile dispatcher + ABI codec
├── precompile_chainlocks.cpp    # 0x...0a03
├── precompile_masternodes.cpp   # 0x...0a04
├── precompile_asset_erc20.cpp   # 0xA55E70...
├── precompile_llmq_oracle.cpp   # 0x...0a02
├── process.{h,cpp}              # ProcessEvm*Tx (EIP-1559 fee + nonce + balance)
├── rawtx.{h,cpp}                # DecodeRawEthTx + EthTxHash + sender recovery
├── receipt.{h,cpp}              # CEvmReceipt + CEvmLog
├── rlp.{h,cpp}                  # RLP encoder + canonical-form decoder
├── signing.{h,cpp}              # EvmAddressForKey + SignEip1559Tx
├── smoke.{h,cpp}                # Phase 0 EVM smoke
├── state_cache.{h,cpp}          # CEvmStateCache (dirty layer + snapshot)
├── state_db.{h,cpp}             # CEvmStateDB (LevelDB wrapper)
└── undo.{h,cpp}                 # CEvmStateUndo + Build/ApplyUndo (reorg journal)
```

```
src/rpc/ethereum.cpp             # All 25 evm/eth/net/web3 RPC handlers
src/rpc/client.cpp               # JSON conversion table updates
src/validation.cpp               # ConnectBlock/DisconnectTip wiring + EVM tx-type
                                 # carve-outs + receipt generation
src/consensus/tx_check.cpp       # vin/vout-empty carve-out for EVM tx types
src/chainparams.cpp              # UPDATE_EVM regtest activation @ height 0
src/httpserver.cpp               # Second listener for -evmrpcport
src/init.cpp                     # pevmstatedb lifecycle + -evmrpcport arg help
src/validation.h                 # CEvmStateDB extern + ConnectBlock/DisconnectBlock
                                 # signatures
```

```
src/test/
├── evm_smoke_tests.cpp          # Phase 0 (3 cases)
├── evm_evmtx_tests.cpp          # Phase 1 (13 cases)
├── evm_state_tests.cpp          # Phase 2.1 + 2.3d snapshot tests (18 cases)
├── evm_host_tests.cpp           # Phase 2.2 + 2.3d nested call tests (14 cases)
├── evm_apply_tests.cpp          # Phase 2.3a/b/c (14 cases)
├── evm_hashing_tests.cpp        # Phase 2.3b Keccak vectors (6 cases)
├── evm_process_tests.cpp        # Phase 2.4 fee accounting (10 cases)
├── evm_connectblock_tests.cpp   # Phase 2.4e block iterator (7 cases)
├── evm_undo_tests.cpp           # Phase 2.6 reorg journal (9 cases)
├── evm_parallel_tests.cpp       # Phase 2.5 worker-pool paired-with-serial (4 cases)
└── evm_rlp_tests.cpp            # Phase 3.5 RLP + envelope decode (12 cases)
```

## Build

### Native Linux

```
$ cd depends && make -j$(nproc) HOST=x86_64-pc-linux-gnu
$ cd ..
$ ./autogen.sh
$ CONFIG_SITE=$PWD/depends/x86_64-pc-linux-gnu/share/config.site \
    ./configure \
      --prefix=/ \
      --enable-debug \
      --enable-tests \
      --disable-bench \
      --without-gui \
      --disable-zmq \
      --enable-wallet \
      --with-incompatible-bdb \
      --with-natpmp
$ make -j$(nproc)
```

### Docker (the dev-loop the branch was built on)

```
$ docker run -dit --name rtm-builder \
    -v $(pwd):/repo \
    ubuntu:22.04 \
    bash -lc 'apt-get update && apt-get install -y \
              build-essential autotools-dev autoconf libtool \
              pkg-config bsdmainutils python3 \
              libssl-dev libevent-dev libboost-all-dev \
              libdb-dev libdb++-dev libminiupnpc-dev libnatpmp-dev \
              libzmq3-dev libqrencode-dev libgmp-dev ccache && \
              sleep infinity'
$ docker exec rtm-builder bash -lc 'cd /repo/depends && make -j$(nproc)'
$ docker exec rtm-builder bash -lc 'cd /repo && ./autogen.sh && \
    CONFIG_SITE=/repo/depends/x86_64-pc-linux-gnu/share/config.site \
    ./configure ... (same flags as above)'
$ docker exec rtm-builder bash -lc 'cd /repo/src && make -j$(nproc)'
```

Subsequent incremental builds are 1–3 minutes with ccache hot.

## Run a regtest node with EVM active

```
$ rm -rf /tmp/rtm-evm-test && mkdir -p /tmp/rtm-evm-test
$ src/raptoreumd \
    -regtest -daemon \
    -datadir=/tmp/rtm-evm-test \
    -rpcuser=u -rpcpassword=p -rpcport=29918 \
    -port=29917 -listen=0 -dnsseed=0 \
    -fallbackfee=0.001
$ CLI="src/raptoreum-cli -regtest -datadir=/tmp/rtm-evm-test -rpcuser=u -rpcpassword=p -rpcport=29918"
$ $CLI eth_chainId
0x1ccf
$ $CLI eth_blockNumber
0x0
```

The `-evmrpcport` defaults to 8545 — point MetaMask at
`http://localhost:8545` with chain id 7375 and you're connected.

UPDATE_EVM is activated at regtest height 0 via the chainparams
entry, so the full execution pipeline runs from the genesis block.
For mainnet/testnet builds, EVM is inert until a coordinated hard
fork (FUP-2.1 / Q-A5).

## Run the unit tests

```
$ src/test/test_raptoreum --run_test=evm_smoke_tests
$ src/test/test_raptoreum --run_test=evm_evmtx_tests
$ src/test/test_raptoreum --run_test=evm_state_tests
$ src/test/test_raptoreum --run_test=evm_host_tests
$ src/test/test_raptoreum --run_test=evm_apply_tests
$ src/test/test_raptoreum --run_test=evm_hashing_tests
$ src/test/test_raptoreum --run_test=evm_process_tests
$ src/test/test_raptoreum --run_test=evm_connectblock_tests
$ src/test/test_raptoreum --run_test=evm_undo_tests
$ src/test/test_raptoreum --run_test=evm_parallel_tests
$ src/test/test_raptoreum --run_test=evm_rlp_tests
```

All 11 suites should report `*** No errors detected`. Per-case
counts are in [`STATUS.md`](STATUS.md).

The non-EVM test suites continue to pass unchanged.

## Quick E2E smoke for the RPC namespace

Using the canonical test private key `0x4646…4646` and Python
`eth-account` for cross-verification:

```bash
$ pip install eth-account

# Derive the EVM address from the key
$ $CLI evm_keyToAddress 0x4646464646464646464646464646464646464646464646464646464646464646
0x9d8a62f656a8d1615c1294fd71e9cfb3e4855a4f

# Verify against eth-account
$ python3 -c "from eth_account import Account; \
    print(Account.from_key(0x4646464646464646464646464646464646464646464646464646464646464646).address.lower())"
0x9d8a62f656a8d1615c1294fd71e9cfb3e4855a4f   # matches

# Sign + submit a tx
$ $CLI evm_sendTransaction 0x4646...4646 \
    '{"nonce":"0x0","gas":"0x5208","maxFeePerGas":"0x64",
      "maxPriorityFeePerGas":"0x1",
      "to":"0x3535353535353535353535353535353535353535",
      "value":"0x0","data":"0x"}'
0x07b187941c7917d132a703ed1063b9278cf686bfb95be605dc6f4da40870346b

# It's in the mempool
$ $CLI getrawmempool
["5188ec3b3c6a8ebf57bea154700afdb5224548d875e12bb745ecfd1d25a1c016"]
```

## Open follow-ups for the build itself

See [`FUP.md`](FUP.md) — relevant items here are:

- **FUP-X.1.** Vendor evmone/intx/ethash under `depends/` for Guix
  deterministic builds.
- **FUP-X.2.** `#ifdef ENABLE_WALLET` guards so `--disable-wallet`
  builds cleanly with the EVM path.

Today: always pass `--enable-wallet`, and build evmone via the
working manual flow under `depends/`.
