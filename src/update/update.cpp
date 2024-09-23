// #include <versionbits.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <consensus/validation.h>
#include <logging.h>
#include <llmq/quorums_commitment.h>
#include <evo/specialtx.h>
#include <update/update.h>
#include <validation.h>
#include <evo/deterministicmns.h>

#include <iostream>
#include <iomanip>
#include <map>
#include <cmath>

// Round voting example - RoundSize = 100
// Height % RoundSize == 0..99 Blocks that vote together in round
// Cache for round stored under *following* block - e.g., Blocks 100-199 vote in round, results stored at height 200.
// Partial rounds are ignored for voting - state cannot change except at height % RoundSize == 0
// So...

// 000..099 Round 1 votes, results active and cached at height 100
// 100..199 Round 2 votes, results active and cached at height 200
// 200..299 Round 3 votes, results active and cached at height 300

const int64_t VoteResult::scaleFactor = 100 * 100; // Scaled arithmetic (value 0.1234 represented by integer 1234)

std::ostream &operator<<(std::ostream &out, EUpdateState state) {
    switch (state) {
        case EUpdateState::Defined  :
            out << "Defined";
            break;
        case EUpdateState::Voting   :
            out << "Voting";
            break;
        case EUpdateState::LockedIn :
            out << "Locked In";
            break;
        case EUpdateState::Active   :
            out << "Active";
            break;
        case EUpdateState::Failed   :
            out << "Failed";
            break;
    }
    return out;
}

std::ostream &operator<<(std::ostream &out, const Update &u) {
    return u.Print(out);
}

VoteResult operator+(const VoteResult &lhs, const VoteResult &rhs) {
    VoteResult result = lhs;
    result += rhs;
    return result;
}

std::ostream &operator<<(std::ostream &os, VoteResult voteResult) { return voteResult.Print(os); }

std::string Update::ToString() const {
    return strprintf(
            "%20s(%3d): Bit: %2d, Round Size: %4d, Start Height: %7d, Voting Period: %4d, Max Rounds: %4d, Grace Rounds: %3d, "
            "Miners: Start: %3d, Min: %3d, FalloffCoeff: %3d,  "
            "Nodes:  Start: %3d, Min: %3d, FalloffCoeff: %3d,  "
            "HeightActivated: %7d, Failed: %d\n",
            name, (int) updateId, bit, roundSize, startHeight, votingPeriod, votingMaxRounds, graceRounds,
            minerThreshold.ThresholdStart(), minerThreshold.ThresholdMin(), minerThreshold.FalloffCoeff(),
            nodeThreshold.ThresholdStart(), nodeThreshold.ThresholdMin(), nodeThreshold.FalloffCoeff(),
            heightActivated, failed
    );
}

std::string VoteResult::ToString() const {
    return strprintf("VoteResult(%6.4lf/%d,%d), Mean: %d", (double) weightedYes / scaleFactor, weight, samples,
                     MeanPercent());
}

VoteResult MinerRoundVoting::GetVote(const CBlockIndex *blockIndex, const Update &update) {
    // LogPrint(BCLog::UPDATES, "Updates: MinerRoundVoting::GetVote, Height: %7d, MinerRoundVoting\n", blockIndex->nHeight);

    // assert(height % update.RoundSize == 0);
    if (blockIndex->nHeight % update.RoundSize() != 0) {
        LogPrint(BCLog::UPDATES, "Updates: Update: %s, MinerRoundVoting::GetVote, Height: %7d, Partial rounds not accepted\n",
                 update.Name().c_str(), blockIndex->nHeight);
        return VoteResult(0, update.RoundSize()); // Assume everyone voted no
    }

    if (blockIndex->nHeight < update.StartHeight() + update.RoundSize()) {
        // LogPrint(BCLog::UPDATES, "Updates: MinerRoundVoting::GetVote, Height: %7d, MinerRoundVoting - Before start, returning 0\n", blockIndex->nHeight);
        return VoteResult(0, update.RoundSize()); // Assume everyone voted no
    }

    // Check cache first:
    if (cache.count({update.UpdateId(), blockIndex})) {
        // LogPrint(BCLog::UPDATES, "Updates: MinerRoundVoting::GetVote, returning cached result\n");
        return cache[{update.UpdateId(), blockIndex}];
    }

    int64_t yesCount = 0;
    const CBlockIndex *curIndex = blockIndex;
    for (int64_t i = update.RoundSize(); i > 0; --i) {
        curIndex = curIndex->pprev;
        if (curIndex == nullptr) {
            LogPrint(BCLog::UPDATES, "Updates: MinerRoundVoting::GetVote, BlockIndex is null, internal error\n");
            break;
        }
        // LogPrint(BCLog::UPDATES, "Updates: MinerRoundVoting::GetVote, Height: %7d, Getting sample\n", curIndex->nHeight);
        if (curIndex->nVersion & update.BitMask()) {
            // LogPrint(BCLog::UPDATES, "Updates: MinerRoundVoting::GetVote, Height: %7d, Yes\n", curIndex->nHeight);
            ++yesCount;
        }
    }
    VoteResult vote(yesCount, update.RoundSize());
    LogPrint(BCLog::UPDATES, "Updates: Update: %s, MinerRoundVoting::GetVote, Height: %7d, %s\n", update.Name().c_str(),
             blockIndex->nHeight, vote.ToString().c_str());
    cache[{update.UpdateId(), blockIndex}] = vote;
    return vote;
}

