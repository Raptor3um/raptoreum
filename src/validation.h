// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_H
#define BITCOIN_VALIDATION_H

#if defined(HAVE_CONFIG_H)
#include <config/raptoreum-config.h>
#endif

#include <amount.h>
#include <coins.h>
#include <fs.h>
#include <optional.h>
#include <protocol.h> // For CMessageHeader::MessageStartChars
#include <policy/feerate.h>
#include <script/script_error.h>
#include <sync.h>
#include <txdb.h>
#include <txmempool.h>
#include <versionbits.h>
#include <serialize.h>
#include <indices/spent_index.h>
#include <indices/future_index.h>

#include <algorithm>
#include <atomic>
#include <exception>
#include <map>
#include <memory>
#include <set>
#include <stdint.h>
#include <utility>
#include <vector>

class CChainState;

class CBlockIndex;

class CBlockTreeDB;

class CBlockUndo;

class CChainParams;

class CInv;

class CConnman;

class CScriptCheck;

class CBlockPolicyEstimator;

class CTxMemPool;

class CValidationState;

class ChainstateManager;

struct PrecomputedTransactionData;

class CTxUndo;

struct CBlockAssetUndo;

class CAssetsDB;

class CAssetsCache;

struct ChainTxData;

struct DisconnectedBlockTransactions;
struct LockPoints;

/** Default for -whitelistrelay. */
static const bool DEFAULT_WHITELISTRELAY = true;
/** Default for -whitelistforcerelay. */
static const bool DEFAULT_WHITELISTFORCERELAY = true;
/** Default for -minrelaytxfee, minimum relay fee for transactions */
static const unsigned int DEFAULT_MIN_RELAY_TX_FEE = 1000;
/** Default for -limitancestorcount, max number of in-mempool ancestors */
static const unsigned int DEFAULT_ANCESTOR_LIMIT = 25;
/** Default for -limitancestorsize, maximum kilobytes of tx + all in-mempool ancestors */
static const unsigned int DEFAULT_ANCESTOR_SIZE_LIMIT = 101;
/** Default for -limitdescendantcount, max number of in-mempool descendants */
static const unsigned int DEFAULT_DESCENDANT_LIMIT = 25;
/** Default for -limitdescendantsize, maximum kilobytes of in-mempool descendants */
static const unsigned int DEFAULT_DESCENDANT_SIZE_LIMIT = 101;
/** Default for -mempoolexpiry, expiration time for mempool transactions in hours */
static const unsigned int DEFAULT_MEMPOOL_EXPIRY = 336;
/** Maximum kilobytes for transactions to store for processing during reorg */
static const unsigned int MAX_DISCONNECTED_TX_POOL_SIZE = 20000;
/** The maximum size of a blk?????.dat file (since 0.8) */
static const unsigned int MAX_BLOCKFILE_SIZE = 0x8000000; // 128 MiB
/** The pre-allocation chunk size for blk?????.dat files (since 0.8) */
static const unsigned int BLOCKFILE_CHUNK_SIZE = 0x1000000; // 16 MiB
/** The pre-allocation chunk size for rev?????.dat files (since 0.8) */
static const unsigned int UNDOFILE_CHUNK_SIZE = 0x100000; // 1 MiB

/** Maximum number of dedicated script-checking threads allowed */
static const int MAX_SCRIPTCHECK_THREADS = 15;
/** -par default (number of script-checking threads, 0 = auto) */
static const int DEFAULT_SCRIPTCHECK_THREADS = 0;
/** Number of blocks that can be requested at any given time from a single peer. */
static const int MAX_BLOCKS_IN_TRANSIT_PER_PEER = 16;
/** Timeout in seconds during which a peer must stall block download progress before being disconnected. */
static const unsigned int BLOCK_STALLING_TIMEOUT = 2;
/** Number of headers sent in one getheaders result. We rely on the assumption that if a peer sends
 *  less than this number, we reached its tip. Changing this value is a protocol upgrade. */
static const unsigned int MAX_HEADERS_RESULTS = 2000;
/** Maximum depth of blocks we're willing to serve as compact blocks to peers
 *  when requested. For older blocks, a regular BLOCK response will be sent. */
static const int MAX_CMPCTBLOCK_DEPTH = 5;
/** Maximum depth of blocks we're willing to respond to GETBLOCKTXN requests for. */
static const int MAX_BLOCKTXN_DEPTH = 10;
/** Size of the "block download window": how far ahead of our current height do we fetch?
 *  Larger windows tolerate larger download speed differences between peer, but increase the potential
 *  degree of disordering of blocks on disk (which make reindexing and pruning harder). We'll probably
 *  want to make this a per-peer adaptive value at some point. */
static const unsigned int BLOCK_DOWNLOAD_WINDOW = 1024;
/** Time to wait between writing blocks/block index to disk. */
static const unsigned int DATABASE_WRITE_INTERVAL = 60 * 60;
/** Time to wait between flushing chainstate to disk. */
static const unsigned int DATABASE_FLUSH_INTERVAL = 24 * 60 * 60;
/** Maximum length of reject messages. */
static const unsigned int MAX_REJECT_MESSAGE_LENGTH = 111;
/** Block download timeout base, expressed in millionths of the block interval (i.e. 2.5 min) */
static const int64_t BLOCK_DOWNLOAD_TIMEOUT_BASE = 1000000;
/** Additional block download timeout per parallel downloading peer (i.e. 1.25 min) */
static const int64_t BLOCK_DOWNLOAD_TIMEOUT_PER_PEER = 500000;

static const int64_t DEFAULT_MAX_TIP_AGE =
        6 * 60 * 60; // ~144 blocks behind -> 2 x fork detection time, was 24 * 60 * 60 in bitcoin
/** Maximum age of our tip for us to be considered current for fee estimation */
static const int64_t MAX_FEE_ESTIMATION_TIP_AGE = 3 * 60 * 60;

