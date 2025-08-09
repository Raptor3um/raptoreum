// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2014-2020 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LLMQ_PARAMS_H
#define BITCOIN_LLMQ_PARAMS_H

#include <cstdint>
#include <string_view>

namespace Consensus {

    enum LLMQType : uint8_t {
        LLMQ_NONE     = 0xFF,
        LLMQ_INVALID  = 0x00, // Do not use - flag to indicate new serialization
        LLMQ_50_60    = 1,    // 50 members, 30 (60%) threshold, one per hour
        LLMQ_400_60   = 2,    // 400 members, 240 (60%) threshold, one every 12 hours
        LLMQ_400_85   = 3,    // 400 members, 340 (85%) threshold, one every 24 hours
        LLMQ_100_67   = 4,    // 100 members, 67 (67%) threshold, one per hour

        // these are LLMQ set when network still young
        // LLMQ_10_60 = 4, // 10 members, 6 (60%) threshold, one per hour
        // LLMQ_40_60 = 5, // 40 members, 24 (60%) threshold, one every 12 hours
        // LLMQ_40_85 = 6, // 40 members, 34 (85%) threshold, one every 24 hours

        // for testing only
        LLMQ_5_60     = 100,  // 5 members, 3 (60%) threshold, one every 12 hours. Params might be different when use -llmqtestparams
        LLMQ_TEST_V17 = 101,  // 3 members, 2 (66%) threshold, one per hour
    };

// Configures a LLMQ and its DKG
// See https://github.com/raptoreum/dips/blob/master/dip-0006.md for more details
    struct LLMQParams {
        LLMQType type;

        // not consensus critical, only used in logging, RPC and UI
        std::string_view name;

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

        // The number of quorums for which we should keep keys. Usually it's equal to signingActiveQuorumCount * 2.
        // Unlike for other quorum types we want to keep data (secret key shares and vvec)
        // for Platform quorums for much longer because Platform can be restarted and
        // it must be able to re-sign stuff.

        int keepOldKeys;

        // How many members should we try to send all sigShares to before we give up.
        int recoveryMembers;
    public:
        // For how many blocks recent DKG info should be kept
        [[ nodiscard ]] constexpr int max_store_depth() const
        {
            return keepOldKeys * dkgInterval;
        }
    };

    static constexpr LLMQParams
    llmq200_2 = {
            .type = LLMQ_50_60,
            .name = "llmq_200_2",
            .size = 200,
            .minSize = 2,
            .threshold = 2,

            .dkgInterval = 30, // one DKG per hour
            .dkgPhaseBlocks = 2,
            .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
            .dkgMiningWindowEnd = 18,
            .dkgBadVotesThreshold = 8,

            .signingActiveQuorumCount = 2, // just a few ones to allow easier testing

            .keepOldConnections = 3,
            .keepOldKeys = 4,
            .recoveryMembers = 3,
    };

    static constexpr LLMQParams
    llmq3_60 = {
            .type = LLMQ_50_60,
            .name = "llmq_3_60",
            .size = 3,
            .minSize = 2,
            .threshold = 2,

            .dkgInterval = 30, // one DKG per hour
            .dkgPhaseBlocks = 2,
            .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
            .dkgMiningWindowEnd = 18,
            .dkgBadVotesThreshold = 2,

            .signingActiveQuorumCount = 2, // just a few ones to allow easier testing

            .keepOldConnections = 3,
            .keepOldKeys = 4,
            .recoveryMembers = 3,
    };

    static constexpr LLMQParams
    llmq5_60 = {
            .type = LLMQ_400_60,
            .name = "llmq_5_60",
            .size = 5,
            .minSize = 4,
            .threshold = 3,

            .dkgInterval = 30 * 12, // one DKG every 12 hours
            .dkgPhaseBlocks = 4,
            .dkgMiningWindowStart = 20, // dkgPhaseBlocks * 5 = after finalization
            .dkgMiningWindowEnd = 28,
            .dkgBadVotesThreshold = 30,

            .signingActiveQuorumCount = 4, // two days worth of LLMQs

            .keepOldConnections = 5,
            .keepOldKeys = 8,
            .recoveryMembers = 5,
    };

