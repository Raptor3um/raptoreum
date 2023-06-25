// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAINPARAMS_H
#define BITCOIN_CHAINPARAMS_H

#include <chainparamsbase.h>
#include <consensus/params.h>
#include <llmq/quorums_parameters.h>
#include <primitives/block.h>
#include <protocol.h>
#include <chain.h>

#include <memory>
#include <vector>

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

typedef std::map<int, uint256> MapCheckpoints;

struct CCheckpointData {
    MapCheckpoints mapCheckpoints;
};

/**
 * Holds various statistics on transactions within a chain. Used to estimate
 * verification progress during chain sync.
 *
 * See also: CChainParams::TxData, GuessVerificationProgress.
 */
struct ChainTxData {
    int64_t nTime;
    int64_t nTxCount;
    double dTxRate;
};

/**
 * CChainParams defines various tweakable parameters of a given instance of the
 * Raptoreum system. There are three: the main network on which people trade goods
 * and services, the public test network which gets reset from time to time and
 * a regression test mode which is intended for private networks only. It has
 * minimal difficulty to ensure that blocks can be found instantly.
 */
class CChainParams {
public:
    enum Base58Type {
        PUBKEY_ADDRESS,
        SCRIPT_ADDRESS,
        SECRET_KEY,     // BIP16
        EXT_PUBLIC_KEY, // BIP32
        EXT_SECRET_KEY, // BIP32

        MAX_BASE58_TYPES
    };

    const Consensus::Params &GetConsensus() const { return consensus; }

    const CMessageHeader::MessageStartChars &MessageStart() const { return pchMessageStart; }

    int GetDefaultPort() const { return nDefaultPort; }

    const CBlock &GenesisBlock() const { return genesis; }

    const CBlock &DevNetGenesisBlock() const { return devnetGenesis; }

    /** Default value for -checkmempool and -checkblockindex argument */
    bool DefaultConsistencyChecks() const { return fDefaultConsistencyChecks; }

    /** Policy: Filter transactions that do not match well-defined patterns */
    bool RequireStandard() const { return fRequireStandard; }

    /** Require addresses specified with "-externalip" parameter to be routable */
    bool RequireRoutableExternalIP() const { return fRequireRoutableExternalIP; }

    /** If this chain allows time to be mocked */
    bool IsMockableChain() const { return m_is_mockable_chain; }

    uint64_t PruneAfterHeight() const { return nPruneAfterHeight; }

    /** Minimum free space (in GB) needed for data directory */
    uint64_t AssumedBlockchainSize() const { return m_assumed_blockchain_size; }

    /** Minimum free space (in GB) needed for data directory when pruned; Does not include prune targets */
    uint64_t AssumedChainStateSize() const { return m_assumed_chain_state_size; }

    /** Make miner stop after a block is found. In RPC, don't return until nGenProcLimit blocks are generated */
    bool MineBlocksOnDemand() const { return fMineBlocksOnDemand; }

    /** Allow multiple addresses to be selected from the same network group (e.g. 192.168.x.x) */
    bool AllowMultipleAddressesFromGroup() const { return fAllowMultipleAddressesFromGroup; }

    /** Allow nodes with the same address and multiple ports */
    bool AllowMultiplePorts() const { return fAllowMultiplePorts; }

    bool MiningRequiresPeers() const { return miningRequiresPeers; }

    /** How long to wait until we allow retrying of a LLMQ connection  */
    int LLMQConnectionRetryTimeout() const { return nLLMQConnectionRetryTimeout; }

    /** Return the BIP70 network string (main, test or regtest) */
    std::string NetworkIDString() const { return strNetworkID; }

    /** Return the list of hostnames to look up for DNS seeds */
    const std::vector <std::string> &DNSSeeds() const { return vSeeds; }

    const std::vector<unsigned char> &Base58Prefix(Base58Type type) const { return base58Prefixes[type]; }

    int ExtCoinType() const { return nExtCoinType; }

