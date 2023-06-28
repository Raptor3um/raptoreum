// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2020 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/raptoreum-config.h>
#endif

#include <init.h>

#include <addrman.h>
#include <amount.h>
#include <banman.h>
#include <base58.h>
#include <chain.h>
#include <chainparams.h>
#include <checkpoints.h>
#include <node/coinstats.h>
#include <compat/sanity.h>
#include <consensus/validation.h>
#include <fs.h>
#include <hash.h>
#include <httpserver.h>
#include <httprpc.h>
#include <interfaces/chain.h>
#include <index/txindex.h>
#include <interfaces/node.h>
#include <key.h>
#include <mapport.h>
#include <validation.h>
#include <miner.h>
#include <netbase.h>
#include <net.h>
#include <net_processing.h>
#include <node/context.h>
#include <policy/feerate.h>
#include <policy/fees.h>
#include <policy/policy.h>
#include <policy/settings.h>
#include <rpc/server.h>
#include <rpc/register.h>
#include <rpc/blockchain.h>
#include <rpc/util.h>
#include <script/standard.h>
#include <script/sigcache.h>
#include <scheduler.h>
#include <shutdown.h>
#include <timedata.h>
#include <txdb.h>
#include <txmempool.h>
#include <torcontrol.h>
#include <ui_interface.h>
#include <util/asmap.h>
#include <util/error.h>
#include <util/moneystr.h>
#include <util/system.h>
#include <util/threadnames.h>
#include <util/validation.h>
#include <validationinterface.h>

#include <smartnode/activesmartnode.h>

#ifdef ENABLE_WALLET
#include <coinjoin/coinjoin-client.h>
#include <coinjoin/coinjoin-client-options.h>
#endif // ENABLE_WALLET

#include <coinjoin/coinjoin-server.h>
#include <dsnotificationinterface.h>
#include <flat-database.h>
#include <governance/governance.h>
#include <smartnode/smartnode-meta.h>
#include <smartnode/smartnode-sync.h>
#include <smartnode/smartnode-utils.h>
#include <messagesigner.h>
#include <netfulfilledman.h>
#include <spork.h>
#include <warnings.h>
#include <walletinitinterface.h>
#include <assets/assets.h>
#include <assets/assetsdb.h>

#include <evo/deterministicmns.h>
#include <llmq/quorums.h>
#include <llmq/quorums_init.h>
#include <llmq/quorums_blockprocessor.h>
#include <llmq/quorums_signing.h>
#include <llmq/quorums_utils.h>

#include <primitives/powcache.h>

#include <statsd_client.h>

#include <stdint.h>
#include <stdio.h>
#include <set>

#include <bls/bls.h>

#ifndef WIN32

#include <attributes.h>
#include <cerrno>
#include <signal.h>
#include <sys/stat.h>

#endif

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/thread.hpp>

#if ENABLE_ZMQ
#include <zmq/zmqabstractnotifier.h>
#include <zmq/zmqnotificationinterface.h>
#include <zmq/zmqrpc.h>
#endif

bool fFeeEstimatesInitialized = false;
static const bool DEFAULT_PROXYRANDOMIZE = true;
static const bool DEFAULT_REST_ENABLE = false;
static const bool DEFAULT_STOPAFTERBLOCKIMPORT = false;

// Dump addresses to banlist.dat every 15 minutes (900s)
static constexpr int DUMP_BANS_INTERVAL = 60 * 15;


static CDSNotificationInterface *pdsNotificationInterface = nullptr;

#ifdef WIN32
// Win32 LevelDB doesn't use filedescriptors, and the ones used for
// accessing block files don't count towards the fd_set size limit
// anyway.
#define MIN_CORE_FILEDESCRIPTORS 0
#else
#define MIN_CORE_FILEDESCRIPTORS 150
#endif

static const char *FEE_ESTIMATES_FILENAME = "fee_estimates.dat";

static const char *DEFAULT_ASMAP_FILENAME = "ip_asn.map";
/**
 * The PID file facilities.
 */
#ifndef WIN32
static const char *RAPTOREUM_PID_FILENAME = "raptoreumd.pid";

static fs::path GetPidFile() {
    return AbsPathForConfigVal(fs::path(gArgs.GetArg("-pid", RAPTOREUM_PID_FILENAME)));
}

[[nodiscard]] static bool CreatePidFile() {
    fsbridge::ofstream file{GetPidFile()};
    if (file) {
        tfm::format(file, "%d\n", getpid());
        return true;
    } else {
        return InitError(
                strprintf(_("Unable to create PID file '%s': %s"), GetPidFile().string(), std::strerror(errno)));
    }
}

#endif

//////////////////////////////////////////////////////////////////////////////
//
// Shutdown
//
//
// Thread management and startup/shutdown:
//
// The network-processing threads are all part of a thread group
// created by AppInit() or the Qt main() function.
//
// A clean exit happens when StartShutdown() or the SIGTERM
// signal handler sets ShutdownRequested(), which makes main thread's
// WaitForShutdown() interrupts the thread group.
// And then, WaitForShutdown() makes all other on-going threads
// in the thread group join the main thread.
// Shutdown() is then called to clean up database connections, and stop other
// threads that should only be stopped after the main network-processing
// threads have exited.
//
// Shutdown for Qt is very similar, only it uses a QTimer to detect
// ShutdownRequested() getting set, and then does the normal Qt
// shutdown thing.
//

static std::unique_ptr <ECCVerifyHandle> globalVerifyHandle;

static boost::thread_group threadGroup;

void Interrupt(NodeContext &node) {
    InterruptHTTPServer();
    InterruptHTTPRPC();
    InterruptRPC();
    InterruptREST();
    InterruptTorControl();
    llmq::InterruptLLMQSystem();
    InterruptMapPort();
    if (node.connman) node.connman->Interrupt();
    if (g_txindex) g_txindex->Interrupt();
}

/** Preparing steps before shutting down or restarting the wallet */
void PrepareShutdown(NodeContext &node) {
    static Mutex g_shutdown_mutex;
    TRY_LOCK(g_shutdown_mutex, lock_shutdown);
    if (!lock_shutdown) return;
    LogPrintf("%s: In progress...\n", __func__);

    /// Note: Shutdown() must be able to handle cases in which initialization failed part of the way,
    /// for example if the data directory was found to be locked.
    /// Be sure that anything that writes files or flushes caches only does this if the respective
    /// module was initialized.
    util::ThreadRename("shutoff");
    mempool.AddTransactionsUpdated(1);
    StopHTTPRPC();
    StopREST();
    StopRPC();
    StopHTTPServer();
    llmq::StopLLMQSystem();

    // fRPCInWarmup should be `false` if we completed the loading sequence
    // before a shutdown request was received
    std::string statusmessage;
    bool fRPCInWarmup = RPCIsInWarmup(&statusmessage);

    for (const auto &client: node.chain_clients) {
        client->flush();
    }
    StopMapPort();

    // Because these depend on each-other, we make sure that neither can be
    // using the other before destroying them.
    if (node.peer_logic) UnregisterValidationInterface(node.peer_logic.get());
    if (node.connman) node.connman->Stop();

    StopTorControl();

    // After everything has been shut down, but before things get flushed, stop the
    // CScheduler/checkqueue, threadGroup/scheduler and load block thread.
    if (node.scheduler) node.scheduler->stop();
    threadGroup.interrupt_all();
    threadGroup.join_all();
    StopScriptCheckWorkerThreads();

    // After there are no more peers/RPC left to give us new data which may generate
    // CValidationInterface callbacks, flush them...
    GetMainSignals().FlushBackgroundCallbacks();

    if (!fRPCInWarmup) {
        // STORE DATA CACHES INTO SERIALIZED DAT FILES
        CFlatDB <CSmartnodeMetaMan> flatdb1("mncache.dat", "magicSmartnodeCache");
        flatdb1.Dump(mmetaman);
        CFlatDB <CNetFulfilledRequestManager> flatdb4("netfulfilled.dat", "magicFulfilledCache");
        flatdb4.Dump(netfulfilledman);
        CFlatDB <CSporkManager> flatdb6("sporks.dat", "magicSporkCache");
        flatdb6.Dump(sporkManager);
        if (!fDisableGovernance) {
            CFlatDB <CGovernanceManager> flatdb3("governance.dat", "magicGovernanceCache");
            flatdb3.Dump(governance);
        }
        CFlatDB <CPowCache> flatdb7("powcache.dat", "powCache");
        flatdb7.Dump(CPowCache::Instance());
    }

    // After the threads that potentially access these pointers have been stopped,
    // destruct and reset all to nullptr.
    node.peer_logic.reset();
    node.connman.reset();
    node.banman.reset();

    if (::mempool.IsLoaded() && gArgs.GetArg("-persistmempool", DEFAULT_PERSIST_MEMPOOL)) {
        DumpMempool(::mempool);
    }

    if (fFeeEstimatesInitialized) {
        ::feeEstimator.FlushUnconfirmed();
        fs::path est_path = GetDataDir() / FEE_ESTIMATES_FILENAME;
        CAutoFile est_fileout(fsbridge::fopen(est_path, "wb"), SER_DISK, CLIENT_VERSION);
        if (!est_fileout.IsNull())
            ::feeEstimator.Write(est_fileout);
        else
            LogPrintf("%s: Failed to write fee estimates to %s\n", __func__, est_path.string());
        fFeeEstimatesInitialized = false;
    }

    // FlushStateToDisk generates a ChainStateFlushed callback, which we should avoid missing
    if (node.chainman) {
        LOCK(cs_main);
        for (CChainState *chainstate: node.chainman->GetAll()) {
            if (chainstate->CanFlushToDisk()) {
                chainstate->ForceFlushStateToDisk();
            }
        }
    }

    GetMainSignals().FlushBackgroundCallbacks();

    if (g_txindex) {
        g_txindex->Stop();
        g_txindex.reset();
    }

    // Any future callbacks will be dropped. This should absolutely be safe - if
    // missing a callback results in an unrecoverable situation, unclean shutdown
    // would too. The only reason to do the above flushes is to let the wallet catch
    // up with our current chain to avoid any strange pruning edge cases and make
    // next startup faster by avoiding rescan.

    if (node.chainman) {
        LOCK(cs_main);
        for (CChainState *chainstate: node.chainman->GetAll()) {
            if (chainstate->CanFlushToDisk()) {
                chainstate->ForceFlushStateToDisk();
                chainstate->ResetCoinsViews();
            }
        }
        pblocktree.reset();
        passetsdb.reset();
        passetsCache.reset();
        llmq::DestroyLLMQSystem();
        deterministicMNManager.reset();
        evoDb.reset();
    }
    for (const auto &client: node.chain_clients) {
        client->stop();
    }

#if ENABLE_ZMQ
    if (g_zmq_notification_interface) {
        UnregisterValidationInterface(g_zmq_notification_interface);
        delete g_zmq_notification_interface;
        g_zmq_notification_interface = nullptr;
    }
#endif

    if (pdsNotificationInterface) {
        UnregisterValidationInterface(pdsNotificationInterface);
        delete pdsNotificationInterface;
        pdsNotificationInterface = nullptr;
    }
    if (fSmartnodeMode) {
        UnregisterValidationInterface(activeSmartnodeManager);
    }

    {
        LOCK(activeSmartnodeInfoCs);
        // make sure to clean up BLS keys before global destructors are called (they have allocated from the secure memory pool)
        activeSmartnodeInfo.blsKeyOperator.reset();
        activeSmartnodeInfo.blsPubKeyOperator.reset();
    }

#ifndef WIN32
    try {
        if (!fs::remove(GetPidFile())) {
            LogPrintf("%s: Unable to remove PID file: File does not exist\n", __func__);
        }
    } catch (const fs::filesystem_error &e) {
        LogPrintf("%s: Unable to remove PID file: %s\n", __func__, e.what());
    }
#endif
    node.chain_clients.clear();
    UnregisterAllValidationInterfaces();
    GetMainSignals().UnregisterBackgroundSignalScheduler();
    GetMainSignals().UnregisterWithMempoolSignals(mempool);
}

/**
* Shutdown is split into 2 parts:
* Part 1: shut down everything but the main wallet instance (done in PrepareShutdown() )
* Part 2: delete wallet instance
*
* In case of a restart PrepareShutdown() was already called before, but this method here gets
* called implicitly when the parent object is deleted. In this case we have to skip the
* PrepareShutdown() part because it was already executed and just delete the wallet instance.
*/
void Shutdown(NodeContext &node) {
    // Shutdown part 1: prepare shutdown
    if (!RestartRequested()) {
        PrepareShutdown(node);
    }
    // Shutdown part 2: delete wallet instance
    globalVerifyHandle.reset();
    ECC_Stop();
    node.mempool = nullptr;
    node.chainman = nullptr;
    node.scheduler.reset();
    LogPrintf("%s: done\n", __func__);
}

/**
 * Signal handlers are very limited in what they are allowed to do.
 * The execution context the handler is invoked in is not guaranteed,
 * so we restrict handler operations to just touching variables:
 */
#ifndef WIN32

static void HandleSIGTERM(int) {
    StartShutdown();
}

static void HandleSIGHUP(int) {
    LogInstance().m_reopen_file = true;
}

#else
static BOOL WINAPI consoleCtrlHandler(DWORD dwCtrlType)
{
    StartShutdown();
    Sleep(INFINITE);
    return true;
}
#endif

#ifndef WIN32

static void registerSignalHandler(int signal, void(*handler)(int)) {
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(signal, &sa, nullptr);
}

#endif

static boost::signals2::connection rpc_notify_block_change_connection;

static void OnRPCStarted() {
    rpc_notify_block_change_connection = uiInterface.NotifyBlockTip_connect(&RPCNotifyBlockChange);
}

static void OnRPCStopped() {
    rpc_notify_block_change_connection.disconnect();
    RPCNotifyBlockChange(false, nullptr);
    g_best_block_cv.notify_all();
    LogPrint(BCLog::RPC, "RPC stopped.\n");
}

std::string GetSupportedSocketEventsStr() {
    std::string strSupportedModes = "'select'";
#ifdef USE_POLL
    strSupportedModes += ", 'poll'";
#endif
#ifdef USE_EPOLL
    strSupportedModes += ", 'epoll'";
#endif
#ifdef USE_KQUEUE
    strSupportedModes += ", 'kqueue'";
#endif
    return strSupportedModes;
}

