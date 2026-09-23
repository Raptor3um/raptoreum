// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <chainparamsbase.h>
#include <llmq/quorums_parameters.h>
#include <test/test_raptoreum.h>
#include <util/system.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(chainparams_tests, BasicTestingSetup
)

// Both regtest-only quorum types are meant to be used together on the same DKG
// cycle. If one is ever given a shorter retention window than the other, whichever
// quorum forms second loses its DKG contributions first, and cleanup can also
// touch an unrelated still-active quorum. Pinning both values (not just their
// equality) turns a future accidental omission -- the exact shape of the keepOldKeys
// regression this suite exists to catch -- into a build failure here instead of a
// bug report from a QGETDATA/key-recovery failure downstream.
BOOST_AUTO_TEST_CASE(regtest_llmq_types_share_retention_window) {
    SelectParams(CBaseChainParams::REGTEST);
    const auto &llmqs = Params().GetConsensus().llmqs;

    BOOST_CHECK_EQUAL(llmqs.at(Consensus::LLMQ_5_60).keepOldKeys, 4);
    BOOST_CHECK_EQUAL(llmqs.at(Consensus::LLMQ_TEST_V17).keepOldKeys, 4);
    BOOST_CHECK_EQUAL(llmqs.at(Consensus::LLMQ_5_60).max_store_depth(),
                      llmqs.at(Consensus::LLMQ_TEST_V17).max_store_depth());
}

BOOST_AUTO_TEST_CASE(llmqtestparams_applies_valid_size_and_threshold) {
    gArgs.ForceSetArg("-llmqtestparams", "5:3");
    auto params = CreateChainParams(CBaseChainParams::REGTEST);
    gArgs.ForceRemoveArg("-llmqtestparams");

    for (const auto type: {Consensus::LLMQ_5_60, Consensus::LLMQ_TEST_V17}) {
        const auto &llmq = params->GetConsensus().llmqs.at(type);
        BOOST_CHECK_EQUAL(llmq.size, 5);
        BOOST_CHECK_EQUAL(llmq.minSize, 3);
        BOOST_CHECK_EQUAL(llmq.threshold, 3);
        BOOST_CHECK_EQUAL(llmq.dkgBadVotesThreshold, 3);
    }
}

BOOST_AUTO_TEST_CASE(llmqtestparams_rejects_missing_colon) {
    gArgs.ForceSetArg("-llmqtestparams", "5");
    BOOST_CHECK_THROW(CreateChainParams(CBaseChainParams::REGTEST), std::runtime_error);
    gArgs.ForceRemoveArg("-llmqtestparams");
}

BOOST_AUTO_TEST_CASE(llmqtestparams_rejects_extra_colon) {
    gArgs.ForceSetArg("-llmqtestparams", "5:3:1");
    BOOST_CHECK_THROW(CreateChainParams(CBaseChainParams::REGTEST), std::runtime_error);
    gArgs.ForceRemoveArg("-llmqtestparams");
}

BOOST_AUTO_TEST_CASE(llmqtestparams_rejects_non_numeric_values) {
    gArgs.ForceSetArg("-llmqtestparams", "a:b");
    BOOST_CHECK_THROW(CreateChainParams(CBaseChainParams::REGTEST), std::runtime_error);
    gArgs.ForceRemoveArg("-llmqtestparams");
}

BOOST_AUTO_TEST_CASE(llmqtestparams_rejects_threshold_above_size) {
    gArgs.ForceSetArg("-llmqtestparams", "2:5");
    BOOST_CHECK_THROW(CreateChainParams(CBaseChainParams::REGTEST), std::runtime_error);
    gArgs.ForceRemoveArg("-llmqtestparams");
}

BOOST_AUTO_TEST_CASE(llmqtestparams_rejects_zero_size) {
    gArgs.ForceSetArg("-llmqtestparams", "0:0");
    BOOST_CHECK_THROW(CreateChainParams(CBaseChainParams::REGTEST), std::runtime_error);
    gArgs.ForceRemoveArg("-llmqtestparams");
}

BOOST_AUTO_TEST_CASE(llmqtestparams_rejects_zero_threshold) {
    gArgs.ForceSetArg("-llmqtestparams", "5:0");
    BOOST_CHECK_THROW(CreateChainParams(CBaseChainParams::REGTEST), std::runtime_error);
    gArgs.ForceRemoveArg("-llmqtestparams");
}

BOOST_AUTO_TEST_SUITE_END()