static const bool DEFAULT_CHECKPOINTS_ENABLED = true;
static const bool DEFAULT_TXINDEX = true;
static const bool DEFAULT_ADDRESSINDEX = false;
static const bool DEFAULT_TIMESTAMPINDEX = false;
static const bool DEFAULT_SPENTINDEX = false;
static const bool DEFAULT_FUTUREINDEX = false;
static const int DEFAULT_POWHEADERTHREADS = 8;
static const unsigned int DEFAULT_BANSCORE_THRESHOLD = 100;
/** Default for -persistmempool */
static const bool DEFAULT_PERSIST_MEMPOOL = true;
/** Default for -syncmempool */
static const bool DEFAULT_SYNC_MEMPOOL = true;

/** Maximum number of headers to announce when relaying blocks with headers message.*/
static const unsigned int MAX_BLOCKS_TO_ANNOUNCE = 16;

/** Maximum number of unconnecting headers announcements before DoS score */
static const int MAX_UNCONNECTING_HEADERS = 10;

static const bool DEFAULT_PEERBLOOMFILTERS = true;

/** Default for -stopatheight */
static const int DEFAULT_STOPATHEIGHT = 0;

extern const std::string strMessageMagic;

struct BlockHasher {
    size_t operator()(const uint256 &hash) const { return hash.GetCheapHash(); }
};

extern CScript COINBASE_FLAGS;
extern RecursiveMutex cs_main;
extern CBlockPolicyEstimator feeEstimator;
extern CTxMemPool mempool;
typedef std::unordered_map<uint256, CBlockIndex *, BlockHasher> BlockMap;
typedef std::unordered_multimap<uint256, CBlockIndex *, BlockHasher> PrevBlockMap;
extern uint64_t nLastBlockTx;
extern uint64_t nLastBlockSize;
extern Mutex g_best_block_mutex;
extern std::condition_variable g_best_block_cv;
extern uint256 g_best_block;
extern std::atomic_bool fImporting;
extern std::atomic_bool fReindex;
extern std::atomic_bool fProcessingHeaders;
extern std::atomic<int> atomicHeaderHeight;
extern bool fAddressIndex;
extern bool fTimestampIndex;
extern bool fSpentIndex;
/** Whether there are dedicated script-checking threads running.
 * False indicates all script checking is done on the main threadMessageHandler thread.
 */
extern bool g_parallel_script_checks;
extern bool fAddressIndex;
extern bool fTimestampIndex;
extern bool fFutureIndex;
extern bool fSpentIndex;
extern bool fIsBareMultisigStd;
extern bool fRequireStandard;
extern unsigned int nBytesPerSigOp;
extern bool fCheckBlockIndex;
extern bool fCheckpointsEnabled;
/** A fee rate smaller than this is considered zero fee (for relaying, mining and transaction creation) */
extern CFeeRate minRelayTxFee;
/** If the tip is older than this (in seconds), the node is considered to be in initial block download. */
extern int64_t nMaxTipAge;

extern bool fLargeWorkForkFound;
extern bool fLargeWorkInvalidChainFound;

extern std::atomic<bool> fDIP0001ActiveAtTip;

/** Block hash whose ancestors we will assume to have valid scripts without checking them. */
extern uint256 hashAssumeValid;

/** Minimum work we will assume exists on some valid chain. */
extern arith_uint256 nMinimumChainWork;

/** Best header we've seen so far (used for getheaders queries' starting points). */
extern CBlockIndex *pindexBestHeader;

/** Pruning-related variables and constants */
/** True if any block files have ever been pruned. */
extern bool fHavePruned;
/** True if we're running in -prune mode. */
extern bool fPruneMode;
/** Number of MiB of block files that we're trying to stay below. */
extern uint64_t nPruneTarget;
/** Block files containing a block-height within MIN_BLOCKS_TO_KEEP of ::ChainActive().Tip() will not be pruned. */
static const unsigned int MIN_BLOCKS_TO_KEEP = 288;
/** Minimum blocks required to signal NODE_NETWORK_LIMITED */
static const unsigned int NODE_NETWORK_LIMITED_MIN_BLOCKS = 288;

static const signed int DEFAULT_CHECKBLOCKS = 50;
static const unsigned int DEFAULT_CHECKLEVEL = 3;

// Require that user allocate at least 945MB for block & undo files (blk???.dat and rev???.dat)
// At 2MB per block, 288 blocks = 576MB.
// Add 15% for Undo data = 662MB
// Add 20% for Orphan block rate = 794MB
// We want the low water mark after pruning to be at least 794 MB and since we prune in
// full block file chunks, we need the high water mark which triggers the prune to be
// one 128MB block file + added 15% undo data = 147MB greater for a total of 941MB
// Setting the target to > than 945MB will make it likely we can respect the target.
static const uint64_t MIN_DISK_SPACE_FOR_BLOCK_FILES = 945 * 1024 * 1024;

/** Open a block file (blk?????.dat) */
FILE *OpenBlockFile(const FlatFilePos &pos, bool fReadOnly = false);

/** Translation to a filesystem path */
fs::path GetBlockPosFilename(const FlatFilePos &pos);

/** Import blocks from an external file */
void LoadExternalBlockFile(const CChainParams &chainparams, FILE *fileIn, FlatFilePos *dbp = nullptr);

/** Ensures we have a genesis block in the block tree, possibly writing one to disk. */
bool LoadGenesisBlock(const CChainParams &chainparams);

/** Unload database information */
void UnloadBlockIndex(CTxMemPool *mempool);

/** Run instances of script checking worker threads */
void StartScriptCheckWorkerThreads(int threads_num);

/** Stop all of the script checking worker threads. */
void StopScriptCheckWorkerThreads();

