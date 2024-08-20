<pre>
  Raptoreum Improvement Proposal
  RIP: 01
  Title: Round Voting
  Author: Jami Bradley <codeiskey@protonmail.com>
  Status: Draft
  Type: Informational
  Created: 2024-03-14
  License: BSD-2-Clause
</pre>

# Abstract

This document specifies an updated voting mechanism, starting with features of Bitcoin's BIP 8 and BIP 9 mechanisms and Dash's dynamic activation thresholds and improving on those.  This process provides the capability for miner and node forking (either independently or coordinated) and make the voting process more responsive than previous algorithms.

Basic features:
- The rules for each update are independent and can be configured as needed for the situation.
- Activation based on block height, not time.
- Parameters for each update determine when a vote starts and ends, number of blocks in voting period, thresholds for acceptance, and grace period.
- An update may be forced at a specified height if the threshold is not reached within some period.
- An update may be scheduled at a specific height with no voting required.
- Voting periods are split into rounds, providing a more responsive threshold check using a sliding period.

The key words "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT", "SHOULD", "SHOULD NOT", "RECOMMENDED", "MAY", and "OPTIONAL" in this document are to be interpreted as described in RFC 2119.

# Motivation

BIP8 and BIP9 provided a forking mechanism using the nVersion field and a fixed number of blocks to produce a vote.
The number of blocks needs to be large to provide a high confidence to the proportion of upgraded miners and nodes.  This
leads to delays in an update even when the threshold may have been reached.

Smartnodes and miners may need to be updated independently and may require different thresholds to trigger an update.
For example, a quorum consensus change can be done without affecting the miners.

# Specification

## Parameters

Each fork deployment is specified by the following per-chain parameters (further elaborated below):

| Parameter         | Description                                                                              |
| ----------------- | ---------------------------------------------------------------------------------------- |
| name              | Short name of proposed update                                                            |
| bit               | Bit indicating the vote in version field                                                 |
| roundSize         | Blocks forming a single round (each round starts at height % roundSize == 0)             |
| startHeight       | When Voting starts (must be a round starting height)                                     |
| votingPeriod      | Number of rounds required for voting threshold check                                     |
| votingMaxRounds   | Proposed update expires after startHeight + roundSize * votingMaxRounds                  |
| graceRounds       | After successful vote, state is locked in for this many rounds before going active.      |
| forcedUpdate      | If the threshold is not reached at expiration, lock it in after the grace period anyway. |
| minerThreshold    | Threshold triplet for miners (thresholdStart, thresholdMin, falloffCoeff)                |
| nodeThreshold     | Threshold triplet for Smartnodes (thresholdStart, thresholdMin, falloffCoeff)            |


1. `name` specifies a very brief description of the fork, reasonable for use as an identifier.
2. `bit` determines which bit in the nVersion field of the block is to be used to signal the fork lock-in and activation. It is chosen from the set {0,1,2,...,28}.
3. `roundSize` determines the number blocks in a single round.  Each round starts at height % roundSize == 0.
4. `startHeight` specifies the height of the first block at which the bit gains its meaning. startHeight % roundSize must be zero.
5. `votingPeriod` specifies the number of rounds that comprise a single vote.
6. `votingMaxRounds` Determines timeout for update voting.  Proposed update expires after startHeight + roundSize * votingMaxRounds.
7. `graceRounds` is used after a successful vote.  The state is locked in for this many rounds before activation.
8. `forcedUpdate` is used to lock in an update at expiration if the threshold is not reached beforehand.
9. `minerThreshold` is the threshold definition for miners.
10. `nodeThreshold` is the threshold definition for nodes.

**Threshold**: A threshold (miner and node) is composed of three fields:

1. `thresholdStart` is the initial requirement for a passing vote.  This is an integer [0..99] indicating the minimum percentage required.
2. `thresholdMin` is the minimum requirement for a passing vote.  This is an integer [0..99] indicating the minimum percentage required.  This is used to keep the threshold above a minimum when computing the falloff.
3. `falloffCoeff` is the multiplier for computing a falloff to lower the threshold during later rounds.

