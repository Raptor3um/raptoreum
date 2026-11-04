// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/quorums_dkgsessionhandler.h>

#include <test/test_raptoreum.h>

#include <boost/test/unit_test.hpp>

// AllNodeIdsMatch backs BatchVerifyMessageSigs' "are these messages all from
// the same peer" shortcut: a batch from one misbehaving peer returns just
// that peer, so the per-message fallback never needs real signature checks
// for the common single-sender case. Run under RegTestingSetup since that's
// the chain this and its caller are meant to run against.
BOOST_FIXTURE_TEST_SUITE(quorums_dkgsessionhandler_tests, RegTestingSetup)

BOOST_AUTO_TEST_CASE(empty_batch_is_vacuously_matching) {
    NodeId sole;
    BOOST_CHECK(llmq::AllNodeIdsMatch({}, sole));
}

BOOST_AUTO_TEST_CASE(single_message_matches_itself) {
    NodeId sole = -1;
    BOOST_CHECK(llmq::AllNodeIdsMatch({42}, sole));
    BOOST_CHECK_EQUAL(sole, 42);
}

BOOST_AUTO_TEST_CASE(several_messages_from_one_peer_match) {
    NodeId sole = -1;
    BOOST_CHECK(llmq::AllNodeIdsMatch({7, 7, 7, 7}, sole));
    BOOST_CHECK_EQUAL(sole, 7);
}

// The exact scenario from the shared-helper review (one real peer's vote
// plus the local node's own self-reported vote, NodeId -1): two distinct
// senders must be detected as not matching, regardless of which one the
// loop sees first. Before the fix, `first` never left its initial `true`
// state once set in the `if` branch, so the `else` branch holding this
// comparison was never reached and a mixed batch was always misreported as
// single-sender -- blaming whichever peer happened to be processed last.
BOOST_AUTO_TEST_CASE(two_different_peers_do_not_match_either_order) {
    NodeId sole = -1;
    BOOST_CHECK(!llmq::AllNodeIdsMatch({42, -1}, sole));

    sole = -1;
    BOOST_CHECK(!llmq::AllNodeIdsMatch({-1, 42}, sole));
}

// A third, later message introducing the mismatch must still be caught --
// confirms the comparison isn't accidentally scoped to just the first pair.
BOOST_AUTO_TEST_CASE(mismatch_detected_after_a_matching_prefix) {
    NodeId sole = -1;
    BOOST_CHECK(!llmq::AllNodeIdsMatch({5, 5, 9}, sole));
}

BOOST_AUTO_TEST_SUITE_END()