/**
 * Return transaction from the block at block_index.
 * If block_index is not provided, fall back to mempool.
 * If mempool is not provided or the tx couldn't be found in mempool, fall back to g_txindex.
 *
 * @param[in]  block_index     The block to read from disk, or nullptr
 * @param[in]  mempool         If block_index is not provided, look in the mempool, if provided
 * @param[in]  hash            The txid
 * @param[in]  consensusParams The params
 * @param[out] hashBlock       The hash of block_index, if the tx was found via block_index
 * @returns                    The tx if found, otherwise nullptr
 */
CTransactionRef
GetTransaction(const CBlockIndex *const block_index, const CTxMemPool *const mempool, const uint256 &hash,
               const Consensus::Params &consensusParams, uint256 &hashBlock);

/**
 * Find the best known block, and make it the tip of the block chain
 *
 * May not be called with cs_main held. May not be called in a
 * validationinterface callback.
 */
bool ActivateBestChain(CValidationState &state, const CChainParams &chainparams,
                       std::shared_ptr<const CBlock> pblock = std::shared_ptr<const CBlock>());

double ConvertBitsToDouble(unsigned int nBits);

CAmount
GetBlockSubsidy(int nBits, int nHeight, const Consensus::Params &consensusParams, bool fSuperblockPartOnly = false);

CAmount GetSmartnodePayment(int nHeight, CAmount blockValue, CAmount specialTxFees);

/** Guess verification progress (as a fraction between 0.0=genesis and 1.0=current tip). */
double GuessVerificationProgress(const ChainTxData &data, const CBlockIndex *pindex);

/** Calculate the amount of disk space the block & undo files currently use */
uint64_t CalculateCurrentUsage();

/**
 *  Actually unlink the specified files
 */
void UnlinkPrunedFiles(const std::set<int> &setFilesToPrune);

/** Prune block files up to a given height */
void PruneBlockFilesManual(int nManualPruneHeight);

/** (try to) add transaction to memory pool */
bool AcceptToMemoryPool(CTxMemPool &pool, CValidationState &state, const CTransactionRef &tx,
                        bool *pfMissingInputs, bool bypass_limits,
                        const CAmount nAbsurdFee, bool fDryRun = false);

bool GetUTXOCoin(const COutPoint &outpoint, Coin &coin, int height);

bool GetUTXOCoin(const COutPoint &outpoint, Coin &coin);

int GetUTXOHeight(const COutPoint &outpoint);

int GetUTXOConfirmations(const COutPoint &outpoint);

/** Get the BIP9 state for a given deployment at the current tip. */
ThresholdState VersionBitsTipState(const Consensus::Params &params, Consensus::DeploymentPos pos);

/** Get the numerical statistics for the BIP9 state for a given deployment at the current tip. */
BIP9Stats VersionBitsTipStatistics(const Consensus::Params &params, Consensus::DeploymentPos pos);

/** Get the block height at which the BIP9 deployment switched into the state for the block building on the current tip. */
int VersionBitsTipStateSinceHeight(const Consensus::Params &params, Consensus::DeploymentPos pos);

/** Apply the effects of this transaction on the UTXO set represented by view */
void UpdateCoins(const CTransaction &tx, CCoinsViewCache &inputs, int nHeight);

void UpdateCoins(const CTransaction &tx, CCoinsViewCache &inputs, CTxUndo &txundo, int nHeight,
                 CAssetsCache *assetCache = nullptr, std::pair <std::string, CBlockAssetUndo> *undoAssetData = nullptr);

/** Transaction validation functions */

/**
 * Check if transaction will be final in the next block to be created.
 *
 * Calls IsFinalTx() with current block height and appropriate block time.
 *
 * See consensus/consensus.h for flag definitions.
 */
bool CheckFinalTx(const CTransaction &tx, int flags = -1);

/**
 * Test whether the LockPoints height and time are still valid on the current chain
 */
bool TestLockPointValidity(const LockPoints *lp);

/**
 * Check if transaction will be BIP 68 final in the next block to be created.
 *
 * Simulates calling SequenceLocks() with data from the tip of the current active chain.
 * Optionally stores in LockPoints the resulting height and time calculated and the hash
 * of the block needed for calculation or skips the calculation and uses the LockPoints
 * passed in for evaluation.
 * The LockPoints should not be considered valid if CheckSequenceLocks returns false.
 *
 * See consensus/consensus.h for flag definitions.
 */
bool CheckSequenceLocks(const CTxMemPool &pool, const CTransaction &tx, int flags, LockPoints *lp = nullptr,
                        bool useExistingLockPoints = false)

EXCLUSIVE_LOCKS_REQUIRED(cs_main);

/**
 * Closure representing one script verification
 * Note that this stores references to the spending transaction
 */
class CScriptCheck {
private:
    CTxOut m_tx_out;
    const CTransaction *ptxTo;
    unsigned int nIn;
    unsigned int nFlags;
    bool cacheStore;
    ScriptError error;
    PrecomputedTransactionData *txdata;

public:
    CScriptCheck() : ptxTo(nullptr), nIn(0), nFlags(0), cacheStore(false), error(SCRIPT_ERR_UNKNOWN_ERROR) {}

    CScriptCheck(const CTxOut &outIn, const CTransaction &txToIn, unsigned int nInIn, unsigned int nFlagsIn,
                 bool cacheIn, PrecomputedTransactionData *txdataIn) :
            m_tx_out(outIn), ptxTo(&txToIn), nIn(nInIn), nFlags(nFlagsIn), cacheStore(cacheIn),
            error(SCRIPT_ERR_UNKNOWN_ERROR), txdata(txdataIn) {}

    bool operator()();