void SetupServerArgs() {
    const auto defaultBaseParams = CreateBaseChainParams(CBaseChainParams::MAIN);
    const auto testnetBaseParams = CreateBaseChainParams(CBaseChainParams::TESTNET);
    const auto regtestBaseParams = CreateBaseChainParams(CBaseChainParams::REGTEST);
    const auto defaultChainParams = CreateChainParams(CBaseChainParams::MAIN);
    const auto testnetChainParams = CreateChainParams(CBaseChainParams::TESTNET);
    const auto regtestChainParams = CreateChainParams(CBaseChainParams::REGTEST);

    // Hidden Options
    std::vector <std::string> hidden_args = {"-h", "-help", "-dbcrashratio", "-forcecompactdb", "-printcrashinfo",
            // GUI Args. These will be overwritten by SetupUIArgs for the GUI
                                             "-choosedatadir", "-lang=<lang>", "-min", "-resetguisettings", "-splash",
                                             "-uiplatform"};

    // Set all of the args and their help
    // When adding new options to the categories, please keep and ensure alphabetical ordering.
    gArgs.AddArg("-?", "Print this help message and exit", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-alertnotify=<cmd>",
                 "Execute command when a relevant alert is received or we see a really long fork (%s in cmd is replaced by message)",
                 ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-assumevalid=<hex>", strprintf(
                         "If this block is in the chain assume that it and its ancestors are valid and potentially skip their script verification (0 to verify all, default: %s, testnet: %s)",
                         defaultChainParams->GetConsensus().defaultAssumeValid.GetHex(),
                         testnetChainParams->GetConsensus().defaultAssumeValid.GetHex()), ArgsManager::ALLOW_ANY,
                 OptionsCategory::OPTIONS);
    gArgs.AddArg("-blocksdir=<dir>",
                 "Specify directory to hold blocks subdirectory for *.dat files (default: <datadir>)",
                 ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-blocknotify=<cmd>",
                 "Execute command when the best block changes (%s in cmd is replaced by block hash)",
                 ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-blockreconstructionextratxn=<n>",
                 strprintf("Extra transactions to keep in memory for compact block reconstructions (default: %u)",
                           DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-blocksonly", strprintf("Whether to operate in a blocks only mode (default: %u)", DEFAULT_BLOCKSONLY),
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-datadir=<dir>", "Specify data directory", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-dbbatchsize",
                 strprintf("Maximum database write batch size in bytes (default: %u)", nDefaultDbBatchSize),
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-dbcache=<n>",
                 strprintf("Set database cache size in megabytes (%d to %d, default: %d)", nMinDbCache, nMaxDbCache,
                           nDefaultDbCache), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-powcachesize=<n>",
                 strprintf("Set ProofOfWork cache size in megabytes (default: %d)", DEFAULT_POW_CACHE_SIZE),
                 ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-powcachevalidate",
                 strprintf("Whether to validate ProofOfWork cache (default: %u)", DEFAULT_VALIDATE_POW_CACHE),
                 ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-powmaxloadsize",
                 strprintf("Set ProofOfWork maximum elements to load (default: %d)", DEFAULT_MAX_LOAD_SIZE),
                 ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-debuglogfile=<file>", strprintf(
            "Specify location of debug log file. Relative paths will be prefixed by a net-specific datadir location. (0 to disable; default: %s)",
            DEFAULT_DEBUGLOGFILE), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-includeconf=<file>",
                 "Specify additional configuration file, relative to the -datadir path (only useable from configuration file, not command line)",
                 ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-loadblock=<file>", "Imports blocks from external blk000??.dat file on startup",
                 ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-maxmempool=<n>", strprintf("Keep the transaction memory pool below <n> megabytes (default: %u)",
                                              DEFAULT_MAX_MEMPOOL_SIZE), ArgsManager::ALLOW_ANY,
                 OptionsCategory::OPTIONS);
    gArgs.AddArg("-maxorphantxsize=<n>",
                 strprintf("Maximum total size of all orphan transactions in megabytes (default: %u)",
                           DEFAULT_MAX_ORPHAN_TRANSACTIONS_SIZE), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-maxrecsigsage=<n>", strprintf("Number of seconds to keep LLMQ recovery sigs (default: %u)",
                                                 llmq::DEFAULT_MAX_RECOVERED_SIGS_AGE), ArgsManager::ALLOW_ANY,
                 OptionsCategory::OPTIONS);
    gArgs.AddArg("-mempoolexpiry=<n>",
                 strprintf("Do not keep transactions in the mempool longer than <n> hours (default: %u)",
                           DEFAULT_MEMPOOL_EXPIRY), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-minimumchainwork=<hex>",
                 strprintf("Minimum work assumed to exist on a valid chain in hex (default: %s, testnet: %s)",
                           defaultChainParams->GetConsensus().nMinimumChainWork.GetHex(),
                           testnetChainParams->GetConsensus().nMinimumChainWork.GetHex()),
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-par=<n>", strprintf(
                         "Set the number of script verification threads (%u to %d, 0 = auto, <0 = leave that many cores free, default: %d)",
                         -GetNumCores(), MAX_SCRIPTCHECK_THREADS, DEFAULT_SCRIPTCHECK_THREADS), ArgsManager::ALLOW_ANY,
                 OptionsCategory::OPTIONS);
    gArgs.AddArg("-persistmempool",
                 strprintf("Whether to save the mempool on shutdown and load on restart (default: %u)",
                           DEFAULT_PERSIST_MEMPOOL), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#ifndef WIN32
    gArgs.AddArg("-pid=<file>", strprintf(
            "Specify pid file. Relative paths will be prefixed by a net-specific datadir location. (default: %s)",
            RAPTOREUM_PID_FILENAME), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#else
    hidden_args.emplace_back("-pid");
#endif
    gArgs.AddArg("-prune=<n>", strprintf(
            "Reduce storage requirements by enabling pruning (deleting) of old blocks. This allows the pruneblockchain RPC to be called to delete specific blocks, and enables automatic pruning of old blocks if a target size in MiB is provided. This mode is incompatible with -txindex, -rescan and -disablegovernance=false. "
            "Warning: Reverting this setting requires re-downloading the entire blockchain. "
            "(default: 0 = disable pruning blocks, 1 = allow manual pruning via RPC, >%u = automatically prune block files to stay under the specified target size in MiB)",
            MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-syncmempool",
                 strprintf("Sync mempool from other nodes on start (default: %u)", DEFAULT_SYNC_MEMPOOL),
                 ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#ifndef WIN32
    gArgs.AddArg("-sysperms",
                 "Create new files with system default permissions, instead of umask 077 (only effective with disabled wallet functionality)",
                 ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#else
    hidden_args.emplace_back("-sysperms");
#endif
    gArgs.AddArg("-version", "Print version and exit", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-powcachesize",
                 strprintf("Set max pow cache size (number of pow hashes) that keeping in memory (default: %d)",
                           DEFAULT_POW_CACHE_SIZE), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-powmaxloadsize", strprintf(
            "Set max pow cache load size (number of pow hashes) that to be written to powcache.dat (default: %d)",
            DEFAULT_MAX_LOAD_SIZE), ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    gArgs.AddArg("-powcachevalidate",
                 "Enable validation of pow hashes from the cache (default: %true). Use of this option will significantly slow down wallet synchronization.",
                 ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);

    gArgs.AddArg("-addressindex", strprintf(
            "Maintain a full address index, used to query for the balance, txids and unspent outputs for addresses (default: %u)",
            DEFAULT_ADDRESSINDEX), ArgsManager::ALLOW_ANY, OptionsCategory::INDEXING);
    gArgs.AddArg("-reindex", "Rebuild chain state and block index from the blk*.dat files on disk",
                 ArgsManager::ALLOW_ANY, OptionsCategory::INDEXING);
    gArgs.AddArg("-reindex-chainstate", "Rebuild chain state from the currently indexed blocks", ArgsManager::ALLOW_ANY,
                 OptionsCategory::INDEXING);
    gArgs.AddArg("-spentindex", strprintf(
            "Maintain a full spent index, used to query the spending txid and input index for an outpoint (default: %u)",
            DEFAULT_SPENTINDEX), ArgsManager::ALLOW_ANY, OptionsCategory::INDEXING);
    gArgs.AddArg("-timestampindex", strprintf(
            "Maintain a timestamp index for block hashes, used to query blocks hashes by a range of timestamps (default: %u)",
            DEFAULT_TIMESTAMPINDEX), ArgsManager::ALLOW_ANY, OptionsCategory::INDEXING);
    gArgs.AddArg("-txindex",
                 strprintf("Maintain a full transaction index, used by the getrawtransaction rpc call (default: %u)",
                           DEFAULT_TXINDEX), ArgsManager::ALLOW_ANY, OptionsCategory::INDEXING);
    gArgs.AddArg("-futureindex",
                 strprintf("Maintain a full future index, used to query future transactions (default: %u)",
                           DEFAULT_FUTUREINDEX), ArgsManager::ALLOW_ANY, OptionsCategory::INDEXING);

    gArgs.AddArg("-addnode=<ip>",
                 "Add a node to connect to and attempt to keep the connection open (see the `addnode` RPC command help for more info). This option can be specified multiple times to add multiple nodes.",
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-allowprivatenet", strprintf("Allow RFC1918 addresses to be relayed and connected to (default: %u)",
                                               DEFAULT_ALLOWPRIVATENET), ArgsManager::ALLOW_ANY,
                 OptionsCategory::CONNECTION);
    gArgs.AddArg("-banscore=<n>",
                 strprintf("Threshold for disconnecting misbehaving peers (default: %u)", DEFAULT_BANSCORE_THRESHOLD),
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-bantime=<n>",
                 strprintf("Number of seconds to keep misbehaving peers from reconnecting (default: %u)",
                           DEFAULT_MISBEHAVING_BANTIME), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-bind=<addr>", "Bind to given address and always listen on it. Use [host]:port notation for IPv6",
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-connect=<ip>",
                 "Connect only to the specified node; -connect=0 disables automatic connections (the rules for this peer are the same as for -addnode). This option can be specified multiple times to connect to multiple nodes.",
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-discover", "Discover own IP addresses (default: 1 when listening and no -externalip or -proxy)",
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-dns",
                 strprintf("Allow DNS lookups for -addnode, -seednode and -connect (default: %u)", DEFAULT_NAME_LOOKUP),
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-dnsseed",
                 "Query for peer addresses via DNS lookup, if low on addresses (default: 1 unless -connect used)",
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-enablebip61", strprintf("Send reject messages per BIP61 (default: %u)", DEFAULT_ENABLE_BIP61),
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-externalip=<ip>", "Specify your own public address", ArgsManager::ALLOW_ANY,
                 OptionsCategory::CONNECTION);
    gArgs.AddArg("-forcednsseed",
                 strprintf("Always query for peer addresses via DNS lookup (default: %u)", DEFAULT_FORCEDNSSEED),
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-listen", "Accept connections from outside (default: 1 if no -proxy or -connect)",
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-listenonion",
                 strprintf("Automatically create Tor hidden service (default: %d)", DEFAULT_LISTEN_ONION),
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-maxconnections=<n>", strprintf(
            "Maintain at most <n> connections to peers (temporary service connections excluded) (default: %u)",
            DEFAULT_MAX_PEER_CONNECTIONS), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-maxreceivebuffer=<n>",
                 strprintf("Maximum per-connection receive buffer, <n>*1000 bytes (default: %u)",
                           DEFAULT_MAXRECEIVEBUFFER), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-maxsendbuffer=<n>",
                 strprintf("Maximum per-connection send buffer, <n>*1000 bytes (default: %u)", DEFAULT_MAXSENDBUFFER),
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-maxtimeadjustment", strprintf(
            "Maximum allowed median peer time offset adjustment. Local perspective of time may be influenced by peers forward or backward by this amount. (default: %u seconds)",
            DEFAULT_MAX_TIME_ADJUSTMENT), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-maxuploadtarget=<n>", strprintf(
            "Tries to keep outbound traffic under the given target (in MiB per 24h), 0 = no limit (default: %d)",
            DEFAULT_MAX_UPLOAD_TARGET), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-onion=<ip:port>",
                 "Use separate SOCKS5 proxy to reach peers via Tor hidden services (default: -proxy)",
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-onlynet=<net>",
                 "Make outgoing connections only through network <net> (ipv4, ipv6 or onion). Incoming connections are not affected by this option. This option can be specified multiple times to allow multiple networks.",
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-peerbloomfilters",
                 strprintf("Support filtering of blocks and transaction with bloom filters (default: %u)",
                           DEFAULT_PEERBLOOMFILTERS), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-peertimeout=<n>", strprintf(
            "Specify p2p connection timeout in seconds. This option determines the amount of time a peer may be inactive before the connection to it is dropped. (minimum: 1, default: %d)",
            DEFAULT_PEER_CONNECT_TIMEOUT), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-permitbaremultisig", strprintf("Relay non-P2SH multisig (default: %u)", DEFAULT_PERMIT_BAREMULTISIG),
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-port=<port>", strprintf("Listen for connections on <port> (default: %u or testnet: %u)",
                                           defaultChainParams->GetDefaultPort(), testnetChainParams->GetDefaultPort()),
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-proxy=<ip:port>", "Connect through SOCKS5 proxy", ArgsManager::ALLOW_ANY,
                 OptionsCategory::CONNECTION);
    gArgs.AddArg("-proxyrandomize", strprintf(
            "Randomize credentials for every proxy connection. This enables Tor stream isolation (default: %u)",
            DEFAULT_PROXYRANDOMIZE), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-seednode=<ip>",
                 "Connect to a node to retrieve peer addresses, and disconnect. This option can be specified multiple times to connect to multiple nodes.",
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-socketevents=<mode>",
                 "Socket events mode, which must be one of 'select', 'poll', 'epoll' or 'kqueue', depending on your system (default: Linux - 'epoll', FreeBSD/Apple - 'kqueue', Windows - 'select')",
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-timeout=<n>", strprintf("Specify connection timeout in milliseconds (minimum: 1, default: %d)",
                                           DEFAULT_CONNECT_TIMEOUT), ArgsManager::ALLOW_ANY,
                 OptionsCategory::CONNECTION);
    gArgs.AddArg("-torcontrol=<ip>:<port>",
                 strprintf("Tor control port to use if onion listening enabled (default: %s)", DEFAULT_TOR_CONTROL),
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
    gArgs.AddArg("-torpassword=<pass>", "Tor control port password (default: empty)", ArgsManager::ALLOW_ANY,
                 OptionsCategory::CONNECTION);
#ifdef USE_UPNP
#if USE_UPNP
    gArgs.AddArg("-upnp", "Use UPnP to map the listening port (default: 1 when listening and no -proxy)", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
#else
    gArgs.AddArg("-upnp", strprintf("Use UPnP to map the listening port (default: %u)", 0), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
#endif
#else
    hidden_args.emplace_back("-upnp");
#endif
#ifdef USE_NATPMP
    gArgs.AddArg("-natpmp", strprintf("Use NAT-PMP to map the listening port (default: %s)", DEFAULT_NATPMP ? "1 when listening and no -proxy" : "0"), ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);
#endif
    gArgs.AddArg("-whitebind=<[permissions@]addr>", "Bind to given address and whitelist peers connecting to it. "
                                                    "Use [host]:port notation for IPv6. Allowed permissions are bloomfilter (allow requesting BIP37 filtered blocks and transactions), "
                                                    "noban (do not ban for misbehavior), "
                                                    "forcerelay (relay even non-standard transactions), "
                                                    "relay (relay even in -blocksonly mode), "
                                                    "and mempool (allow requesting BIP35 mempool contents). "
                                                    "Specify multiple permissions separated by commas (default: noban,mempool,relay). Can be specified multiple times.",
                 ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);

    gArgs.AddArg("-whitelist=<[permissions@]IP address or network>",
                 "Whitelist peers connecting from the given IP address (e.g. 1.2.3.4) or "
                 "CIDR notated network(e.g. 1.2.3.0/24). Uses same permissions as "
                 "-whitebind. Can be specified multiple times.", ArgsManager::ALLOW_ANY, OptionsCategory::CONNECTION);

    g_wallet_init_interface.AddWalletOptions();

#if ENABLE_ZMQ
    gArgs.AddArg("-zmqpubhashblock=<address>", "Enable publish hash block in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubhashchainlock=<address>", "Enable publish hash block (locked via ChainLocks) in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubhashgovernanceobject=<address>", "Enable publish hash of governance objects (like proposals) in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubhashgovernancevote=<address>", "Enable publish hash of governance votes in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubhashinstantsenddoublespend=<address>", "Enable publish transaction hashes of attempted InstantSend double spend in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubhashrecoveredsig=<address>", "Enable publish message hash of recovered signatures (recovered by LLMQs) in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubhashtx=<address>", "Enable publish hash transaction in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubhashtxlock=<address>", "Enable publish hash transaction (locked via InstantSend) in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubrawblock=<address>", "Enable publish raw block in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubrawchainlock=<address>", "Enable publish raw block (locked via ChainLocks) in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubrawchainlocksig=<address>", "Enable publish raw block (locked via ChainLocks) and CLSIG message in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubrawinstantsenddoublespend=<address>", "Enable publish raw transactions of attempted InstantSend double spend in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubrawrecoveredsig=<address>", "Enable publish raw recovered signatures (recovered by LLMQs) in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubrawtx=<address>", "Enable publish raw transaction in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubrawtxlock=<address>", "Enable publish raw transaction (locked via InstantSend) in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
    gArgs.AddArg("-zmqpubrawtxlocksig=<address>", "Enable publish raw transaction (locked via InstantSend) and ISLOCK in <address>", ArgsManager::ALLOW_ANY, OptionsCategory::ZMQ);
#else
    hidden_args.emplace_back("-zmqpubhashblock=<address>");
    hidden_args.emplace_back("-zmqpubhashchainlock=<address>");
    hidden_args.emplace_back("-zmqpubhashgovernanceobject=<address>");
    hidden_args.emplace_back("-zmqpubhashgovernancevote=<address>");
    hidden_args.emplace_back("-zmqpubhashinstantsenddoublespend=<address>");
    hidden_args.emplace_back("-zmqpubhashrecoveredsig=<address>");
    hidden_args.emplace_back("-zmqpubhashtx=<address>");
    hidden_args.emplace_back("-zmqpubhashtxlock=<address>");
    hidden_args.emplace_back("-zmqpubrawblock=<address>");
    hidden_args.emplace_back("-zmqpubrawchainlock=<address>");
    hidden_args.emplace_back("-zmqpubrawchainlocksig=<address>");
    hidden_args.emplace_back("-zmqpubrawinstantsenddoublespend=<address>");
    hidden_args.emplace_back("-zmqpubrawrecoveredsig=<address>");
    hidden_args.emplace_back("-zmqpubrawtx=<address>");
    hidden_args.emplace_back("-zmqpubrawtxlock=<address>");
    hidden_args.emplace_back("-zmqpubrawtxlocksig=<address>");
#endif

    gArgs.AddArg("-checkblockindex", strprintf(
                         "Do a full consistency check for the block tree, setBlockIndexCandidates, ::ChainActive() and mapBlocksUnlinked occasionally. (default: %u)",
                         defaultChainParams->DefaultConsistencyChecks()), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY,
                 OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-checkblocks=<n>",
                 strprintf("How many blocks to check at startup (default: %u, 0 = all)", DEFAULT_CHECKBLOCKS),
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-checklevel=<n>", strprintf("How thorough the block verification of -checkblocks is: "
                                              "level 0 reads the blocks from disk, "
                                              "level 1 verifies block validity, "
                                              "level 2 verifies undo data, "
                                              "level 3 checks disconnection of the tip blocks, "
                                              "level 4 tries to reconnect the blocks, "
                                              "each level inclusive of previous levels "
                                              "(0-4, default: %u)", DEFAULT_CHECKLEVEL),
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-checkmempool=<n>", strprintf("Run checks every <n> transactions (default: %u)",
                                                defaultChainParams->DefaultConsistencyChecks()),
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-checkpoints", strprintf("Disable expensive verification for known chain history (default: %u)",
                                           DEFAULT_CHECKPOINTS_ENABLED),
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-deprecatedrpc=<method>", "Allows deprecated RPC method(s) to be used",
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-dropmessagestest=<n>", "Randomly drop 1 of every <n> network messages",
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-limitancestorcount=<n>",
                 strprintf("Do not accept transactions if number of in-mempool ancestors is <n> or more (default: %u)",
                           DEFAULT_ANCESTOR_LIMIT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY,
                 OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-limitancestorsize=<n>", strprintf(
                         "Do not accept transactions whose size with all in-mempool ancestors exceeds <n> kilobytes (default: %u)",
                         DEFAULT_ANCESTOR_SIZE_LIMIT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY,
                 OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-limitdescendantcount=<n>", strprintf(
            "Do not accept transactions if any ancestor would have <n> or more in-mempool descendants (default: %u)",
            DEFAULT_DESCENDANT_LIMIT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-limitdescendantsize=<n>", strprintf(
                         "Do not accept transactions if any ancestor would have more than <n> kilobytes of in-mempool descendants (default: %u).",
                         DEFAULT_DESCENDANT_SIZE_LIMIT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY,
                 OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-stopafterblockimport",
                 strprintf("Stop running after importing blocks from disk (default: %u)", DEFAULT_STOPAFTERBLOCKIMPORT),
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-stopatheight",
                 strprintf("Stop running after reaching the given height in the main chain (default: %u)",
                           DEFAULT_STOPATHEIGHT), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY,
                 OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-watchquorums=<n>",
                 strprintf("Watch and validate quorum communication (default: %u)", llmq::DEFAULT_WATCH_QUORUMS),
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-addrmantest", "Allows to test address relay on localhost",
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);

    gArgs.AddArg("-debug=<category>",
                 "Output debugging information (default: -nodebug, supplying <category> is optional). "
                 "If <category> is not supplied or if <category> = 1, output all debugging information. <category> can be: " +
                 LogInstance().LogCategoriesString() + ".", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-debugexclude=<category>", strprintf(
                         "Exclude debugging information for a category. Can be used in conjunction with -debug=1 to output debug logs for all categories except one or more specified categories."),
                 ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-disablegovernance", strprintf("Disable governance validation (0-1, default: %u)", 0),
                 ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-help-debug", "Print help message with debugging options and exit", ArgsManager::ALLOW_ANY,
                 OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-logips", strprintf("Include IP addresses in debug output (default: %u)", DEFAULT_LOGIPS),
                 ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-logtimemicros",
                 strprintf("Add microsecond precision to debug timestamps (default: %u)", DEFAULT_LOGTIMEMICROS),
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-logtimestamps",
                 strprintf("Prepend debug output with timestamp (default: %u)", DEFAULT_LOGTIMESTAMPS),
                 ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
#ifdef HAVE_THREAD_LOCAL
    gArgs.AddArg("-logthreadnames", strprintf("Add thread names to debug messages (default: %u)", DEFAULT_LOGTHREADNAMES), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
#else
    hidden_args.emplace_back("-logthreadnames");
#endif
    gArgs.AddArg("-logsourcelocations", strprintf(
            "Prepend debug output with name of the originating source location (source file, line number and function name) (default: %u)",
            DEFAULT_LOGSOURCELOCATIONS), ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-maxsigcachesize=<n>",
                 strprintf("Limit sum of signature cache and script execution cache sizes to <n> MiB (default: %u)",
                           DEFAULT_MAX_SIG_CACHE_SIZE), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY,
                 OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-maxtipage=<n>",
                 strprintf("Maximum tip age in seconds to consider node in initial block download (default: %u)",
                           DEFAULT_MAX_TIP_AGE), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY,
                 OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-mocktime=<n>", "Replace actual time with " + UNIX_EPOCH_TIME + "(default: 0)",
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-minsporkkeys=<n>",
                 "Overrides minimum spork signers to change spork value. Only useful for regtest and devnet. Using this on mainnet or testnet will ban you.",
                 ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-printpriority",
                 strprintf("Log transaction fee per kB when mining blocks (default: %u)", DEFAULT_PRINTPRIORITY),
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-printtoconsole",
                 "Send trace/debug info to console (default: 1 when no -daemon. To disable logging to file, set -nodebuglogfile)",
                 ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-pushversion", "Protocol version OVERRIDE to report to other Raptoreum nodes", ArgsManager::ALLOW_ANY,
                 OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-shrinkdebugfile", "Shrink debug.log file on client startup (default: 1 when no -debug)",
                 ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-sporkaddr=<raptoreumaddress>",
                 "Override spork address. Only useful for regtest and devnet. Using this on mainnet or testnet will ban you.",
                 ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-sporkkey=<privatekey>", "Set the private key to be used for signing spork messages.",
                 ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    gArgs.AddArg("-uacomment=<cmt>", "Append comment to the user agent string", ArgsManager::ALLOW_ANY,
                 OptionsCategory::DEBUG_TEST);

    SetupChainParamsBaseOptions();

    gArgs.AddArg("-llmq-data-recovery=<n>", strprintf("Enable automated quorum data recovery (default: %u)",
                                                      llmq::DEFAULT_ENABLE_QUORUM_DATA_RECOVERY),
                 ArgsManager::ALLOW_ANY, OptionsCategory::SMARTNODE);
    gArgs.AddArg("-llmq-qvvec-sync=<quorum_name>:<mode>", strprintf(
                         "Defines from which LLMQ type the smartnode should sync quorum verification vectors. Can be used multiple times with different LLMQ types. <mode>: %d (sync always from all quorums of the type defined by <quorum_name>), %d (sync from all quorums of the type defined by <quorum_name> if a member of any of the quorums)",
                         (int32_t) llmq::QvvecSyncMode::Always, (int32_t) llmq::QvvecSyncMode::OnlyIfTypeMember),
                 ArgsManager::ALLOW_ANY, OptionsCategory::SMARTNODE);
    gArgs.AddArg("-smartnodeblsprivkey=<hex>",
                 "Set the smartnode BLS private key and enable the client to act as a smartnode",
                 ArgsManager::ALLOW_ANY, OptionsCategory::SMARTNODE);
    gArgs.AddArg("-platform-user=<user>",
                 "Set the username for the \"platform user\", a restricted user intended to be used by Raptoreum Platform, to the specified username.",
                 ArgsManager::ALLOW_ANY, OptionsCategory::SMARTNODE);

    gArgs.AddArg("-acceptnonstdtxn",
                 strprintf("Relay and mine \"non-standard\" transactions (%sdefault: %u)", "testnet/regtest only; ",
                           !testnetChainParams->RequireStandard()), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY,
                 OptionsCategory::NODE_RELAY);
    gArgs.AddArg("-dustrelayfee=<amt>", strprintf(
                         "Fee rate (in %s/kB) used to defined dust, the value of an output such that it will cost more than its value in fees at this fee rate to spend it. (default: %s)",
                         CURRENCY_UNIT, FormatMoney(DUST_RELAY_TX_FEE)), ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY,
                 OptionsCategory::NODE_RELAY);
    gArgs.AddArg("-incrementalrelayfee=<amt>", strprintf(
                         "Fee rate (in %s/kB) used to define cost of relay, used for mempool limiting and BIP 125 replacement. (default: %s)",
                         CURRENCY_UNIT, FormatMoney(DEFAULT_INCREMENTAL_RELAY_FEE)),
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::NODE_RELAY);
    gArgs.AddArg("-bytespersigop", strprintf("Minimum bytes per sigop in transactions we relay and mine (default: %u)",
                                             DEFAULT_BYTES_PER_SIGOP), ArgsManager::ALLOW_ANY,
                 OptionsCategory::NODE_RELAY);
    gArgs.AddArg("-datacarrier",
                 strprintf("Relay and mine data carrier transactions (default: %u)", DEFAULT_ACCEPT_DATACARRIER),
                 ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);
    gArgs.AddArg("-datacarriersize",
                 strprintf("Maximum size of data in data carrier transactions we relay and mine (default: %u)",
                           MAX_OP_RETURN_RELAY), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);
    gArgs.AddArg("-minrelaytxfee=<amt>", strprintf(
            "Fees (in %s/kB) smaller than this are considered zero fee for relaying, mining and transaction creation (default: %s)",
            CURRENCY_UNIT, FormatMoney(DEFAULT_MIN_RELAY_TX_FEE)), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);
    gArgs.AddArg("-whitelistforcerelay", strprintf(
            "Add 'forcerelay' permission to whitelisted inbound peers with default permissions. This will relay transactions even if the transactions were already in the mempool or violate local relay policy. (default: %d)",
            DEFAULT_WHITELISTFORCERELAY), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);
    gArgs.AddArg("-whitelistrelay", strprintf(
            "Add 'relay' permission to whitelisted inbound peers with default permissions. The will accept relayed transactionseven when not relaying transactions (default: %d)",
            DEFAULT_WHITELISTRELAY), ArgsManager::ALLOW_ANY, OptionsCategory::NODE_RELAY);

    gArgs.AddArg("-blockmaxsize=<n>",
                 strprintf("Set maximum block size in bytes (default: %d)", DEFAULT_BLOCK_MAX_SIZE),
                 ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    gArgs.AddArg("-blockmintxfee=<amt>", strprintf(
                         "Set lowest fee rate (in %s/kB) for transactions to be included in block creation. (default: %s)",
                         CURRENCY_UNIT, FormatMoney(DEFAULT_BLOCK_MIN_TX_FEE)), ArgsManager::ALLOW_ANY,
                 OptionsCategory::BLOCK_CREATION);
    gArgs.AddArg("-blockversion=<n>", "Override block version to test forking scenarios",
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::BLOCK_CREATION);

    gArgs.AddArg("-rest", strprintf("Accept public REST requests (default: %u)", DEFAULT_REST_ENABLE),
                 ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcallowip=<ip>",
                 "Allow JSON-RPC connections from specified source. Valid for <ip> are a single IP (e.g. 1.2.3.4), a network/netmask (e.g. 1.2.3.4/255.255.255.0) or a network/CIDR (e.g. 1.2.3.4/24). This option can be specified multiple times",
                 ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcauth=<userpw>",
                 "Username and hashed password for JSON-RPC connections. The field <userpw> comes in the format: <USERNAME>:<SALT>$<HASH>. A canonical python script is included in share/rpcuser. The client then connects normally using the rpcuser=<USERNAME>/rpcpassword=<PASSWORD> pair of arguments. This option can be specified multiple times",
                 ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcbind=<addr>[:port]",
                 "Bind to given address to listen for JSON-RPC connections. Do not expose the RPC server to untrusted networks such as the public internet! This option is ignored unless -rpcallowip is also passed. Port is optional and overrides -rpcport. Use [host]:port notation for IPv6. This option can be specified multiple times (default: 127.0.0.1 and ::1 i.e., localhost, or if -rpcallowip has been specified, 0.0.0.0 and :: i.e., all addresses)",
                 ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::RPC);
    gArgs.AddArg("-rpccookiefile=<loc>",
                 "Location of the auth cookie. Relative paths will be prefixed by a net-specific datadir location. (default: data dir)",
                 ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcpassword=<pw>", "Password for JSON-RPC connections", ArgsManager::ALLOW_ANY,
                 OptionsCategory::RPC);
    gArgs.AddArg("-rpcport=<port>",
                 strprintf("Listen for JSON-RPC connections on <port> (default: %u, testnet: %u, regtest: %u)",
                           defaultBaseParams->RPCPort(), testnetBaseParams->RPCPort(), regtestBaseParams->RPCPort()),
                 ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcservertimeout=<n>",
                 strprintf("Timeout during HTTP requests (default: %d)", DEFAULT_HTTP_SERVER_TIMEOUT),
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcthreads=<n>",
                 strprintf("Set the number of threads to service RPC calls (default: %d)", DEFAULT_HTTP_THREADS),
                 ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcuser=<user>", "Username for JSON-RPC connections", ArgsManager::ALLOW_ANY, OptionsCategory::RPC);
    gArgs.AddArg("-rpcworkqueue=<n>", strprintf("Set the depth of the work queue to service RPC calls (default: %d)",
                                                DEFAULT_HTTP_WORKQUEUE),
                 ArgsManager::ALLOW_ANY | ArgsManager::DEBUG_ONLY, OptionsCategory::RPC);
    gArgs.AddArg("-server", "Accept command line and JSON-RPC commands", ArgsManager::ALLOW_ANY, OptionsCategory::RPC);

    gArgs.AddArg("-statsenabled", strprintf("Publish internal stats to statsd (default: %u)", DEFAULT_STATSD_ENABLE),
                 ArgsManager::ALLOW_ANY, OptionsCategory::STATSD);
    gArgs.AddArg("-statshost=<ip>", strprintf("Specify statsd host (default: %s)", DEFAULT_STATSD_HOST),
                 ArgsManager::ALLOW_ANY, OptionsCategory::STATSD);
    gArgs.AddArg("-statshostname=<ip>", strprintf("Specify statsd host name (default: %s)", DEFAULT_STATSD_HOSTNAME),
                 ArgsManager::ALLOW_ANY, OptionsCategory::STATSD);
    gArgs.AddArg("-statsport=<port>", strprintf("Specify statsd port (default: %u)", DEFAULT_STATSD_PORT),
                 ArgsManager::ALLOW_ANY, OptionsCategory::STATSD);
    gArgs.AddArg("-statsns=<ns>",
                 strprintf("Specify additional namespace prefix (default: %s)", DEFAULT_STATSD_NAMESPACE),
                 ArgsManager::ALLOW_ANY, OptionsCategory::STATSD);
    gArgs.AddArg("-statsperiod=<seconds>",
                 strprintf("Specify the number of seconds between periodic measurements (default: %d)",
                           DEFAULT_STATSD_PERIOD), ArgsManager::ALLOW_ANY, OptionsCategory::STATSD);
#if HAVE_DECL_DAEMON
    gArgs.AddArg("-daemon", "Run in the background as a daemon and accept commands", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
#else
    hidden_args.emplace_back("-daemon");
#endif

    // Add Hidden Arguments Option
    gArgs.AddHiddenArgs(hidden_args);
}

std::string LicenseInfo() {
    const std::string URL_SOURCE_CODE = "<https://github.com/Raptor3um/raptoreum>";
    const std::string URL_WEBSITE = "<https://raptoreum.com>";

    return CopyrightHolders(_("Copyright (C)"), 2014, COPYRIGHT_YEAR) + "\n" +
           "\n" +
           strprintf(_("Please contribute if you find %s useful. "
                       "Visit %s for further information about the software."),
                     PACKAGE_NAME, URL_WEBSITE) +
           "\n" +
           strprintf(_("The source code is available from %s."),
                     URL_SOURCE_CODE) +
           "\n" +
           "\n" +
           _("This is experimental software.") + "\n" +
           strprintf(_("Distributed under the MIT software license, see the accompanying file %s or %s"), "COPYING",
                     "<https://opensource.org/licenses/MIT>") + "\n" +
           "\n" +
           strprintf(
                   _("This product includes software developed by the OpenSSL Project for use in the OpenSSL Toolkit %s and cryptographic software written by Eric Young and UPnP software written by Thomas Bernard."),
                   "<https://www.openssl.org>") +
           "\n";
}

static void BlockNotifyCallback(bool initialSync, const CBlockIndex *pBlockIndex) {
    if (initialSync || !pBlockIndex)
        return;

    std::string strCmd = gArgs.GetArg("-blocknotify", "");
    if (!strCmd.empty()) {
        boost::replace_all(strCmd, "%s", pBlockIndex->GetBlockHash().GetHex());
        std::thread t(runCommand, strCmd);
        t.detach(); // thread runs free
    }
}

static bool fHaveGenesis = false;
static Mutex g_genesis_wait_mutex;
static std::condition_variable g_genesis_wait_cv;

static void BlockNotifyGenesisWait(bool, const CBlockIndex *pBlockIndex) {
    if (pBlockIndex != nullptr) {
        {
            LOCK(g_genesis_wait_mutex);
            fHaveGenesis = true;
        }
        g_genesis_wait_cv.notify_all();
    }
}

struct CImportingNow {
    CImportingNow() {
        assert(fImporting == false);
        fImporting = true;
    }

    ~CImportingNow() {
        assert(fImporting == true);
        fImporting = false;
    }
};


// If we're using -prune with -reindex, then delete block files that will be ignored by the
// reindex.  Since reindexing works by starting at block file 0 and looping until a blockfile
// is missing, do the same here to delete any later block files after a gap.  Also delete all
// rev files since they'll be rewritten by the reindex anyway.  This ensures that vinfoBlockFile
// is in sync with what's actually on disk by the time we start downloading, so that pruning
// works correctly.
static void CleanupBlockRevFiles() {
    std::map <std::string, fs::path> mapBlockFiles;

    // Glob all blk?????.dat and rev?????.dat files from the blocks directory.
    // Remove the rev files immediately and insert the blk file paths into an
    // ordered map keyed by block file index.
    LogPrintf("Removing unusable blk?????.dat and rev?????.dat files for -reindex with -prune\n");
    fs::path blocksdir = GetBlocksDir();
    for (fs::directory_iterator it(blocksdir); it != fs::directory_iterator(); it++) {
        if (fs::is_regular_file(*it) &&
            it->path().filename().string().length() == 12 &&
            it->path().filename().string().substr(8, 4) == ".dat") {
            if (it->path().filename().string().substr(0, 3) == "blk")
                mapBlockFiles[it->path().filename().string().substr(3, 5)] = it->path();
            else if (it->path().filename().string().substr(0, 3) == "rev")
                remove(it->path());
        }
    }

    // Remove all block files that aren't part of a contiguous set starting at
    // zero by walking the ordered map (keys are block file indices) by
    // keeping a separate counter.  Once we hit a gap (or if 0 doesn't exist)
    // start removing block files.
    int nContigCounter = 0;
    for (const std::pair<const std::string, fs::path> &item: mapBlockFiles) {
        if (atoi(item.first) == nContigCounter) {
            nContigCounter++;
            continue;
        }
        remove(item.second);
    }
}

static void ThreadImport(ChainstateManager &chainman, std::vector <fs::path> vImportFiles) {
    const CChainParams &chainparams = Params();
    util::ThreadRename("loadblk");
    ScheduleBatchPriority();

    {
        CImportingNow imp;

        // -reindex
        if (fReindex) {
            int nFile = 0;
            while (true) {
                FlatFilePos pos(nFile, 0);
                if (!fs::exists(GetBlockPosFilename(pos)))
                    break; // No block files left to reindex
                FILE *file = OpenBlockFile(pos, true);
                if (!file)
                    break; // This error is logged in OpenBlockFile
                LogPrintf("Reindexing block file blk%05u.dat...\n", (unsigned int) nFile);
                LoadExternalBlockFile(chainparams, file, &pos);
                if (ShutdownRequested()) {
                    LogPrintf("Shutdown requested. Exit %s\n", __func__);
                    return;
                }
                nFile++;
            }
            pblocktree->WriteReindexing(false);
            fReindex = false;
            LogPrintf("Reindexing finished\n");
            // To avoid ending up in a situation without genesis block, re-try initializing (no-op if reindexing worked):
            LoadGenesisBlock(chainparams);
        }

        // hardcoded $DATADIR/bootstrap.dat
        fs::path pathBootstrap = GetDataDir() / "bootstrap.dat";
        if (fs::exists(pathBootstrap)) {
            FILE *file = fsbridge::fopen(pathBootstrap, "rb");
            if (file) {
                fs::path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
                LogPrintf("Importing bootstrap.dat...\n");
                LoadExternalBlockFile(chainparams, file);
                if (!RenameOver(pathBootstrap, pathBootstrapOld)) {
                    throw std::runtime_error("Rename failed");
                }
            } else {
                LogPrintf("Warning: Could not open bootstrap file %s\n", pathBootstrap.string());
            }
        }

        // -loadblock=
        for (const fs::path &path: vImportFiles) {
            FILE *file = fsbridge::fopen(path, "rb");
            if (file) {
                LogPrintf("Importing blocks file %s...\n", path.string());
                LoadExternalBlockFile(chainparams, file);
            } else {
                LogPrintf("Warning: Could not open blocks file %s\n", path.string());
            }
        }

        // scan for better chains in the block chain database, that are not yet connected in the active best chain

        // We van not hold cs_main during ActivateBestChain even thought we are accessomg
        // the chainman unique_ptr since ABC requires us not to be holding cs_main
        // so retrieve the relevant pointers before the ABC call.
        for (CChainState *chainstate: WITH_LOCK(::cs_main, return chainman.GetAll())) {
            CValidationState state;
            if (!chainstate->ActivateBestChain(state, chainparams, nullptr)) {
                LogPrintf("Failed to connect best block (%s)\n", FormatStateMessage(state));
                StartShutdown();
                return;
            }
        }

        if (gArgs.GetBoolArg("-stopafterblockimport", DEFAULT_STOPAFTERBLOCKIMPORT)) {
            LogPrintf("Stopping after block import\n");
            StartShutdown();
            return;
        }
    } // End scope of CImportingNow

    // force UpdatedBlockTip to initialize nCachedBlockHeight for DS, MN payments and budgets
    // but don't call it directly to prevent triggering of other listeners like zmq etc.
    // GetMainSignals().UpdatedBlockTip(::ChainActive().Tip());
    pdsNotificationInterface->InitializeCurrentBlockTip();

    {
        // Get all UTXOs for each MN collateral in one go so that we can fill coin cache early
        // and reduce further locking overhead for cs_main in other parts of code including GUI
        LogPrintf("Filling coin cache with smartnode UTXOs...\n");
        LOCK(cs_main);
        int64_t nStart = GetTimeMillis();
        auto mnList = deterministicMNManager->GetListAtChainTip();
        mnList.ForEachMN(false, [&](const CDeterministicMNCPtr &dmn) {
            Coin coin;
            GetUTXOCoin(dmn->collateralOutpoint, coin);
        });
        LogPrintf("Filling coin cache with smartnode UTXOs: done in %dms\n", GetTimeMillis() - nStart);
    }

    if (fSmartnodeMode) {
        assert(activeSmartnodeManager);
        activeSmartnodeManager->Init(::ChainActive().Tip());
    }

    g_wallet_init_interface.AutoLockSmartnodeCollaterals();

    if (gArgs.GetArg("-persistmempool", DEFAULT_PERSIST_MEMPOOL)) {
        LoadMempool(::mempool);
    }
    ::mempool.SetIsLoaded(!ShutdownRequested());
}

void PeriodicStats() {
    assert(gArgs.GetBoolArg("-statsenabled", DEFAULT_STATSD_ENABLE));
    CCoinsStats stats;
    ::ChainstateActive().ForceFlushStateToDisk();
    if (WITH_LOCK(cs_main, return GetUTXOStats(&::ChainstateActive().CoinsDB(), stats, CoinStatsHashType::NONE))) {
        statsClient.gauge("utxoset.tx", stats.nTransactions, 1.0f);
        statsClient.gauge("utxoset.txOutputs", stats.nTransactionOutputs, 1.0f);
        statsClient.gauge("utxoset.dbSizeBytes", stats.nDiskSize, 1.0f);
        statsClient.gauge("utxoset.blockHeight", stats.nHeight, 1.0f);
        statsClient.gauge("utxoset.totalAmount", (double) stats.nTotalAmount / (double) COIN, 1.0f);
    } else {
        // something went wrong
        LogPrintf("%s: GetUTXOStats failed\n", __func__);
    }

    // short version of GetNetworkHashPS(120, -1);
    CBlockIndex *tip = ::ChainActive().Tip();
    CBlockIndex *pindex = tip;
    int64_t minTime = pindex->GetBlockTime();
    int64_t maxTime = minTime;
    for (int i = 0; i < 120 && pindex->pprev != nullptr; i++) {
        pindex = pindex->pprev;
        int64_t time = pindex->GetBlockTime();
        minTime = std::min(time, minTime);
        maxTime = std::max(time, maxTime);
    }
    arith_uint256 workDiff = tip->nChainWork - pindex->nChainWork;
    int64_t timeDiff = maxTime - minTime;
    double nNetworkHashPS = workDiff.getdouble() / timeDiff;

    statsClient.gaugeDouble("network.hashesPerSecond", nNetworkHashPS);
    statsClient.gaugeDouble("network.terahashesPerSecond", nNetworkHashPS / 1e12);
    statsClient.gaugeDouble("network.petahashesPerSecond", nNetworkHashPS / 1e15);
    statsClient.gaugeDouble("network.exahashesPerSecond", nNetworkHashPS / 1e18);
    // No need for cs_main, we never use null tip here
    statsClient.gaugeDouble("network.difficulty", (double) GetDifficulty(tip));

    statsClient.gauge("transactions.txCacheSize", WITH_LOCK(cs_main,
    return ::ChainstateActive().CoinsTip().GetCacheSize()), 1.0f);
    statsClient.gauge("transactions.totalTransactions", tip->nChainTx, 1.0f);

    statsClient.gauge("transactions.mempool.totalTransactions", mempool.size(), 1.0f);
    statsClient.gauge("transactions.mempool.totalTxBytes", (int64_t) mempool.GetTotalTxSize(), 1.0f);
    statsClient.gauge("transactions.mempool.memoryUsageBytes", (int64_t) mempool.DynamicMemoryUsage(), 1.0f);
    statsClient.gauge("transactions.mempool.minFeePerKb",
                      mempool.GetMinFee(gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000).GetFeePerK(),
                      1.0f);
}

/** Sanity checks
 *  Ensure that Raptoreum Core is running in a usable environment with all
 *  necessary library support.
 */
static bool InitSanityCheck() {
    if (!ECC_InitSanityCheck()) {
        return InitError("Elliptic curve cryptography sanity check failure. Aborting.");
    }

    if (!glibc_sanity_test() || !glibcxx_sanity_test())
        return false;

    if (!BLSInit()) {
        return false;
    }

    if (!Random_SanityCheck()) {
        return InitError("OS cryptographic RNG sanity check failure. Aborting.");
    }

    if (!ChronoSanityCheck()) {
        return InitError("Clock epoch mismatch. Aborting.");
    }

    return true;
}

static bool AppInitServers(const util::Ref &context, NodeContext &node) {
    RPCServer::OnStarted(&OnRPCStarted);
    RPCServer::OnStopped(&OnRPCStopped);
    if (!InitHTTPServer())
        return false;
    StartRPC();
    node.rpc_interruption_point = RpcInterruptionPoint;
    if (!StartHTTPRPC(context))
        return false;
    if (gArgs.GetBoolArg("-rest", DEFAULT_REST_ENABLE) && !StartREST(context))
        return false;
    StartHTTPServer();
    return true;
}

// Parameter interaction based on rules
void InitParameterInteraction() {
    // when specifying an explicit binding address, you want to listen on it
    // even when -connect or -proxy is specified
    if (gArgs.IsArgSet("-bind")) {
        if (gArgs.SoftSetBoolArg("-listen", true))
            LogPrintf("%s: parameter interaction: -bind set -> setting -listen=1\n", __func__);
    }
    if (gArgs.IsArgSet("-whitebind")) {
        if (gArgs.SoftSetBoolArg("-listen", true))
            LogPrintf("%s: parameter interaction: -whitebind set -> setting -listen=1\n", __func__);
    }

    if (gArgs.IsArgSet("-connect")) {
        // when only connecting to trusted nodes, do not seed via DNS, or listen by default
        if (gArgs.SoftSetBoolArg("-dnsseed", false))
            LogPrintf("%s: parameter interaction: -connect set -> setting -dnsseed=0\n", __func__);
        if (gArgs.SoftSetBoolArg("-listen", false))
            LogPrintf("%s: parameter interaction: -connect set -> setting -listen=0\n", __func__);
    }

    if (gArgs.IsArgSet("-proxy")) {
        // to protect privacy, do not listen by default if a default proxy server is specified
        if (gArgs.SoftSetBoolArg("-listen", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -listen=0\n", __func__);
        // to protect privacy, do not map ports when a proxy is set. The user may still specify -listen=1
        // to listen locally, so don't rely on this happening through -listen below.
        if (gArgs.SoftSetBoolArg("-upnp", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -upnp=0\n", __func__);
        if (gArgs.SoftSetBoolArg("-natpmp", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -natpmp=0\n", __func__);
        // to protect privacy, do not discover addresses by default
        if (gArgs.SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -discover=0\n", __func__);
    }

    if (!gArgs.GetBoolArg("-listen", DEFAULT_LISTEN)) {
        // do not map ports or try to retrieve public IP when not listening (pointless)
        if (gArgs.SoftSetBoolArg("-upnp", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -upnp=0\n", __func__);
        if (gArgs.SoftSetBoolArg("-natmpm", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -natpmp=0\n", __func__);
        if (gArgs.SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -discover=0\n", __func__);
        if (gArgs.SoftSetBoolArg("-listenonion", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -listenonion=0\n", __func__);
    }

    if (gArgs.IsArgSet("-externalip")) {
        // if an explicit public IP is specified, do not try to find others
        if (gArgs.SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -externalip set -> setting -discover=0\n", __func__);
    }

    // disable whitelistrelay in blocksonly mode
    if (gArgs.GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY)) {
        if (gArgs.SoftSetBoolArg("-whitelistrelay", false))
            LogPrintf("%s: parameter interaction: -blocksonly=1 -> setting -whitelistrelay=0\n", __func__);
    }

    // Forcing relay from whitelisted hosts implies we will accept relays from them in the first place.
    if (gArgs.GetBoolArg("-whitelistforcerelay", DEFAULT_WHITELISTFORCERELAY)) {
        if (gArgs.SoftSetBoolArg("-whitelistrelay", true))
            LogPrintf("%s: parameter interaction: -whitelistforcerelay=1 -> setting -whitelistrelay=1\n", __func__);
    }

    int64_t nPruneArg = gArgs.GetArg("-prune", 0);
    if (nPruneArg > 0) {
        if (gArgs.SoftSetBoolArg("-disablegovernance", true)) {
            LogPrintf("%s: parameter interaction: -prune=%d -> setting -disablegovernance=true\n", __func__, nPruneArg);
        }
        if (gArgs.SoftSetBoolArg("-txindex", false)) {
            LogPrintf("%s: parameter interaction: -prune=%d -> setting -txindex=false\n", __func__, nPruneArg);
        }
    }

    // Make sure additional indexes are recalculated correctly in VerifyDB
    // (we must reconnect blocks whenever we disconnect them for these indexes to work)
    bool fAdditionalIndexes =
            gArgs.GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX) ||
            gArgs.GetBoolArg("-spentindex", DEFAULT_SPENTINDEX) ||
            gArgs.GetBoolArg("-timestampindex", DEFAULT_TIMESTAMPINDEX) ||
            gArgs.GetBoolArg("-futureindex", DEFAULT_FUTUREINDEX);

    if (fAdditionalIndexes && gArgs.GetArg("-checklevel", DEFAULT_CHECKLEVEL) < 4) {
        gArgs.ForceSetArg("-checklevel", "4");
        LogPrintf("%s: parameter interaction: additional indexes -> setting -checklevel=4\n", __func__);
    }

    if (gArgs.IsArgSet("-smartnodeblsprivkey") && gArgs.SoftSetBoolArg("-disablewallet", true)) {
        LogPrintf("%s: parameter interaction: -smartnodeblsprivkey set -> setting -disablewallet=1\n", __func__);
    }

    // Warn if network-specific options (-addnode, -connect, etc)
    // are specified at default section in config file, but them are
    // not overridden at the command line or in this network's
    // section at the config file.
    std::string network = gArgs.GetChainName();
    for (const auto &arg: gArgs.GetUnsuitableSectionOnlyArgs()) {
        InitWarning(
                strprintf(_("Config settings for %s only applied on %s network when in [%s] section."), arg, network,
                          network));
    }

    // Warn if unrecognized section name are present in the config file.
    for (const auto &section: gArgs.GetUnrecognizedSections()) {
        InitWarning(strprintf("%s:%i " + _("Section [%s] is not recognized."), section.m_file, section.m_line,
                              section.m_name));
    }
}

static std::string ResolveErrMsg(const char *const optname, const std::string &strBind) {
    return strprintf(_("Cannot resolve -%s address: '%s'"), optname, strBind);
}

/**
 * Initialize global loggers.
 *
 * Note that this is called very early in the process lifetime,
 * so you should be careful about what global state you rely on here.
 */
void InitLogging() {
    LogInstance().m_print_to_file = !gArgs.IsArgNegated("-debuglogfile");
    LogInstance().m_file_path = AbsPathForConfigVal(gArgs.GetArg("-debuglogfile", DEFAULT_DEBUGLOGFILE));
    LogInstance().m_print_to_console = gArgs.GetBoolArg("-printtoconsole", !gArgs.GetBoolArg("-daemon", false));
    LogInstance().m_log_timestamps = gArgs.GetBoolArg("-logtimestamps", DEFAULT_LOGTIMESTAMPS);
    LogInstance().m_log_time_micros = gArgs.GetBoolArg("-logtimemicros", DEFAULT_LOGTIMEMICROS);
#ifdef HAVE_THREAD_LOCAL
    LogInstance().m_log_threadnames = gArgs.GetBoolArg("-logthreadnames", DEFAULT_LOGTHREADNAMES);
#endif
    LogInstance().m_log_sourcelocations = gArgs.GetBoolArg("-logsourcelocations", DEFAULT_LOGSOURCELOCATIONS);

    fLogIPs = gArgs.GetBoolArg("-logips", DEFAULT_LOGIPS);

    std::string version_string = FormatFullVersion();
#ifdef DEBUG_CORE
    version_string += " (debug build)";
#else
    version_string += " (release build)";
#endif
    LogPrintf(PACKAGE_NAME
    " version %s\n", version_string);
}

namespace { // Variables internal to initialization process only

    int nMaxConnections;
    int nUserMaxConnections;
    int nFD;
    ServiceFlags nLocalServices = ServiceFlags(NODE_NETWORK | NODE_NETWORK_LIMITED);
    int64_t peer_connect_timeout;

} // namespace

[[noreturn]] static void new_handler_terminate() {
    // Rather than throwing std::bad-alloc if allocation fails, terminate
    // immediately to (try to) avoid chain corruption.
    // Since LogPrintf may itself allocate memory, set the handler directly
    // to terminate first.
    std::set_new_handler(std::terminate);
    LogPrintf("Error: Out of memory. Terminating.\n");

    // The log was successful, terminate now.
    std::terminate();
};

bool AppInitBasicSetup() {
    // ********************************************************* Step 1: setup
#ifdef _MSC_VER
    // Turn off Microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFileA("NUL", GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, 0));
    // Disable confusing "helpful" text message on abort, Ctrl-C
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifdef WIN32
    // Enable heap terminate-on-corruption
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
#endif
    if (!InitShutdownState()) {
        return InitError("Initializing wait-for-shutdown state failed.");
    }

    if (!SetupNetworking()) {
        return InitError("Initializing networking failed");
    }

#ifndef WIN32
    if (!gArgs.GetBoolArg("-sysperms", false)) {
        umask(077);
    }

    // Clean shutdown on SIGTERM
    registerSignalHandler(SIGTERM, HandleSIGTERM);
    registerSignalHandler(SIGINT, HandleSIGTERM);

    // Reopen debug.log on SIGHUP
    registerSignalHandler(SIGHUP, HandleSIGHUP);

    // Ignore SIGPIPE, otherwise it will bring the daemon down if the client closes unexpectedly
    signal(SIGPIPE, SIG_IGN);
#else
    SetConsoleCtrlHandler(consoleCtrlHandler, true);
#endif

    std::set_new_handler(new_handler_terminate);

    return true;
}

bool AppInitParameterInteraction() {
    const CChainParams &chainparams = Params();
    // ********************************************************* Step 2: parameter interactions

    // also see: InitParameterInteraction()

    if (!fs::is_directory(GetBlocksDir())) {
        return InitError(
                strprintf(_("Specified blocks directory \"%s\" does not exist."), gArgs.GetArg("-blocksdir", "")));
    }

    // if using block pruning, then disallow txindex and require disabling governance validation
    if (gArgs.GetArg("-prune", 0)) {
        if (gArgs.GetBoolArg("-txindex", DEFAULT_TXINDEX))
            return InitError(_("Prune mode is incompatible with -txindex."));
        if (!gArgs.GetBoolArg("-disablegovernance", false)) {
            return InitError(_("Prune mode is incompatible with -disablegovernance=false."));
        }
    }

    if (gArgs.IsArgSet("-devnet")) {
        // Require setting of ports when running devnet
        if (gArgs.GetArg("-listen", DEFAULT_LISTEN) && !gArgs.IsArgSet("-port")) {
            return InitError(_("-port must be specified when -devnet and -listen are specified"));
        }
        if (gArgs.GetArg("-server", false) && !gArgs.IsArgSet("-rpcport")) {
            return InitError(_("-rpcport must be specified when -devnet and -server are specified"));
        }
        if (gArgs.GetArgs("-devnet").size() > 1) {
            return InitError(_("-devnet can only be specified once"));
        }
    }

    fAllowPrivateNet = gArgs.GetBoolArg("-allowprivatenet", DEFAULT_ALLOWPRIVATENET);

    // -bind and -whitebind can't be set when not listening
    size_t nUserBind = gArgs.GetArgs("-bind").size() + gArgs.GetArgs("-whitebind").size();
    if (nUserBind != 0 && !gArgs.GetBoolArg("-listen", DEFAULT_LISTEN)) {
        return InitError("Cannot set -bind or -whitebind together with -listen=0");
    }

    // Make sure enough file descriptors are available
    int nBind = std::max(nUserBind, size_t(1));
    nUserMaxConnections = gArgs.GetArg("-maxconnections", DEFAULT_MAX_PEER_CONNECTIONS);
    nMaxConnections = std::max(nUserMaxConnections, 0);

    // Trim requested connection counts, to fit into system limitations
    // <int> in std::min<int>(...) to work around FreeBSD compilation issue described in #2695
    nFD = RaiseFileDescriptorLimit(nMaxConnections + MIN_CORE_FILEDESCRIPTORS + MAX_ADDNODE_CONNECTIONS);
#ifdef USE_POLL
    int fd_max = nFD;
#else
    int fd_max = FD_SETSIZE;
#endif
    nMaxConnections = std::max(
            std::min<int>(nMaxConnections, fd_max - nBind - MIN_CORE_FILEDESCRIPTORS - MAX_ADDNODE_CONNECTIONS), 0);
    if (nFD < MIN_CORE_FILEDESCRIPTORS)
        return InitError(_("Not enough file descriptors available."));
    nMaxConnections = std::min(nFD - MIN_CORE_FILEDESCRIPTORS - MAX_ADDNODE_CONNECTIONS, nMaxConnections);

    if (nMaxConnections < nUserMaxConnections)
        InitWarning(strprintf(_("Reducing -maxconnections from %d to %d, because of system limitations."),
                              nUserMaxConnections, nMaxConnections));

    // ********************************************************* Step 3: parameter-to-internal-flags
    if (gArgs.IsArgSet("-debug")) {
        // Special-case: if -debug=0/-nodebug is set, turn off debugging messages
        const std::vector <std::string> categories = gArgs.GetArgs("-debug");

        if (std::none_of(categories.begin(), categories.end(),
                         [](std::string cat) { return cat == "0" || cat == "none"; })) {
            for (const auto &cat: categories) {
                if (!LogInstance().EnableCategory(cat)) {
                    InitWarning(strprintf(_("Unsupported logging category %s=%s."), "-debug", cat));
                }
            }
        }
    }

    // Now remove the logging categories which were explicitly excluded
    for (const std::string &cat: gArgs.GetArgs("-debugexclude")) {
        if (!LogInstance().DisableCategory(cat)) {
            InitWarning(strprintf(_("Unsupported logging category %s=%s."), "-debugexclude", cat));
        }
    }

    // Checkmempool and checkblockindex default to true in regtest mode
    int ratio = std::min<int>(
            std::max<int>(gArgs.GetArg("-checkmempool", chainparams.DefaultConsistencyChecks() ? 1 : 0), 0), 1000000);
    if (ratio != 0) {
        mempool.setSanityCheck(1.0 / ratio);
    }
    fCheckBlockIndex = gArgs.GetBoolArg("-checkblockindex", chainparams.DefaultConsistencyChecks());
    fCheckpointsEnabled = gArgs.GetBoolArg("-checkpoints", DEFAULT_CHECKPOINTS_ENABLED);

    hashAssumeValid = uint256S(gArgs.GetArg("-assumevalid", chainparams.GetConsensus().defaultAssumeValid.GetHex()));
    if (!hashAssumeValid.IsNull())
        LogPrintf("Assuming ancestors of block %s have valid signatures.\n", hashAssumeValid.GetHex());
    else
        LogPrintf("Validating signatures for all blocks.\n");

    if (gArgs.IsArgSet("-minimumchainwork")) {
        const std::string minChainWorkStr = gArgs.GetArg("-minimumchainwork", "");
        if (!IsHexNumber(minChainWorkStr)) {
            return InitError(strprintf("Invalid non-hex (%s) minimum chain work value specified", minChainWorkStr));
        }
        nMinimumChainWork = UintToArith256(uint256S(minChainWorkStr));
    } else {
        nMinimumChainWork = UintToArith256(chainparams.GetConsensus().nMinimumChainWork);
    }
    LogPrintf("Setting nMinimumChainWork=%s\n", nMinimumChainWork.GetHex());
    if (nMinimumChainWork < UintToArith256(chainparams.GetConsensus().nMinimumChainWork)) {
        LogPrintf("Warning: nMinimumChainWork set below default value of %s\n",
                  chainparams.GetConsensus().nMinimumChainWork.GetHex());
    }

    // mempool limits
    int64_t nMempoolSizeMax = gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    int64_t nMempoolSizeMin = gArgs.GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT) * 1000 * 40;
    if (nMempoolSizeMax < 0 || nMempoolSizeMax < nMempoolSizeMin)
        return InitError(strprintf(_("-maxmempool must be at least %d MB"), std::ceil(nMempoolSizeMin / 1000000.0)));
    // incremental relay fee sets the minimum feerate increase necessary for BIP 125 replacement in the mempool
    // and the amount the mempool min fee increases above the feerate of txs evicted due to mempool limiting.
    if (gArgs.IsArgSet("-incrementalrelayfee")) {
        CAmount n = 0;
        if (!ParseMoney(gArgs.GetArg("-incrementalrelayfee", ""), n))
            return InitError(AmountErrMsg("incrementalrelayfee", gArgs.GetArg("-incrementalrelayfee", "")));
        incrementalRelayFee = CFeeRate(n);
    }

    // block pruning; get the amount of disk space (in MiB) to allot for block & undo files
    int64_t nPruneArg = gArgs.GetArg("-prune", 0);
    if (nPruneArg < 0) {
        return InitError(_("Prune cannot be configured with a negative value."));
    }
    nPruneTarget = (uint64_t) nPruneArg * 1024 * 1024;
    if (nPruneArg == 1) {  // manual pruning: -prune=1
        LogPrintf(
                "Block pruning enabled.  Use RPC call pruneblockchain(height) to manually prune block and undo files.\n");
        nPruneTarget = std::numeric_limits<uint64_t>::max();
        fPruneMode = true;
    } else if (nPruneTarget) {
        if (gArgs.GetBoolArg("-regtest", false)) {
            // we use 1MB blocks to test this on regtest
            if (nPruneTarget < 550 * 1024 * 1024) {
                return InitError(
                        strprintf(_("Prune configured below the minimum of %d MiB.  Please use a higher number."),
                                  550));
            }
        } else {
            if (nPruneTarget < MIN_DISK_SPACE_FOR_BLOCK_FILES) {
                return InitError(
                        strprintf(_("Prune configured below the minimum of %d MiB.  Please use a higher number."),
                                  MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024));
            }
        }
        LogPrintf("Prune configured to target %uMiB on disk for block and undo files.\n", nPruneTarget / 1024 / 1024);
        fPruneMode = true;
    }

    nConnectTimeout = gArgs.GetArg("-timeout", DEFAULT_CONNECT_TIMEOUT);
    if (nConnectTimeout <= 0) {
        nConnectTimeout = DEFAULT_CONNECT_TIMEOUT;
    }

    peer_connect_timeout = gArgs.GetArg("-peertimeout", DEFAULT_PEER_CONNECT_TIMEOUT);
    if (peer_connect_timeout <= 0) {
        return InitError("peertimeout cannot be configured with a negative value.");
    }

    if (gArgs.IsArgSet("-minrelaytxfee")) {
        CAmount n = 0;
        if (!ParseMoney(gArgs.GetArg("-minrelaytxfee", ""), n)) {
            return InitError(AmountErrMsg("minrelaytxfee", gArgs.GetArg("-minrelaytxfee", "")));
        }
        // High fee check is done afterward in WalletParameterInteraction()
        ::minRelayTxFee = CFeeRate(n);
    } else if (incrementalRelayFee > ::minRelayTxFee) {
        // Allow only setting incrementalRelayFee to control both
        ::minRelayTxFee = incrementalRelayFee;
        LogPrintf("Increasing minrelaytxfee to %s to match incrementalrelayfee\n", ::minRelayTxFee.ToString());
    }

    // Sanity check argument for min fee for including tx in block
    // TODO: Harmonize which arguments need sanity checking and where that happens
    if (gArgs.IsArgSet("-blockmintxfee")) {
        CAmount n = 0;
        if (!ParseMoney(gArgs.GetArg("-blockmintxfee", ""), n))
            return InitError(AmountErrMsg("blockmintxfee", gArgs.GetArg("-blockmintxfee", "")));
    }

    // Feerate used to define dust.  Shouldn't be changed lightly as old
    // implementations may inadvertently create non-standard transactions
    if (gArgs.IsArgSet("-dustrelayfee")) {
        CAmount n = 0;
        if (!ParseMoney(gArgs.GetArg("-dustrelayfee", ""), n))
            return InitError(AmountErrMsg("dustrelayfee", gArgs.GetArg("-dustrelayfee", "")));
        dustRelayFee = CFeeRate(n);
    }

    fRequireStandard = !gArgs.GetBoolArg("-acceptnonstdtxn", !chainparams.RequireStandard());
    if (chainparams.RequireStandard() && !fRequireStandard)
        return InitError(
                strprintf("acceptnonstdtxn is not currently supported for %s chain", chainparams.NetworkIDString()));
    nBytesPerSigOp = gArgs.GetArg("-bytespersigop", nBytesPerSigOp);

    if (!g_wallet_init_interface.ParameterInteraction()) return false;

    fIsBareMultisigStd = gArgs.GetBoolArg("-permitbaremultisig", DEFAULT_PERMIT_BAREMULTISIG);
    fAcceptDatacarrier = gArgs.GetBoolArg("-datacarrier", DEFAULT_ACCEPT_DATACARRIER);
    nMaxDatacarrierBytes = gArgs.GetArg("-datacarriersize", nMaxDatacarrierBytes);

    // Option to startup with mocktime set (used for regression testing):
    SetMockTime(gArgs.GetArg("-mocktime", 0)); // SetMockTime(0) is a no-op

    if (gArgs.GetBoolArg("-peerbloomfilters", DEFAULT_PEERBLOOMFILTERS))
        nLocalServices = ServiceFlags(nLocalServices | NODE_BLOOM);

    nMaxTipAge = gArgs.GetArg("-maxtipage", DEFAULT_MAX_TIP_AGE);

    if (gArgs.IsArgSet("-smartnodeblsprivkey")) {
        if (!gArgs.GetBoolArg("-listen", DEFAULT_LISTEN) && Params().RequireRoutableExternalIP()) {
            return InitError("Smartnode must accept connections from outside, set -listen=1");
        }
        if (!gArgs.GetBoolArg("-txindex", DEFAULT_TXINDEX)) {
            return InitError("Smartnode must have transaction index enabled, set -txindex=1");
        }
        if (!gArgs.GetBoolArg("-peerbloomfilters", DEFAULT_PEERBLOOMFILTERS)) {
            return InitError("Smartnode must have bloom filters enabled, set -peerbloomfilters=1");
        }
        if (gArgs.GetArg("-prune", 0) > 0) {
            return InitError("Smartnode must have no pruning enabled, set -prune=0");
        }
        if (gArgs.GetArg("-maxconnections", DEFAULT_MAX_PEER_CONNECTIONS) < DEFAULT_MAX_PEER_CONNECTIONS) {
            return InitError(
                    strprintf("Smartnode must be able to handle at least %d connections, set -maxconnections=%d",
                              DEFAULT_MAX_PEER_CONNECTIONS, DEFAULT_MAX_PEER_CONNECTIONS));
        }
        if (gArgs.GetBoolArg("-disablegovernance", false)) {
            return InitError(_("You can not disable governance validation on a smartnode."));
        }
    }

    fDisableGovernance = gArgs.GetBoolArg("-disablegovernance", false);
    LogPrintf("fDisableGovernance %d\n", fDisableGovernance);

    if (fDisableGovernance) {
        InitWarning(_("You are starting with governance validation disabled.") +
                    (fPruneMode ? " " + _("This is expected because you are running a pruned node.") : ""));
    }

    return true;
}

static bool LockDataDirectory(bool probeOnly) {
    // Make sure only a single Raptoreum Core process is using the data directory.
    fs::path datadir = GetDataDir();
    if (!DirIsWritable(datadir)) {
        return InitError(strprintf(_("Cannot write to data directory '%s'; check permissions."), datadir.string()));
    }
    if (!LockDirectory(datadir, ".lock", probeOnly)) {
        return InitError(strprintf(_("Cannot obtain a lock on data directory %s. %s is probably already running."),
                                   datadir.string(), PACKAGE_NAME));
    }
    return true;
}

bool AppInitSanityChecks() {
    // ********************************************************* Step 4: sanity checks

    // Initialize elliptic curve code
    std::string sha256_algo = SHA256AutoDetect();
    LogPrintf("Using the '%s' SHA256 implementation\n", sha256_algo);
    RandomInit();
    ECC_Start();
    globalVerifyHandle.reset(new ECCVerifyHandle());

    // Sanity check
    if (!InitSanityCheck())
        return InitError(strprintf(_("Initialization sanity check failed. %s is shutting down."), PACKAGE_NAME));

    // Probe the data directory lock to give an early error message, if possible
    // We cannot hold the data directory lock here, as the forking for daemon() hasn't yet happened,
    // and a fork will cause weird behavior to it.
    return LockDataDirectory(true);
}

bool AppInitLockDataDirectory() {
    // After daemonization get the data directory lock again and hold on to it until exit
    // This creates a slight window for a race condition to happen, however this condition is harmless: it
    // will at most make us exit without printing a message to console.
    if (!LockDataDirectory(false)) {
        // Detailed error printed inside LockDataDirectory
        return false;
    }
    return true;
}

bool AppInitInterfaces(NodeContext &node) {
    node.chain = interfaces::MakeChain(node);
    // Create client interfaces for wallets that are supposed to be loaded
    // according to -wallet and -disablewallet options. This only constructs
    // the interfaces. It doesn't load wallet data. Wallets are actually
    // get loaded when load() and start() interface methods are called below.
    g_wallet_init_interface.Construct(node);
    return true;
}

bool AppInitMain(const util::Ref &context, NodeContext &node, interfaces::BlockAndHeaderTipInfo *tip_info) {
    const CChainParams &chainparams = Params();
    // ********************************************************* Step 4a: application initialization
#ifndef WIN32
    if (!CreatePidFile()) {
        // Detailed error printed inside CreatePidFile().
        return false;
    }
#endif
    if (LogInstance().m_print_to_file) {
        if (gArgs.GetBoolArg("-shrinkdebugfile", LogInstance().DefaultShrinkDebugFile())) {
            // Do this first since it both loads a bunch of debug.log into memory,
            // and because this needs to happen before any other debug.log printing
            LogInstance().ShrinkDebugFile();
        }
    }
    if (!LogInstance().StartLogging()) {
        return InitError(strprintf("Could not open debug log file %s", LogInstance().m_file_path.string()));
    }

    if (!LogInstance().m_log_timestamps)
        LogPrintf("Startup time: %s\n", FormatISO8601DateTime(GetTime()));
    LogPrintf("Default data directory %s\n", GetDefaultDataDir().string());
    LogPrintf("Using data directory %s\n", GetDataDir().string());

    fs::path config_file_path = GetConfigFile(gArgs.GetArg("-conf", BITCOIN_CONF_FILENAME));
    if (fs::exists(config_file_path)) {
        LogPrintf("Config file: %s\n", config_file_path.string());
    } else if (gArgs.IsArgSet("-conf")) {
        InitWarning(strprintf(_("The specified config file %s does not exist\n"), config_file_path.string()));
    } else {
        LogPrintf("Config file: %s not found, skipping\n", config_file_path.string());
    }

    LogPrintf("Using at most %i automatic connections (%i file descriptors available)\n", nMaxConnections, nFD);

    // Warn about relative -datadir path.
    if (gArgs.IsArgSet("-datadir") && !fs::path(gArgs.GetArg("-datadir", "")).is_absolute()) {
        LogPrintf(
                "Warning: relative datadir option '%s' specified, which will be interpreted relative to the " /* Continued */
                "current working directory '%s'. This is fragile, because if Raptoreum Core is started in the future "
                "from a different location, it will be unable to locate the current data files. There could "
                "also be data loss if Raptoreum Core is started while in a temporary directory.\n",
                gArgs.GetArg("-datadir", ""), fs::current_path().string());
    }

    InitSignatureCache();
    InitScriptExecutionCache();

    int script_threads = gArgs.GetArg("-par", DEFAULT_SCRIPTCHECK_THREADS);
    if (script_threads <= 0) {
        // -par=0 means autodetect (number of cores - 1 script threads)
        // -par=-n means "leave n cores free" (number of cores - n - 1 script threads)
        script_threads += GetNumCores();
    }

    // Subtract 1 because the main thread counts towards the par threads
    script_threads = std::max(script_threads - 1, 0);

    // Number of script-checking threads <= MAX_SCRIPTCHECK_THREADS
    script_threads = std::min(script_threads, MAX_SCRIPTCHECK_THREADS);

    LogPrintf("Script verification uses %d additional threads\n", script_threads);
    if (script_threads >= 1) {
        g_parallel_script_checks = true;
        StartScriptCheckWorkerThreads(script_threads);
    }

    std::vector <std::string> vSporkAddresses;
    if (gArgs.IsArgSet("-sporkaddr")) {
        vSporkAddresses = gArgs.GetArgs("-sporkaddr");
    } else {
        vSporkAddresses = Params().SporkAddresses();
    }
    for (const auto &address: vSporkAddresses) {
        if (!sporkManager.SetSporkAddress(address)) {
            return InitError(_("Invalid spork address specified with -sporkaddr"));
        }
    }

    int minsporkkeys = gArgs.GetArg("-minsporkkeys", Params().MinSporkKeys());
    if (!sporkManager.SetMinSporkKeys(minsporkkeys)) {
        return InitError(_("Invalid minimum number of spork signers specified with -minsporkkeys"));
    }


    if (gArgs.IsArgSet("-sporkkey")) { // spork priv key
        if (!sporkManager.SetPrivKey(gArgs.GetArg("-sporkkey", ""))) {
            return InitError(_("Unable to sign spork message, wrong key?"));
        }
    }

    assert(!node.scheduler);
    node.scheduler = MakeUnique<CScheduler>();

    // Start the lightweight task scheduler thread
    CScheduler::Function serviceLoop = [&node] { node.scheduler->serviceQueue(); };
    threadGroup.create_thread(std::bind(&TraceThread < CScheduler::Function > , "scheduler", serviceLoop));

    // Gather some entropy once per minute.
    node.scheduler->scheduleEvery([] { RandAddPeriodic(); }, 60000);

    GetMainSignals().RegisterBackgroundSignalScheduler(*node.scheduler);
    GetMainSignals().RegisterWithMempoolSignals(mempool);

    tableRPC.InitPlatformRestrictions();

    /* Register RPC commands regardless of -server setting so they will be
     * available in the GUI RPC console even if external calls are disabled.
     */
    RegisterAllCoreRPCCommands(tableRPC);
    for (const auto &client: node.chain_clients) {
        client->registerRpcs();
    }
#if ENABLE_ZMQ
    RegisterZMQRPCCommands(tableRPC);
#endif

    /* Start the RPC server already.  It will be started in "warmup" mode
     * and not really process calls already (but it will signify connections
     * that the server is there and will be ready later).  Warmup mode will
     * be disabled when initialisation is finished.
     */
    if (gArgs.GetBoolArg("-server", false)) {
        uiInterface.InitMessage_connect(SetRPCWarmupStatus);
        if (!AppInitServers(context, node))
            return InitError(_("Unable to start HTTP server. See debug log for details."));
    }

    // ********************************************************* Step 5: verify wallet database integrity

    if (!g_wallet_init_interface.InitAutoBackup()) return false;
    for (const auto &client: node.chain_clients) {
        if (!client->verify()) {
            return false;
        }
    }

    // ********************************************************* Step 6: network initialization
    // Note that we absolutely cannot open any actual connections
    // until the very end ("start node") as the UTXO/block state
    // is not yet setup and may end up being set up twice if we
    // need to reindex later.

    assert(!node.banman);
    node.banman = MakeUnique<BanMan>(GetDataDir() / "banlist.dat", &uiInterface,
                                     gArgs.GetArg("-bantime", DEFAULT_MISBEHAVING_BANTIME));
    assert(!node.connman);
    node.connman = std::make_unique<CConnman>(GetRand(std::numeric_limits<uint64_t>::max()),
                                              GetRand(std::numeric_limits<uint64_t>::max()));

    // Make mempool generally available in the node context. For example the connection manager, wallet or RPC threads,
    // which are all started after this, may use it from the node context.
    assert(!node.mempool);
    node.mempool = &::mempool;
    assert(!node.chainman);
    node.chainman = &g_chainman;
    ChainstateManager &chainman = EnsureChainman(node);

    node.peer_logic.reset(
            new PeerLogicValidation(node.connman.get(), node.banman.get(), *node.scheduler, *node.chainman,
                                    *node.mempool, gArgs.GetBoolArg("-enablebip61", DEFAULT_ENABLE_BIP61)));
    RegisterValidationInterface(node.peer_logic.get());

    // sanitize comments per BIP-0014, format user agent and check total size
    std::vector <std::string> uacomments;

    if (chainparams.NetworkIDString() == CBaseChainParams::DEVNET) {
        // Add devnet name to user agent. This allows to disconnect nodes immediately if they don't belong to our own devnet
        uacomments.push_back(strprintf("devnet=%s", gArgs.GetDevNetName()));
    }

    for (const std::string &cmt: gArgs.GetArgs("-uacomment")) {
        if (cmt != SanitizeString(cmt, SAFE_CHARS_UA_COMMENT))
            return InitError(strprintf(_("User Agent comment (%s) contains unsafe characters."), cmt));
        uacomments.push_back(cmt);
    }
    strSubVersion = FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, uacomments);
    if (strSubVersion.size() > MAX_SUBVERSION_LENGTH) {
        return InitError(strprintf(
                _("Total length of network version string (%i) exceeds maximum length (%i). Reduce the number or size of uacomments."),
                strSubVersion.size(), MAX_SUBVERSION_LENGTH));
    }

    if (gArgs.IsArgSet("-onlynet")) {
        std::set<enum Network> nets;
        for (const std::string &snet: gArgs.GetArgs("-onlynet")) {
            enum Network net = ParseNetwork(snet);
            if (net == NET_UNROUTABLE)
                return InitError(strprintf(_("Unknown network specified in -onlynet: '%s'"), snet));
            nets.insert(net);
        }
        for (int n = 0; n < NET_MAX; n++) {
            enum Network net = (enum Network) n;
            if (!nets.count(net))
                SetLimited(net);
        }
    }

    // Check for host lookup allowed before parsing any network related parameters
    fNameLookup = gArgs.GetBoolArg("-dns", DEFAULT_NAME_LOOKUP);

    bool proxyRandomize = gArgs.GetBoolArg("-proxyrandomize", DEFAULT_PROXYRANDOMIZE);
    // -proxy sets a proxy for all outgoing network traffic
    // -noproxy (or -proxy=0) as well as the empty string can be used to not set a proxy, this is the default
    std::string proxyArg = gArgs.GetArg("-proxy", "");
    SetLimited(NET_ONION);
    if (proxyArg != "" && proxyArg != "0") {
        CService proxyAddr;
        if (!Lookup(proxyArg.c_str(), proxyAddr, 9050, fNameLookup)) {
            return InitError(strprintf(_("Invalid -proxy address or hostname: '%s'"), proxyArg));
        }

        proxyType addrProxy = proxyType(proxyAddr, proxyRandomize);
        if (!addrProxy.IsValid())
            return InitError(strprintf(_("Invalid -proxy address or hostname: '%s'"), proxyArg));

        SetProxy(NET_IPV4, addrProxy);
        SetProxy(NET_IPV6, addrProxy);
        SetProxy(NET_ONION, addrProxy);
        SetNameProxy(addrProxy);
        SetLimited(NET_ONION, false); // by default, -proxy sets onion as reachable, unless -noonion later
    }

    // -onion can be used to set only a proxy for .onion, or override normal proxy for .onion addresses
    // -noonion (or -onion=0) disables connecting to .onion entirely
    // An empty string is used to not override the onion proxy (in which case it defaults to -proxy set above, or none)
    std::string onionArg = gArgs.GetArg("-onion", "");
    if (onionArg != "") {
        if (onionArg == "0") { // Handle -noonion/-onion=0
            SetLimited(NET_ONION); // set onions as unreachable
        } else {
            CService onionProxy;
            if (!Lookup(onionArg.c_str(), onionProxy, 9050, fNameLookup)) {
                return InitError(strprintf(_("Invalid -onion address or hostname: '%s'"), onionArg));
            }
            proxyType addrOnion = proxyType(onionProxy, proxyRandomize);
            if (!addrOnion.IsValid())
                return InitError(strprintf(_("Invalid -onion address or hostname: '%s'"), onionArg));
            SetProxy(NET_ONION, addrOnion);
            SetLimited(NET_ONION, false);
        }
    }

    // see Step 2: parameter interactions for more information about these
    fListen = gArgs.GetBoolArg("-listen", DEFAULT_LISTEN);
    fDiscover = gArgs.GetBoolArg("-discover", true);
    fRelayTxes = !gArgs.GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY);

    for (const std::string &strAddr: gArgs.GetArgs("-externalip")) {
        CService addrLocal;
        if (Lookup(strAddr.c_str(), addrLocal, GetListenPort(), fNameLookup) && addrLocal.IsValid())
            AddLocal(addrLocal, LOCAL_MANUAL);
        else
            return InitError(ResolveErrMsg("externalip", strAddr));
    }

    // Read asmap file if configured
    if (gArgs.IsArgSet("-asmap")) {
        fs::path asmap_path = fs::path(gArgs.GetArg("-asmap", ""));
        if (asmap_path.empty()) {
            asmap_path = DEFAULT_ASMAP_FILENAME;
        }
        if (!asmap_path.is_absolute()) {
            asmap_path = GetDataDir() / asmap_path;
        }
        if (!fs::exists(asmap_path)) {
            InitError(strprintf(_("Could not find asmap file %s"), asmap_path));
            return false;
        }
        std::vector<bool> asmap = CAddrMan::DecodeAsmap(asmap_path);
        if (asmap.size() == 0) {
            InitError(strprintf(_("Could not parse asmap file %s"), asmap_path));
            return false;
        }
        const uint256 asmap_version = SerializeHash(asmap);
        node.connman->SetAsmap(std::move(asmap));
        LogPrintf("Using asmap version %s for IP bucketing\n", asmap_version.ToString());
    } else {
        LogPrintf("Using /16 prefix for IP bucketing\n");
    }

#if ENABLE_ZMQ
    g_zmq_notification_interface = CZMQNotificationInterface::Create();

    if (g_zmq_notification_interface) {
        RegisterValidationInterface(g_zmq_notification_interface);
    }
#endif

    pdsNotificationInterface = new CDSNotificationInterface(*node.connman);
    RegisterValidationInterface(pdsNotificationInterface);

    // ********************************************************* Step 7a: Load sporks

    uiInterface.InitMessage(_("Loading sporks cache..."));
    CFlatDB <CSporkManager> flatdb6("sporks.dat", "magicSporkCache");
    if (!flatdb6.Load(sporkManager)) {
        return InitError(_("Failed to load sporks cache from") + "\n" + (GetDataDir() / "sporks.dat").string());
    }

    // ********************************************************* Step 7b: load block chain

    fReindex = gArgs.GetBoolArg("-reindex", false);
    bool fReindexChainState = gArgs.GetBoolArg("-reindex-chainstate", false);

    // cache size calculations
    int64_t nTotalCache = (gArgs.GetArg("-dbcache", nDefaultDbCache) << 20);
    nTotalCache = std::max(nTotalCache, nMinDbCache << 20); // total cache cannot be less than nMinDbCache
    nTotalCache = std::min(nTotalCache, nMaxDbCache << 20); // total cache cannot be greater than nMaxDbcache
    int64_t nBlockTreeDBCache = std::min(nTotalCache / 8, nMaxBlockDBCache << 20);
    nTotalCache -= nBlockTreeDBCache;
    int64_t nTxIndexCache = std::min(nTotalCache / 8,
                                     gArgs.GetBoolArg("-txindex", DEFAULT_TXINDEX) ? nMaxTxIndexCache << 20 : 0);
    nTotalCache -= nTxIndexCache;
    int64_t nCoinDBCache = std::min(nTotalCache / 2,
                                    (nTotalCache / 4) + (1 << 23)); // use 25%-50% of the remainder for disk cache
    nCoinDBCache = std::min(nCoinDBCache, nMaxCoinsDBCache << 20); // cap total coins db cache
    nTotalCache -= nCoinDBCache;
    int64_t nCoinCacheUsage = nTotalCache; // the rest goes to in-memory cache
    int64_t nMempoolSizeMax = gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    int64_t nEvoDbCache = 1024 * 1024 * 16; // TODO
    LogPrintf("Cache configuration:\n");
    LogPrintf("* Using %.1fMiB for block index database\n", nBlockTreeDBCache * (1.0 / 1024 / 1024));
    if (gArgs.GetBoolArg("-txindex", DEFAULT_TXINDEX)) {
        LogPrintf("* Using %.1fMiB for transaction index database\n", nTxIndexCache * (1.0 / 1024 / 1024));
    }
    LogPrintf("* Using %.1fMiB for chain state database\n", nCoinDBCache * (1.0 / 1024 / 1024));
    LogPrintf("* Using %.1fMiB for in-memory UTXO set (plus up to %.1fMiB of unused mempool space)\n",
              nCoinCacheUsage * (1.0 / 1024 / 1024), nMempoolSizeMax * (1.0 / 1024 / 1024));

    bool fLoaded = false;

    while (!fLoaded && !ShutdownRequested()) {
        bool fReset = fReindex;
        auto is_coinsview_empty = [&](CChainState *chainstate)
        EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
        {
            return fReset || fReindexChainState || chainstate->CoinsTip().GetBestBlock().IsNull();
        };
        std::string strLoadError;

        uiInterface.InitMessage(_("Loading block index..."));

        do {
            bool failed_verification = false;
            const int64_t load_block_index_start_time = GetTimeMillis();

            try {
                LOCK(cs_main);
                chainman.InitializeChainstate();
                chainman.m_total_coinstip_cache = nCoinCacheUsage;
                chainman.m_total_coinsdb_cache = nCoinDBCache;

                UnloadBlockIndex(node.mempool);

                // new CBlockTreeDB tries to delete the existing file, which
                // fails if it's still open from the previous loop. Close it first:
                pblocktree.reset();
                pblocktree.reset(new CBlockTreeDB(nBlockTreeDBCache, false, fReset));

                passetsdb.reset();
                passetsdb.reset(new CAssetsDB(nBlockTreeDBCache, false, fReset));
                passetsCache.reset();
                passetsCache.reset(new CAssetsCache());

                if (!passetsdb->LoadAssets()) {
                    strLoadError = _("Failed to load Assets Database");
                    break;
                }

                llmq::DestroyLLMQSystem();
                // Same logic as above with pblocktree
                evoDb.reset();
                evoDb.reset(new CEvoDB(nEvoDbCache, false, fReset || fReindexChainState));
                deterministicMNManager.reset();
                deterministicMNManager.reset(new CDeterministicMNManager(*evoDb, *node.connman));

                llmq::InitLLMQSystem(*evoDb, *node.mempool, *node.connman, false, fReset || fReindexChainState);

                if (fReset) {
                    pblocktree->WriteReindexing(true);
                    //If we're reindexing in prune mode, wipe away unusable block files and all undo data files
                    if (fPruneMode)
                        CleanupBlockRevFiles();
                }

                if (ShutdownRequested()) break;

                // LoadBlockIndex will load fHavePruned if we've ever removed a
                // block file from disk.
                // Note that it also sets fReindex based on the disk flag!
                // From here on out fReindex and fReset mean something different!
                if (!chainman.LoadBlockIndex(chainparams)) {
                    strLoadError = _("Error loading block database");
                    break;
                }

                if (!fDisableGovernance && !gArgs.GetBoolArg("-txindex", DEFAULT_TXINDEX)
                    && chainparams.NetworkIDString() !=
                       CBaseChainParams::REGTEST) { // TODO remove this when pruning is fixed. See https://github.com/dashpay/dash/pull/1817 and https://github.com/dashpay/dash/pull/1743
                    return InitError(
                            _("Transaction index can't be disabled with governance validation enabled. Either start with -disablegovernance command line switch or enable transaction index."));
                }

                // If the loaded chain has a wrong genesis, bail out immediately
                // (we're likely using a testnet datadir, or the other way around).
                if (!::BlockIndex().empty() &&
                    !LookupBlockIndex(chainparams.GetConsensus().hashGenesisBlock)) {
                    return InitError(_("Incorrect or no genesis block found. Wrong datadir for network?"));
                }

                if (!chainparams.GetConsensus().hashDevnetGenesisBlock.IsNull() && !::BlockIndex().empty() &&
                    ::BlockIndex().count(chainparams.GetConsensus().hashDevnetGenesisBlock) == 0)
                    return InitError(
                            _("Incorrect or no devnet genesis block found. Wrong datadir for devnet specified?"));

                // Check for changed -addressindex state
                if (fAddressIndex != gArgs.GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX)) {
                    strLoadError = _("You need to rebuild the database using -reindex to change -addressindex");
                    break;
                }

                // Check for changed -timestampindex state
                if (fTimestampIndex != gArgs.GetBoolArg("-timestampindex", DEFAULT_TIMESTAMPINDEX)) {
                    strLoadError = _("You need to rebuild the database using -reindex to change -timestampindex");
                    break;
                }

                // Check for changed -spentindex state
                if (fSpentIndex != gArgs.GetBoolArg("-spentindex", DEFAULT_SPENTINDEX)) {
                    strLoadError = _("You need to rebuild the database using -reindex to change -spentindex");
                    break;
                }

                // Check for changed -futureindex state
                if (fFutureIndex != gArgs.GetBoolArg("-futureindex", DEFAULT_FUTUREINDEX)) {
                    strLoadError = _("You need to rebuild the database using -reindex to change -futureindex");
                    break;
                }

                // Check for changed -prune state.  What we are concerned about is a user who has pruned blocks
                // in the past, but is now trying to run unpruned.
                if (fHavePruned && !fPruneMode) {
                    strLoadError = _(
                            "You need to rebuild the database using -reindex to go back to unpruned mode.  This will redownload the entire blockchain");
                    break;
                }

                // At this point blocktree args are consistent with what's on disk.
                // If we're not mid-reindex (based on disk + args), add a genesis block on disk
                // (otherwise we use the one already on disk).
                // This is called again in ThreadImport after the reindex completes.
                if (!fReindex && !LoadGenesisBlock(chainparams)) {
                    strLoadError = _("Error initializing block database");
                    break;
                }

                // At this point we're either in reindex or we've loaded a useful
                // block tree into BlockIndex()!

                bool failed_chainstate_init = false;
                for (CChainState *chainstate: chainman.GetAll()) {
                    LogPrintf("Initializing chainstate %s\n", chainstate->ToString());
                    chainstate->InitCoinsDB(/* cache_size_bytes */ nCoinDBCache,
                            /* in_memory */ false,
                            /* should_wpe */ fReset || fReindexChainState);

                    chainstate->CoinsErrorCatcher().AddReadErrCallback([]() {
                        uiInterface.ThreadSafeMessageBox(_("Error reading from database, shutting down."),
                                                         "", CClientUIInterface::MSG_ERROR);
                    });

                    // If necessary, upgrade from older database format.
                    // This is a no-op if we cleared the coinsviewdb with -reindex or -reindex-chainstate
                    if (!chainstate->CoinsDB().Upgrade()) {
                        strLoadError = _("Error upgrading chainstate database");
                        failed_chainstate_init = true;
                        break;
                    }

                    // Replay blocks is a no-op if we cleared the coinsviewDB with -reindex or -reindex-chainstate
                    if (!chainstate->ReplayBlocks(chainparams)) {
                        strLoadError = _(
                                "Unable to rerply blocks. You will need tpo rebuild the database using -reindex-chainstate.");
                        failed_chainstate_init = true;
                        break;
                    }

                    // The on-disc coinsdb is now in a good state, create cache
                    chainstate->InitCoinsCache(nCoinCacheUsage);
                    assert(chainstate->CanFlushToDisk());

                    // flush evodb
                    if (&::ChainstateActive() == chainstate && !evoDb->CommitRootTransaction()) {
                        strLoadError = _("Failed to commit EvoDB");
                        failed_chainstate_init = true;
                        strLoadError = _("Error initializing block database");
                        break;
                    }

                    if (!is_coinsview_empty(chainstate)) {
                        // LoadChainTip initializes the chain based on CoinsTip()'s best block
                        if (!chainstate->LoadChainTip(chainparams)) {
                            strLoadError = _("Error initializing block database");
                            failed_chainstate_init = true;
                            break; // out of the per-chainstate loop
                        }
                        assert(chainstate->m_chain.Tip() != nullptr);
                    }
                }

                if (failed_chainstate_init) {
                    break; // out of the chainstate activation do-while
                }

                if (!deterministicMNManager->UpgradeDBIfNeeded() || !llmq::quorumBlockProcessor->UpgradeDB()) {
                    strLoadError = _("Error upgrading evo database");
                    break;
                }

                for (CChainState *chainstate: chainman.GetAll()) {
                    if (!is_coinsview_empty(chainstate)) {
                        uiInterface.InitMessage(_("Verifying blocks..."));
                        if (fHavePruned && gArgs.GetArg("-checkblocks", DEFAULT_CHECKBLOCKS) > MIN_BLOCKS_TO_KEEP) {
                            LogPrintf(
                                    "Prune: pruned datadir may not have more than %d blocks. Only checking available blocks\n",
                                    MIN_BLOCKS_TO_KEEP);
                        }

                        CBlockIndex *tip = chainstate->m_chain.Tip();
                        RPCNotifyBlockChange(true, tip);
                        if (tip && tip->nTime > GetAdjustedTime() + 7200) {
                            strLoadError = _("The block database contains a block which appears to be from the future. "
                                             "This may only be due to your computer's date and time being set incorrectly. "
                                             "Only rebuild the block database if you are sure that your computer's date and time are correct.");
                            failed_verification = true;
                            break;
                        }

                        // Only verify the DB of the active chainstate. This is fixed in later
                        // work when we allow VerifyDB to be parameterized by chainstate.
                        if (&::ChainstateActive() == chainstate &&
                            !CVerifyDB().VerifyDB(chainparams, &chainstate->CoinsDB(),
                                                  gArgs.GetArg("-checklevel", DEFAULT_CHECKLEVEL),
                                                  gArgs.GetArg("-checkblocks", DEFAULT_CHECKBLOCKS))) {
                            strLoadError = _("Corrupted block database detected");
                            failed_verification = true;
                            break;
                        }
                    } else {
                        if (&::ChainstateActive() == chainstate && !evoDb->IsEmpty()) {
                            // EvoDB processed some blocks earlier but we have no blocks anymore, something is not right
                            strLoadError = _("Error initializing block database");
                            failed_verification = true;
                            break;
                        }
                    }

                    if (gArgs.GetArg("-checklevel", DEFAULT_CHECKLEVEL) >= 3) {
                        ResetBlockFailureFlags(nullptr);
                    }
                }
            } catch (const std::exception &e) {
                LogPrintf("%s\n", e.what());
                strLoadError = _("Error opening block database");
                failed_verification = true;
                break;
            }

            if (!failed_verification) {
                fLoaded = true;
                LogPrintf(" block index %15dms\n", GetTimeMillis() - load_block_index_start_time);
            }
        } while (false);

        if (!fLoaded && !ShutdownRequested()) {
            // first suggest a reindex
            if (!fReset) {
                bool fRet = uiInterface.ThreadSafeQuestion(
                        strLoadError + ".\n\n" + _("Do you want to rebuild the block database now?"),
                        strLoadError + ".\nPlease restart with -reindex or -reindex-chainstate to recover.",
                        "", CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT);
                if (fRet) {
                    fReindex = true;
                    AbortShutdown();
                } else {
                    LogPrintf("Aborted block database rebuild. Exiting.\n");
                    return false;
                }
            } else {
                return InitError(strLoadError);
            }
        }
    }

    // As LoadBlockIndex can take several minutes, it's possible the user
    // requested to kill the GUI during the last operation. If so, exit.
    // As the program has not fully started yet, Shutdown() is possibly overkill.
    if (ShutdownRequested()) {
        LogPrintf("Shutdown requested. Exiting.\n");
        return false;
    }

    fs::path est_path = GetDataDir() / FEE_ESTIMATES_FILENAME;
    CAutoFile est_filein(fsbridge::fopen(est_path, "rb"), SER_DISK, CLIENT_VERSION);
    // Allowed to fail as this file IS missing on first startup.
    if (!est_filein.IsNull())
        ::feeEstimator.Read(est_filein);
    fFeeEstimatesInitialized = true;

    // ********************************************************* Step 8a: load powcache.dat

    {
        fs::path pathDB = GetDataDir();
        std::string strDBName = "powcache.dat";

        LOCK(cs_pow);
        // Always load the powcache if available:
        uiInterface.InitMessage(_("Loading POW cache..."));
        fs::path powCacheFile = pathDB / strDBName;
        if (!fs::exists(powCacheFile)) {
            uiInterface.InitMessage("Loading POW cache for the first time. This could take a minute...");
        }

        CFlatDB <CPowCache> flatdb7(strDBName, "powCache");
        if (!flatdb7.Load(CPowCache::Instance())) {
            return InitError(_("Failed to load POW cache from") + "\n" + (pathDB / strDBName).string());
        }
    }

    // ********************************************************* Step 8b: start indexers
    if (gArgs.GetBoolArg("-txindex", DEFAULT_TXINDEX)) {
        g_txindex = MakeUnique<TxIndex>(nTxIndexCache, false, fReindex);
        g_txindex->Start();
    }

    // ********************************************************* Step 9: load wallet
    for (const auto &client: node.chain_clients) {
        if (!client->load()) {
            return false;
        }
    }

    // As InitLoadWallet can take several minutes, it's possible the user
    // requested to kill the GUI during the last operation. If so, exit.
    if (ShutdownRequested()) {
        LogPrintf("Shutdown requested. Exiting.\n");
        return false;
    }
    // ********************************************************* Step 10: data directory maintenance


    // if pruning, unset the service bit and perform the initial blockstore prune
    // after any wallet rescanning has taken place.
    if (fPruneMode) {
        LogPrintf("Unsetting NODE_NETWORK on prune mode\n");
        nLocalServices = ServiceFlags(nLocalServices & ~NODE_NETWORK);
        if (!fReindex) {
            LOCK(cs_main);
            for (CChainState *chainstate: chainman.GetAll()) {
                uiInterface.InitMessage(_("Pruning blockstore..."));
                chainstate->PruneAndFlush();
            }
        }
    }

    // As PruneAndFlush can take several minutes, it's possible the user
    // requested to kill the GUI during the last operation. If so, exit.
    if (ShutdownRequested()) {
        LogPrintf("Shutdown requested. Exiting.\n");
        return false;
    }

    // ********************************************************* Step 10a: Prepare Smartnode related stuff
    fSmartnodeMode = false;
    std::string strSmartNodeBLSPrivKey = gArgs.GetArg("-smartnodeblsprivkey", "");
    if (!strSmartNodeBLSPrivKey.empty()) {
        auto binKey = ParseHex(strSmartNodeBLSPrivKey);
        CBLSSecretKey keyOperator(binKey);
        if (!keyOperator.IsValid()) {
            return InitError(_("Invalid smartnodeblsprivkey. Please see documentation."));
        }
        fSmartnodeMode = true;
        {
            LOCK(activeSmartnodeInfoCs);
            activeSmartnodeInfo.blsKeyOperator = std::make_unique<CBLSSecretKey>(keyOperator);
            activeSmartnodeInfo.blsPubKeyOperator = std::make_unique<CBLSPublicKey>(
                    activeSmartnodeInfo.blsKeyOperator->GetPublicKey());
        }
        LogPrintf("SMARTNODE:\n");
        LogPrintf("  blsPubKeyOperator: %s\n", keyOperator.GetPublicKey().ToString());
    }

    if (fSmartnodeMode) {
        // Create and register activeSmartnodeManager, will init later in ThreadImport
        activeSmartnodeManager = new CActiveSmartnodeManager(*node.connman);
        RegisterValidationInterface(activeSmartnodeManager);
    }

    {
        LOCK(activeSmartnodeInfoCs);
        if (activeSmartnodeInfo.blsKeyOperator == nullptr) {
            activeSmartnodeInfo.blsKeyOperator = std::make_unique<CBLSSecretKey>();
        }
        if (activeSmartnodeInfo.blsPubKeyOperator == nullptr) {
            activeSmartnodeInfo.blsPubKeyOperator = std::make_unique<CBLSPublicKey>();
        }
    }

    // ********************************************************* Step 10b: setup CoinJoin

    g_wallet_init_interface.InitCoinJoinSettings();

    // ********************************************************* Step 10b: Load cache data

    // LOAD SERIALIZED DAT FILES INTO DATA CACHES FOR INTERNAL USE

    bool fLoadCacheFiles = !(fReindex || fReindexChainState) && (::ChainActive().Tip() != nullptr);
    fs::path pathDB = GetDataDir();
    std::string strDBName;

    strDBName = "mncache.dat";
    uiInterface.InitMessage(_("Loading smartnode cache..."));
    CFlatDB <CSmartnodeMetaMan> flatdb1(strDBName, "magicSmartnodeCache");
    if (fLoadCacheFiles) {
        if (!flatdb1.Load(mmetaman)) {
            return InitError(_("Failed to load smartnode cache from") + "\n" + (pathDB / strDBName).string());
        }
    } else {
        CSmartnodeMetaMan mmetamanTmp;
        if (!flatdb1.Dump(mmetamanTmp)) {
            return InitError(_("Failed to clear smartnode cache at") + "\n" + (pathDB / strDBName).string());
        }
    }

    strDBName = "governance.dat";
    uiInterface.InitMessage(_("Loading governance cache..."));
    CFlatDB <CGovernanceManager> flatdb3(strDBName, "magicGovernanceCache");
    if (fLoadCacheFiles && !fDisableGovernance) {
        if (!flatdb3.Load(governance)) {
            return InitError(_("Failed to load governance cache from") + "\n" + (pathDB / strDBName).string());
        }
        governance.InitOnLoad();
    } else {
        CGovernanceManager governanceTmp;
        if (!flatdb3.Dump(governanceTmp)) {
            return InitError(_("Failed to clear governance cache at") + "\n" + (pathDB / strDBName).string());
        }
    }

    strDBName = "netfulfilled.dat";
    uiInterface.InitMessage(_("Loading fulfilled requests cache..."));
    CFlatDB <CNetFulfilledRequestManager> flatdb4(strDBName, "magicFulfilledCache");
    if (fLoadCacheFiles) {
        if (!flatdb4.Load(netfulfilledman)) {
            return InitError(_("Failed to load fulfilled requests cache from") + "\n" + (pathDB / strDBName).string());
        }
    } else {
        CNetFulfilledRequestManager netfulfilledmanTmp;
        if (!flatdb4.Dump(netfulfilledmanTmp)) {
            return InitError(_("Failed to clear fulfilled requests cache at") + "\n" + (pathDB / strDBName).string());
        }
    }

    // ********************************************************* Step 10c: schedule Raptoreum-specific tasks

    node.scheduler->scheduleEvery(std::bind(&CNetFulfilledRequestManager::DoMaintenance, std::ref(netfulfilledman)),
                                  60000); // value in milliseconds
    node.scheduler->scheduleEvery(
            std::bind(&CSmartnodeSync::DoMaintenance, std::ref(smartnodeSync), std::ref(*node.connman)),
            1000); // value in milliseconds
    node.scheduler->scheduleEvery(std::bind(&CSmartnodeUtils::DoMaintenance, std::ref(*node.connman)), 1000);
    node.scheduler->scheduleEvery(std::bind(&CDeterministicMNManager::DoMaintenance, std::ref(*deterministicMNManager)),
                                  10000);

    if (!fDisableGovernance) {
        node.scheduler->scheduleEvery(
                std::bind(&CGovernanceManager::DoMaintenance, std::ref(governance), std::ref(*node.connman)),
                300000); // value in milliseconds
    }

    if (fSmartnodeMode) {
        node.scheduler->scheduleEvery(
                std::bind(&CCoinJoinServer::DoMaintenance, std::ref(coinJoinServer), std::ref(*node.connman)), 1000);
#ifdef ENABLE_WALLET
        } else if(CCoinJoinClientOptions::IsEnabled()) {
            node.scheduler->scheduleEvery(std::bind(&DoCoinJoinMaintenance, std::ref(*node.connman)), 1000);
#endif // ENABLE_WALLET
    }

    // Periodic flush of POW Cache if cache has grown enough
    node.scheduler->scheduleEvery(std::bind(&CPowCache::DoMaintenance, &CPowCache::Instance()), 60000);

    if (gArgs.GetBoolArg("-statsenabled", DEFAULT_STATSD_ENABLE)) {
        int nStatsPeriod = std::min(
                std::max((int) gArgs.GetArg("-statsperiod", DEFAULT_STATSD_PERIOD), MIN_STATSD_PERIOD),
                MAX_STATSD_PERIOD);
        node.scheduler->scheduleEvery(PeriodicStats, nStatsPeriod * 1000);
    }

    llmq::StartLLMQSystem();

    // ********************************************************* Step 11: import blocks

    if (!CheckDiskSpace(GetDataDir())) {
        InitError(strprintf(_("Error: Disk space is low for %s"), GetDataDir()));
        return false;
    }
    if (!CheckDiskSpace(GetBlocksDir())) {
        InitError(strprintf(_("Error: Disk space is low for %s"), GetBlocksDir()));
        return false;
    }

    // Either install a handler to notify us when genesis activates, or set fHaveGenesis directly.
    // No locking, as this happens before any background thread is started.
    boost::signals2::connection block_notify_genesis_wait_connection;
    if (::ChainActive().Tip() == nullptr) {
        block_notify_genesis_wait_connection = uiInterface.NotifyBlockTip_connect(BlockNotifyGenesisWait);
    } else {
        fHaveGenesis = true;
    }

    if (gArgs.IsArgSet("-blocknotify"))
        uiInterface.NotifyBlockTip_connect(BlockNotifyCallback);

    std::vector <fs::path> vImportFiles;
    for (const std::string &strFile: gArgs.GetArgs("-loadblock")) {
        vImportFiles.push_back(strFile);
    }

    threadGroup.create_thread([=, &chainman] { ThreadImport(chainman, vImportFiles); });

    // Wait for genesis block to be processed
    {
        WAIT_LOCK(g_genesis_wait_mutex, lock);
        // We previously could hang here if StartShutdown() is called prior to
        // ThreadImport getting started, so instead we just wait on a timer to
        // check ShutdownRequested() regularly.
        while (!fHaveGenesis && !ShutdownRequested()) {
            g_genesis_wait_cv.wait_for(lock, std::chrono::milliseconds(500));
        }
        block_notify_genesis_wait_connection.disconnect();
    }

    // As importing blocks can take several minutes, it's possible the user
    // requested to kill the GUI during one of the last operations. If so, exit.
    if (ShutdownRequested()) {
        LogPrintf("Shutdown requested. Exiting.\n");
        return false;
    }

    // ********************************************************* Step 12: start node

    int chain_active_height;

    //// debug print
    {
        LOCK(cs_main);
        LogPrintf("block tree size = %u\n", ::BlockIndex().size());
        chain_active_height = ::ChainActive().Height();
        if (tip_info) {
            tip_info->block_height = chain_active_height;
            tip_info->block_time = ::ChainActive().Tip() ? ::ChainActive().Tip()->GetBlockTime()
                                                         : Params().GenesisBlock().GetBlockTime();
            tip_info->block_hash = ::ChainActive().Tip() ? ::ChainActive().Tip()->GetBlockHash()
                                                         : Params().GenesisBlock().GetHash();
            tip_info->verification_progress = GuessVerificationProgress(Params().TxData(), ::ChainActive().Tip());
        }
        if (tip_info && ::pindexBestHeader) {
            tip_info->header_height = ::pindexBestHeader->nHeight;
            tip_info->header_time = ::pindexBestHeader->GetBlockTime();
        }
    }
    LogPrintf("::ChainActive().Height() = %d\n", chain_active_height);
    if (gArgs.GetBoolArg("-listenonion", DEFAULT_LISTEN_ONION))
        StartTorControl();

    Discover();

    // Map ports with UPnP
    StartMapPort(gArgs.GetBoolArg("-upnp", DEFAULT_UPNP), gArgs.GetBoolArg("-natpmp", DEFAULT_NATPMP));

    CConnman::Options connOptions;
    connOptions.nLocalServices = nLocalServices;
    connOptions.nMaxConnections = nMaxConnections;
    connOptions.nMaxOutbound = std::min(MAX_OUTBOUND_CONNECTIONS, connOptions.nMaxConnections);
    connOptions.nMaxAddnode = MAX_ADDNODE_CONNECTIONS;
    connOptions.nMaxFeeler = 1;
    connOptions.nBestHeight = chain_active_height;
    connOptions.uiInterface = &uiInterface;
    connOptions.m_banman = node.banman.get();
    connOptions.m_msgproc = node.peer_logic.get();
    connOptions.nSendBufferMaxSize = 1000 * gArgs.GetArg("-maxsendbuffer", DEFAULT_MAXSENDBUFFER);
    connOptions.nReceiveFloodSize = 1000 * gArgs.GetArg("-maxreceivebuffer", DEFAULT_MAXRECEIVEBUFFER);
    connOptions.m_added_nodes = gArgs.GetArgs("-addnode");

    connOptions.nMaxOutboundLimit = 1024 * 1024 * gArgs.GetArg("-maxuploadtarget", DEFAULT_MAX_UPLOAD_TARGET);
    connOptions.m_peer_connect_timeout = peer_connect_timeout;

    for (const std::string &strBind: gArgs.GetArgs("-bind")) {
        CService addrBind;
        if (!Lookup(strBind.c_str(), addrBind, GetListenPort(), false)) {
            return InitError(ResolveErrMsg("bind", strBind));
        }
        connOptions.vBinds.push_back(addrBind);
    }
    for (const std::string &strBind: gArgs.GetArgs("-whitebind")) {
        CService addrBind;
        if (!Lookup(strBind.c_str(), addrBind, 0, false)) {
            return InitError(ResolveErrMsg("whitebind", strBind));
        }
        if (addrBind.GetPort() == 0) {
            return InitError(strprintf(_("Need to specify a port with -whitebind: '%s'"), strBind));
        }
        connOptions.vWhiteBinds.push_back(addrBind);
    }

    for (const auto &net: gArgs.GetArgs("-whitelist")) {
        CSubNet subnet;
        LookupSubNet(net.c_str(), subnet);
        if (!subnet.IsValid())
            return InitError(strprintf(_("Invalid netmask specified in -whitelist: '%s'"), net));
        connOptions.vWhitelistedRange.push_back(subnet);
    }

    connOptions.vSeedNodes = gArgs.GetArgs("-seednode");

    // Initiate outbound connections unless connect=0
    connOptions.m_use_addrman_outgoing = !gArgs.IsArgSet("-connect");
    if (!connOptions.m_use_addrman_outgoing) {
        const auto connect = gArgs.GetArgs("-connect");
        if (connect.size() != 1 || connect[0] != "0") {
            connOptions.m_specified_outgoing = connect;
        }
    }

    std::string strSocketEventsMode = gArgs.GetArg("-socketevents", DEFAULT_SOCKETEVENTS);
    if (strSocketEventsMode == "select") {
        connOptions.socketEventsMode = CConnman::SOCKETEVENTS_SELECT;
#ifdef USE_POLL
        } else if (strSocketEventsMode == "poll") {
            connOptions.socketEventsMode = CConnman::SOCKETEVENTS_POLL;
#endif
#ifdef USE_EPOLL
        } else if (strSocketEventsMode == "epoll") {
            connOptions.socketEventsMode = CConnman::SOCKETEVENTS_EPOLL;
#endif
#ifdef USE_KQUEUE
        } else if (strSocketEventsMode == "kqueue") {
            connOptions.socketEventsMode = CConnman::SOCKETEVENTS_KQUEUE;
#endif
    } else {
        return InitError(strprintf(_("Invalid -socketevents ('%s') specified. Only these modes are supported: %s"),
                                   strSocketEventsMode, GetSupportedSocketEventsStr()));
    }

    if (!node.connman->Start(*node.scheduler, connOptions)) {
        return false;
    }

    // ********************************************************* Step 13: finished

    SetRPCWarmupFinished();
    uiInterface.InitMessage(_("Done loading"));

    for (const auto &client: node.chain_clients) {
        client->start(*node.scheduler);
    }

    BanMan *banman = node.banman.get();
    node.scheduler->scheduleEvery([banman] {
        banman->DumpBanlist();
    }, DUMP_BANS_INTERVAL * 1000);

    return true;
}
