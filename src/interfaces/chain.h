// Copyright (c) 2018 The Bitcoin Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_INTERFACES_CHAIN_H
#define BITCOIN_INTERFACES_CHAIN_H

#include <optional.h>               // For Optional and nullopt
#include <primitives/transaction.h> // For CTransactionRef

#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

class CBlock;

class CConnman;

class CRPCCommand;

class CScheduler;

class CValidationState;

class CFeeRate;

class CBlockIndex;

class Coin;

class uint256;

struct CBlockLocator;
struct FeeCalculation;
struct NodeContext;
enum class MemPoolRemovalReason;

namespace llmq {
    class CChainLockSig;

    struct CInstantSendLock;
} // namespace llmq

using CTransactionRef = std::shared_ptr<const CTransaction>;

namespace interfaces {

    class Wallet;

    class Handler;

//! Interface giving clients (wallet processes, maybe other analysis tools in
//! the future) ability to access to the chain state, receive notifications,
//! estimate fees, and submit transactions.
//!
//! TODO: Current chain methods are too low level, exposing too much of the
//! internal workings of the bitcoin node, and not being very convenient to use.
//! Chain methods should be cleaned up and simplified over time. Examples:
//!
//! * The initMessages() and showProgress() methods which the wallet uses to send
//!   notifications to the GUI should go away when GUI and wallet can directly
//!   communicate with each other without going through the node
//!   (https://github.com/bitcoin/bitcoin/pull/15288#discussion_r253321096).
//!
//! * The handleRpc, registerRpcs, rpcEnableDeprecated methods and other RPC
//!   methods can go away if wallets listen for HTTP requests on their own
//!   ports instead of registering to handle requests on the node HTTP port.
//!
//! * Move fee estimation queries to an asynchronous interface and let the
//!   wallet cache it, fee estimation being driven by node mempool, wallet
//!   should be the consumer.
//!
//! * The `guessVerificationProgress`, `getBlockHeight`, `getBlockHash`, etc
//!   methods can go away if rescan logic is moved on the node side, and wallet
//!   only register rescan request.
    class Chain {
    public:
        virtual ~Chain() {}

        //! Get current chain height, not including genesis block (returns 0 if
        //! chain only contains genesis block, nullopt if chain does not contain
        //! any blocks)
        virtual Optional<int> getHeight() = 0;

        //! Get block height above genesis block. Returns 0 for genesis block,
        //! 1 for following block, and so on. Returns nullopt for a block not
        //! included in the current chain.
        virtual Optional<int> getBlockHeight(const uint256 &hash) = 0;

        //! Get block hash. Height must be valid or this function will abort.
        virtual uint256 getBlockHash(int height) = 0;

        //! Get block time. Height must be valid or this function will abort.
        virtual int64_t getBlockTime(int height) = 0;

        //! Get block median time past. Height must be valid or this function
        //! will abort.
        virtual int64_t getBlockMedianTimePast(int height) = 0;

        //! Check that the block is available on disk (i.e. has not been
        //! pruned), and contains transactions.
        virtual bool haveBlockOnDisk(int height) = 0;

        //! Return height of the first block in the chain with timestamp equal
        //! or greater than the given time and height equal or greater than the
        //! given height, or nullopt if there is no block with a high enough
        //! timestamp and height. Also return the block hash as an optional output parameter
        //! (to avoid the cost of a second lookup in case this information is needed.)
        virtual Optional<int> findFirstBlockWithTimeAndHeight(int64_t time, int height, uint256 *hash) = 0;

        //! Return height of last block in the specified range which is pruned, or
        //! nullopt if no block in the range is pruned. Range is inclusive.
        virtual Optional<int> findPruned(int start_height = 0, Optional<int> stop_height = nullopt) = 0;

        //! Return height of the specified block if it is on the chain, otherwise
        //! return the height of the highest block on chain that's an ancestor
        //! of the specified block, or nullopt if there is no common ancestor.
        //! Also return the height of the specified block as an optional output
        //! parameter (to avoid the cost of a second hash lookup in case this
        //! information is desired).
        virtual Optional<int> findFork(const uint256 &hash, Optional<int> *height) = 0;

        //! Get locator for the current chain tip.
        virtual CBlockLocator getTipLocator() = 0;

        //! Return height of the highest block on chain in common with the locator,
        //! which will either be the original block used to create the locator,
        //! or one of its ancestors.
        virtual Optional<int> findLocatorFork(const CBlockLocator &locator) = 0;

        //! Check if transaction will be final given chain height current time.
        virtual bool checkFinalTx(const CTransaction &tx) = 0;

        //! Return whether node has the block and optionally return block metadata
        //! or contents.
        //!
        //! If a block pointer is provided to retrieve the block contents, and the
        //! block exists but doesn't have data (for example due to pruning), the
        //! block will be empty and all fields set to null.
        virtual bool findBlock(const uint256 &hash,
                               CBlock *block = nullptr,
                               int64_t *time = nullptr,
                               int64_t *max_time = nullptr) = 0;

        //! Look up unspent output information. Returns coins in the mempool and in
        //! the current chain UTXO set. Iterates through all the keys in the map and
        //! populates the values.
        virtual void findCoins(std::map <COutPoint, Coin> &coins) = 0;

        //! Estimate fraction of total transactions verified if blocks up to
        //! the specified block hash are verified.
        virtual double guessVerificationProgress(const uint256 &block_hash) = 0;

        //! Check if transaction has descendants in mempool.
        virtual bool hasDescendantsInMempool(const uint256 &txid) = 0;

