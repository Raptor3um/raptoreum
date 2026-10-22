// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/quorums_dkgabsencetracker.h>

#include <test/test_raptoreum.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(quorums_dkgabsencetracker_tests, RegTestingSetup)

namespace {
    using namespace llmq;

    const Consensus::LLMQType TYPE_A = Consensus::LLMQ_5_60;
    const Consensus::LLMQType TYPE_B = Consensus::LLMQ_TEST_V17;
    const uint256 MEMBER_A = uint256S("11");
    const uint256 MEMBER_B = uint256S("22");
    const uint256 MEMBER_C = uint256S("33");
    const uint256 NEVER_SELECTED = uint256S("ff");

    // confirmedAbsent[i] applies to selected[i] -- same convention
    // CDKGSession::GetMemberProTxHashes()/BuildAbsenceRecord() use.
    CDKGAbsenceRecord MakeRecord(const std::vector<bool> &confirmedAbsent) {
        CDKGAbsenceRecord r;
        r.confirmedAbsent = confirmedAbsent;
        return r;
    }
} // namespace

BOOST_AUTO_TEST_CASE(unselected_member_has_zero_stats) {
    CDKGAbsenceTracker tracker;
    auto stats = tracker.GetStats(TYPE_A, NEVER_SELECTED);
    BOOST_CHECK_EQUAL(stats.roundsSelected, 0u);
    BOOST_CHECK_EQUAL(stats.roundsReportedAbsent, 0u);
}

BOOST_AUTO_TEST_CASE(one_round_no_record_counts_selection_only) {
    CDKGAbsenceTracker tracker;
    tracker.RecordRound(TYPE_A, {MEMBER_A, MEMBER_B}, nullptr);

    auto a = tracker.GetStats(TYPE_A, MEMBER_A);
    BOOST_CHECK_EQUAL(a.roundsSelected, 1u);
    BOOST_CHECK_EQUAL(a.roundsReportedAbsent, 0u);
}

BOOST_AUTO_TEST_CASE(one_round_with_record_counts_both) {
    CDKGAbsenceTracker tracker;
    // MEMBER_A confirmed absent, MEMBER_B not.
    auto record = MakeRecord({true, false});
    tracker.RecordRound(TYPE_A, {MEMBER_A, MEMBER_B}, &record);

    auto a = tracker.GetStats(TYPE_A, MEMBER_A);
    BOOST_CHECK_EQUAL(a.roundsSelected, 1u);
    BOOST_CHECK_EQUAL(a.roundsReportedAbsent, 1u);

    auto b = tracker.GetStats(TYPE_A, MEMBER_B);
    BOOST_CHECK_EQUAL(b.roundsSelected, 1u);
    BOOST_CHECK_EQUAL(b.roundsReportedAbsent, 0u);
}

BOOST_AUTO_TEST_CASE(accumulates_across_rounds) {
    CDKGAbsenceTracker tracker;
    // Round 1: A absent, B not.
    auto r1 = MakeRecord({true, false});
    tracker.RecordRound(TYPE_A, {MEMBER_A, MEMBER_B}, &r1);
    // Round 2: A not absent this time, B is. Order of the selected list
    // can differ round to round (different DKG membership selection) --
    // this checks the tracker keys on the proTxHash, not position.
    auto r2 = MakeRecord({false, true});
    tracker.RecordRound(TYPE_A, {MEMBER_A, MEMBER_B}, &r2);
    // Round 3: neither selected, C is -- a round A and B had no part in.
    tracker.RecordRound(TYPE_A, {MEMBER_C}, nullptr);

    auto a = tracker.GetStats(TYPE_A, MEMBER_A);
    BOOST_CHECK_EQUAL(a.roundsSelected, 2u);
    BOOST_CHECK_EQUAL(a.roundsReportedAbsent, 1u);

    auto b = tracker.GetStats(TYPE_A, MEMBER_B);
    BOOST_CHECK_EQUAL(b.roundsSelected, 2u);
    BOOST_CHECK_EQUAL(b.roundsReportedAbsent, 1u);

    auto c = tracker.GetStats(TYPE_A, MEMBER_C);
    BOOST_CHECK_EQUAL(c.roundsSelected, 1u);
    BOOST_CHECK_EQUAL(c.roundsReportedAbsent, 0u);
}