    void swap(CScriptCheck &check) {
        std::swap(ptxTo, check.ptxTo);
        std::swap(m_tx_out, check.m_tx_out);
        std::swap(nIn, check.nIn);
        std::swap(nFlags, check.nFlags);
        std::swap(cacheStore, check.cacheStore);
        std::swap(error, check.error);
        std::swap(txdata, check.txdata);
    }

    ScriptError GetScriptError() const { return error; }
};

bool GetTimestampIndex(const unsigned int &high, const unsigned int &low, std::vector <uint256> &hashes);

bool GetSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value);

bool GetFutureIndex(CFutureIndexKey &key, CFutureIndexValue &value);

bool GetAddressIndex(uint160 addressHash, int type, std::vector <std::pair<CAddressIndexKey, CAmount>> &addressIndex,
                     int start = 0, int end = 0);

bool GetAddressUnspent(uint160 addressHash, int type,
                       std::vector <std::pair<CAddressUnspentKey, CAddressUnspentValue>> &unspentOutputs);

/** Initializes the script-execution cache */
void InitScriptExecutionCache();


/** Functions for disk access for blocks */
bool ReadBlockFromDisk(CBlock &block, const FlatFilePos &pos, const Consensus::Params &consensusParams);

bool ReadBlockFromDisk(CBlock &block, const CBlockIndex *pindex, const Consensus::Params &consensusParams);

bool UndoReadFromDisk(CBlockUndo &blockundo, const CBlockIndex *pindex);

/** Functions for validating blocks and updating the block tree */

/** Context-independent validity checks */
bool CheckBlock(const CBlock &block, CValidationState &state, const Consensus::Params &consensusParams, int nHeight,
                bool fCheckPOW = true, bool fCheckMerkleRoot = true);

/** Check a block is completely valid from start to finish (only works on top of our current best block) */
bool TestBlockValidity(CValidationState &state, const CChainParams &chainparams, const CBlock &block,
                       CBlockIndex *pindexPrev, bool fCheckPOW = true, bool fCheckMerkleRoot = true)

EXCLUSIVE_LOCKS_REQUIRED(cs_main);

/** RAII wrapper for VerifyDB: Verify consistency of the block and coin databases */
class CVerifyDB {
public:
    CVerifyDB();

    ~CVerifyDB();

    bool VerifyDB(const CChainParams &chainparams, CCoinsView *coinsview, int nCheckLevel, int nCheckDepth);
};

CBlockIndex *LookupBlockIndex(const uint256 &hash);

/** Find the last common block between the parameter chain and a locator. */
CBlockIndex *FindForkInGlobalIndex(const CChain &chain, const CBlockLocator &locator);

enum DisconnectResult {
    DISCONNECT_OK,
    DISCONNECT_UNCLEAN,
    DISCONNECT_FAILED
};

class ConnectTrace;

enum class FlushStateMode {
    NONE,
    IF_NEEDED,
    PERIODIC,
    ALWAYS
};

struct CBlockIndexWorkComparator {
    bool operator()(const CBlockIndex *pa, const CBlockIndex *pb) const;
};

/**
 * Maintains a tree of blocks (stored in `m_block_index`) which is consulted
 * to determine where the most-work tip is.
 *
 * This data is used mostly in `CChainState` - information about, e.g.,
 * candidate tips is not maintained here.
 */
class BlockManager {
public:
    BlockMap m_block_index
    GUARDED_BY(cs_main);
    PrevBlockMap m_prev_block_index
    GUARDED_BY(cs_main);

    /** In order to efficiently track invalidity of headers, we keep the set of
      * blocks which we tried to connect and found to be invalid here (ie which
      * were set to BLOCK_FAILED_VALID since the last restart). We can then
      * walk this set and check if a new header is a descendant of something in
      * this set, preventing us from having to walk m_block_index when we try
      * to connect a bad block and fail.
      *
      * While this is more complicated than marking everything which descends
      * from an invalid block as invalid at the time we discover it to be
      * invalid, doing so would require walking all of m_block_index to find all
      * descendants. Since this case should be very rare, keeping track of all
      * BLOCK_FAILED_VALID blocks in a set should be just fine and work just as
      * well.
      *
      * Because we already walk m_block_index in height-order at startup, we go
      * ahead and mark descendants of invalid blocks as FAILED_CHILD at that time,
      * instead of putting things in this set.
      */
    std::set<CBlockIndex *> m_failed_blocks;

    /**
     * All pairs A->B, where A (or one of its ancestors) misses transactions, but B has transactions.
     * Pruned nodes may have entries where B is missing data.
     */
    std::multimap<CBlockIndex *, CBlockIndex *> m_blocks_unlinked;

    /**
     * Load the blocktree off disk and into memory. Populate certain metadata
     * per index entry (nStatus, nChainWork, nTimeMax, etc.) as well as peripheral
     * collections like setDirtyBlockIndex.
     *
     * @param[out] block_index_candidates  Fill this set with any valid blocks for
     *                                     which we've downloaded all transactions.
     */
    bool LoadBlockIndex(const Consensus::Params &consensus_params, CBlockTreeDB &blocktree,
                        std::set<CBlockIndex *, CBlockIndexWorkComparator> &block_index_candidates)

    EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Clear all data members. */
    void Unload()

    EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    CBlockIndex *AddToBlockIndex(const CBlockHeader &block, enum BlockStatus nStatus = BLOCK_VALID_TREE)

    EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Create a new block index entry for a given block hash */
    CBlockIndex *InsertBlockIndex(const uint256 &hash)

    EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     * If a block header hasn't already been seen, call CheckBlockHeader on it, ensure
     * that it doesn't descend from an invalid block, and then add it to m_block_index.
     */
    bool AcceptBlockHeader(const CBlockHeader &block, CValidationState &state, const CChainParams &chainparams,
                           CBlockIndex **ppindex)

    EXCLUSIVE_LOCKS_REQUIRED(cs_main);
};

