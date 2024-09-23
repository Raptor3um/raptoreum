// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include <uint256.h>
#include <founder_payment.h>
#include <llmq/quorums_parameters.h>
#include <smartnode/smartnode-collaterals.h>

namespace Consensus {

/**
 * future fee share for smartnode, miner and dev
 */
    struct FutureRewardShare {
        float smartnode;
        float miner;
        float founder;

        FutureRewardShare() : smartnode(0), miner(0), founder(0) {}

        FutureRewardShare(float _smartnode, float _miner, float _founder) : smartnode(_smartnode), miner(_miner),
                                                                            founder(_founder) {}
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

        std::map <LLMQType, LLMQParams> llmqs;
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
template<typename T>
struct is_serializable_enum;
template<>
struct is_serializable_enum<Consensus::LLMQType> : std::true_type {
};

#endif // BITCOIN_CONSENSUS_PARAMS_H
