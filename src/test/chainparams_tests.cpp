// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <test/test_raptoreum.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(chainparams_tests, BasicTestingSetup)

// consensus.defaultAssumeValid must resolve to a real block hash, not the
// null hash. uint256S() only strips a literal "0x" prefix (uint256.cpp,
// SetHex()); anything else, including a look-alike like "ox", is not
// recognised, and the following hex scan then fails on the very first
// character, leaving the value at its post-memset default of all zero
// bytes -- silently, with no error or warning.
BOOST_AUTO_TEST_CASE(mainnet_default_assume_valid_is_not_null)
        {
                const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
                const uint256 &assumeValid = chainParams->GetConsensus().defaultAssumeValid;

                BOOST_CHECK(!assumeValid.IsNull());
                BOOST_CHECK_EQUAL(assumeValid.GetHex(),
                                   "6fb0b649723f51b67484019409fef94d077f17c8d88645e08c000b2e4fd3e28a");
        }

BOOST_AUTO_TEST_SUITE_END()