/**
 * A convenience class for constructing the CCoinsView* hierarchy used
 * to facilitate access to the UTXO set.
 *
 * This class consists of an arrangement of layered CCoinsView objects,
 * preferring to store and retrieve coins in memory via `m_cacheview` but
 * ultimately falling back on cache misses to the canonical store of UTXOs on
 * disk, `m_dbview`.
 */
class CoinsViews {

public:
    //! The lowest level of the CoinsViews cache hierarchy sits in a leveldb database on disk.
    //! All unspent coins reside in this store.
    CCoinsViewDB m_dbview
    GUARDED_BY(cs_main);

    //! This view wraps access to the leveldb instance and handles read errors gracefully.
    CCoinsViewErrorCatcher m_catcherview
    GUARDED_BY(cs_main);

    //! This is the top layer of the cache hierarchy - it keeps as many coins in memory as
    //! can fit per the dbcache setting.
    std::unique_ptr <CCoinsViewCache> m_cacheview
    GUARDED_BY(cs_main);

    //! This constructor initializes CCoinsViewDB and CCoinsViewErrorCatcher instances, but it
    //! *does not* create a CCoinsViewCache instance by default. This is done separately because the
    //! presence of the cache has implications on whether or not we're allowed to flush the cache's
    //! state to disk, which should not be done until the health of the database is verified.
    //!
    //! All arguments forwarded onto CCoinsViewDB.
    CoinsViews(std::string ldb_name, size_t cache_size_bytes, bool in_memory, bool should_wipe);

    //! Initialize the CCoinsViewCache member.
    void InitCache()

    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
};

enum class CoinsCacheSizeState {
    //! The coins cache is in immediate need of a flush.
    CRITICAL = 2,
    //! The cache is at >= 90% capacity.
    LARGE = 1,
    OK = 0
};

/**
 * CChainState stores and provides an API to update our local knowledge of the
 * current best chain.
 *
 * Eventually, the API here is targeted at being exposed externally as a
 * consumable libconsensus library, so any functions added must only call
 * other class member functions, pure functions in other parts of the consensus
 * library, callbacks via the validation interface, or read/write-to-disk
 * functions (eventually this will also be via callbacks).
 *
 * Anything that is contingent on the current tip of the chain is stored here,
 * whereas block information and metadata independent of the current tip is
 * kept in `BlockMetadataManager`.
 */
class CChainState {
private:

    /**
     * Every received block is assigned a unique and increasing identifier, so we
     * know which one to give priority in case of a fork.
     */
    RecursiveMutex cs_nBlockSequenceId;
    /** Blocks loaded from disk are assigned id 0, so start the counter at 1. */
    int32_t nBlockSequenceId = 1;
    /** Decreasing counter (used by subsequent preciousblock calls). */
    int32_t nBlockReverseSequenceId = -1;
    /** chainwork for the last block that preciousblock has been applied to. */
    arith_uint256 nLastPreciousChainwork = 0;

    /**
     * the ChainState CriticalSection
     * A lock that must be held when modifying this ChainState - held in ActivateBestChain()
     */
    RecursiveMutex m_cs_chainstate;

    /**
     * Whether this chainstate is undergoing initial block download.
     *
     * Mutable because we need to be able to mark IsInitialBlockDownload()
     * const, which latches this for caching purposes.
     */
    mutable std::atomic<bool> m_cached_finished_ibd{false};

    //! Reference to a BlockManager instance which itself is shared across all
    //! CChainState instances. Keeping a local reference allows us to test more
    //! easily as opposed to referencing a global.
    BlockManager &m_blockman;

    //! Manages the UTXO set, which is a reflection of the contents of `m_chain`.
    std::unique_ptr <CoinsViews> m_coins_views;

public:
    explicit CChainState(BlockManager &blockman, uint256 from_snapshot_blockhash = uint256());

    /**
     * Initialize the CoinsViews UTXO set database management data structures. The in-memory
     * cache is initialized separately.
     *
     * All parameters forwarded to CoinsViews.
     */
    void InitCoinsDB(
            size_t cache_size_bytes,
            bool in_memory,
            bool should_wipe,
            std::string leveldb_name = "chainstate");

    //! Initialize the in-memory coins cache (to be done after the health of the on-disk database
    //! is verified).
    void InitCoinsCache(size_t cache_size_bytes)

    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    //! @returns whether or not the CoinsViews object has been fully initialized and we can
    //!          safely flush this object to disk.
    bool CanFlushToDisk()

    EXCLUSIVE_LOCKS_REQUIRED(cs_main) {
            return m_coins_views && m_coins_views->m_cacheview;
    }

    //! The current chain of blockheaders we consult and build on.
    //! @see CChain, CBlockIndex.
    CChain m_chain;

    /**
     * The blockchain which is the base of the snapshot this chainstate was createwd from.
     *
     * IsNull() if this chainstate was not created from a snapshot.
     */
    const uint256 m_from_snapshot_blockhash{};

    /**
     * The set of all CBlockIndex entries with BLOCK_VALID_TRANSACTIONS (for itself and all ancestors) and
     * as good as our current tip or better. Entries may be failed, though, and pruning nodes may be
     * missing the data for the block.
     */
    std::set<CBlockIndex *, CBlockIndexWorkComparator> setBlockIndexCandidates;

    //! @returns A reference to the in-memory cache of the UTXO set.
    CCoinsViewCache &CoinsTip()

    EXCLUSIVE_LOCKS_REQUIRED(cs_main)
            {
                    assert(m_coins_views->m_cacheview);
            return *m_coins_views->m_cacheview.get();
            }

    //! @returns A reference to the on-disk UTXO set database.
    CCoinsViewDB &CoinsDB()

    EXCLUSIVE_LOCKS_REQUIRED(cs_main)
            {
                    return m_coins_views->m_dbview;
            }

