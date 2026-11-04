// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/quorums_dkgabsence.h>
#include <test/test_raptoreum.h>

#include <boost/test/unit_test.hpp>

// VerifyAbsenceRecordCore is the pure core of CDKGAbsenceRecord::Verify()
// (see quorums_dkgabsence.h/.cpp), extracted specifically so the single
// most important invariant in this file -- that confirmedAbsent is exactly
// the intersection of every bundled vote, with no duplicate or non-member
// reporter and no out-of-range bit -- is pinned directly, with plain
// values. Verify() itself still needs a live chain/DML (to resolve real
// quorum membership) and real BLS signatures (to check each vote actually
// came from its claimed reporter) for the parts this core doesn't cover.
BOOST_FIXTURE_TEST_SUITE(quorums_dkgabsence_tests, BasicTestingSetup)

namespace {
    const Consensus::LLMQType TYPE = Consensus::LLMQ_5_60;
    const uint256 QHASH = uint256S("aa");
    const uint256 OTHER_QHASH = uint256S("bb");
    const uint256 MEMBER_A = uint256S("11");
    const uint256 MEMBER_B = uint256S("22");
    const uint256 MEMBER_C = uint256S("33");
    const uint256 MEMBER_D = uint256S("44");
    const uint256 NON_MEMBER = uint256S("ff");
    const std::set<uint256> MEMBERS = {MEMBER_A, MEMBER_B, MEMBER_C, MEMBER_D};

    llmq::CDKGAbsenceVote MakeVote(const uint256 &reporter, const std::vector<bool> &absent) {
        llmq::CDKGAbsenceVote v;
        v.llmqType = TYPE;
        v.quorumHash = QHASH;
        v.proTxHash = reporter;
        v.absentMembers = absent;
        return v;
    }
} // namespace

BOOST_AUTO_TEST_CASE(empty_votes_accepts_all_false) {
    std::vector<bool> confirmedAbsent = {false, false, false, false};
    BOOST_CHECK(llmq::VerifyAbsenceRecordCore(TYPE, QHASH, 4, 4, MEMBERS, confirmedAbsent, {}));
}

BOOST_AUTO_TEST_CASE(empty_votes_rejects_any_claim) {
    // zero evidence can't support any claim, even just one bit
    std::vector<bool> confirmedAbsent = {true, false, false, false};
    BOOST_CHECK(!llmq::VerifyAbsenceRecordCore(TYPE, QHASH, 4, 4, MEMBERS, confirmedAbsent, {}));
}

BOOST_AUTO_TEST_CASE(single_vote_exact_match_accepted) {
    std::vector<bool> absent = {true, false, true, false}; // B reports A and C absent
    auto vote = MakeVote(MEMBER_B, absent);
    BOOST_CHECK(llmq::VerifyAbsenceRecordCore(TYPE, QHASH, 4, 4, MEMBERS, absent, {vote}));
}

BOOST_AUTO_TEST_CASE(intersection_not_union_one_honest_vote_overrides) {
    // A and C both report B absent; A also claims D absent, but C's own
    // vote says D is NOT absent (C must have heard from D). The override
    // property: one honest "present" removes D from confirmedAbsent no
    // matter how many other votes claimed otherwise.
    auto voteA = MakeVote(MEMBER_A, {false, true, false, true});  // claims B, D absent
    auto voteC = MakeVote(MEMBER_C, {false, true, false, false}); // claims only B absent
    std::vector<bool> expected = {false, true, false, false};     // intersection: only B
    BOOST_CHECK(llmq::VerifyAbsenceRecordCore(TYPE, QHASH, 4, 4, MEMBERS, expected, {voteA, voteC}));
}

BOOST_AUTO_TEST_CASE(intersection_not_union_honest_vote_first_still_overrides) {
    // same as above with the bundle order reversed -- the override property
    // has to hold regardless of which vote a builder happens to put first
    // (real bundle order is by message hash, i.e. arbitrary). Pins the
    // AND-accumulation itself: a "last vote wins" implementation would
    // compute {false,true,false,true} here and fail this check, even though
    // it happens to pass the voteA-then-voteC ordering above.
    auto voteA = MakeVote(MEMBER_A, {false, true, false, true});
    auto voteC = MakeVote(MEMBER_C, {false, true, false, false});
    std::vector<bool> expected = {false, true, false, false};
    BOOST_CHECK(llmq::VerifyAbsenceRecordCore(TYPE, QHASH, 4, 4, MEMBERS, expected, {voteC, voteA}));
}

BOOST_AUTO_TEST_CASE(superset_claim_rejected) {
    // the real intersection only supports B absent; confirmedAbsent also
    // (falsely) claims D -- a fabricated, inflated claim.
    auto voteA = MakeVote(MEMBER_A, {false, true, false, true});
    auto voteC = MakeVote(MEMBER_C, {false, true, false, false});
    std::vector<bool> inflated = {false, true, false, true};
    BOOST_CHECK(!llmq::VerifyAbsenceRecordCore(TYPE, QHASH, 4, 4, MEMBERS, inflated, {voteA, voteC}));
}

BOOST_AUTO_TEST_CASE(subset_claim_rejected) {
    // the real intersection supports B absent; confirmedAbsent
    // under-claims (empty) -- understating evidence is rejected too, not
    // just inflating it.
    auto voteA = MakeVote(MEMBER_A, {false, true, false, false});
    auto voteC = MakeVote(MEMBER_C, {false, true, false, false});
    std::vector<bool> understated = {false, false, false, false};
    BOOST_CHECK(!llmq::VerifyAbsenceRecordCore(TYPE, QHASH, 4, 4, MEMBERS, understated, {voteA, voteC}));
}