Using these three fields, the threshold for the current round is as follows:

`threshold = max(thresholdStart - roundNumber * roundNumber / falloffCoeff, thresholdMin)`

Where roundNumber == 0 at the round starting at `startHeight`.

## Selection guidelines

The following guidelines are suggested for selecting these parameters for a fork:

1. `name` should be selected such that no two forks, concurrent or otherwise, ever use the same name. For deployments described in a single RIP, it is recommended to use the name "ripN" where N is the appropriate RIP number.
2. `bit` should be selected such that no two concurrent forks use the same bit. The bit chosen should not overlap with active usage (legitimately or otherwise) for other purposes.
3. `roundSize` should be kept balanced - larger sizes improves cache performance but can delay the update waiting for large rounds.  If Smartnodes are voting for the update, the round size _must_ be large enough to contain at least a few quorums.  If not, the node vote for the round will be zero, likely delaying the update.
4. `startHeight` should be set to some block height in the future.
5. `votingPeriod`, combined with roundSize, should provide enough blocks for strong confidence that the update will go smoothly when activated.
6. `votingMaxRounds` should be chosen to provide sufficient time for upgrades.  If there is an urgent update and it must be forced, this can be lowered to activate it sooner, understanding the consequences of possibly not having a solid consensus.
7. `graceRounds` should be chosen to provide reasonable time for any remaining updates to be performed.
8. `forcedUpdate` should be used sparingly in situations where the update is necessary for the long term health of the chain.
9. `minerThreshold` and `nodeThreshold` should be set based on the needs of the two groups (miners and Smartnodes).

   a. `thresholdStart` vote threshold for the first round (the tested threshold may lower over time).  For updates that
      only involve one group of nodes (e.g., Smartnodes only), this can be set to zero for the other group.

   b. `thresholdMin` allows the threshold to reduce over time.  This may be useful for important updates where the update
      may lock in early if there is very high adoption but still activate if there is good adoption over a longer term.

   c. `fallofCoeff` determines how quickly we reduce the threshold from the start to the min.  Depending on roundSize,
      the time for the falloff can be estimated.

## States

With each block and update, we associate a deployment state. The possible states are:
| State             | Meaning                                                                                  |
| ----------------- | ---------------------------------------------------------------------------------------- |
| `Defined`         | Proposed update is defined, but voting has not begun.                                    |
| `Voting`          | Voting on proposed update is active.                                                     |
| `LockedIn`        | Vote was successful, we are in the grace period for remaining miners/nodes to update.    |
| `Active`          | Update has been enacted - it is active and logic should be in place.                     |
| `Failed`          | Update has failed and will not be enacted.                                               |

## New consensus rules

The new consensus rules for each fork are enforced for each block that has Active state.

## Round Votes

Within a single round, votes are collected in different ways for miners and Smartnodes.

#### Miner Vote Representation Within Blocks

The nVersion block header field is to be interpreted as a 32-bit little-endian integer (as present), and bits are selected within this integer as values (1 << N) where N is the bit number.

Blocks in the Voting state get an nVersion whose bit position bit is set to 1. The top 3 bits of such blocks must be
001, so the range of actually possible nVersion values is [0x20000000...0x3FFFFFFF], inclusive.

Due to the constraints set by BIP 34, BIP 66 and BIP 65, we only have 0x7FFFFFFB possible nVersion values available.
This restricts us to at most 30 independent deployments. By restricting the top 3 bits to 001 we get 29 out of those
for the purposes of this proposal, and support two future upgrades for different mechanisms (top bits 010 and 011).
When a block nVersion does not have top bits 001, it is treated as if all
bits are 0 for the purposes of deployments.

Miners should continue setting the bit in LockedIn phase so uptake is visible, though this has no effect on consensus rules.

### Miner Round Votes