    static constexpr LLMQParams
    llmq5_85 = {
            .type = LLMQ_400_85,
            .name = "llmq_5_85",
            .size = 5,
            .minSize = 4,
            .threshold = 3,

            .dkgInterval = 30 * 24, // one DKG every 24 hours
            .dkgPhaseBlocks = 4,
            .dkgMiningWindowStart = 20, // dkgPhaseBlocks * 5 = after finalization
            .dkgMiningWindowEnd = 48, // give it a larger mining window to make sure it is mined
            .dkgBadVotesThreshold = 30,

            .signingActiveQuorumCount = 4, // four days worth of LLMQs

            .keepOldConnections = 5,
            .keepOldKeys = 8,
            .recoveryMembers = 5,
    };

    static constexpr LLMQParams
    llmq20_60 = {
            .type = LLMQ_400_60,
            .name = "llmq_20_60",
            .size = 20,
            .minSize = 15,
            .threshold = 12,

            .dkgInterval = 30 * 12, // one DKG every 12 hours
            .dkgPhaseBlocks = 4,
            .dkgMiningWindowStart = 20, // dkgPhaseBlocks * 5 = after finalization
            .dkgMiningWindowEnd = 28,
            .dkgBadVotesThreshold = 30,

            .signingActiveQuorumCount = 4, // two days worth of LLMQs

            .keepOldConnections = 5,
            .keepOldKeys = 8,
            .recoveryMembers = 5,
    };

    static constexpr LLMQParams
    llmq20_85 = {
            .type = LLMQ_400_85,
            .name = "llmq_20_85",
            .size = 20,
            .minSize = 18,
            .threshold = 17,

            .dkgInterval = 30 * 24, // one DKG every 24 hours
            .dkgPhaseBlocks = 4,
            .dkgMiningWindowStart = 20, // dkgPhaseBlocks * 5 = after finalization
            .dkgMiningWindowEnd = 48, // give it a larger mining window to make sure it is mined
            .dkgBadVotesThreshold = 30,

            .signingActiveQuorumCount = 4, // four days worth of LLMQs

            .keepOldConnections = 5,
            .keepOldKeys = 8,
            .recoveryMembers = 5,
    };

    static constexpr LLMQParams
    llmq10_60 = {
            .type = LLMQ_50_60,
            .name = "llmq_10_60",
            .size = 10,
            .minSize = 8,
            .threshold = 7,

            .dkgInterval = 30, // one DKG per hour
            .dkgPhaseBlocks = 2,
            .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
            .dkgMiningWindowEnd = 18,
            .dkgBadVotesThreshold = 8,

            .signingActiveQuorumCount = 6, // just a few ones to allow easier testing

            .keepOldConnections = 7,
            .keepOldKeys = 12,
            .recoveryMembers = 7,
    };

    static constexpr LLMQParams
    llmq40_60 = {
            .type = LLMQ_400_60,
            .name = "llmq_40_60",
            .size = 40,
            .minSize = 30,
            .threshold = 24,

            .dkgInterval = 30 * 12, // one DKG every 12 hours
            .dkgPhaseBlocks = 4,
            .dkgMiningWindowStart = 20, // dkgPhaseBlocks * 5 = after finalization
            .dkgMiningWindowEnd = 28,
            .dkgBadVotesThreshold = 30,

            .signingActiveQuorumCount = 4, // two days worth of LLMQs

            .keepOldConnections = 5,
            .keepOldKeys = 8,
            .recoveryMembers = 5,
    };

    static constexpr LLMQParams
    llmq40_85 = {
            .type = LLMQ_400_85,
            .name = "llmq_40_85",
            .size = 40,
            .minSize = 35,
            .threshold = 34,

            .dkgInterval = 30 * 24, // one DKG every 24 hours
            .dkgPhaseBlocks = 4,
            .dkgMiningWindowStart = 20, // dkgPhaseBlocks * 5 = after finalization
            .dkgMiningWindowEnd = 48, // give it a larger mining window to make sure it is mined
            .dkgBadVotesThreshold = 30,

            .signingActiveQuorumCount = 4, // four days worth of LLMQs

            .keepOldConnections = 5,
            .keepOldKeys = 8,
            .recoveryMembers = 5,
    };

