// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/quorums_dkgabsenceestimate.h>
#include <test/test_raptoreum.h>

#include <boost/test/unit_test.hpp>

// CDKGAbsenceEstimate merges one or more independently-relayed CDKGAbsenceRecords for the
// same round into a single running view, instead of CDKGAbsenceTracker::RecordRound's
// one-call-per-round assumption (see quorums_dkgabsenceestimate.h for why that assumption
// doesn't hold for a non-member fed by the QABSENCERECORD relay). Pinned here with plain
// records -- MergeRecord trusts its input the same way CDKGAbsenceTracker::RecordRound does;
// by the time either is called, CDKGAbsenceRecord::Verify() has already run.
BOOST_FIXTURE_TEST_SUITE(quorums_dkgabsenceestimate_tests, BasicTestingSetup)

namespace {
    const Consensus::LLMQType TYPE = Consensus::LLMQ_5_60;
    const uint256 QHASH = uint256S("aa");
    const uint256 OTHER_QHASH = uint256S("bb");
    const uint256 MEMBER_A = uint256S("11");
    const uint256 MEMBER_B = uint256S("22");
    const uint256 MEMBER_C = uint256S("33");
    const uint256 MEMBER_D = uint256S("44");
    const std::vector<uint256> CANDIDATES = {MEMBER_A, MEMBER_B, MEMBER_C, MEMBER_D};

    llmq::CDKGAbsenceVote MakeVote(const uint256 &quorumHash, const uint256 &reporter,
                                    const std::vector<bool> &absent) {
        llmq::CDKGAbsenceVote v;
        v.llmqType = TYPE;
        v.quorumHash = quorumHash;
        v.proTxHash = reporter;
        v.absentMembers = absent;
        return v;
    }

    // confirmedAbsent is taken as given, not recomputed from votes -- MergeRecord doesn't
    // re-verify a record's own internal intersection (that's Verify()'s job, already done by
    // the time a record reaches here), it only folds record.votes into the round's running
    // union.
    llmq::CDKGAbsenceRecord MakeRecord(const uint256 &quorumHash, const std::vector<bool> &confirmedAbsent,
                                        const std::vector<llmq::CDKGAbsenceVote> &votes) {
        llmq::CDKGAbsenceRecord r;
        r.llmqType = TYPE;
        r.quorumHash = quorumHash;
        r.confirmedAbsent = confirmedAbsent;
        r.votes = votes;
        return r;
    }

    llmq::CDKGAbsenceStats StatsFor(const llmq::CDKGAbsenceEstimate &estimate, const uint256 &member) {
        return estimate.GetStats(TYPE, member);
    }
} // namespace

BOOST_AUTO_TEST_CASE(first_record_counts_selected_and_absent) {
    llmq::CDKGAbsenceEstimate estimate;
    auto voteA = MakeVote(QHASH, MEMBER_A, {false, true, false, false}); // A reports B absent
    auto record = MakeRecord(QHASH, {false, true, false, false}, {voteA});

    estimate.MergeRecord(CANDIDATES, record);

    // Every candidate gets credited as selected, regardless of whether confirmedAbsent names
    // them -- selection is about having been in the round at all, not about the outcome.
    for (const auto &member: CANDIDATES) {
        BOOST_CHECK_EQUAL(StatsFor(estimate, member).roundsSelected, 1u);
    }
    BOOST_CHECK_EQUAL(StatsFor(estimate, MEMBER_B).roundsReportedAbsent, 1u);
    BOOST_CHECK_EQUAL(StatsFor(estimate, MEMBER_A).roundsReportedAbsent, 0u);
}

BOOST_AUTO_TEST_CASE(second_record_same_round_does_not_double_count_selected) {
    llmq::CDKGAbsenceEstimate estimate;
    auto voteA = MakeVote(QHASH, MEMBER_A, {false, true, false, false});
    auto voteC = MakeVote(QHASH, MEMBER_C, {false, true, false, false}); // independent reporter, same claim

    estimate.MergeRecord(CANDIDATES, MakeRecord(QHASH, {false, true, false, false}, {voteA}));
    estimate.MergeRecord(CANDIDATES, MakeRecord(QHASH, {false, true, false, false}, {voteC}));

    // Two records relayed for the same round must still count as one round, not two.
    BOOST_CHECK_EQUAL(StatsFor(estimate, MEMBER_A).roundsSelected, 1u);
    BOOST_CHECK_EQUAL(StatsFor(estimate, MEMBER_B).roundsReportedAbsent, 1u);
}

BOOST_AUTO_TEST_CASE(override_vote_in_a_later_record_retracts_absence_credit) {
    llmq::CDKGAbsenceEstimate estimate;
    // Record 1: A alone claims B absent.
    auto voteA = MakeVote(QHASH, MEMBER_A, {false, true, false, false});
    estimate.MergeRecord(CANDIDATES, MakeRecord(QHASH, {false, true, false, false}, {voteA}));
    BOOST_REQUIRE_EQUAL(StatsFor(estimate, MEMBER_B).roundsReportedAbsent, 1u);

    // Record 2, relayed later, independently: C's own vote says nobody was absent -- C must
    // have heard from B. The merged view now has to fold A's and C's votes together, and the
    // intersection drops B, the same override property a single record already enforces.
    auto voteC = MakeVote(QHASH, MEMBER_C, {false, false, false, false});
    estimate.MergeRecord(CANDIDATES, MakeRecord(QHASH, {false, false, false, false}, {voteC}));

    BOOST_CHECK_EQUAL(StatsFor(estimate, MEMBER_B).roundsReportedAbsent, 0u);
    // Still one round, not two -- the retraction doesn't touch roundsSelected.
    BOOST_CHECK_EQUAL(StatsFor(estimate, MEMBER_A).roundsSelected, 1u);
}

BOOST_AUTO_TEST_CASE(different_round_tracked_independently) {
    llmq::CDKGAbsenceEstimate estimate;
    auto voteA = MakeVote(QHASH, MEMBER_A, {false, true, false, false});
    auto voteB = MakeVote(OTHER_QHASH, MEMBER_B, {true, false, false, false}); // different round, A absent this time

    estimate.MergeRecord(CANDIDATES, MakeRecord(QHASH, {false, true, false, false}, {voteA}));
    estimate.MergeRecord(CANDIDATES, MakeRecord(OTHER_QHASH, {true, false, false, false}, {voteB}));

    // Two distinct rounds -- selected count reflects both, and each round's own absence stays
    // scoped to it rather than merging across rounds.
    BOOST_CHECK_EQUAL(StatsFor(estimate, MEMBER_A).roundsSelected, 2u);
    BOOST_CHECK_EQUAL(StatsFor(estimate, MEMBER_A).roundsReportedAbsent, 1u); // only from OTHER_QHASH's round
    BOOST_CHECK_EQUAL(StatsFor(estimate, MEMBER_B).roundsReportedAbsent, 1u); // only from QHASH's round
}

BOOST_AUTO_TEST_CASE(unknown_member_returns_zero_valued_stats) {
    llmq::CDKGAbsenceEstimate estimate;
    auto stats = StatsFor(estimate, MEMBER_D);
    BOOST_CHECK_EQUAL(stats.roundsSelected, 0u);
    BOOST_CHECK_EQUAL(stats.roundsReportedAbsent, 0u);
}

BOOST_AUTO_TEST_SUITE_END()