Within a single round, the votes for all the blocks is tallied.  There are [0..roundSize] votes for all active updates.

The vote is computed using fixed point math with a scale factor of 1000.  Percentages are rounded off.  For example, with a roundSize of 200 and 99 "yes" votes, the scaled percentage would be 4950 -> 49.50, rounded off to 50%.

### Smartnode Vote Representation Within Blocks

Smartnodes can only vote using Quorums.  Individual Quorum members place their votes in the `qcontrib` message.  This
message does not contain a version flag, so the llmqType is used to identify the new qcontrib format:

    {
        uint8_t zero;     // Old llmqType (always set to zer0)
        uint8_t llmqType; // Real llmqType
        int32_t nVersion; // qcontrib version, processed the same way as the block nVersion.

        // The remaining fields are as before:
        quorumHash;
        proTxHash;
        *vvec;
        *contributions;
    }

The quorum's commitment message present totals of all of the signers' vote contributions.  In the `qpcommit` and
`qfcommit` messages we use the initial llmqType field the same way as the contribution:

    {
        uint8_t zero;        // Old llmqType (always set to zer0)
        uint8_t llmqType;    // Real llmqType
        bool    roundVoting; // Always True

        // A repeating list of votes, "null" terminated with a zero bit, zero votes pair:
        [
            uint8_t  bit;   // Bit number (0-28)
            uint16_t votes; // Number of "yes" votes
        ]
        uint8_t  bit;       // Zero - ending list of votes.
        uint16_t votes;     // Zero - ending list of votes.

        // The remaining fields are as before:
        quorumHash
        proTxHash
        DYNBITSET(validMembers)
        quorumPublicKey
        quorumVvecHash
        quorumSig
        sig
    }

If Round Voting is active, node contributions and premature commitments must use the new messages providing vote information.  If not, the message will be rejected in the normal fashion, potentially punishing and eventually banning the node.

### Smartnode Round Votes

Smartnode round votes are collected from all final quorum commitments within blocks from that round.  Due to the varying
size of quorums, when they occur, and how many signers are present, we do not exactly how many votes will appear in the round.

To determine the node round vote, we look at the sum of all votes and signers present in those quorums.  The percentage for
that round is determined using fixed point math in the same way as the miner votes, but in this case it is the
sum(votes) / sum(signers).  If there are no quorum final commitments in the round, the vote percentage is zero.

## Period Votes

A period vote is the real vote that affects change.  It takes the last `votePeriods` round votes and combines them to produce
and overall vote.

### Miner Period Votes

The miner period vote is straightforward.  It is the sum of all votes divided by the number of blocks in the period, using fixed point math and rounding as defined earlier.

### Node Period Votes

Due to the variability of quorums and the number of signers, we need to sum of all votes divided by the sum of all signers in the period, using fixed point math and rounding as defined earlier.

## State transitions

All blocks before `startHeight` shall have the state `Defined`.

All blocks within a round shall have the same state.

The blocks in the first round (height: startHeight..(startHeight + roundSize - 1)) shall have the state `Active`

The state for a round is determined by the previous votingPeriod rounds.  For example, if the votingPeriod is 10, then the previous 10 rounds are combined to form a vote and this vote determines if the state changes for the following round.

When looking back at previous rounds to determine a vote, rounds that are before the genesis block or before startHeight are treated as a 0% vote.

The two main transitions are best describe with pseudo code:

    case Active:
        if (nHeight > lastVotingHeight)
        {
            if (forcedUpdate) {
               State = LockedIn;
               finalHeight = nHeight + roundSize * graceRounds;
            }
            else {
               State = Failed;
            }
        }

        if (minerPeriodVote >= minerThreshold && nodePeriodVote >= nodeThreshold) {
            State = LockedIn;
            finalHeight = nHeight + roundSize * graceRounds;
        }
        break;

    case LockedIn:
        if (nHeight >= finalHeight) {
            State = Active;
        }
        break;