    //! @returns A reference to a wrapped view of the in-memory UTXO set that
    //!     handles disk read errors gracefully.
    CCoinsViewErrorCatcher &CoinsErrorCatcher()

    EXCLUSIVE_LOCKS_REQUIRED(cs_main)
            {
                    return m_coins_views->m_catcherview;
            }

    //! Destructs all objects related to accessing the UTXO set.
    void ResetCoinsViews() { m_coins_views.reset(); }

    //! The cache size of the on-disk coins view.
    size_t m_coinsdb_cache_size_bytes{0};

    //! The cache size of the in-memory coins view.
    size_t m_coinstip_cache_size_bytes{0};

    //! Resize the CoinsViews caches dynamically and flush state to disk.
    //! @returns true unless an error occurred during the flush.
    bool ResizeCoinsCaches(size_t coinstip_size, size_t coinsdb_size)

    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    /**
     * Update the on-disk chain state.
     * The caches and indexes are flushed depending on the mode we're called with
     * if they're too large, if it's been a while since the last write,
     * or always and in all cases if we're in prune mode and are deleting files.
     *
     * If FlushStateMode::NONE is used, then FlushStateToDisk(...) won't do anything
     * besides checking if we need to prune.
     */
    bool FlushStateToDisk(const CChainParams &chainparams, CValidationState &state, FlushStateMode mode,
                          int nManualPruneHeight = 0);

    void ForceFlushStateToDisk();

    void PruneAndFlush();

    bool ActivateBestChain(
            CValidationState &state,
            const CChainParams &chainparams,
            std::shared_ptr<const CBlock> pblock);

    bool
    AcceptBlock(const std::shared_ptr<const CBlock> &pblock, CValidationState &state, const CChainParams &chainparams,
                CBlockIndex **ppindex, bool fRequested, const FlatFilePos *dbp, bool *fNewBlock);

    // Block (dis)connection on a given view:
    DisconnectResult DisconnectBlock(const CBlock &block, const CBlockIndex *pindex, CCoinsViewCache &view,
                                     CAssetsCache *assetsCache = nullptr);

    bool ConnectBlock(const CBlock &block, CValidationState &state, CBlockIndex *pindex, CCoinsViewCache &view,
                      const CChainParams &chainparams, CAssetsCache *assetsCache = nullptr, bool fJustCheck = false);

    // Apply the effects of a block disconnection on the UTXO set.
    bool DisconnectTip(CValidationState &state, const CChainParams &chainparams,
                       DisconnectedBlockTransactions *disconnectpool)

    EXCLUSIVE_LOCKS_REQUIRED(cs_main, ::mempool
    .cs);

    // Manual block validity manipulation:
    bool PreciousBlock(CValidationState &state, const CChainParams &params, CBlockIndex *pindex)

    LOCKS_EXCLUDED(cs_main);

    bool InvalidateBlock(CValidationState &state, const CChainParams &chainparams, CBlockIndex *pindex)

    EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    bool MarkConflictingBlock(CValidationState &state, const CChainParams &chainparams, CBlockIndex *pindex)

    EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    void ResetBlockFailureFlags(CBlockIndex *pindex)

    EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Replay blocks that aren't fully applied to the database. */
    bool ReplayBlocks(const CChainParams &params);

    bool RewindBlockIndex(const CChainParams &params);

    bool LoadGenesisBlock(const CChainParams &chainparams);

    bool AddGenesisBlock(const CChainParams &chainparams, const CBlock &block, CValidationState &state);

    void PruneBlockIndexCandidates();

    void UnloadBlockIndex();

    /** Check whether we are doing an initial block download (synchronizing from disk or network) */
    bool IsInitialBlockDownload() const;

    void CheckBlockIndex(const Consensus::Params &consensusParams);

    /** Update the chain tip based on database information, i.e. CoinsTip()'s best block. */
    bool LoadChainTip(const CChainParams &chainparams)

    EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    std::string ToString()

    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    //! Dictates whether we need to flush the cache to disk or not.
    //!
    //! @return the state of the size of the coins cache.
    CoinsCacheSizeState GetCoinsCacheSizeState(const CTxMemPool *tx_pool)

    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    CoinsCacheSizeState
    GetCoinsCacheSizeState(const CTxMemPool *tx_pool, size_t max_coins_cache_size_bytes, size_t max_mempool_size_bytes)

    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

private:
    bool ActivateBestChainStep(CValidationState &state, const CChainParams &chainparams, CBlockIndex *pindexMostWork,
                               const std::shared_ptr<const CBlock> &pblock, bool &fInvalidFound,
                               ConnectTrace &connectTrace)

    EXCLUSIVE_LOCKS_REQUIRED(cs_main, ::mempool
    .cs);

    bool ConnectTip(CValidationState &state, const CChainParams &chainparams, CBlockIndex *pindexNew,
                    const std::shared_ptr<const CBlock> &pblock, ConnectTrace &connectTrace,
                    DisconnectedBlockTransactions &disconnectpool)

    EXCLUSIVE_LOCKS_REQUIRED(cs_main, ::mempool
    .cs);

    void InvalidBlockFound(CBlockIndex *pindex, const CValidationState &state);

    CBlockIndex *FindMostWorkChain();

    void ReceivedBlockTransactions(const CBlock &block, CValidationState &state, CBlockIndex *pindexNew,
                                   const FlatFilePos &pos);

    bool RollforwardBlock(const CBlockIndex *pindex, CCoinsViewCache &inputs, const CChainParams &params,
                          CAssetsCache *assetsCache);

    friend ChainstateManager;
};

/** Mark a block as precious and reorganize.
 *
 * May not be called in a
 * validationinterface callback.
 */
bool PreciousBlock(CValidationState &state, const CChainParams &params, CBlockIndex *pindex)

LOCKS_EXCLUDED(cs_main);

