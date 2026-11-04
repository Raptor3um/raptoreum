// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <net.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

// CConnman::TryMarkVerified's cap check needs a live CNode/CConnman pair (vNodes, cs_vNodes) to
// exercise through the real class -- this codebase's test suite does not construct one, same
// limitation quorums_dkgsessionmgr_tests.cpp documents for an analogous case.
// WouldExceedVerifiedInboundCap is the part that doesn't need any of that: a pure function of an
// already-gathered count, pulled out so the boundary can be pinned directly.

BOOST_AUTO_TEST_SUITE(evo_mnauth_conncap_tests)

BOOST_AUTO_TEST_CASE(cap_allows_room_below_the_bound)
{
    uint256 proRegTxHash = uint256S("1");
    std::vector<uint256> existing(MAX_VERIFIED_INBOUND_PER_PROTX - 1, proRegTxHash);

    BOOST_CHECK(!WouldExceedVerifiedInboundCap(existing, proRegTxHash));
}

BOOST_AUTO_TEST_CASE(cap_rejects_at_the_boundary)
{
    uint256 proRegTxHash = uint256S("1");
    std::vector<uint256> existing(MAX_VERIFIED_INBOUND_PER_PROTX, proRegTxHash);

    BOOST_CHECK(WouldExceedVerifiedInboundCap(existing, proRegTxHash));
}

BOOST_AUTO_TEST_CASE(cap_is_independent_per_identity)
{
    uint256 atTheCap = uint256S("1");
    uint256 freshIdentity = uint256S("2");
    std::vector<uint256> existing(MAX_VERIFIED_INBOUND_PER_PROTX, atTheCap);

    // A different proRegTxHash is unaffected by how many connections another identity already
    // holds -- the cap is per-identity, not a global ceiling on verified inbound connections.
    BOOST_CHECK(!WouldExceedVerifiedInboundCap(existing, freshIdentity));
}

BOOST_AUTO_TEST_CASE(cap_tolerates_a_legitimate_reconnect)
{
    // The cap is deliberately not 1: a reconnect racing a socket not yet detected as dead must
    // still be able to produce a second verified inbound leg for the same identity.
    BOOST_CHECK(MAX_VERIFIED_INBOUND_PER_PROTX >= 2);

    uint256 proRegTxHash = uint256S("1");
    std::vector<uint256> oneExistingLeg(1, proRegTxHash);

    BOOST_CHECK(!WouldExceedVerifiedInboundCap(oneExistingLeg, proRegTxHash));
}

BOOST_AUTO_TEST_SUITE_END()
