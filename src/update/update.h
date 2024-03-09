#ifndef RAPTOREUM_UPDATE_HPP
#define RAPTOREUM_UPDATE_HPP


#include <assert.h>
#include <iostream>
#include <iomanip>
#include <map>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <mutex>
#include <chain.h>
#include <sync.h>

enum class EUpdate
{
   DEPLOYMENT_V17 = 0,
   ROUND_VOTING   = 1,

   MAX_VERSION_BITS_DEPLOYMENTS
};

enum class EUpdateState
{
   Unknown = 0, // Update does not exist or state is unknown
   Defined,     // Proposed update is defined, but voting has not begun.
   Voting,      // Voting on proposed update is active.
   LockedIn,    // Vote was successful, we are in the grace period for remaining miners/nodes to update.
   Active,      // Update has been enacted - it is active and logic should be in place.
   Failed       // Update has failed and will not be enacted.
};

/** What block version to use for new blocks (pre versionbits) */
static const int32_t VERSIONBITS_LAST_OLD_BLOCK_VERSION = 4;
/** What bits to set in version for versionbits blocks */
static const int32_t VERSIONBITS_TOP_BITS = 0x20000000UL;
/** What bitmask determines whether versionbits is in use */
static const int32_t VERSIONBITS_TOP_MASK = 0xE0000000UL;
/** Total bits available for versionbits */
static const int32_t VERSIONBITS_NUM_BITS = 29;


std::ostream& operator<<(std::ostream& out, EUpdateState state);

typedef std::pair<EUpdate, const CBlockIndex*> UpdateCacheKey;

class VoteThreshold
{
   public :
      VoteThreshold(int64_t thresholdStart, int64_t thresholdMin, int64_t falloffCoeff) : thresholdStart(thresholdStart), thresholdMin(thresholdMin), falloffCoeff(falloffCoeff)
      {
         if
         (
            (thresholdMin < 0)              ||
            (thresholdStart < thresholdMin) ||
            (thresholdStart > 100)          ||
            (falloffCoeff < 1)
         )
         {
            throw std::invalid_argument("Invalid VoteThreshold arguments");
         }
      }

      int64_t ThresholdStart() const { return thresholdStart; }
      int64_t ThresholdMin() const   { return thresholdMin;   }
      int64_t FalloffCoeff() const   { return falloffCoeff;   }

      int64_t GetThreshold(int64_t roundNumber) const
      {
         return std::max(thresholdStart - roundNumber * roundNumber / falloffCoeff, thresholdMin);
      }

   private :
      int64_t       thresholdStart;      // Initial threshold at start of voting
      int64_t       thresholdMin;        // Minimum allowed threshold
      int64_t       falloffCoeff;        // Reduces ThresholdStart each round
};

class Update
{
   public :
      Update(EUpdate updateId, std::string name, int bit, int64_t roundSize, int64_t startHeight, int64_t votingPeriod, int64_t votingMaxRounds, int64_t graceRounds, bool forcedUpdate,
               VoteThreshold minerThreshold, VoteThreshold nodeThreshold, bool failed = false, int64_t heightActivated = -1) :
         updateId(updateId),
         name(name),
         bit(bit),
         roundSize(roundSize),
         startHeight(startHeight),
         votingPeriod(votingPeriod),
         votingMaxRounds(votingMaxRounds),
         graceRounds(graceRounds),
         forcedUpdate(forcedUpdate),
         minerThreshold(minerThreshold),
         nodeThreshold(nodeThreshold),
         failed(failed),
         heightActivated(heightActivated)
   {
         // Validate input:
         if
         (
            (bit < 0 || bit > 28)                                         ||
            (roundSize < 1)                                               ||
            (startHeight < 0)                                             ||
            (votingPeriod < 1)                                            ||
            (votingMaxRounds < 1)                                         ||
            (graceRounds < 0)                                             ||
            (heightActivated < -1)                                        ||
            (startHeight % roundSize != 0)                                ||
            (votingPeriod > votingMaxRounds)
         )
         {
            throw std::invalid_argument("Invalid argument(s) for Update " + name);
         }
      }

