// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <chainparamsbase.h>
#include <llmq/quorums_dkgsessionmgr.h>
#include <llmq/quorums_parameters.h>
#include <test/test_raptoreum.h>

#include <boost/test/unit_test.hpp>

// CDKGSessionManager::CleanupOldContributions() decides whether to keep a
// contribution by comparing the active chain's tip height against the height
// of the quorum it belongs to, entirely inline inside a loop that also holds
// cs_db/cs_main and iterates a real LevelDB cursor. That loop itself isn't
// reasonably unit-testable in isolation -- exercising it for real needs a
// live DB, a real chain, AND real deterministic-masternode-list quorum
// membership (GetVerifiedContributions(), the only public read-back, resolves
// membership before it will return anything). llmq::IsContributionExpired()
// is the one part of that decision that doesn't need any of that: a pure
// height comparison, pulled out so it can be pinned directly.
BOOST_FIXTURE_TEST_SUITE(quorums_dkgsessionmgr_tests, BasicTestingSetup
)

BOOST_AUTO_TEST_CASE(is_contribution_expired_respects_max_store_depth) {
    SelectParams(CBaseChainParams::REGTEST);
    const auto &params = Params().GetConsensus().llmqs.at(Consensus::LLMQ_TEST_V17);
    const int depth = params.max_store_depth();
    BOOST_REQUIRE_EQUAL(depth, 120); // keepOldKeys=4 * dkgInterval=30, pinned so a change here is deliberate

    // Exactly at the boundary: kept, not expired yet.
    BOOST_CHECK(!llmq::IsContributionExpired(/*nTipHeight=*/300 + depth, /*nQuorumHeight=*/300, params));
    // One block past the boundary: expired.
    BOOST_CHECK(llmq::IsContributionExpired(/*nTipHeight=*/300 + depth + 1, /*nQuorumHeight=*/300, params));
    // Well within the window: not expired.
    BOOST_CHECK(!llmq::IsContributionExpired(/*nTipHeight=*/310, /*nQuorumHeight=*/300, params));
    // The quorum's own formation height (zero blocks old): never expired.
    BOOST_CHECK(!llmq::IsContributionExpired(/*nTipHeight=*/300, /*nQuorumHeight=*/300, params));
}

BOOST_AUTO_TEST_SUITE_END()
