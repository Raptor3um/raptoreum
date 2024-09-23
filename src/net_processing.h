// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NET_PROCESSING_H
#define BITCOIN_NET_PROCESSING_H

#include <consensus/params.h>
#include <net.h>
#include <sync.h>
#include <validationinterface.h>

class CTxMemPool;

class ChainstateManager;

extern RecursiveMutex cs_main;

/** Default for -maxorphantxsize, maximum size in megabytes the orphan map can grow before entries are removed */
static const unsigned int DEFAULT_MAX_ORPHAN_TRANSACTIONS_SIZE = 10; // this allows around 100 TXs of max size (and many more of normal size)
/** Default number of orphan+recently-replaced txn to keep around for block reconstruction */
static const unsigned int DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN = 100;
/** Default for BIP61 (sending reject messages) */
static constexpr bool DEFAULT_ENABLE_BIP61 = true;

class PeerLogicValidation final : public CValidationInterface, public NetEventsInterface {
private:
    CConnman *const connman;
    BanMan *const m_banman;
    ChainstateManager &m_chainman;
    CTxMemPool &m_mempool;

    bool SendRejectsAndCheckIfBanned(CNode *pnode, bool enable_bip61)

    EXCLUSIVE_LOCKS_REQUIRED(cs_main);
public:
    PeerLogicValidation(CConnman *connmanIn, BanMan *banman, CScheduler &scheduler, ChainstateManager &chainman,
                        CTxMemPool &pool, bool enable_bip61);

    /**
     * Overridden from CValidationInterface.
     */
    void BlockConnected(const std::shared_ptr<const CBlock> &pblock, const CBlockIndex *pindexConnected,
                        const std::vector <CTransactionRef> &vtxConflicted) override;

    /**
     * Overridden from CValidationInterface.
     */
    void UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload) override;

    /**
     * Overridden from CValidationInterface.
     */
    void BlockChecked(const CBlock &block, const CValidationState &state) override;

    /**
     * Overridden from CValidationInterface.
     */
    void NewPoWValidBlock(const CBlockIndex *pindex, const std::shared_ptr<const CBlock> &pblock) override;

    /** Initialize a peer by adding it to mapNodeState and pushing a message requesting its version */
    void InitializeNode(CNode *pnode) override;

    /** Handle removal of a peer by updating various state and removing it from mapNodeState */
    void FinalizeNode(NodeId nodeid, bool &fUpdateConnectionTime) override;

    /**
    * Process protocol messages received from a given node
    *
    * @param[in]   pfrom           The node which we have received messages from.
    * @param[in]   interrupt       Interrupt condition for processing threads
    */
    bool ProcessMessages(CNode *pfrom, std::atomic<bool> &interrupt) override;

    /**
    * Send queued protocol messages to be sent to a give node.
    *
    * @param[in]   pto             The node which we are sending messages to.
    * @return                      True if there is more work to be done
    */
    bool SendMessages(CNode *pto) override

    EXCLUSIVE_LOCKS_REQUIRED(pto
    ->cs_sendProcessing);

    /** Consider evicting an outbound peer based on the amount of time they've been behind our tip */
    void ConsiderEviction(CNode *pto, int64_t time_in_seconds)

    EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /** Evict extra outbound peers. If we think our tip may be stale, connect to an extra outbound */
    void CheckForStaleTipAndEvictPeers(const Consensus::Params &consensusParams);

    /** If we have extra outbound peers, try to disconnect the one with the oldest block announcement */
    void EvictExtraOutboundPeers(int64_t time_in_seconds)

    EXCLUSIVE_LOCKS_REQUIRED(cs_main);

private:
    int64_t m_stale_tip_check_time; //! Next time to check for stale tip

    /** Enable BIP61 (sending reject messages) */
    const bool m_enable_bip61;
};

struct CNodeStateStats {
    int nMisbehavior = 0;
    int nSyncHeight = -1;
    int nCommonHeight = -1;
    std::vector<int> vHeightInFlight;
};

/** Get statistics from node state */
bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats);

bool IsBanned(NodeId nodeid)

EXCLUSIVE_LOCKS_REQUIRED(cs_main);

// Upstream moved this into net_processing.cpp (13417), however since we use Misbehaving in a number of raptoreum specific
// files such as mnauth.cpp and governance.cpp it makes sense to keep it in the header
/** Increase a node's misbehavior score. */
void Misbehaving(NodeId nodeid, int howmuch, const std::string &message = "")

EXCLUSIVE_LOCKS_REQUIRED(cs_main);

void EraseObjectRequest(NodeId nodeId, const CInv &inv)

EXCLUSIVE_LOCKS_REQUIRED(cs_main);

void RequestObject(NodeId nodeId, const CInv &inv, std::chrono::microseconds current_time, bool fForce = false)

EXCLUSIVE_LOCKS_REQUIRED(cs_main);

size_t GetRequestedObjectCount(NodeId nodeId)

EXCLUSIVE_LOCKS_REQUIRED(cs_main);

/** Relay transaction to every node */
void RelayTransaction(const uint256 &, const CConnman &connman);

#endif // BITCOIN_NET_PROCESSING_H