VoteResult NodeRoundVoting::GetVote(const CBlockIndex *blockIndex, const Update &update) {
    // LogPrint(BCLog::UPDATES, "Updates: NodeRoundVoting::GetVote, Height: %7d, NodeRoundVoting\n", blockIndex->nHeight);

    // assert(height % update.RoundSize == 0);
    if (blockIndex->nHeight % update.RoundSize() != 0) {
        LogPrint(BCLog::UPDATES, "Updates: Update: %s, NodeRoundVoting::GetVote, Height: %7d, Partial rounds not accepted\n",
                 update.Name().c_str(), blockIndex->nHeight);
        return VoteResult();
    }

    if (blockIndex->nHeight < update.StartHeight() + update.RoundSize()) {
        LogPrint(BCLog::UPDATES, "Updates: Update: %s, NodeRoundVoting::GetVote, Height: %7d, NodeRoundVoting - Before start\n",
                 update.Name().c_str(), blockIndex->nHeight);
        return VoteResult();
    }

    // Dump the cache:
    // LogPrint(BCLog::UPDATES, "Updates: NodeRoundVoting: Vote Cache\n");
    // for (auto it: cache)
    // {
    //    LogPrint(BCLog::UPDATES, "   Updates: Height: %d, %s\n", it.first.second->nHeight, it.second.ToString().c_str());
    // }

    // Check cache first:
    if (cache.count({update.UpdateId(), blockIndex})) {
        // LogPrint(BCLog::UPDATES, "Updates: NodeRoundVoting::GetVote, returning cached result\n");
        return cache[{update.UpdateId(), blockIndex}];
    }

    const CBlockIndex *curIndex = blockIndex;
    VoteResult vote;
    for (int64_t i = update.RoundSize(); i > 0; --i) {
        curIndex = curIndex->pprev;
        if (curIndex == nullptr) {
            LogPrint(BCLog::UPDATES, "Updates: NodeRoundVoting::GetVote, BlockIndex is null, internal error\n");
            break;
        }

        // Search all transactions for final quorum commitments:
        CBlock block;
        bool r = ReadBlockFromDisk(block, curIndex, Params().GetConsensus());
        assert(r);

        // LogPrint(BCLog::UPDATES, "Updates: NodeRoundVoting, Height: %d, Transactions: %d\n", curIndex->nHeight, block.vtx.size());

        for (size_t i = 1; i < block.vtx.size(); i++) {
            auto &tx = block.vtx[i];

            if (tx->nType == TRANSACTION_QUORUM_COMMITMENT) {
                llmq::CFinalCommitmentTxPayload qc;
                if (!GetTxPayload(*tx, qc)) {
                    LogPrint(BCLog::UPDATES,
                             "Updates: NodeRoundVoting, Height: %d, Tx: %d, Quorum Commitment - GetTxPayload failed\n",
                             curIndex->nHeight, i);
                }
                if (qc.commitment.IsNull()) {
                    continue;
                }

                // LogPrint(BCLog::UPDATES, "Updates: NodeRoundVoting, Height: %d, Tx: %d, Quorum Commitment - ValidMembers: %3d, Signers: %3d\n", curIndex->nHeight, i, qc.commitment.CountValidMembers(), qc.commitment.CountSigners());
                if (qc.commitment.CountValidMembers()) {
                    int64_t samples = qc.commitment.CountValidMembers();
                    for (const auto it: qc.commitment.quorumUpdateVotes) {
                        if (update.Bit() == it.bit) {
                            LogPrint(BCLog::UPDATES,
                                     "Updates: NodeRoundVoting, Height: %d, Tx: %d, Quorum Commitment - ValidMembers: %3d, Signers: %3d, yes: %3d\n",
                                     curIndex->nHeight, i, qc.commitment.CountValidMembers(),
                                     qc.commitment.CountSigners(), it.votes);
                            vote += VoteResult(it.votes, samples);
                        }
                    }
                }

                // LogPrint(BCLog::UPDATES, "Updates: NodeRoundVoting, Height: %d, Tx: %d, Quorum Commitment\n", curIndex->nHeight, i);
            }
        }
    }
    LogPrint(BCLog::UPDATES, "Updates: Update: %s, NodeRoundVoting::GetVote, Height: %7d, %s\n", update.Name().c_str(),
             blockIndex->nHeight, vote.ToString().c_str());
    cache[{update.UpdateId(), blockIndex}] = vote;
    return vote;
}

