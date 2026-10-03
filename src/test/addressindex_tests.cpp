// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>
#include <txdb.h>

#include <boost/test/unit_test.hpp>

#include <limits>

BOOST_FIXTURE_TEST_SUITE(addressindex_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(height_ranges_across_assets)
{
    SetDataDir("addressindex");
    CBlockTreeDB db(1 << 20, /*fMemory=*/true);
    uint160 address;
    address.SetHex("02");
    const std::string assetA(64, 'a');
    const std::string assetB(64, 'b');
    const int maxHeight = std::numeric_limits<int>::max();
    // Index order: serialized asset first, then height (not height across assets).
    const std::vector<std::pair<std::string, int>> rows{
        {"RTM", 100}, {"RTM", 137}, {"RTM", 138}, {"RTM", 200},
        {assetA, 90}, {assetA, 137}, {assetA, 138}, {assetA, 200}, {assetA, maxHeight},
        {assetB, 80}, {assetB, 137}, {assetB, 138},
    };
    std::vector<std::pair<CAddressIndexKey, CAmount>> entries;
    for (size_t i = 0; i < rows.size(); ++i) {
        entries.emplace_back(CAddressIndexKey(1, address, rows[i].first, rows[i].second,
                                              i % 2, uint256S("01"), i, i % 2),
                             i % 2 ? -CAmount(i + 1) : CAmount(i + 1));
    }
    BOOST_REQUIRE(db.WriteAddressIndex(entries));
    for (const auto& hash : {"01", "03"}) {
        uint160 neighbor;
        neighbor.SetHex(hash);
        BOOST_REQUIRE(db.WriteAddressIndex({{CAddressIndexKey(1, neighbor, 137, 0, uint256S("02"), 0, false), 999}}));
    }

    const struct {
        int start;
        int end;
        std::vector<size_t> expected;
    } cases[]{
        {maxHeight, maxHeight, {8}},
        {137, 138, {1, 2, 5, 6, 10, 11}},
        {138, 138, {2, 6, 11}},
        {139, 199, {}},
        {1, 79, {}},
        {200, 200, {3, 7}},
        {201, maxHeight, {8}},
        {200, 137, {}},
        {0, 138, {0, 1, 2, 4, 5, 6, 9, 10, 11}},
        {0, 0, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
        // A lone start has historically been ignored; RPCs require both bounds.
        {137, 0, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    };
    for (const auto& test : cases) {
        BOOST_TEST_CONTEXT("start=" << test.start << ", end=" << test.end) {
            std::vector<std::pair<CAddressIndexKey, CAmount>> actual;
            BOOST_REQUIRE(db.ReadAddressIndex(address, 1, actual, test.start, test.end));
            BOOST_REQUIRE_EQUAL(actual.size(), test.expected.size());
            for (size_t i = 0; i < actual.size(); ++i) {
                const auto& expected = entries[test.expected[i]];
                BOOST_CHECK_EQUAL(actual[i].first.type, expected.first.type);
                BOOST_CHECK(actual[i].first.hashBytes == expected.first.hashBytes);
                BOOST_CHECK_EQUAL(actual[i].first.asset, expected.first.asset);
                BOOST_CHECK_EQUAL(actual[i].first.blockHeight, expected.first.blockHeight);
                BOOST_CHECK_EQUAL(actual[i].first.txindex, expected.first.txindex);
                BOOST_CHECK(actual[i].first.txhash == expected.first.txhash);
                BOOST_CHECK_EQUAL(actual[i].first.index, expected.first.index);
                BOOST_CHECK_EQUAL(actual[i].first.spending, expected.first.spending);
                BOOST_CHECK_EQUAL(actual[i].second, expected.second);
            }
        }
    }

    uint160 missing;
    missing.SetHex("04");
    std::vector<std::pair<CAddressIndexKey, CAmount>> actual;
    BOOST_REQUIRE(db.ReadAddressIndex(missing, 1, actual, 137, 138));
    BOOST_CHECK(actual.empty());
}

BOOST_AUTO_TEST_CASE(address_type_boundary)
{
    SetDataDir("addressindex");
    CBlockTreeDB db(1 << 20, /*fMemory=*/true);
    uint160 address;
    address.SetHex(std::string(40, 'f'));
    // The last type-1 address is immediately followed by the same hash at type 2.
    BOOST_REQUIRE(db.WriteAddressIndex({
        {CAddressIndexKey(1, address, 100, 0, uint256S("01"), 0, false), 11},
        {CAddressIndexKey(1, address, 200, 0, uint256S("02"), 0, false), 12},
        {CAddressIndexKey(2, address, 100, 0, uint256S("03"), 0, false), 21},
        {CAddressIndexKey(2, address, 200, 0, uint256S("04"), 0, false), 22},
    }));

    const struct {
        int start;
        int end;
        size_t count;
    } cases[]{{0, 0, 2}, {100, 100, 1}, {101, 199, 0}, {200, 200, 1}, {0, 100, 1}};
    for (int type : {1, 2}) {
        for (const auto& test : cases) {
            BOOST_TEST_CONTEXT("type=" << type << ", start=" << test.start << ", end=" << test.end) {
                std::vector<std::pair<CAddressIndexKey, CAmount>> actual;
                BOOST_REQUIRE(db.ReadAddressIndex(address, type, actual, test.start, test.end));
                BOOST_CHECK_EQUAL(actual.size(), test.count);
                for (const auto& entry : actual) {
                    BOOST_CHECK_EQUAL(entry.first.type, type);
                    BOOST_CHECK(entry.first.hashBytes == address);
                }
            }
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