BOOST_AUTO_TEST_CASE(quorum_types_are_tracked_independently) {
    CDKGAbsenceTracker tracker;
    auto recordA = MakeRecord({true});
    tracker.RecordRound(TYPE_A, {MEMBER_A}, &recordA);
    // Same member, a different type, never reported absent there.
    tracker.RecordRound(TYPE_B, {MEMBER_A}, nullptr);

    auto statsA = tracker.GetStats(TYPE_A, MEMBER_A);
    BOOST_CHECK_EQUAL(statsA.roundsSelected, 1u);
    BOOST_CHECK_EQUAL(statsA.roundsReportedAbsent, 1u);

    auto statsB = tracker.GetStats(TYPE_B, MEMBER_A);
    BOOST_CHECK_EQUAL(statsB.roundsSelected, 1u);
    BOOST_CHECK_EQUAL(statsB.roundsReportedAbsent, 0u);
}

// confirmedAbsent is sized to the quorum's nominal params.size, which can
// be larger than the actual candidate list (members.size()) a round was
// selected from -- e.g. one registered smartnode already PoSe-banned and
// thus excluded from selection. The extra trailing confirmedAbsent entries
// are not real candidate slots; RecordRound must still record the real
// ones rather than discarding the whole round over this normal, expected
// size difference.
BOOST_AUTO_TEST_CASE(shorter_selected_members_list_is_still_recorded) {
    CDKGAbsenceTracker tracker;
    // 3 confirmedAbsent entries, only 2 real selected members.
    auto record = MakeRecord({true, false, true});
    tracker.RecordRound(TYPE_A, {MEMBER_A, MEMBER_B}, &record);

    auto a = tracker.GetStats(TYPE_A, MEMBER_A);
    BOOST_CHECK_EQUAL(a.roundsSelected, 1u);
    BOOST_CHECK_EQUAL(a.roundsReportedAbsent, 1u);

    auto b = tracker.GetStats(TYPE_A, MEMBER_B);
    BOOST_CHECK_EQUAL(b.roundsSelected, 1u);
    BOOST_CHECK_EQUAL(b.roundsReportedAbsent, 0u);
}

// The other direction is a real caller error: a selectedMembers list
// longer than confirmedAbsent would index past its end below. This must
// still be refused entirely, not partially applied.
BOOST_AUTO_TEST_CASE(longer_selected_members_list_is_rejected_entirely) {
    CDKGAbsenceTracker tracker;
    auto record = MakeRecord({true}); // 1 entry for 2 members
    tracker.RecordRound(TYPE_A, {MEMBER_A, MEMBER_B}, &record);

    // Neither member should show up at all -- the whole round was discarded,
    // not partially applied.
    BOOST_CHECK_EQUAL(tracker.GetStats(TYPE_A, MEMBER_A).roundsSelected, 0u);
    BOOST_CHECK_EQUAL(tracker.GetStats(TYPE_A, MEMBER_B).roundsSelected, 0u);
}

BOOST_AUTO_TEST_CASE(get_all_stats_lists_every_tracked_member) {
    CDKGAbsenceTracker tracker;
    tracker.RecordRound(TYPE_A, {MEMBER_A, MEMBER_B}, nullptr);

    auto all = tracker.GetAllStats(TYPE_A);
    BOOST_CHECK_EQUAL(all.size(), 2u);
    BOOST_CHECK(all.count(MEMBER_A));
    BOOST_CHECK(all.count(MEMBER_B));
    BOOST_CHECK(!all.count(MEMBER_C));
}

BOOST_AUTO_TEST_CASE(get_all_stats_empty_for_untouched_type) {
    CDKGAbsenceTracker tracker;
    tracker.RecordRound(TYPE_A, {MEMBER_A}, nullptr);
    BOOST_CHECK(tracker.GetAllStats(TYPE_B).empty());
}

BOOST_AUTO_TEST_SUITE_END()
