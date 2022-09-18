# Copyright (c) 2013-2016 The Bitcoin Core developers
# Copyright (c) 2014-2018 The Dash Core developers
# Copyright (c) 2020-2022 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

DIST_SUBDIRS = secp256k1

AM_LDFLAGS = $(LIBTOOL_LDFLAGS) $(HARDENED_LDFLAGS) $(GPROF_LDFLAGS) $(SANITIZER_LDFLAGS) $(LTO_LDFLAGS) $(CORE_LDFLAGS)
AM_CXXFLAGS = $(DEBUG_CXXFLAGS) $(HARDENED_CXXFLAGS) $(WARN_CXXFLAGS) $(NOWARN_CXXFLAGS) $(ERROR_CXXFLAGS) $(GPROF_CXXFLAGS) $(SANITIZER_CXXFLAGS) $(LTO_CXXFLAGS) $(CORE_CXXFLAGS)
AM_CPPFLAGS = $(DEBUG_CPPFLAGS) $(HARDENED_CPPFLAGS) $(CORE_CPPFLAGS)
AM_LIBTOOLFLAGS = --preserve-dup-deps
PTHREAD_FLAGS = $(PTHREAD_CFLAGS) $(PTHREAD_LIBS)
EXTRA_LIBRARIES =

if ENABLE_STACKTRACES
if ENABLE_CRASH_HOOKS
if CRASH_HOOKS_WRAPPED_CXX_ABI
LDFLAGS_WRAP_EXCEPTIONS = -Wl,-wrap,__cxa_allocate_exception -Wl,-wrap,__cxa_free_exception
if TARGET_WINDOWS
LDFLAGS_WRAP_EXCEPTIONS += -Wl,-wrap,_assert -Wl,-wrap,_wassert
else
LDFLAGS_WRAP_EXCEPTIONS += -Wl,-wrap,__assert_fail
endif
endif
endif

if TARGET_WINDOWS
BACKTRACE_LIB = -ldbghelp -lbacktrace
else
BACKTRACE_LIB = -lbacktrace
endif
endif #ENABLE_STACKTRACES

BITCOIN_INCLUDES=-I$(builddir) -I$(srcdir)/secp256k1/include -I$(srcdir)/$(UNIVALUE_INCLUDE_DIR_INT) $(BDB_CPPFLAGS) $(BOOST_CPPFLAGS) $(LEVELDB_CPPFLAGS)

BLS_LIBS=-lbls-dash -lrelic_s

LIBBITCOIN_SERVER=libraptoreum_server.a
LIBBITCOIN_COMMON=libraptoreum_common.a
LIBBITCOIN_CONSENSUS=libraptoreum_consensus.a
LIBBITCOIN_CLI=libraptoreum_cli.a
LIBBITCOIN_UTIL=libraptoreum_util.a
LIBBITCOIN_CRYPTO=crypto/libraptoreum_crypto.a
LIBBITCOINQT=qt/libraptoreumqt.a
LIBSECP256K1=secp256k1/libsecp256k1.la

if ENABLE_ZMQ
LIBBITCOIN_ZMQ=libraptoreum_zmq.a
endif
if BUILD_BITCOIN_LIBS
LIBBITCOINCONSENSUS=libraptoreumconsensus.la
endif
if ENABLE_WALLET
LIBBITCOIN_WALLET=libraptoreum_wallet.a
LIBBITCOIN_WALLET_TOOL=libraptoreum_wallet_tool.a
endif

if ENABLE_SSE41
LIBBITCOIN_CRYPTO_SSE41 = crypto/libraptoreum_crypto_sse41.a
LIBBITCOIN_CRYPTO += $(LIBBITCOIN_CRYPTO_SSE41)
endif
if ENABLE_AVX2
LIBBITCOIN_CRYPTO_AVX2 = crypto/libraptoreum_crypto_avx2.a
LIBBITCOIN_CRYPTO += $(LIBBITCOIN_CRYPTO_AVX2)
endif
if ENABLE_X86_SHANI
LIBBITCOIN_CRYPTO_X86_SHANI = crypto/libraptoreum_crypto_x86_shani.a
LIBBITCOIN_CRYPTO += $(LIBBITCOIN_CRYPTO_X86_SHANI)
endif
if ENABLE_ARM_SHANI
LIBBITCOIN_CRYPTO_ARM_SHANI = crypto/libraptoreum_crypto_arm_shani.a
LIBBITCOIN_CRYPTO += $(LIBBITCOIN_CRYPTO_ARM_SHANI)
endif