    static constexpr LLMQParams
    llmq50_60 = {
            .type = LLMQ_50_60,
            .name = "llmq_50_60",
            .size = 50,
            .minSize = 40,
            .threshold = 30,

            .dkgInterval = 30, // one DKG per hour
            .dkgPhaseBlocks = 2,
            .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
            .dkgMiningWindowEnd = 18,
            .dkgBadVotesThreshold = 40,

            .signingActiveQuorumCount = 24, // a full day worth of LLMQs

            .keepOldConnections = 25,
            .keepOldKeys = 48,
            .recoveryMembers = 25,
    };

    static constexpr LLMQParams
    llmq400_60 = {
            .type = LLMQ_400_60,
            .name = "llmq_400_60",
            .size = 400,
            .minSize = 300,
            .threshold = 240,

            .dkgInterval = 30 * 12, // one DKG every 12 hours
            .dkgPhaseBlocks = 4,
            .dkgMiningWindowStart = 20, // dkgPhaseBlocks * 5 = after finalization
            .dkgMiningWindowEnd = 28,
            .dkgBadVotesThreshold = 300,

            .signingActiveQuorumCount = 4, // two days worth of LLMQs

            .keepOldConnections = 5,
            .keepOldKeys = 8,
            .recoveryMembers = 100,
    };

// Used for deployment and min-proto-version signalling, so it needs a higher threshold
    static constexpr LLMQParams
    llmq400_85 = {
            .type = LLMQ_400_85,
            .name = "llmq_400_85",
            .size = 400,
            .minSize = 350,
            .threshold = 340,

            .dkgInterval = 30 * 24, // one DKG every 24 hours
            .dkgPhaseBlocks = 4,
            .dkgMiningWindowStart = 20, // dkgPhaseBlocks * 5 = after finalization
            .dkgMiningWindowEnd = 48, // give it a larger mining window to make sure it is mined
            .dkgBadVotesThreshold = 300,

            .signingActiveQuorumCount = 4, // four days worth of LLMQs

            .keepOldConnections = 5,
            .keepOldKeys = 8,
            .recoveryMembers = 100,
    };