      EUpdate              UpdateId() const        { return updateId;             }
      const std::string&   Name() const            { return name;                 }
      int                  Bit() const             { return bit;                  }
      int64_t              RoundSize() const       { return roundSize;            }
      int64_t              StartHeight() const     { return startHeight;          }
      int64_t              VotingPeriod() const    { return votingPeriod;         }
      int64_t              VotingMaxRounds() const { return votingMaxRounds;      }
      int64_t              GraceRounds() const     { return graceRounds;          }
      bool                 ForcedUpdate() const    { return forcedUpdate;         }
      const VoteThreshold& MinerThreshold() const  { return minerThreshold;       }
      const VoteThreshold& NodeThreshold() const   { return nodeThreshold;        }
      bool                 Failed() const          { return failed;               }
      int64_t              HeightActivated() const { return heightActivated;      }
      uint32_t             BitMask() const         { return ((uint32_t)1) << bit; }

      std::string          ToString() const;

      std::ostream&        Print(std::ostream& os) const
      {
         os << ToString();
         return os;
      }

   private :
      // Core information:
      EUpdate       updateId;            // Enum of proposed update
      std::string   name;                // Name of proposed update
      int           bit;                 // Bit indicating the vote in version field
      int64_t       roundSize;           // Blocks forming a single round (each round starts at height % roundSize == 0)
      int64_t       startHeight;         // When Voting starts (must be a round starting height)
      int64_t       votingPeriod;        // Number of rounds required for voting threshold check
      int64_t       votingMaxRounds;     // Proposed update expires after startHeight + roundSize * votingMaxRounds
      int64_t       graceRounds;         // After successful vote, state is locked in for this many rounds before going active.
      bool          forcedUpdate;        // If the threshold is not reached at expiration, lock it in after the grace period anyway.

      // Thresholds
      VoteThreshold minerThreshold;
      VoteThreshold nodeThreshold;

      // Flags for activation height to bypass vote checking (old votes)
      bool          failed;              // True if the proposed update failed and should be ignored
      int64_t       heightActivated;     // -1 if proposed update should be evaluated.  Set to height when activated to bypass evaluation (old votes, for performance).
};

std::ostream& operator<<(std::ostream& out, const Update& u);


// (Height, state)
typedef std::map<int64_t, EUpdateState> EUpdateStateCache;

// RoundVoteCache...

class VoteResult
{
   public :
      VoteResult(int64_t yes = 0, int64_t sampleSize = 0) : weightedYes(0), weight(0), samples(0)
      {
         if ((yes < 0) || (sampleSize < 0) || (yes > sampleSize))
         {
            throw std::invalid_argument("Invalid arguments to VoteResult constructor.");
         }

         if (sampleSize > 0)
         {
            int64_t percent = scaleFactor * yes / sampleSize;
            weightedYes = percent * sampleSize;
            weight      = sampleSize;
            samples     = 1;
         }
      }

      int64_t MeanPercent() const
      {
         return (weight == 0) ? 0 : ((weightedYes / weight) + 50) / 100; // Round off to the nearest percent
      }

      VoteResult& operator+=(const VoteResult& rhs)
      {
         weightedYes += rhs.weightedYes;
         weight      += rhs.weight;
         samples     += rhs.samples;
         return *this;
      }

      std::string   ToString() const;
      std::ostream& Print(std::ostream& os) const
      {
         os << ToString();
         return os;
      }

   private :
      static const int64_t scaleFactor;

      int64_t weightedYes;
      int64_t weight;
      int64_t samples;
};

VoteResult operator+(const VoteResult& lhs, const VoteResult &rhs);

std::ostream& operator<<(std::ostream &os, VoteResult voteResult);

class IRoundVoting
{
   public :
      virtual ~IRoundVoting() {};
      /// @brief Return voting results for proposed update at the round ending one block before blockIndex.
      /// @details A round vote is only returned for completed rounds (all blocks available in the round).
      ///          The only valid heights are where blockIndex->nHeight % update.roundSize == 0.
      /// @param blockIndex First block after the round (blockIndex->nHeight % update.roundSize == 0).
      ///                   Partial rounds are never calculated.
      /// @param update Proposed update being evaluated.
      /// @return Vote results for the full round ending before immediately blockIndex.  If the round is before the
      ///         proposed update is active, an empty vote result is returned (0% voted yes).
      virtual VoteResult GetVote(const CBlockIndex* blockIndex, const Update& update) = 0;
};