VoteResult MinerUpdateVoting::GetVote(const CBlockIndex *blockIndex, const Update &update) {
    // assert(height % update.roundSize == 0);
    if (blockIndex->nHeight < update.StartHeight() + update.RoundSize() * update.VotingPeriod()) {
        return VoteResult(0, update.RoundSize() * update.VotingPeriod()); // Assume everyone voted no for the entire period
    }

    // Simply collect all of the round votes for a full average in the voting period:
    VoteResult vote;
    for (int lookback = 0; lookback < update.VotingPeriod(); ++lookback) {
        const CBlockIndex *roundBlockIndex = blockIndex->GetAncestor(
                blockIndex->nHeight - lookback * update.RoundSize());
        if (roundBlockIndex == nullptr)
            break; // Nothing more
        VoteResult roundVote = minerRoundVoting->GetVote(roundBlockIndex, update);
        // LogPrint(BCLog::UPDATES, "Updates: MinerUpdateVoting::GetVote - retrieved vote for height %d: %s\n", roundBlockIndex->nHeight, roundVote.ToString());
        vote += roundVote;
    }
    LogPrint(BCLog::UPDATES, "Updates: Update: %s, MinerUpdateVoting::GetVote, Height: %7d, %s\n", update.Name().c_str(),
             blockIndex->nHeight, vote.ToString().c_str());
    return vote;
}

VoteResult NodeUpdateVoting::GetVote(const CBlockIndex *blockIndex, const Update &update) {
    // assert(height % update.roundSize == 0);
    if (blockIndex->nHeight < update.StartHeight() + update.RoundSize() * update.VotingPeriod()) {
        return VoteResult(0, update.RoundSize() * update.VotingPeriod()); // Assume everyone voted no for the entire period
    }

    // Simply collect all of the round votes for a full average in the voting period:
    VoteResult vote;
    for (int lookback = 0; lookback < update.VotingPeriod(); ++lookback) {
        const CBlockIndex *roundBlockIndex = blockIndex->GetAncestor(
                blockIndex->nHeight - lookback * update.RoundSize());
        if (roundBlockIndex == nullptr)
            break; // Nothing more
        VoteResult roundVote = nodeRoundVoting->GetVote(roundBlockIndex, update);
        // LogPrint(BCLog::UPDATES, "Updates: NodeUpdateVoting::GetVote - retrieved vote for height %d: %s\n", roundBlockIndex->nHeight, roundVote.ToString());
        vote += roundVote;
    }
    LogPrint(BCLog::UPDATES, "Updates: Update: %s, NodeUpdateVoting::GetVote, Height: %7d, %s\n", update.Name().c_str(),
             blockIndex->nHeight, vote.ToString().c_str());
    return vote;
}


UpdateManager::UpdateManager() :
        minerRoundVoting(),
        nodeRoundVoting(),
        minerUpdateVoting(&minerRoundVoting),
        nodeUpdateVoting(&nodeRoundVoting) {
}

UpdateManager::~UpdateManager() {};

