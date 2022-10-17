// Copyright (c) 2014-2020 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <validation.h>

#include <test/test_raptoreum.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(subsidy_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(block_subsidy_test)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);

    uint32_t nPrevBits;
    int32_t nPrevHeight;
    CAmount nSubsidy;

    // details for block 4249 (subsidy returned will be for block 4250)
    nPrevBits = 0x1c4a47c4;
    nPrevHeight = 4249;
    nSubsidy = GetBlockSubsidy(nPrevBits, nPrevHeight, chainParams->GetConsensus(), false);
    BOOST_CHECK_EQUAL(nSubsidy, 500000000000ULL);

    // details for block 553535 (subsidy returned will be for block 553536)
    nPrevBits = 0x1c4a47c4;
    nPrevHeight = 553535;
    nSubsidy = GetBlockSubsidy(nPrevBits, nPrevHeight, chainParams->GetConsensus(), false);
    BOOST_CHECK_EQUAL(nSubsidy, 499000000000ULL);

    // details for block 2105680 (subsidy returned will be for block 2105681)
    nPrevBits = 0x1c29ec00;
    nPrevHeight = 2105680;
    nSubsidy = GetBlockSubsidy(nPrevBits, nPrevHeight, chainParams->GetConsensus(), false);
    BOOST_CHECK_EQUAL(nSubsidy, 425000000000ULL);

    // details for block 54655273698 (subsidy returned will be for block 5273699)
    nPrevBits = 0x1c29ec00;
    nPrevHeight = 5273698;
    nSubsidy = GetBlockSubsidy(nPrevBits, nPrevHeight, chainParams->GetConsensus(), false);
    BOOST_CHECK_EQUAL(nSubsidy, 128000000000ULL);

    // details for block 7378635 (subsidy returned will be for block 7378636)
    nPrevBits = 0x1c08ba34;
    nPrevHeight = 7378635;
    nSubsidy = GetBlockSubsidy(nPrevBits, nPrevHeight, chainParams->GetConsensus(), false);
    BOOST_CHECK_EQUAL(nSubsidy, 29500000000ULL);

    // details for block 8399219 (subsidy returned will be for block 8399220)
    nPrevBits = 0x1b10cf42;
    nPrevHeight = 8399219;
    nSubsidy = GetBlockSubsidy(nPrevBits, nPrevHeight, chainParams->GetConsensus(), false);
    BOOST_CHECK_EQUAL(nSubsidy, 5500000000ULL);

    // details for block 14735288 (subsidy returned will be for block 14735289)
    nPrevBits = 0x1b11548e;
    nPrevHeight = 14735288;
    nSubsidy = GetBlockSubsidy(nPrevBits, nPrevHeight, chainParams->GetConsensus(), false);
    BOOST_CHECK_EQUAL(nSubsidy, 5400000000ULL);

    // 1st subsidy reduction happens here

    // details for block 15798386 (subsidy returned will be for block 15798387)
    nPrevBits = 0x1b10d50b;
    nPrevHeight = 15798386;
    nSubsidy = GetBlockSubsidy(nPrevBits, nPrevHeight, chainParams->GetConsensus(), false);
    BOOST_CHECK_EQUAL(nSubsidy, 500000000ULL);
}

BOOST_AUTO_TEST_SUITE_END()
