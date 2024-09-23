// Copyright (c) 2014-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * See https://www.boost.org/doc/libs/1_78_0/libs/test/doc/html/boost_test/adv_scenarios/single_header_customizations/multiple_translation_units.html
 */

#include <net.h>

#include <test/test_raptoreum.h>

#include <boost/signals2/signal.hpp>
#include <boost/test/included/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(main_tests, TestingSetup
)

bool ReturnFalse() { return false; }

bool ReturnTrue() { return true; }

BOOST_AUTO_TEST_CASE(test_combiner_all)
        {
                boost::signals2::signal < bool(), CombinerAll > Test;
        BOOST_CHECK(Test());
        Test.connect(&ReturnFalse);
        BOOST_CHECK(!Test());
        Test.connect(&ReturnTrue);
        BOOST_CHECK(!Test());
        Test.disconnect(&ReturnFalse);
        BOOST_CHECK(Test());
        Test.disconnect(&ReturnTrue);
        BOOST_CHECK(Test());
        }

BOOST_AUTO_TEST_SUITE_END()