    static constexpr LLMQParams
    llmq200_60 = {
            .type = LLMQ_400_60,
            .name = "llmq_200_60",
            .size = 200,
            .minSize = 150,
            .threshold = 120,

            .dkgInterval = 30 * 12, // one DKG every 12 hours
            .dkgPhaseBlocks = 8,
            .dkgMiningWindowStart = 40, // dkgPhaseBlocks * 5 = after finalization
            .dkgMiningWindowEnd = 56,
            .dkgBadVotesThreshold = 150,

            .signingActiveQuorumCount = 4, // two days worth of LLMQs

            .keepOldConnections = 5,
            .keepOldKeys = 8,
            .recoveryMembers = 50,
    };


// Used for deployment and min-proto-version signalling, so it needs a higher threshold
    static constexpr LLMQParams
    llmq200_85 = {
            .type = LLMQ_400_85,
            .name = "llmq_200_85",
            .size = 200,
            .minSize = 175,
            .threshold = 170,

            .dkgInterval = 30 * 24, // one DKG every 24 hours
            .dkgPhaseBlocks = 8,
            .dkgMiningWindowStart = 40, // dkgPhaseBlocks * 5 = after finalization
            .dkgMiningWindowEnd = 56, // give it a larger mining window to make sure it is mined
            .dkgBadVotesThreshold = 150,

            .signingActiveQuorumCount = 4, // four days worth of LLMQs

            .keepOldConnections = 5,
            .keepOldKeys = 8,
            .recoveryMembers = 50,
    };

// this one is for testing only
    static constexpr LLMQParams
    llmq_test_v17 = {
            .type = LLMQ_TEST_V17,
            .name = "llmq_test_v17",
            .size = 3,
            .minSize = 2,
            .threshold = 2,

            .dkgInterval = 30, // one DKG per hour
            .dkgPhaseBlocks = 2,
            .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
            .dkgMiningWindowEnd = 18,
            .dkgBadVotesThreshold = 2,

            .signingActiveQuorumCount = 2, // just a few ones to allow easier testing

            .keepOldConnections = 3,
            .keepOldKeys = 4,
            .recoveryMembers = 3,
    };

// Used for Platform
    static Consensus::LLMQParams llmq100_67_mainnet = {
            .type = Consensus::LLMQ_100_67,
            .name = "llmq_100_67",
            .size = 100,
            .minSize = 80,
            .threshold = 67,

            .dkgInterval = 30, // one DKG per hour
            .dkgPhaseBlocks = 2,
            .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
            .dkgMiningWindowEnd = 18,
            .dkgBadVotesThreshold = 80,

            .signingActiveQuorumCount = 24, // a full day worth of LLMQs

            .keepOldConnections = 25,
            .keepOldKeys = 48,
            .recoveryMembers = 50,
    };


// Used for Platform
    static Consensus::LLMQParams llmq100_67_testnet = {
            .type = Consensus::LLMQ_100_67,
            .name = "llmq_100_67",
            .size = 100,
            .minSize = 80,
            .threshold = 67,

            .dkgInterval = 24, // one DKG per hour
            .dkgPhaseBlocks = 2,
            .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
            .dkgMiningWindowEnd = 18,
            .dkgBadVotesThreshold = 80,

            .signingActiveQuorumCount = 24, // a full day worth of LLMQs

            .keepOldConnections = 25,
            .keepOldKeys = 48,
            .recoveryMembers = 50,
    };

// Used for recording quorum voting
    class CQuorumUpdateVote
    {
    public :
        uint8_t  bit;   // Bit number (0-28)
        uint16_t votes; // Number of "yes" votes

        SERIALIZE_METHODS(CQuorumUpdateVote, obj) {
            READWRITE(obj.bit, obj.votes);
        }
        bool operator<(const CQuorumUpdateVote &rhs) const { return bit < rhs.bit; }
    };

    class CQuorumUpdateVoteVec : public std::vector<Consensus::CQuorumUpdateVote>
    {
    public :
        template<typename Stream>
        void Serialize(Stream &s) const {
            for (int i = 0; i < size(); ++i) {
               s << at(i);
            }
            // "null" termination
            s << CQuorumUpdateVote { 0, 0 };
        }

        template<typename Stream>
        void Unserialize(Stream &s) {
            CQuorumUpdateVote vote;
            do {
               s >> vote;
               if (vote.votes != 0) {
                  emplace_back(vote);
               }
            } while (vote.votes != 0);
        }

        void AddVote(uint8_t bit)
        {
            for (int i = 0; i < size(); ++i)
            {
               if (at(i).bit == bit)
               {
                  ++at(i).votes;
                  return;
               }
            }
            // Add a new update vote and sort the results (for serialization)
            push_back(CQuorumUpdateVote { bit, 1});
            std::sort(begin(), end());
        }

        void AddVotes(uint32_t version)
        {
            static constexpr uint32_t updateBits = (1 << 29) - 1;

            uint32_t bits = version & updateBits;
            uint8_t bit = 0;
            while (bits != 0)
            {
               if (bits & 1)
               {
                  AddVote(bit);
               }
               bits >>= 1;
               ++bit;
            }
        }

        bool operator ==(const CQuorumUpdateVoteVec& rhs) const {
            if (size() != rhs.size())
                return false;

            for (int i = 0; i < size(); ++i)
            {
                if (at(i).bit != rhs[i].bit)
                    return false;
                if (at(i).votes != rhs[i].votes)
                    return false;
            }
            return true;
        }
        bool operator!=(const CQuorumUpdateVoteVec& rhs) const {
            return !(*this == rhs);
        }
    };

} // namespace Consensus

#endif // BITCOIN_LLMQ_PARAMS_H