$(LIBSECP256K1): $(wildcard secp256k1/src/*.h) $(wildcard secp256k1/src/*.c) $(wildcard secp256k1/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F)

# Make is not made aware of per-object dependencies to avoid limiting building parallelization
# But to build the less dependent modules first, we manually select their order here:
EXTRA_LIBRARIES += \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_COMMON) \
  $(LIBBITCOIN_CONSENSUS) \
  $(LIBBITCOIN_SERVER) \
  $(LIBBITCOIN_CLI) \
  $(LIBBITCOIN_WALLET) \
  $(LIBBITCOIN_WALLET_TOOL) \
  $(LIBBITCOIN_ZMQ)

lib_LTLIBRARIES = $(LIBBITCOINCONSENSUS)
noinst_LTLIBRARIES =

bin_PROGRAMS =
noinst_PROGRAMS =
TESTS =
BENCHMARKS =

if BUILD_BITCOIND
  bin_PROGRAMS += raptoreumd
endif

if BUILD_BITCOIN_CLI
  bin_PROGRAMS += raptoreum-cli
endif

if BUILD_BITCOIN_TX
  bin_PROGRAMS += raptoreum-tx
endif
if ENABLE_WALLET
if BUILD_BITCOIN_WALLET
  bin_PROGRAMS += raptoreum-wallet
endif
endif

.PHONY: FORCE check-symbols check-security
# raptoreum core #
BITCOIN_CORE_H = \
  addrdb.h \
  addressindex.h \
  addrman.h \
  attributes.h \
  banman.h \
  base58.h \
  batchedlogger.h \
  bech32.h \
  bip39.h \
  bip39_english.h \
  blockencodings.h \
  bloom.h \
  cachemap.h \
  cachemultimap.h \
  blockfilter.h \
  chain.h \
  chainparams.h \
  chainparamsbase.h \
  chainparamsseeds.h \
  checkpoints.h \
  checkqueue.h \
  clientversion.h \
  coinjoin/coinjoin.h \
  coinjoin/coinjoin-client.h \
  coinjoin/coinjoin-client-options.h \
  coinjoin/coinjoin-server.h \
  coinjoin/coinjoin-util.h \
  coins.h \
  compat.h \
  compat/assumptions.h \
  compat/byteswap.h \
  compat/cpuid.h \
  compat/endian.h \
  compat/sanity.h \
  compressor.h \
  consensus/consensus.h \
  consensus/tx_check.h \
  consensus/tx_verify.h \
  core_io.h \
  core_memusage.h \
  cuckoocache.h \
  ctpl_stl.h \
  cxxtimer.hpp \
  evo/cbtx.h \
  evo/deterministicmns.h \
  evo/evodb.h \
  evo/mnauth.h \
  evo/providertx.h \
  evo/simplifiedmns.h \
  evo/specialtx.h \
  dsnotificationinterface.h \
  governance/governance.h \
  governance/governance-classes.h \
  governance/governance-exceptions.h \
  governance/governance-object.h \
  governance/governance-validators.h \
  governance/governance-vote.h \
  governance/governance-votedb.h \
  future/fee.h \
  future/utils.h \
  flat-database.h \
  hdchain.h \
  flatfile.h \
  fs.h \
  httprpc.h \
  httpserver.h \
  indices/spent_index.h \
  indices/future_index.h \
  index/base.h \
  index/disktxpos.h \
  index/txindex.h \
  indirectmap.h \
  init.h \
  interfaces/chain.h \
  interfaces/handler.h \
  interfaces/node.h \
  interfaces/wallet.h \
  key.h \
  key_io.h \
  keystore.h \
  dbwrapper.h \
  limitedmap.h \
  llmq/quorums.h \
  llmq/quorums_blockprocessor.h \
  llmq/quorums_commitment.h \
  llmq/quorums_chainlocks.h \
  llmq/quorums_debug.h \
  llmq/quorums_dkgsessionhandler.h \
  llmq/quorums_dkgsessionmgr.h \
  llmq/quorums_dkgsession.h \
  llmq/quorums_init.h \
  llmq/quorums_instantsend.h \
  llmq/quorums_signing.h \
  llmq/quorums_signing_shares.h \
  llmq/quorums_utils.h \
  llmq/quorums_parameters.h \
  logging.h \
  logging_timer.h \
  smartnode/activesmartnode.h \
  smartnode/smartnode-meta.h \
  smartnode/smartnode-payments.h \
  smartnode/smartnode-sync.h \
  smartnode/smartnode-utils.h \
  smartnode/smartnode-collaterals.h \
  mapport.h \
  memusage.h \
  merkleblock.h \
  messagesigner.h \
  miner.h \
  net.h \
  net_processing.h \
  netaddress.h \
  netbase.h \
  netfulfilledman.h \
  netmessagemaker.h \
  node/coin.h \
  node/coinstats.h \
  node/context.h \
  node/transaction.h \
  node/utxo_snapshot.h \
  noui.h \
  optional.h \
  policy/feerate.h \
  policy/fees.h \
  policy/policy.h \
  policy/settings.h \
  pow.h \
  protocol.h \
  random.h \
  randomenv.h \
  reverse_iterator.h \
  rpc/blockchain.h \
  rpc/client.h \
  rpc/mining.h \
  rpc/protocol.h \
  rpc/rawtransaction_util.h \
  rpc/register.h \
  rpc/request.h \
  rpc/server.h \
  rpc/util.h \
  rpc/specialtx_utilities.h \
  saltedhasher.h \
  scheduler.h \
  script/descriptor.h \
  script/sigcache.h \
  script/sign.h \
  script/standard.h \
  shutdown.h \
  spork.h \
  stacktraces.h \
  streams.h \
  statsd_client.h \
  support/allocators/mt_pooled_secure.h \
  support/allocators/pooled_secure.h \
  support/allocators/secure.h \
  support/allocators/zeroafterfree.h \
  support/cleanse.h \
  support/events.h \
  support/lockedpool.h \
  sync.h \
  threadsafety.h \
  threadinterrupt.h \
  timedata.h \
  torcontrol.h \
  txdb.h \
  txmempool.h \
  ui_interface.h \
  undo.h \
  unordered_lru_cache.h \
  util/bip32.h \
  util/bytevectorhash.h \
  util/check.h \
  util/error.h \
  util/fees.h \
  util/spanparsing.h \
  util/system.h \
  util/asmap.h \
  util/macros.h \
  util/memory.h \
  util/moneystr.h \
  util/ranges.h \
  util/ref.h \
  util/time.h \
  util/string.h \
  util/threadnames.h \
  util/vector.h \
  util/url.h \
  util/validation.h \
  validation.h \
  validationinterface.h \
  versionbits.h \
  versionbitsinfo.h \
  walletinitinterface.h \
  wallet/bdb.h \
  wallet/coincontrol.h \
  wallet/context.h \
  wallet/crypter.h \
  wallet/db.h \
  wallet/fees.h \
  wallet/ismine.h \
  wallet/load.h \
  wallet/rpcwallet.h \
  wallet/salvage.h \
  wallet/wallet.h \
  wallet/walletdb.h \
  wallet/wallettool.h \
  wallet/walletutil.h \
  wallet/coinselection.h \
  warnings.h \
  zmq/zmqabstractnotifier.h \
  zmq/zmqconfig.h\
  zmq/zmqnotificationinterface.h \
  zmq/zmqpublishnotifier.h \
  zmq/zmqrpc.h


obj/build.h: FORCE
	@$(MKDIR_P) $(builddir)/obj
	@$(top_srcdir)/share/genbuild.sh "$(abs_top_builddir)/src/obj/build.h" \
	  "$(abs_top_srcdir)"
libraptoreum_util_a-clientversion.$(OBJEXT): obj/build.h

# server: shared between raptoreumd and raptoreum-qt
# Contains code accessing mempool and chain state that is meant to be separated
# from wallet and GUI code (see node/README.md). Shared code should go in
# libraptoreum_common or libraptoreum_util libraries instead.
libraptoreum_server_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(MINIUPNPC_CPPFLAGS) $(NATPMP_CPPFLAGS) $(EVENT_CFLAGS) $(EVENT_PTHREADS_CFLAGS)
libraptoreum_server_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libraptoreum_server_a_SOURCES = \
  addrdb.cpp \
  addrman.cpp \
  banman.cpp \
  batchedlogger.cpp \
  blockencodings.cpp \
  blockfilter.cpp \
  chain.cpp \
  checkpoints.cpp \
  coinjoin/coinjoin.cpp \
  coinjoin/coinjoin-client-options.cpp\
  coinjoin/coinjoin-server.cpp \
  consensus/tx_verify.cpp \
  dbwrapper.cpp \
  dsnotificationinterface.cpp \
  evo/cbtx.cpp \
  evo/deterministicmns.cpp \
  evo/evodb.cpp \
  evo/mnauth.cpp \
  evo/providertx.cpp \
  evo/simplifiedmns.cpp \
  evo/specialtx.cpp \
  flatfile.cpp \
  httprpc.cpp \
  httpserver.cpp \
  interfaces/chain.cpp \
  interfaces/node.cpp \
  index/base.cpp \
  index/txindex.cpp \
  init.cpp \
  mapport.cpp \
  governance/governance.cpp \
  governance/governance-classes.cpp \
  governance/governance-object.cpp \
  governance/governance-validators.cpp \
  governance/governance-vote.cpp \
  governance/governance-votedb.cpp \
  future/fee.cpp \
  llmq/quorums.cpp \
  llmq/quorums_blockprocessor.cpp \
  llmq/quorums_commitment.cpp \
  llmq/quorums_chainlocks.cpp \
  llmq/quorums_debug.cpp \
  llmq/quorums_dkgsessionhandler.cpp \
  llmq/quorums_dkgsessionmgr.cpp \
  llmq/quorums_dkgsession.cpp \
  llmq/quorums_init.cpp \
  llmq/quorums_instantsend.cpp \
  llmq/quorums_signing.cpp \
  llmq/quorums_signing_shares.cpp \
  llmq/quorums_utils.cpp \
  smartnode/activesmartnode.cpp \
  smartnode/smartnode-meta.cpp \
  smartnode/smartnode-payments.cpp \
  smartnode/smartnode-sync.cpp \
  smartnode/smartnode-utils.cpp \
  messagesigner.cpp \
  miner.cpp \
  net.cpp \
  netfulfilledman.cpp \
  net_processing.cpp \
  node/coin.cpp \
  node/coinstats.cpp \
  node/context.cpp \
  node/transaction.cpp \
  ui_interface.cpp \
  noui.cpp \
  policy/fees.cpp \
  policy/policy.cpp \
  policy/settings.cpp \
  pow.cpp \
  rest.cpp \
  rpc/blockchain.cpp \
  rpc/smartnode.cpp \
  rpc/governance.cpp \
  rpc/mining.cpp \
  rpc/misc.cpp \
  rpc/net.cpp \
  rpc/rawtransaction.cpp \
  rpc/rpcevo.cpp \
  rpc/rpcquorums.cpp \
  rpc/server.cpp \
  rpc/coinjoin.cpp \
  script/sigcache.cpp \
  shutdown.cpp \
  spork.cpp \
  statsd_client.cpp \
  timedata.cpp \
  torcontrol.cpp \
  txdb.cpp \
  txmempool.cpp \
  ui_interface.cpp \
  validation.cpp \
  validationinterface.cpp \
  versionbits.cpp \
  $(BITCOIN_CORE_H)

if ENABLE_WALLET
libraptoreum_server_a_SOURCES += wallet/init.cpp
endif
if !ENABLE_WALLET
libraptoreum_server_a_SOURCES += dummywallet.cpp
endif

if ENABLE_ZMQ
libraptoreum_zmq_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(ZMQ_CFLAGS)
libraptoreum_zmq_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libraptoreum_zmq_a_SOURCES = \
  zmq/zmqabstractnotifier.cpp \
  zmq/zmqnotificationinterface.cpp \
  zmq/zmqpublishnotifier.cpp \
  zmq/zmqrpc.cpp
endif


# wallet: shared between raptoreumd and raptoreum-qt, but only linked
# when wallet enabled
libraptoreum_wallet_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libraptoreum_wallet_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libraptoreum_wallet_a_SOURCES = \
  coinjoin/coinjoin-client.cpp \
  coinjoin/coinjoin-client-options.cpp \
  coinjoin/coinjoin-util.cpp \
  interfaces/wallet.cpp \
  wallet/bdb.cpp \
  wallet/coincontrol.cpp \
  wallet/context.cpp \
  wallet/crypter.cpp \
  wallet/db.cpp \
  wallet/fees.cpp \
  wallet/ismine.cpp \
  wallet/load.cpp \
  wallet/rpcdump.cpp \
  wallet/rpcwallet.cpp \
  wallet/salvage.cpp \
  wallet/wallet.cpp \
  wallet/walletdb.cpp \
  wallet/walletutil.cpp \
  wallet/coinselection.cpp \
  $(BITCOIN_CORE_H)

libraptoreum_wallet_tool_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libraptoreum_wallet_tool_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libraptoreum_wallet_tool_a_SOURCES = \
  wallet/wallettool.cpp \
  $(BITCOIN_CORE_H)

# crypto primitives library
crypto_libraptoreum_crypto_a_CPPFLAGS = $(AM_CPPFLAGS) $(PIC_FLAGS)
crypto_libraptoreum_crypto_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS) $(PIC_FLAGS)
crypto_libraptoreum_crypto_a_SOURCES = \
  crypto/aes.cpp \
  crypto/aes.h \
  crypto/chacha_poly_aead.h \
  crypto/chacha_poly_aead.cpp \
  crypto/chacha20.h \
  crypto/chacha20.cpp \
  crypto/common.h \
  crypto/hmac_sha256.cpp \
  crypto/hmac_sha256.h \
  crypto/hmac_sha512.cpp \
  crypto/hmac_sha512.h \
  crypto/poly1305.h \
  crypto/poly1305.cpp \
  crypto/pkcs5_pbkdf2_hmac_sha512.cpp \
  crypto/pkcs5_pbkdf2_hmac_sha512.h \
  crypto/ripemd160.cpp \
  crypto/aes_helper.c \
  crypto/blake.c \
  crypto/bmw.c \
  crypto/cubehash.c \
  crypto/echo.c \
  crypto/groestl.c \
  crypto/jh.c \
  crypto/keccak.c \
  crypto/luffa.c \
  crypto/shavite.c \
  crypto/simd.c \
  crypto/skein.c \
  crypto/sph_hamsi.c \
  crypto/sph_hamsi_helper.c \
  crypto/sph_fugue.c \
  crypto/sph_shabal.c \
  crypto/sph_whirlpool.c \
  crypto/sph_sha2.c \
  crypto/sph_sha512.c \
  crypto/ripemd160.h \
  crypto/sha1.cpp \
  crypto/sha1.h \
  crypto/sha256.cpp \
  crypto/sha256.h \
  crypto/sph_blake.h \
  crypto/sph_bmw.h \
  crypto/sph_cubehash.h \
  crypto/sph_echo.h \
  crypto/sph_groestl.h \
  crypto/sph_jh.h \
  crypto/sph_keccak.h \
  crypto/sph_luffa.h \
  crypto/sph_shavite.h \
  crypto/sph_simd.h \
  crypto/sph_skein.h \
  crypto/sph_hamsi.h \
  crypto/sph_fugue.h \
  crypto/sph_shabal.h \
  crypto/sph_whirlpool.h \
  crypto/sph_sha2.h \
  crypto/sph_types.h \
  crypto/sha512.cpp \
  crypto/sha512.h

if USE_ASM
crypto_libraptoreum_crypto_a_SOURCES += crypto/sha256_sse4.cpp
endif

crypto_libraptoreum_crypto_sse41_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
crypto_libraptoreum_crypto_sse41_a_CPPFLAGS = $(AM_CPPFLAGS)
crypto_libraptoreum_crypto_sse41_a_CXXFLAGS += $(SSE41_CXXFLAGS)
crypto_libraptoreum_crypto_sse41_a_CPPFLAGS += -DENABLE_SSE41
crypto_libraptoreum_crypto_sse41_a_SOURCES = crypto/sha256_sse41.cpp

crypto_libraptoreum_crypto_avx2_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
crypto_libraptoreum_crypto_avx2_a_CPPFLAGS = $(AM_CPPFLAGS)
crypto_libraptoreum_crypto_avx2_a_CXXFLAGS += $(AVX2_CXXFLAGS)
crypto_libraptoreum_crypto_avx2_a_CPPFLAGS += -DENABLE_AVX2
crypto_libraptoreum_crypto_avx2_a_SOURCES = crypto/sha256_avx2.cpp

crypto_libraptoreum_crypto_a_SOURCES += \
  cryptonote/aesb.c \
  cryptonote/c_blake256.c \
  cryptonote/c_groestl.c \
  cryptonote/c_jh.c \
  cryptonote/c_keccak.c \
  cryptonote/c_skein.c \
  cryptonote/oaes_lib.c \
  cryptonote/hash-ops.c \
  cryptonote/slow-hash.c \
  cryptonote/slow-hash.h \
  cryptonote/wild_keccak.cpp \
  cryptonote/c_blake256.h \
  cryptonote/c_groestl.h \
  cryptonote/c_jh.h \
  cryptonote/c_keccak.h \
  cryptonote/crypto.h \
  cryptonote/c_skein.h \
  cryptonote/groestl_tables.h \
  cryptonote/int-util.h \
  cryptonote/oaes_config.h \
  cryptonote/hash-ops.h \
  cryptonote/oaes_lib.h \
  cryptonote/skein_port.h \
  cryptonote/variant2_int_sqrt.h \
  cryptonote/wild_keccak.h \
  cryptonote/warnings.h

crypto_libraptoreum_crypto_x86_shani_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
crypto_libraptoreum_crypto_x86_shani_a_CPPFLAGS = $(AM_CPPFLAGS)
crypto_libraptoreum_crypto_x86_shani_a_CXXFLAGS += $(X86_SHANI_CXXFLAGS)
crypto_libraptoreum_crypto_x86_shani_a_CPPFLAGS += -DENABLE_X86_SHANI
crypto_libraptoreum_crypto_x86_shani_a_SOURCES = crypto/sha256_x86_shani.cpp

crypto_libraptoreum_crypto_arm_shani_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
crypto_libraptoreum_crypto_arm_shani_a_CPPFLAGS = $(AM_CPPFLAGS)
crypto_libraptoreum_crypto_arm_shani_a_CXXFLAGS += $(ARM_SHANI_CXXFLAGS)
crypto_libraptoreum_crypto_arm_shani_a_CPPFLAGS += -DENABLE_ARM_SHANI
crypto_libraptoreum_crypto_arm_shani_a_SOURCES = crypto/sha256_arm_shani.cpp

# consensus: shared between all executables that validate any consensus rules.
libraptoreum_consensus_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libraptoreum_consensus_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libraptoreum_consensus_a_SOURCES = \
  arith_uint256.cpp \
  arith_uint256.h \
  bls/bls.cpp \
  bls/bls.h \
  amount.h \
  founder_payment.cpp \
  founder_payment.h \
  hash_selection.h \
  hash_selection.cpp \
  hash.cpp \
  hash.h \
  cryptonote/slow-hash.h \
  consensus/merkle.cpp \
  consensus/merkle.h \
  consensus/params.h \
  consensus/tx_check.cpp \
  consensus/validation.h \
  prevector.h \
  primitives/block.cpp \
  primitives/block.h \
  primitives/powcache.cpp \
  primitives/powcache.h \
  primitives/transaction.cpp \
  primitives/transaction.h \
  pubkey.cpp \
  pubkey.h \
  script/raptoreumconsensus.cpp \
  script/interpreter.cpp \
  script/interpreter.h \
  script/script.cpp \
  script/script.h \
  script/script_error.cpp \
  script/script_error.h \
  serialize.h \
  span.h \
  streams.h \
  tinyformat.h \
  uint256.cpp \
  uint256.h \
  util/strencodings.cpp \
  util/strencodings.h \
  version.h

# common: shared between raptoreumd, and raptoreum-qt and non-server tools
libraptoreum_common_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libraptoreum_common_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libraptoreum_common_a_SOURCES = \
  base58.cpp \
  bech32.cpp \
  founder_payment.cpp \
  bip39.cpp \
  bloom.cpp \
  chainparams.cpp \
  smartnode/smartnode-collaterals.cpp \
  future/utils.cpp \
  coins.cpp \
  compressor.cpp \
  core_read.cpp \
  core_write.cpp \
  hdchain.cpp \
  key.cpp \
  key_io.cpp \
  keystore.cpp \
  merkleblock.cpp \
  netaddress.cpp \
  netbase.cpp \
  policy/feerate.cpp \
  policy/policy.cpp \
  protocol.cpp \
  rpc/rawtransaction_util.cpp \
  rpc/util.cpp \
  saltedhasher.cpp \
  scheduler.cpp \
  script/descriptor.cpp \
  script/sign.cpp \
  script/standard.cpp \
  versionbits.cpp \
  versionbitsinfo.cpp \
  warnings.cpp \
  $(BITCOIN_CORE_H)

# util: shared between all executables.
# This library *must* be included to make sure that the glibc
# sanity checks are linked.
libraptoreum_util_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libraptoreum_util_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libraptoreum_util_a_SOURCES = \
  bls/bls_batchverifier.h \
  bls/bls_ies.cpp \
  bls/bls_ies.h \
  bls/bls_worker.cpp \
  bls/bls_worker.h \
  support/lockedpool.cpp \
  chainparamsbase.cpp \
  clientversion.cpp \
  compat/glibc_sanity.cpp \
  compat/glibcxx_sanity.cpp \
  fs.cpp \
  interfaces/handler.cpp \
  logging.cpp \
  random.cpp \
  randomenv.cpp \
  rpc/request.cpp \
  stacktraces.cpp \
  support/cleanse.cpp \
  sync.cpp \
  threadinterrupt.cpp \
  util/bip32.cpp \
  util/system.cpp \
  util/asmap.cpp \
  util/moneystr.cpp \
  util/spanparsing.cpp \
  util/strencodings.cpp \
  util/string.cpp \
  util/time.cpp \
  util/threadnames.cpp \
  util/error.cpp \
  util/fees.cpp \
  util/url.cpp \
  util/validation.cpp \
  $(BITCOIN_CORE_H)

# cli: shared between raptoreumd and raptoreum-qt
libraptoreum_cli_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libraptoreum_cli_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libraptoreum_cli_a_SOURCES = \
  compat/stdin.h \
  compat/stdin.cpp \
  rpc/client.cpp \
  $(BITCOIN_CORE_H)

nodist_libraptoreum_util_a_SOURCES = $(srcdir)/obj/build.h
#

# raptoreumd binary #
raptoreumd_SOURCES = raptoreumd.cpp
raptoreumd_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
raptoreumd_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
raptoreumd_LDFLAGS = $(LDFLAGS_WRAP_EXCEPTIONS) $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS) $(PTHREAD_FLAGS)

if TARGET_WINDOWS
raptoreumd_SOURCES += raptoreumd-res.rc
endif

raptoreumd_LDADD = \
  $(LIBBITCOIN_SERVER) \
  $(LIBBITCOIN_WALLET) \
  $(LIBBITCOIN_SERVER) \
  $(LIBBITCOIN_COMMON) \
  $(LIBUNIVALUE) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_ZMQ) \
  $(LIBBITCOIN_CONSENSUS) \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBLEVELDB) \
  $(LIBLEVELDB_SSE42) \
  $(LIBMEMENV) \
  $(LIBSECP256K1)

raptoreumd_LDADD += $(BACKTRACE_LIB) $(BOOST_LIBS) $(BDB_LIBS) $(MINIUPNPC_LIBS) $(NATPMP_LIBS) $(EVENT_PTHREADS_LIBS) $(EVENT_LIBS) $(ZMQ_LIBS) $(BLS_LIBS) $(GMP_LIBS)

# raptoreum-cli binary #
raptoreum_cli_SOURCES = raptoreum-cli.cpp
raptoreum_cli_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(EVENT_CFLAGS)
raptoreum_cli_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
raptoreum_cli_LDFLAGS = $(LDFLAGS_WRAP_EXCEPTIONS) $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS) $(PTHREAD_FLAGS)

if TARGET_WINDOWS
raptoreum_cli_SOURCES += raptoreum-cli-res.rc
endif

raptoreum_cli_LDADD = \
  $(LIBBITCOIN_CLI) \
  $(LIBUNIVALUE) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_CRYPTO)

raptoreum_cli_LDADD += $(BACKTRACE_LIB) $(BOOST_LIBS) $(EVENT_LIBS) $(BLS_LIBS) $(GMP_LIBS)

# raptoreum-tx binary #
raptoreum_tx_SOURCES = raptoreum-tx.cpp
raptoreum_tx_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
raptoreum_tx_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
raptoreum_tx_LDFLAGS = $(LDFLAGS_WRAP_EXCEPTIONS) $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS) $(PTHREAD_FLAGS)

if TARGET_WINDOWS
raptoreum_tx_SOURCES += raptoreum-tx-res.rc
endif

raptoreum_tx_LDADD = \
  $(LIBUNIVALUE) \
  $(LIBBITCOIN_COMMON) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_CONSENSUS) \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBSECP256K1)

raptoreum_tx_LDADD += $(BACKTRACE_LIB) $(BOOST_LIBS) $(BLS_LIBS) $(GMP_LIBS)

# raptoreum-wallet binary #
raptoreum_wallet_SOURCES = raptoreum-wallet.cpp
raptoreum_wallet_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
raptoreum_wallet_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
raptoreum_wallet_LDFLAGS = $(LDFLAGS_WRAP_EXCEPTIONS) $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS) $(PTHREAD_FLAGS)

if TARGET_WINDOWS
raptoreum_wallet_SOURCES += raptoreum-wallet-res.rc
endif

# Libraries below may be listed more than once to resolve circular dependencies (see
# https://eli.thegreenplace.net/2013/07/09/library-order-in-static-linking#circular-dependency)
raptoreum_wallet_LDADD = \
  $(LIBBITCOIN_WALLET_TOOL) \
  $(LIBBITCOIN_SERVER) \
  $(LIBBITCOIN_WALLET) \
  $(LIBBITCOIN_SERVER) \
  $(LIBBITCOIN_COMMON) \
  $(LIBBITCOIN_CONSENSUS) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBBITCOIN_ZMQ) \
  $(LIBLEVELDB) \
  $(LIBLEVELDB_SSE42) \
  $(LIBMEMENV) \
  $(LIBSECP256K1) \
  $(LIBUNIVALUE)

raptoreum_wallet_LDADD += $(BACKTRACE_LIB) $(BOOST_LIBS) $(BDB_LIBS) $(EVENT_PTHREADS_LIBS) $(EVENT_LIBS) $(MINIUPNPC_LIBS) $(NATPMP_LIBS) $(ZMQ_LIBS) $(BLS_LIBS) $(GMP_LIBS)
#

# raptoreumconsensus library #
if BUILD_BITCOIN_LIBS
include_HEADERS = script/raptoreumconsensus.h
libraptoreumconsensus_la_SOURCES = support/cleanse.cpp $(crypto_libraptoreum_crypto_a_SOURCES) $(libraptoreum_consensus_a_SOURCES)

libraptoreumconsensus_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined $(RELDFLAGS) $(LIBTOOL_APP_LDFLAGS) $(PTHREAD_FLAGS)
libraptoreumconsensus_la_LIBADD = $(LIBSECP256K1) $(BLS_LIBS) $(GMP_LIBS)
libraptoreumconsensus_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/obj -I$(srcdir)/secp256k1/include -DBUILD_BITCOIN_INTERNAL
libraptoreumconsensus_la_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)

endif
#

CTAES_DIST =  crypto/ctaes/bench.c
CTAES_DIST += crypto/ctaes/ctaes.c
CTAES_DIST += crypto/ctaes/ctaes.h
CTAES_DIST += crypto/ctaes/README.md
CTAES_DIST += crypto/ctaes/test.c

CLEANFILES = $(EXTRA_LIBRARIES)

CLEANFILES += *.gcda *.gcno
CLEANFILES += compat/*.gcda compat/*.gcno
CLEANFILES += consensus/*.gcda consensus/*.gcno
CLEANFILES += crc32c/src/*.gcda crc32c/src/*.gcno
CLEANFILES += crypto/*.gcda crypto/*.gcno
CLEANFILES += policy/*.gcda policy/*.gcno
CLEANFILES += primitives/*.gcda primitives/*.gcno
CLEANFILES += script/*.gcda script/*.gcno
CLEANFILES += support/*.gcda support/*.gcno
CLEANFILES += univalue/*.gcda univalue/*.gcno
CLEANFILES += wallet/*.gcda wallet/*.gcno
CLEANFILES += wallet/test/*.gcda wallet/test/*.gcno
CLEANFILES += zmq/*.gcda zmq/*.gcno
CLEANFILES += obj/build.h

EXTRA_DIST = $(CTAES_DIST)

config/raptoreum-config.h: config/stamp-h1
	@$(MAKE) -C $(top_builddir) $(subdir)/$(@)
config/stamp-h1: $(top_srcdir)/$(subdir)/config/raptoreum-config.h.in $(top_builddir)/config.status
	$(AM_V_at)$(MAKE) -C $(top_builddir) $(subdir)/$(@)
$(top_srcdir)/$(subdir)/config/raptoreum-config.h.in:  $(am__configure_deps)
	$(AM_V_at)$(MAKE) -C $(top_srcdir) $(subdir)/config/raptoreum-config.h.in


clean-local:
	-$(MAKE) -C secp256k1 clean
	-rm -f leveldb/*/*.gcda leveldb/*/*.gcno leveldb/helpers/memenv/*.gcda leveldb/helpers/memenv/*.gcno
	-rm -f config.h
	-rm -rf test/__pycache__
	-rm -rf *.dSYM test/*.dSYM bench/*.dSYM qt/*.dSYM qt/test/*.dSYM

.rc.o:
	@test -f $(WINDRES)
	## FIXME: How to get the appropriate modulename_CPPFLAGS in here?
	$(AM_V_GEN) $(WINDRES) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(CPPFLAGS) -DWINDRES_PREPROC -i $< -o $@

check-symbols: $(bin_PROGRAMS)
	@echo "Running symbol and dynamic library checks..."
	$(AM_V_at) $(PYTHON) $(top_srcdir)/contrib/devtools/symbol-check.py $(bin_PROGRAMS)

check-security: $(bin_PROGRAMS)
if HARDEN
	@echo "Checking binary security..."
	$(AM_V_at) $(PYTHON) $(top_srcdir)/contrib/devtools/security-check.py $(bin_PROGRAMS)
endif

include Makefile.crc32c.include
include Makefile.leveldb.include

if ENABLE_TESTS
include Makefile.test.include
endif

if ENABLE_BENCH
include Makefile.bench.include
endif

if ENABLE_QT
include Makefile.qt.include
endif

if ENABLE_QT_TESTS
include Makefile.qttest.include
endif

include Makefile.univalue.include