class IUpdateVoting
{
   public :
      virtual ~IUpdateVoting() {};

      /// @brief Return voting results for proposed update for all rounds in the votingPeriod ending immediately before blockIndex.
      /// @details A proposed update vote is a combination of all rounds in the votingPeriod that ends immediately before blockIndex.
      ///          Rounds before the start of the proposed update are treated as a 0% vote and are included in the results.
      /// @param blockIndex First block following the last round for update check (blockIndex->nHeight % update.roundSize == 0).
      /// @param update Update being evaluated.
      /// @return Cumulative voting results for the last votingPeriod rounds.
      virtual VoteResult GetVote(const CBlockIndex* blockIndex, const Update& update) = 0;
};

class MinerRoundVoting : public IRoundVoting
{
   public :
      MinerRoundVoting() {};
      virtual ~MinerRoundVoting() {};

      VoteResult GetVote(const CBlockIndex* blockIndex, const Update& update) override;

   private :
      std::map<UpdateCacheKey, VoteResult> cache;
};

class NodeRoundVoting : public IRoundVoting
{
   public :
      NodeRoundVoting() {};
      virtual ~NodeRoundVoting() {};

      VoteResult GetVote(const CBlockIndex* blockIndex, const Update& update) override;

   private :
      std::map<UpdateCacheKey, VoteResult> cache;
};

class MinerUpdateVoting : public IUpdateVoting
{
   public :
      MinerUpdateVoting(MinerRoundVoting* minerRoundVoting) : minerRoundVoting(minerRoundVoting) {};
      virtual ~MinerUpdateVoting() {};

      VoteResult GetVote(const CBlockIndex* blockIndex, const Update &update) override;

    protected:
      MinerRoundVoting* minerRoundVoting;
};

class NodeUpdateVoting : public IUpdateVoting
{
   public :
      NodeUpdateVoting(NodeRoundVoting* nodeRoundVoting) : nodeRoundVoting(nodeRoundVoting) {};
      virtual ~NodeUpdateVoting() {};

      VoteResult GetVote(const CBlockIndex* blockIndex, const Update &update) override;

    protected:
      NodeRoundVoting* nodeRoundVoting;
};

typedef struct StateInfo
{
   EUpdateState State;
   int64_t      FinalHeight;    // Active height when state in (LockedIn, Active), Failed height when state == Failed
} StateInfo;

class UpdateManager
{
   private :
      // Singleton
      static UpdateManager* _instance;
      static std::once_flag _instance_flag;

   public:
      static UpdateManager& Instance()
      {
         std::call_once(UpdateManager::_instance_flag, []() { UpdateManager::_instance = new UpdateManager(); });
         return *UpdateManager::_instance;
      }

      UpdateManager();
      virtual ~UpdateManager();

      bool Add(Update update);

      const Update* GetUpdate(enum EUpdate eUpdate) const;
      bool IsActive(enum EUpdate eUpdate, const CBlockIndex* blockIndex);

      StateInfo State(enum EUpdate eUpdate, const CBlockIndex* blockIndex);

      uint32_t ComputeBlockVersion(const CBlockIndex* blockIndex);

   private :
      typedef std::unordered_map<EUpdate, Update>     UpdateMap;
      typedef std::unordered_map<EUpdate, StateInfo>  FinalStateMap; // Caches final states and heights of all completed proposals

      typedef std::unordered_map<const CBlockIndex*, EUpdateState> StateMap;

      RecursiveMutex                      updateMutex;
      MinerRoundVoting                    minerRoundVoting;
      NodeRoundVoting                     nodeRoundVoting;
      MinerUpdateVoting                   minerUpdateVoting;
      NodeUpdateVoting                    nodeUpdateVoting;
      FinalStateMap                       finalStates;
      UpdateMap                           updates;             // Update parameters (does not contain states)
      std::map<UpdateCacheKey, StateInfo> states;
};

#endif // RAPTOREUM_UPDATE_HPP
