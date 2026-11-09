// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/quorums_ghoststats.h>
#include <test/test_raptoreum.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(quorums_ghoststats_tests, BasicTestingSetup)

// Mainnet-shaped llmq400_85 numbers: dkgInterval=720, dkgMiningWindowEnd=48.
static const Consensus::LLMQParams params24h = [] {
    Consensus::LLMQParams p{};
    p.dkgInterval = 720;
    p.dkgMiningWindowEnd = 48;
    return p;
}();

BOOST_AUTO_TEST_CASE(exact_trigger_height_is_valid)
{
    // slotStart=0 -> anchorHeight=48 -> trigger height=49
    BOOST_CHECK_EQUAL(llmq::GetGhostBanSlotStart(49, params24h), 0);
    // second cycle: slotStart=720 -> anchorHeight=768 -> trigger height=769
    BOOST_CHECK_EQUAL(llmq::GetGhostBanSlotStart(769, params24h), 720);
    // a later cycle, to catch a bug that only shows up once numbers get larger
    BOOST_CHECK_EQUAL(llmq::GetGhostBanSlotStart(720 * 10 + 49, params24h), 720 * 10);
}

BOOST_AUTO_TEST_CASE(one_block_either_side_is_invalid)
{
    BOOST_CHECK_EQUAL(llmq::GetGhostBanSlotStart(48, params24h), -1);  // one too early
    BOOST_CHECK_EQUAL(llmq::GetGhostBanSlotStart(50, params24h), -1);  // one too late
    BOOST_CHECK_EQUAL(llmq::GetGhostBanSlotStart(769 - 1, params24h), -1);
    BOOST_CHECK_EQUAL(llmq::GetGhostBanSlotStart(769 + 1, params24h), -1);
}

BOOST_AUTO_TEST_CASE(every_height_within_a_cycle_is_invalid_except_the_one)
{
    // Across one full cycle (720 blocks), exactly one height should validate.
    int validCount = 0;
    for (int h = 1; h <= 720; h++) {
        if (llmq::GetGhostBanSlotStart(h, params24h) >= 0) {
            validCount++;
        }
    }
    BOOST_CHECK_EQUAL(validCount, 1);
}

BOOST_AUTO_TEST_CASE(heights_before_the_window_can_ever_close_are_invalid)
{
    BOOST_CHECK_EQUAL(llmq::GetGhostBanSlotStart(0, params24h), -1);
    BOOST_CHECK_EQUAL(llmq::GetGhostBanSlotStart(1, params24h), -1);
    BOOST_CHECK_EQUAL(llmq::GetGhostBanSlotStart(47, params24h), -1); // anchorHeight=46 < 48
}

BOOST_AUTO_TEST_CASE(is_ghost_threshold_comparison)
{
    // missed*100 >= selected*threshold, integer-only
    BOOST_CHECK(llmq::IsGhost({3, 2}, 51));  // 2/3 = 66.67% >= 51%
    BOOST_CHECK(!llmq::IsGhost({3, 1}, 51)); // 1/3 = 33.33% < 51%
    // exact boundary: 51/100 == 51/100
    BOOST_CHECK(llmq::IsGhost({100, 51}, 51));
    BOOST_CHECK(!llmq::IsGhost({100, 50}, 51));
}

BOOST_AUTO_TEST_CASE(chunk_count_boundaries)
{
    BOOST_CHECK_EQUAL(llmq::GhostBanChunkCount(0), 1u);   // always >=1, even with nothing to list
    BOOST_CHECK_EQUAL(llmq::GhostBanChunkCount(1), 1u);
    BOOST_CHECK_EQUAL(llmq::GhostBanChunkCount(250), 1u); // exactly one chunk's worth: must not round up
    BOOST_CHECK_EQUAL(llmq::GhostBanChunkCount(251), 2u); // one over: must round up
    BOOST_CHECK_EQUAL(llmq::GhostBanChunkCount(500), 2u);
    BOOST_CHECK_EQUAL(llmq::GhostBanChunkCount(501), 3u);
    BOOST_CHECK_EQUAL(llmq::GhostBanChunkCount(2400), 10u); // realistic worst case: every core-scope slot in a day
}

BOOST_AUTO_TEST_SUITE_END()