/** Mark a block as invalid. */
bool InvalidateBlock(CValidationState &state, const CChainParams &chainparams, CBlockIndex *pindex)

EXCLUSIVE_LOCKS_REQUIRED(cs_main);

/** Mark a block as conflicting. */
bool MarkConflictingBlock(CValidationState &state, const CChainParams &chainparams, CBlockIndex *pindex)

EXCLUSIVE_LOCKS_REQUIRED(cs_main);

/** Remove invalidity status from a block and its descendants. */
void ResetBlockFailureFlags(CBlockIndex *pindex)

EXCLUSIVE_LOCKS_REQUIRED(cs_main);

/**
 * Provides an interface for creating and interacting with one or two
 * chainstates: an IBD chainstate generated by downloading blocks, and
 * an optional snapshot chainstate loaded from a UTXO snapshot. Managed
 * chainstates can be maintained at different heights simultaneously.
 *
 * This class provides abstractions that allow the retrieval of the current
 * most-work chainstate ("Active") as well as chainstates which may be in
 * background use to validate UTXO snapshots.
 *
 * Definitions:
 *
 * *IBD chainstate*: a chainstate whose current state has been "fully"
 *   validated by the initial block download process.
 *
 * *Snapshot chainstate*: a chainstate populated by loading in an
 *    assumeutxo UTXO snapshot.
 *
 * *Active chainstate*: the chainstate containing the current most-work
 *    chain. Consulted by most parts of the system (net_processing,
 *    wallet) as a reflection of the current chain and UTXO set.
 *    This may either be an IBD chainstate or a snapshot chainstate.
 *
 * *Background IBD chainstate*: an IBD chainstate for which the
 *    IBD process is happening in the background while use of the
 *    active (snapshot) chainstate allows the rest of the system to function.
 *
 * *Validated chainstate*: the most-work chainstate which has been validated
 *   locally via initial block download. This will be the snapshot chainstate
 *   if a snapshot was loaded and all blocks up to the snapshot starting point
 *   have been downloaded and validated (via background validation), otherwise
 *   it will be the IBD chainstate.
 */
class ChainstateManager {
private:
    //! The chainstate used under normal operation (i.e. "regular" IBD) or, if
    //! a snapshot is in use, for background validation.
    //!
    //! Its contents (including on-disk data) will be deleted *upon shutdown*
    //! after background validation of the snapshot has completed. We do not
    //! free the chainstate contents immediately after it finishes validation
    //! to cautiously avoid a case where some other part of the system is still
    //! using this pointer (e.g. net_processing).
    //!
    //! Once this pointer is set to a corresponding chainstate, it will not
    //! be reset until init.cpp:Shutdown(). This means it is safe to acquire
    //! the contents of this pointer with ::cs_main held, release the lock,
    //! and then use the reference without concern of it being deconstructed.
    //!
    //! This is especially important when, e.g., calling ActivateBestChain()
    //! on all chainstates because we are not able to hold ::cs_main going into
    //! that call.
    std::unique_ptr <CChainState> m_ibd_chainstate;

    //! A chainstate initialized on the basis of a UTXO snapshot. If this is
    //! non-null, it is always our active chainstate.
    //!
    //! Once this pointer is set to a corresponding chainstate, it will not
    //! be reset until init.cpp:Shutdown(). This means it is safe to acquire
    //! the contents of this pointer with ::cs_main held, release the lock,
    //! and then use the reference without concern of it being deconstructed.
    //!
    //! This is especially important when, e.g., calling ActivateBestChain()
    //! on all chainstates because we are not able to hold ::cs_main going into
    //! that call.
    std::unique_ptr <CChainState> m_snapshot_chainstate;

    //! Points to either the ibd or snapshot chainstate; indicates our
    //! most-work chain.
    //!
    //! Once this pointer is set to a corresponding chainstate, it will not
    //! be reset until init.cpp:Shutdown(). This means it is safe to acquire
    //! the contents of this pointer with ::cs_main held, release the lock,
    //! and then use the reference without concern of it being deconstructed.
    //!
    //! This is especially important when, e.g., calling ActivateBestChain()
    //! on all chainstates because we are not able to hold ::cs_main going into
    //! that call.
    CChainState *m_active_chainstate{nullptr};

    //! If true, the assumed-valid chainstate has been fully validated
    //! by the background validation chainstate.
    bool m_snapshot_validated{false};

    // For access to m_active_chainstate.
    friend CChainState &ChainstateActive();

    friend CChain &ChainActive();

public:
    //! A single BlockManager instance is shared across each constructed
    //! chainstate to avoid duplicating block metadata.
    BlockManager m_blockman
    GUARDED_BY(::cs_main);

    //! The total number of bytes available for us to use across all in-memory
    //! coins caches. This will be split somehow across chainstates.
    int64_t m_total_coinstip_cache{0};
    //
    //! The total number of bytes available for us to use across all leveldb
    //! coins databases. This will be split somehow across chainstates.
    int64_t m_total_coinsdb_cache{0};

    //! Instantiate a new chainstate and assign it based upon whether it is
    //! from a snapshot.
    //!
    //! @param[in] snapshot_blockhash   If given, signify that this chainstate
    //!                                 is based on a snapshot.
    CChainState &InitializeChainstate(const uint256 &snapshot_blockhash = uint256())

    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    //! Get all chainstates currently being used.
    std::vector<CChainState *> GetAll();

    //! The most-work chain.
    CChainState &ActiveChainstate() const;

    CChain &ActiveChain() const { return ActiveChainstate().m_chain; }

    int ActiveHeight() const { return ActiveChain().Height(); }

    CBlockIndex *ActiveTip() const { return ActiveChain().Tip(); }

    BlockMap &BlockIndex()

    EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
            {
                    return m_blockman.m_block_index;
            }

    bool IsSnapshotActive() const;

