// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include <uint256.h>
#include <founder_payment.h>
#include <smartnode/smartnode-collaterals.h>
#include <map>
#include <string>

namespace Consensus {

enum DeploymentPos
{
    DEPLOYMENT_TESTDUMMY,
    DEPLOYMENT_V17, // Hard Fork v17

    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in versionbits.cpp
    MAX_VERSION_BITS_DEPLOYMENTS
};

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit;
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime;
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout;
    /** The number of past blocks (including the block under consideration) to be taken into account for locking in a fork. */
    int64_t nWindowSize{0};
    /** A starting number of blocks, in the range of 1..nWindowSize, which must signal for a fork in order to lock it in. */
    int64_t nThresholdStart{0};
    /** A minimum number of blocks, in the range of 1..nWindowSize, which must signal for a fork in order to lock it in. */
    int64_t nThresholdMin{0};
    /** A coefficient which adjusts the speed a required number of signaling blocks is decreasing from nThresholdStart to nThresholdMin at with each period. */
    int64_t nFalloffCoeff{0};
};

enum LLMQType : uint8_t
{
    LLMQ_NONE = 0xff,

    LLMQ_50_60 = 1, // 50 members, 30 (60%) threshold, one per hour
    LLMQ_400_60 = 2, // 400 members, 240 (60%) threshold, one every 12 hours
    LLMQ_400_85 = 3, // 400 members, 340 (85%) threshold, one every 24 hours
    LLMQ_100_67 = 4, // 100 members, 67 (67%) threshold, one per hour
	// these are LLMQ set when network still young
//	LLMQ_10_60 = 4, // 10 members, 6 (60%) threshold, one per hour
//	LLMQ_40_60 = 5, // 40 members, 24 (60%) threshold, one every 12 hours
//	LLMQ_40_85 = 6, // 40 members, 34 (85%) threshold, one every 24 hours

    // for testing only
    LLMQ_5_60 = 100, // 5 members, 3 (60%) threshold, one per hour. Params might be different when use -llmqtestparams
    LLMQ_TEST_V17 = 101, // 3 members, 2 (66%) threshold, one per hour
};

// Configures a LLMQ and its DKG
// See https://github.com/raptor3um/dips/blob/master/dip-0006.md for more details
struct LLMQParams {
    LLMQType type;

    // not consensus critical, only used in logging, RPC and UI
    std::string name;

    // the size of the quorum, e.g. 50 or 400
    int size;

    // The minimum number of valid members after the DKK. If less members are determined valid, no commitment can be
    // created. Should be higher then the threshold to allow some room for failing nodes, otherwise quorum might end up
    // not being able to ever created a recovered signature if more nodes fail after the DKG
    int minSize;

    // The threshold required to recover a final signature. Should be at least 50%+1 of the quorum size. This value
    // also controls the size of the public key verification vector and has a large influence on the performance of
    // recovery. It also influences the amount of minimum messages that need to be exchanged for a single signing session.
    // This value has the most influence on the security of the quorum. The number of total malicious smartnodes
    // required to negatively influence signing sessions highly correlates to the threshold percentage.
    int threshold;

    // The interval in number blocks for DKGs and the creation of LLMQs. If set to 24 for example, a DKG will start
    // every 24 blocks, which is approximately once every hour.
    int dkgInterval;

    // The number of blocks per phase in a DKG session. There are 6 phases plus the mining phase that need to be processed
    // per DKG. Set this value to a number of blocks so that each phase has enough time to propagate all required
    // messages to all members before the next phase starts. If blocks are produced too fast, whole DKG sessions will
    // fail.
    int dkgPhaseBlocks;

    // The starting block inside the DKG interval for when mining of commitments starts. The value is inclusive.
    // Starting from this block, the inclusion of (possibly null) commitments is enforced until the first non-null
    // commitment is mined. The chosen value should be at least 5 * dkgPhaseBlocks so that it starts right after the
    // finalization phase.
    int dkgMiningWindowStart;

    // The ending block inside the DKG interval for when mining of commitments ends. The value is inclusive.
    // Choose a value so that miners have enough time to receive the commitment and mine it. Also take into consideration
    // that miners might omit real commitments and revert to always including null commitments. The mining window should
    // be large enough so that other miners have a chance to produce a block containing a non-null commitment. The window
    // should at the same time not be too large so that not too much space is wasted with null commitments in case a DKG
    // session failed.
    int dkgMiningWindowEnd;