BOOST_AUTO_TEST_CASE(duplicate_reporter_rejected) {
    auto vote1 = MakeVote(MEMBER_A, {false, true, false, false});
    auto vote2 = MakeVote(MEMBER_A, {false, true, false, false}); // same reporter twice
    BOOST_CHECK(!llmq::VerifyAbsenceRecordCore(TYPE, QHASH, 4, 4, MEMBERS,
                                                {false, true, false, false}, {vote1, vote2}));
}

BOOST_AUTO_TEST_CASE(non_member_reporter_rejected) {
    auto vote = MakeVote(NON_MEMBER, {false, true, false, false}); // NON_MEMBER not in MEMBERS
    BOOST_CHECK(!llmq::VerifyAbsenceRecordCore(TYPE, QHASH, 4, 4, MEMBERS,
                                                {false, true, false, false}, {vote}));
}

BOOST_AUTO_TEST_CASE(confirmed_absent_size_mismatch_rejected) {
    // with a non-empty bundle, a wrong-size confirmedAbsent would also fail
    // the final vector equality on its own (different sizes never compare
    // equal), which would mask this check -- use an EMPTY bundle instead,
    // where that equality never runs and the explicit size check is the
    // only thing standing between accept and reject.
    //
    // All-false, not {false, true, false}: a set bit here would also get
    // rejected by the empty-votes/no-claim-without-evidence check below,
    // masking the size check the same way a non-empty bundle would -- an
    // all-false vector is rejected ONLY if the size check itself still runs.
    std::vector<bool> wrongSize = {false, false, false}; // 3, not paramsSize=4
    BOOST_CHECK(!llmq::VerifyAbsenceRecordCore(TYPE, QHASH, 4, 4, MEMBERS, wrongSize, {}));
}

BOOST_AUTO_TEST_CASE(vote_absent_members_too_short_rejected) {
    auto vote = MakeVote(MEMBER_A, {false, true, false}); // 3, not paramsSize=4
    BOOST_CHECK(!llmq::VerifyAbsenceRecordCore(TYPE, QHASH, 4, 4, MEMBERS,
                                                {false, true, false, false}, {vote}));
}

BOOST_AUTO_TEST_CASE(vote_absent_members_too_long_rejected) {
    // the too-short case above would also be caught by the AND-loop
    // indexing past a shorter vector -- this direction is the one that
    // can't be: a too-LONG vote (DYNBITSET deserializes any length a peer
    // sends) has every one of the first 4 bits matching, and would be
    // silently accepted if the size check were the only thing skipped --
    // the AND-loop only ever reads indices < paramsSize, so it never even
    // looks at the extra bit.
    auto vote = MakeVote(MEMBER_A, {false, true, false, false, true}); // 5, not 4
    BOOST_CHECK(!llmq::VerifyAbsenceRecordCore(TYPE, QHASH, 4, 4, MEMBERS,
                                                {false, true, false, false}, {vote}));
}

// There is no standalone test for an out-of-range bit in confirmedAbsent
// itself (as opposed to in a vote): with a non-empty bundle, that specific
// check is provably unreachable as a deciding factor -- any vote sharing
// the out-of-range bit gets rejected by the loop below first, and if no
// vote shares it the final intersection-vs-confirmedAbsent equality already
// rejects the mismatch on its own. The check stays in the implementation
// as cheap defense in depth (see quorums_dkgabsence.cpp); it just can't be
// pinned by any input shaped to isolate it.
BOOST_AUTO_TEST_CASE(out_of_range_bit_in_a_vote_rejected) {
    // only 3 real members even though paramsSize=4 (CalculateQuorum handed
    // back fewer than the configured size) -- index 3 maps to nobody and
    // must never be set. Two votes, not one: voteB's bit 3 is false, so
    // without the bounds check the AND would correctly zero out bit 3
    // anyway and this would pass for the wrong reason (masked, the same
    // way the single-vote version was).
    std::set<uint256> threeMembers = {MEMBER_A, MEMBER_B, MEMBER_C};
    auto voteA = MakeVote(MEMBER_A, {false, false, false, true}); // bit 3 out of range
    auto voteB = MakeVote(MEMBER_B, {false, false, false, false});
    BOOST_CHECK(!llmq::VerifyAbsenceRecordCore(TYPE, QHASH, 4, 3, threeMembers,
                                                {false, false, false, false}, {voteA, voteB}));
}

BOOST_AUTO_TEST_CASE(vote_llmqtype_mismatch_rejected) {
    auto vote = MakeVote(MEMBER_A, {false, true, false, false});
    vote.llmqType = Consensus::LLMQ_TEST_V17; // doesn't match the record's TYPE
    BOOST_CHECK(!llmq::VerifyAbsenceRecordCore(TYPE, QHASH, 4, 4, MEMBERS,
                                                {false, true, false, false}, {vote}));
}

BOOST_AUTO_TEST_CASE(vote_quorumhash_mismatch_rejected) {
    auto vote = MakeVote(MEMBER_A, {false, true, false, false});
    vote.quorumHash = OTHER_QHASH; // doesn't match the record's QHASH
    BOOST_CHECK(!llmq::VerifyAbsenceRecordCore(TYPE, QHASH, 4, 4, MEMBERS,
                                                {false, true, false, false}, {vote}));
}

BOOST_AUTO_TEST_SUITE_END()