    const std::vector <SeedSpec6> &FixedSeeds() const { return vFixedSeeds; }

    const CCheckpointData &Checkpoints() const { return checkpointData; }

    const ChainTxData &TxData() const { return chainTxData; }

    void UpdateDIP3Parameters(int nActivationHeight, int nEnforcementHeight);

    void
    UpdateBudgetParameters(int nSmartnodePaymentsStartBlock, int nBudgetPaymentsStartBlock, int nSuperblockStartBlock);

    void UpdateSubsidyAndDiffParams(int nMinimumDifficultyBlocks, int nHighSubsidyBlocks, int nHighSubsidyFactor);

    void UpdateLLMQChainLocks(Consensus::LLMQType llmqType);

    void UpdateLLMQInstantSend(Consensus::LLMQType llmqType);

    void UpdateLLMQParams(size_t totalMnCount, int height, bool lowLLMQParams = false);

    int PoolMinParticipants() const { return nPoolMinParticipants; }

    int PoolMaxParticipants() const { return nPoolMaxParticipants; }

    int FulfilledRequestExpireTime() const { return nFulfilledRequestExpireTime; }

    bool IsFutureActive(CBlockIndex *index) const {
        int height = index == nullptr ? 0 : index->nHeight;
        return height >= GetConsensus().nFutureForkBlock;
    };

    bool IsAssetsActive(CBlockIndex *index) const {
        int height = index == nullptr ? 0 : index->nHeight;
        return height >= GetConsensus().nAssetsForkBlock;
    };

    const std::vector <std::string> &SporkAddresses() const { return vSporkAddresses; }

    int MinSporkKeys() const { return nMinSporkKeys; }

    bool BIP9CheckSmartnodesUpgraded() const { return fBIP9CheckSmartnodesUpgraded; }

protected:
    CChainParams() {}

    Consensus::Params consensus;
    CMessageHeader::MessageStartChars pchMessageStart;
    int nDefaultPort;
    uint64_t nPruneAfterHeight;
    uint64_t m_assumed_blockchain_size;
    uint64_t m_assumed_chain_state_size;
    std::vector <std::string> vSeeds;
    std::vector<unsigned char> base58Prefixes[MAX_BASE58_TYPES];
    int nExtCoinType;
    std::string strNetworkID;
    CBlock genesis;
    CBlock devnetGenesis;
    std::vector <SeedSpec6> vFixedSeeds;
    bool fDefaultConsistencyChecks;
    bool fRequireStandard;
    bool fRequireRoutableExternalIP;
    bool fMineBlocksOnDemand;
    bool fAllowMultipleAddressesFromGroup;
    bool fAllowMultiplePorts;
    bool m_is_mockable_chain;
    bool miningRequiresPeers;
    int nLLMQConnectionRetryTimeout;
    CCheckpointData checkpointData;
    ChainTxData chainTxData;
    int nPoolMinParticipants;
    int nPoolNewMinParticipants;
    int nPoolMaxParticipants;
    int nPoolNewMaxParticipants;
    int nFulfilledRequestExpireTime;
    std::vector <std::string> vSporkAddresses;
    int nMinSporkKeys;
    bool fBIP9CheckSmartnodesUpgraded;
};

/**
 * Creates and returns a std::unique_ptr<CChainParams> of the chosen chain.
 * @returns a CChainParams* of the chosen chain.
 * @throws a std::runtime_error if the chain is not supported.
 */
std::unique_ptr <CChainParams> CreateChainParams(const std::string &chain);

/**
 * Return the currently selected parameters. This won't change after app
 * startup, except for unit tests.
 */
const CChainParams &Params();

/**
 * Sets the params returned by Params() to those for the given BIP70 chain name.
 * @throws std::runtime_error when the chain is not supported.
 */
void SelectParams(const std::string &chain);

void UpdateLLMQParams(size_t totalMnCount, int height, bool lowLLMQParams = false);

#endif // BITCOIN_CHAINPARAMS_H