        //! Transaction is added to memory pool, if the transaction fee is below the
        //! amount specified by max_tx_fee, and broadcast to all peers if relay is set to true.
        //! Return false if the transaction could not be added due to the fee or for another reason.
        virtual bool broadcastTransaction(const CTransactionRef &tx, std::string &err_string, const CAmount &max_tx_fee,
                                          bool relay) = 0;

        //! Calculate mempool ancestor and descendant counts for the given transaction.
        virtual void getTransactionAncestry(const uint256 &txid, size_t &ancestors, size_t &descendants) = 0;

        //! Get the node's package limits.
        //! Currently only returns the ancestor and descendant count limits, but could be enhanced to
        //! return more policy settings.
        virtual void getPackageLimits(unsigned int &limit_ancestor_count, unsigned int &limit_descendant_count) = 0;

        //! Check chain limits.
        virtual bool checkChainLimits(CTransactionRef tx) = 0;

        //! Estimate smart fee.
        virtual CFeeRate estimateSmartFee(int num_blocks, bool conservative, FeeCalculation *calc = nullptr) = 0;

        //! Fee estimator max target.
        virtual unsigned int estimateMaxBlocks() = 0;

        //! Pool min fee.
        virtual CFeeRate mempoolMinFee() = 0;

        //! Relay current minimum fee (from -minrelaytxfee and -incrementalrelayfee settings).
        virtual CFeeRate relayMinFee() = 0;

        //! Relay incremental fee setting (-incrementalrelayfee), reflecting cost of relay.
        virtual CFeeRate relayIncrementalFee() = 0;

        //! Relay dust fee setting (-dustrelayfee), reflecting lowest rate it's economical to spend.
        virtual CFeeRate relayDustFee() = 0;

        //! Check if pruning is enabled.
        virtual bool getPruneMode() = 0;

        //! Check if p2p enabled.
        virtual bool p2pEnabled() = 0;

        //! Check if the node is ready to broadcast transactions.
        virtual bool isReadyToBroadcast() = 0;

        //! Check if in IBD.
        virtual bool isInitialBlockDownload() = 0;

        //! Check if shutdown requested.
        virtual bool shutdownRequested() = 0;

        //! Get adjusted time.
        virtual int64_t getAdjustedTime() = 0;

        //! Send init message.
        virtual void initMessage(const std::string &message) = 0;

        //! Send init warning.
        virtual void initWarning(const std::string &message) = 0;

        //! Send init error.
        virtual void initError(const std::string &message) = 0;

        //! Send progress indicator.
        virtual void showProgress(const std::string &title, int progress, bool resume_possible) = 0;

        //! Chain notifications.
        class Notifications {
        public:
            virtual ~Notifications() {}

            virtual void TransactionAddedToMempool(const CTransactionRef &tx, int64_t nAcceptTime) {}

            virtual void TransactionRemovedFromMempool(const CTransactionRef &ptx, MemPoolRemovalReason reason) {}

            virtual void
            BlockConnected(const CBlock &block, const std::vector <CTransactionRef> &tx_conflicted, int height) {}

            virtual void BlockDisconnected(const CBlock &block, int height) {}

            virtual void UpdatedBlockTip() {}

            virtual void ChainStateFlushed(const CBlockLocator &locator) {}

            virtual void NotifyChainLock(const CBlockIndex *pindexChainLock,
                                         const std::shared_ptr<const llmq::CChainLockSig> &clsig) {}

            virtual void NotifyTransactionLock(const CTransactionRef &tx,
                                               const std::shared_ptr<const llmq::CInstantSendLock> &islock) {}
        };

        //! Register handler for notifications.
        virtual std::unique_ptr <Handler> handleNotifications(std::shared_ptr <Notifications> notifications) = 0;

        //! Wait for pending notifications to be processed unless block hash points to the current
        //! chain tip.
        virtual void waitForNotificationsIfTipChanged(const uint256 &old_tip) = 0;

        //! Register handler for RPC. Command is not copied, so reference
        //! needs to remain valid until Handler is disconnected.
        virtual std::unique_ptr <Handler> handleRpc(const CRPCCommand &command) = 0;

        //! Check if deprecated RPC is enabled.
        virtual bool rpcEnableDeprecated(const std::string &method) = 0;

        //! Run function after given number of seconds. Cancel any previous calls with same name.
        virtual void rpcRunLater(const std::string &name, std::function<void()> fn, int64_t seconds) = 0;

        //! Synchronously send TransactionAddedToMempool notifications about all
        //! current mempool transactions to the specified handler and return after
        //! the last one is sent. These notifications aren't coordinated with async
        //! notifications sent by handleNotifications, so out of date async
        //! notifications from handleNotifications can arrive during and after
        //! synchronous notifications from requestMempoolTransactions. Clients need
        //! to be prepared to handle this by ignoring notifications about unknown
        //! removed transactions and already added new transactions.
        virtual void requestMempoolTransactions(Notifications &notifications) = 0;
    };

//! Interface to let node manage chain clients (wallets, or maybe tools for
//! monitoring and analysis in the future).
    class ChainClient {
    public:
        virtual ~ChainClient() {}

        //! Register rpcs.
        virtual void registerRpcs() = 0;

        //! Check for errors before loading.
        virtual bool verify() = 0;

        //! Load saved state.
        virtual bool load() = 0;

        //! Start client execution and provide a scheduler.
        virtual void start(CScheduler &scheduler) = 0;

        //! Save state to disk.
        virtual void flush() = 0;

        //! Shut down client.
        virtual void stop() = 0;

        //! Set mock time.
        virtual void setMockTime(int64_t time) = 0;
    };

//! Return implementation of Chain interface.
    std::unique_ptr <Chain> MakeChain(NodeContext &node);

} // namespace interfaces

#endif // BITCOIN_INTERFACES_CHAIN_H