    Optional <uint256> SnapshotBlockhash() const;

    //! Is there a snapshot in use and has it been fully validated?
    bool IsSnapshotValidated() const { return m_snapshot_validated; }

    //! @returns true if this chainstate is being used to validate an active
    //!          snapshot in the background.
    bool IsBackgroundIBD(CChainState *chainstate) const;

    //! Return the most-work chainstate that has been fully validated.
    //!
    //! During background validation of a snapshot, this is the IBD chain. After
    //! background validation has completed, this is the snapshot chain.
    CChainState &ValidatedChainstate() const;

    CChain &ValidatedChain() const { return ValidatedChainstate().m_chain; }

    CBlockIndex *ValidatedTip() const { return ValidatedChain().Tip(); }

    /**
 * Process an incoming block. This only returns after the best known valid
 * block is made active. Note that it does not, however, guarantee that the
 * specific block passed to it has been checked for validity!
 *
 * If you want to *possibly* get feedback on whether pblock is valid, you must
 * install a CValidationInterface (see validationinterface.h) - this will have
 * its BlockChecked method called whenever *any* block completes validation.
 *
 * Note that we guarantee that either the proof-of-work is valid on pblock, or
 * (and possibly also) BlockChecked will have been called.
 *
 * May not be called in a
 * validationinterface callback.
 *
 * @param[in]   pblock  The block we want to process.
 * @param[in]   fForceProcessing Process this block even if unrequested; used for non-network block sources and whitelisted peers.
 * @param[out]  fNewBlock A boolean which is set to indicate if the block was first received via this call
 * @returns     If the block was processed, independently of block validity
 */
    bool
    ProcessNewBlock(const CChainParams &chainparams, const std::shared_ptr<const CBlock> pblock, bool fForceProcessing,
                    bool *fNewBlock)

    LOCKS_EXCLUDED(cs_main);

    /**
     * Process incoming block headers.
     *
     * May not be called in a
     * validationinterface callback.
     *
     * @param[in]  block The block headers themselves
     * @param[out] state This may be set to an Error state if any error occurred processing them
     * @param[in]  chainparams The params for the chain we want to connect to
     * @param[out] ppindex If set, the pointer will be set to point to the last new block index object for the given headers
     * @param[out] first_invalid First header that fails validation, if one exists
     */
    bool ProcessNewBlockHeaders(const std::vector <CBlockHeader> &block, CValidationState &state,
                                const CChainParams &chainparams, const CBlockIndex **ppindex = nullptr,
                                CBlockHeader *first_invalid = nullptr)

    LOCKS_EXCLUDED(cs_main);

    //! Mark one block file as pruned (modify associated database entries)
    void PruneOneBlockFile(const int fileNumber)

    EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    //! Load the block tree and coins database from disk, initializing state if we're running with -reindex
    bool LoadBlockIndex(const CChainParams &chainparams)

    EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    //! Unload block index and chain data before shutdown.
    void Unload()

    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);

    //! Clear (deconstruct) chainstate data.
    void Reset();

    //! Check to see if caches are out of balance and if so, call
    //! ResizeCoinsCaches() as needed.
    void MaybeRebalanceCaches()

    EXCLUSIVE_LOCKS_REQUIRED(::cs_main);
};

/** DEPRECATED! Please use node.chainman instead. May only be used in validation.cpp internally */
extern ChainstateManager g_chainman
GUARDED_BY(::cs_main);

/** Please prefer the identicat ChainstateManager::ActiveChainstate */
CChainState &ChainstateActive();

/** Please prefer the identical ChainstateManager::ActiveChain */
CChain &ChainActive();

/** Please prefer the identical ChainstateManager::BlockIndex */
BlockMap &BlockIndex();

/** @returns the global previous block index map. */
PrevBlockMap &PrevBlockIndex();

/** Global variable that points to the active block tree (protected by cs_main) */
extern std::unique_ptr <CBlockTreeDB> pblocktree;

/** Global variable that point to the active assets database (protected by cs_main) */
extern std::unique_ptr <CAssetsDB> passetsdb;

/** Global variable that point to the active assets cache (protected by cs_main) */
extern std::unique_ptr <CAssetsCache> passetsCache;

/**
 * Return the spend height, which is one more than the inputs.GetBestBlock().
 * While checking, GetBestBlock() refers to the parent block. (protected by cs_main)
 * This is also true for mempool checks.
 */
int GetSpendHeight(const CCoinsViewCache &inputs);

extern VersionBitsCache versionbitscache;

/**
 * Determine what nVersion a new block should use.
 */
int32_t ComputeBlockVersion(const CBlockIndex *pindexPrev, const Consensus::Params &params,
                            bool fCheckSmartnodesUpgraded = false);

/**
 * Return true if hash can be found in ::ChainActive() at nBlockHeight height.
 * Fills hashRet with found hash, if no nBlockHeight is specified - ::ChainActive().Height() is used.
 */
bool GetBlockHash(uint256 &hashRet, int nBlockHeight = -1);

/** Reject codes greater or equal to this can be returned by AcceptToMemPool
 * for transactions, to signal internal conditions. They cannot and should not
 * be sent over the P2P network.
 */
static const unsigned int REJECT_INTERNAL = 0x100;
/** Too high fee. Can not be triggered by P2P transactions */
static const unsigned int REJECT_HIGHFEE = 0x100;

/** Get block file info entry for one block file */
CBlockFileInfo *GetBlockFileInfo(size_t n);

/** Dump the mempool to disk. */
bool DumpMempool(const CTxMemPool &pool);

/** Load the mempool from disk. */
bool LoadMempool(CTxMemPool &pool);

//! Check whether the block associated with this index entry is pruned or not.
inline bool IsBlockPruned(const CBlockIndex *pblockindex) {
    return (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0);
}

#endif // BITCOIN_VALIDATION_H
