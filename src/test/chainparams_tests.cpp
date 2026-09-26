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

namespace {
// gArgs is process-global and BasicTestingSetup never resets it, so a test
// that sets -llmqtestparams and returns without clearing it again leaks the
// value into every later test in the binary. Six of the seven cases below
// route CreateChainParams() through BOOST_CHECK_THROW, which already
// guarantees the cleanup line still runs either way -- but the one valid
// case does not, so an unexpected future throw there would leave
// -llmqtestparams=5:3 set for the rest of the process with no obvious
// cause downstream. A scope guard makes every case safe the same way,
// rather than leaving just that one case exposed.
struct ScopedArg {
    std::string name;
    ScopedArg(std::string argName, const std::string &value) : name(std::move(argName)) {
        gArgs.ForceSetArg(name, value);
    }
    ~ScopedArg() { gArgs.ForceRemoveArg(name); }
};
} // namespace

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
    ScopedArg arg("-llmqtestparams", "5:3");
    auto params = CreateChainParams(CBaseChainParams::REGTEST);

    for (const auto type: {Consensus::LLMQ_5_60, Consensus::LLMQ_TEST_V17}) {
        const auto &llmq = params->GetConsensus().llmqs.at(type);
        BOOST_CHECK_EQUAL(llmq.size, 5);
        BOOST_CHECK_EQUAL(llmq.minSize, 3);
        BOOST_CHECK_EQUAL(llmq.threshold, 3);
        BOOST_CHECK_EQUAL(llmq.dkgBadVotesThreshold, 3);
    }
}

BOOST_AUTO_TEST_CASE(llmqtestparams_rejects_missing_colon) {
    ScopedArg arg("-llmqtestparams", "5");
    BOOST_CHECK_THROW(CreateChainParams(CBaseChainParams::REGTEST), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(llmqtestparams_rejects_extra_colon) {
    ScopedArg arg("-llmqtestparams", "5:3:1");
    BOOST_CHECK_THROW(CreateChainParams(CBaseChainParams::REGTEST), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(llmqtestparams_rejects_non_numeric_values) {
    ScopedArg arg("-llmqtestparams", "a:b");
    BOOST_CHECK_THROW(CreateChainParams(CBaseChainParams::REGTEST), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(llmqtestparams_rejects_threshold_above_size) {
    ScopedArg arg("-llmqtestparams", "2:5");
    BOOST_CHECK_THROW(CreateChainParams(CBaseChainParams::REGTEST), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(llmqtestparams_rejects_zero_size) {
    ScopedArg arg("-llmqtestparams", "0:0");
    BOOST_CHECK_THROW(CreateChainParams(CBaseChainParams::REGTEST), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(llmqtestparams_rejects_zero_threshold) {
    ScopedArg arg("-llmqtestparams", "5:0");
    BOOST_CHECK_THROW(CreateChainParams(CBaseChainParams::REGTEST), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