bool UpdateManager::Add(Update update) {
    // Check for existence first and erase if exist
    auto it = updates.find(update.UpdateId());
    if (it != updates.end())
        updates.erase(it);
    updates.emplace(update.UpdateId(), update);
    // LogPrint(BCLog::UPDATES, "Updates: UpdateManager Added: %s\n", update.ToString());
    LogPrintf("Updates: UpdateManager Added: %s\n", update.ToString());
    return true;
}

const Update *UpdateManager::GetUpdate(enum EUpdate eUpdate) const {
    auto it = updates.find(eUpdate);
    if (it != updates.end())
        return &(it->second);
    else
        return nullptr;
}

bool UpdateManager::IsActive(enum EUpdate eUpdate, const CBlockIndex *blockIndex) {
    return State(eUpdate, blockIndex).State == EUpdateState::Active;
}

bool UpdateManager::IsAssetsActive(const CBlockIndex *blockIndex) {
    return IsActive(EUpdate::ROUND_VOTING, blockIndex);
}

StateInfo UpdateManager::State(enum EUpdate eUpdate, const CBlockIndex *blockIndex) {
    const Update *update = GetUpdate(eUpdate);
    VoteStats voteStats = {VoteResult(), VoteResult(), 0, 0, false, false};
    if (!update || blockIndex == nullptr) {
        return {EUpdateState::Unknown, -1, voteStats};
    }

    // Check for forced updates (bypass voting logic and associated overhead for old updates):
    if (update->HeightActivated() >= 0) {
        if (blockIndex->nHeight >= update->HeightActivated()) {
            return {update->Failed() ? EUpdateState::Failed : EUpdateState::Active, update->HeightActivated(),
                    voteStats};
        } else {
            return {EUpdateState::Defined, -1, voteStats};
        }
    }

    LOCK2(cs_main, updateMutex);

    // Dump the final state cache:
    // LogPrint(BCLog::UPDATES, "Updates: FinalState cache\n");
    // for (auto const& finalState : finalStates)
    // {
    //    LogPrint(BCLog::UPDATES, "   Update: %s, FinalState: %d, FinalHeight: %7d\n", GetUpdate(finalState.first)->Name().c_str(), finalState.second.State, finalState.second.FinalHeight);
    // }

    // // Dump the state cache:
    // LogPrint(BCLog::UPDATES, "Updates: State cache\n");
    // for (auto const& state : states)
    // {
    //    UpdateCacheKey key = state.first;
    //    StateInfo      stateInfo = state.second;
    //    // LogPrint(BCLog::UPDATES, "   Updates: key.first: %d, key.second: %p\n", (int)key.first, key.second);
    //    // LogPrint(BCLog::UPDATES, "   Updates: stateInfo.first: %d, stateInfo.second: %d\n", (int)stateInfo.State, stateInfo.FinalHeight);
    //    const Update* update = GetUpdate(key.first);
    //    LogPrint(BCLog::UPDATES, "   Updates: Update: %s, Height: %6d, State: %9d, FinalHeight: %6d, BlockIndex: %p\n", GetUpdate(key.first)->Name().c_str(), key.second->nHeight, stateInfo.State, stateInfo.FinalHeight, key.second);
    // }

    // See if this proposal is in final state for quick return:
    if (finalStates.count(eUpdate)) {
        StateInfo stateInfo = finalStates[eUpdate];
        if (blockIndex->nHeight >= stateInfo.FinalHeight) {
            // LogPrint(BCLog::UPDATES, "Updates: Update: %s, State: %d, FinalHeight: %7d - fast return\n", update->Name().c_str(), stateInfo.State, stateInfo.FinalHeight);
            return stateInfo;
        }
    }

    int64_t roundSize = update->RoundSize();
    int64_t startHeight = update->StartHeight();
    int64_t lastVotingHeight = update->StartHeight() + roundSize * (update->VotingMaxRounds() - 1);
    int64_t votingPeriod = update->VotingPeriod();
    int64_t roundNumber = (blockIndex->nHeight - startHeight) / roundSize;

    // If we are not within one round of starting, just return defined:
    if (roundNumber < -1) {
        return {EUpdateState::Defined, -1, voteStats};
    }

    LogPrint(BCLog::UPDATES,
             "Updates: Update: %s, roundSize: %d, startHeight: %7d, lastVotingHeight: %d, votingPeriod: %d, roundNumber: %d\n",
             update->Name().c_str(), roundSize, startHeight, lastVotingHeight, votingPeriod, roundNumber);

    // The state of each round is stored at the height of the first block AFTER the round (height % roundSize == 0)
    LogPrint(BCLog::UPDATES, "Updates: Update: %s, State: blockIndex: %p, Height: %7d\n", update->Name().c_str(),
             (void *) blockIndex, blockIndex->nHeight);
    if (blockIndex != nullptr) {
        blockIndex = blockIndex->GetAncestor(blockIndex->nHeight - blockIndex->nHeight % roundSize);
    }
    LogPrint(BCLog::UPDATES, "Updates: Update: %s, State: blockIndex: %p, Height: %7d after normalization\n",
             update->Name().c_str(), (void *) blockIndex, blockIndex->nHeight);

    // Walk backwards in steps of roundSize to find a BlockIndex whose information is known
    const CBlockIndex *pIndexPrev = blockIndex;
    std::vector<const CBlockIndex *> vToCompute;
    while (states.count({eUpdate, pIndexPrev}) == 0) {
        // LogPrint(BCLog::UPDATES, "Updates: State: pIndexPrev: %p, Height: %7d, count: %d\n", (void*)pIndexPrev, pIndexPrev->nHeight, states.count({eUpdate, pIndexPrev}));

        if (pIndexPrev == nullptr || pIndexPrev->nHeight == 0) {
            states[{eUpdate, pIndexPrev}] = {EUpdateState::Defined, -1,
                                             voteStats}; // The genesis block is by definition defined.
            LogPrint(BCLog::UPDATES, "Updates: Genesis block, state: Defined\n");
            break;
        }
        if (pIndexPrev->nHeight < startHeight) {
            states[{eUpdate, pIndexPrev}] = {EUpdateState::Defined, -1,
                                             voteStats}; // One round before the starting height
            LogPrint(BCLog::UPDATES, "Updates: Height: %7d, Before startHeight, state: Defined\n", pIndexPrev->nHeight);
            break;
        }

        vToCompute.push_back(pIndexPrev);
        pIndexPrev = pIndexPrev->GetAncestor(pIndexPrev->nHeight - roundSize);
    }
    LogPrint(BCLog::UPDATES, "Updates: Lookback complete.  Rounds to compute: %d\n", vToCompute.size());

    // At this point, states[{eUpdate, pIndexPrev}] is known
    assert(states.count({eUpdate, pIndexPrev}));
    StateInfo stateInfo = states[{eUpdate, pIndexPrev}];

    // Now walk forward and compute the state of descendants of pindexPrev
    while (!vToCompute.empty()) {
        StateInfo stateNext = stateInfo;
        pIndexPrev = vToCompute.back();
        vToCompute.pop_back();

        // LogPrint(BCLog::UPDATES, "Updates: Update: %s, Height: %7d, Compute - starting state: %d\n", update->Name().c_str(), pIndexPrev->nHeight, stateInfo.State);

        switch (stateInfo.State) {
            case EUpdateState::Defined: {
                if (pIndexPrev->nHeight > lastVotingHeight) {
                    stateNext.State = EUpdateState::Failed;
                    stateNext.FinalHeight = pIndexPrev->nHeight;
                    stateNext.voteStats = stateInfo.voteStats;
                    // Save the final state:
                    finalStates[eUpdate] = stateNext;
                    // No longer need to continue checking rounds:
                    vToCompute.clear();
                    break;
                }

                if (pIndexPrev->nHeight >= startHeight) {
                    stateNext.State = EUpdateState::Voting;
                    stateNext.voteStats = stateInfo.voteStats;
                    stateNext.FinalHeight = -1;
                }
                // Fall through to collect the vote for the first round
            }

            case EUpdateState::Voting: {
                if (pIndexPrev->nHeight > lastVotingHeight) {
                    if (update->ForcedUpdate()) {
                        stateNext = {EUpdateState::LockedIn, pIndexPrev->nHeight + roundSize * update->GraceRounds(),
                                     voteStats};
                        LogPrint(BCLog::UPDATES,
                                 "Updates: Update: %s, Voting: Height: %7d, Voting failed, forcing update, Activation at height: %7d\n",
                                 update->Name().c_str(), pIndexPrev->nHeight, stateNext.FinalHeight);
                    } else {
                        stateNext = {EUpdateState::Failed, pIndexPrev->nHeight, stateInfo.voteStats};
                    }
                    break;
                }

                // Check Miner votes:
                {
                    VoteResult minerUpdateResult = minerUpdateVoting.GetVote(pIndexPrev, *update);
                    voteStats.minerUpdateResult = minerUpdateResult;
                    // double  confidenceLow = minerUpdateResult.ComputeConfidenceIntervalLow() * 100.0;
                    int64_t minerMean = minerUpdateResult.MeanPercent();
                    voteStats.currentMinerThreshold = update->MinerThreshold().GetThreshold(roundNumber);
                    if (minerMean >= voteStats.currentMinerThreshold) {
                        LogPrint(BCLog::UPDATES,
                                 "Updates: Update: %s, Voting: Height: %7d, Miners %s, Threshold: %3d - Miners approve\n",
                                 update->Name().c_str(), pIndexPrev->nHeight, minerUpdateResult.ToString().c_str(),
                                 voteStats.currentMinerThreshold);
                        voteStats.minersApproved = true;
                    } else {
                        LogPrint(BCLog::UPDATES,
                                 "Updates: Update: %s, Voting: Height: %7d, Miners %s, Threshold: %3d\n",
                                 update->Name().c_str(), pIndexPrev->nHeight, minerUpdateResult.ToString().c_str(),
                                 voteStats.currentMinerThreshold);
                    }
                }

                // Check Node votes:
                {
                    VoteResult nodeUpdateResult = nodeUpdateVoting.GetVote(pIndexPrev, *update);
                    voteStats.nodeUpdateResult = nodeUpdateResult;
                    int64_t nodeMean = nodeUpdateResult.MeanPercent();
                    voteStats.currentNodeThreshold = update->NodeThreshold().GetThreshold(roundNumber);
                    if (nodeMean >= voteStats.currentNodeThreshold) {
                        LogPrint(BCLog::UPDATES,
                                 "Updates: Update: %s, Voting: Height: %7d, Nodes  %s, Threshold: %3d - Nodes  approve\n",
                                 update->Name().c_str(), pIndexPrev->nHeight, nodeUpdateResult.ToString().c_str(),
                                 voteStats.currentNodeThreshold);
                        voteStats.nodesApproved = true;
                    } else {
                        LogPrint(BCLog::UPDATES,
                                 "Updates: Update: %s, Voting: Height: %7d, Nodes  %s, Threshold: %3d\n",
                                 update->Name().c_str(), pIndexPrev->nHeight, nodeUpdateResult.ToString().c_str(),
                                 voteStats.currentNodeThreshold);
                    }
                }

                if (voteStats.minersApproved && voteStats.nodesApproved) {
                    stateNext = {EUpdateState::LockedIn, pIndexPrev->nHeight + roundSize * update->GraceRounds(),
                                 voteStats};
                    LogPrint(BCLog::UPDATES,
                             "Updates: Update: %s, Voting: Height: %7d, Proposal has been locked in, Activation at height: %7d\n",
                             update->Name().c_str(), pIndexPrev->nHeight, stateNext.FinalHeight);
                }
                break;
            }

            case EUpdateState::LockedIn: {
                // Wait for the grace period:
                if (pIndexPrev->nHeight >= stateNext.FinalHeight) {
                    stateNext.State = EUpdateState::Active;
                    stateNext.voteStats = stateInfo.voteStats;
                    // Save the final state:
                    finalStates[eUpdate] = stateNext;
                    // No longer need to continue checking rounds:
                    vToCompute.clear();
                }
                break;
            }

            case EUpdateState::Failed:
            case EUpdateState::Active: {
                // Nothing happens, these are terminal states.
                break;
            }
        }
        // LogPrint(BCLog::UPDATES, "Updates: Update: %s, Caching %d for Height: %7d\n", update->Name().c_str(), stateNext.State, pIndexPrev->nHeight);
        states[{eUpdate, pIndexPrev}] = stateInfo = stateNext;
    }
    return stateInfo;
}

uint32_t UpdateManager::ComputeBlockVersion(const CBlockIndex *blockIndex) {
    LOCK2(cs_main, updateMutex);
    uint32_t nVersion = VERSIONBITS_TOP_BITS;
    for (auto const &update: updates) {
        StateInfo si = State(update.first, blockIndex);
        if (si.State == EUpdateState::Voting || si.State == EUpdateState::LockedIn)
            nVersion |= update.second.BitMask();
    }
    return nVersion;
}