    // In the complaint phase, members will vote on other members being bad (missing valid contribution). If at least
    // dkgBadVotesThreshold have voted for another member to be bad, it will considered to be bad by all other members
    // as well. This serves as a protection against late-comers who send their contribution on the bring of
    // phase-transition, which would otherwise result in inconsistent views of the valid members set
    int dkgBadVotesThreshold;

    // Number of quorums to consider "active" for signing sessions
    int signingActiveQuorumCount;

    // Used for intra-quorum communication. This is the number of quorums for which we should keep old connections. This
    // should be at least one more then the active quorums set.
    int keepOldConnections;

    // How many members should we try to send all sigShares to before we give up.
    int recoveryMembers;
};

/**
 * future fee share for smartnode, miner and dev
 */
struct FutureRewardShare {
	float smartnode;
	float miner;
	float founder;
	FutureRewardShare() : smartnode(0), miner(0), founder(0) {}
	FutureRewardShare(float _smartnode, float _miner, float _founder) : smartnode(_smartnode), miner(_miner), founder(_founder) {}
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    uint256 hashDevnetGenesisBlock;
    int nSubsidyHalvingInterval;
    int nSmartnodePaymentsStartBlock;
    int nSmartnodePaymentsIncreaseBlock;
    int nSmartnodePaymentsIncreasePeriod; // in blocks
    int nInstantSendConfirmationsRequired; // in blocks
    int nInstantSendKeepLock; // in blocks
    int nBudgetPaymentsStartBlock;
    int nBudgetPaymentsCycleBlocks;
    int nBudgetPaymentsWindowBlocks;
    int nSuperblockStartBlock;
    uint256 nSuperblockStartHash;
    int nSuperblockCycle; // in blocks
    int nGovernanceMinQuorum; // Min absolute vote count to trigger an action
    int nGovernanceFilterElements;
    int nSmartnodeMinimumConfirmations;
    bool BIPCSVEnabled;
    bool BIP147Enabled;
    /** Block height and hash at which BIP34 becomes active */
    bool BIP34Enabled;
    /** Block height at which BIP65 becomes active */
    bool BIP65Enabled;
    /** Block height at which BIP66 becomes active */
    bool BIP66Enabled;
    /** Block height at which DIP0001 becomes active */
    bool DIP0001Enabled;
    /** Block height at which DIP0003 becomes active */
    bool DIP0003Enabled;
    bool DIP0008Enabled;
    /** Block height at which DIP0003 becomes enforced */
    //int DIP0003EnforcementHeight;
    /**
     * Minimum blocks including miner confirmation of the total of nMinerConfirmationWindow blocks in a retargeting period,
     * (nPowTargetTimespan / nPowTargetSpacing) which is also used for BIP9 deployments.
     * Default BIP9Deployment::nThresholdStart value for deployments where it's not specified and for unknown deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t nRuleChangeActivationThreshold;
    // Default BIP9Deployment::nWindowSize value for deployments where it's not specified and for unknown deployments.
    uint32_t nMinerConfirmationWindow;
    BIP9Deployment vDeployments[MAX_VERSION_BITS_DEPLOYMENTS];
    /** Proof of work parameters */
    uint256 powLimit;
    bool fPowAllowMinDifficultyBlocks;
    bool fPowNoRetargeting;
    int64_t nPowTargetSpacing;
    int64_t nPowTargetTimespan;
    int nPowDGWHeight;
    int DGWBlocksAvg;
    int64_t DifficultyAdjustmentInterval() const { return nPowTargetTimespan / nPowTargetSpacing; }
    uint256 nMinimumChainWork;
    uint256 defaultAssumeValid;

    /** these parameters are only used on devnet and can be configured from the outside */
    int nMinimumDifficultyBlocks{0};
    int nHighSubsidyBlocks{0};
    int nHighSubsidyFactor{1};

    std::map<LLMQType, LLMQParams> llmqs;
    LLMQType llmqTypeChainLocks;
    LLMQType llmqTypeInstantSend{LLMQ_NONE};
    LLMQType llmqTypePlatform{LLMQ_NONE};

    FounderPayment nFounderPayment;
    FutureRewardShare nFutureRewardShare;
    SmartnodeCollaterals nCollaterals;
    int smartnodePaymentFixedBlock;
    int nFutureForkBlock;

};
} // namespace Consensus

// This must be outside of all namespaces. We must also duplicate the forward declaration of is_serializable_enum to
// avoid inclusion of serialize.h here.
template<typename T> struct is_serializable_enum;
template<> struct is_serializable_enum<Consensus::LLMQType> : std::true_type {};

#endif // BITCOIN_CONSENSUS_PARAMS_H
