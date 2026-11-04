// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <governance/governance-vote.h>
#include <primitives/transaction.h>
#include <serialize.h>
#include <streams.h>
#include <uint256.h>
#include <version.h>

#include <boost/test/unit_test.hpp>

// CGovernanceVote::vchSig is wrapped in LIMITED_VECTOR for network reads only (disk/hash/write
// paths keep the unbounded default -- they're never fed attacker-controlled bytes the way a P2P
// read is). Cover the real message type directly: an oversized claim must be rejected before
// allocation, and the exact-size check (65-byte ECDSA or 96-byte BLS, nothing else) must hold.

namespace {
    void WriteVoteHeader(CDataStream &ss) {
        ss << COutPoint(uint256S("1"), 0) << uint256S("1") << int{1} /*outcome*/ << int{1} /*signal*/
           << int64_t{1700000000};
    }

    CDataStream MakeVoteWire(size_t sigLen) {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        WriteVoteHeader(ss);
        ss << std::vector<unsigned char>(sigLen, 0xAA);
        return ss;
    }

    bool ThrowsOnRead(CDataStream &ss, std::string *what = nullptr) {
        CGovernanceVote vote;
        try {
            ss >> vote;
        } catch (const std::ios_base::failure &e) {
            if (what) *what = e.what();
            return true;
        }
        return false;
    }
} // namespace

BOOST_AUTO_TEST_SUITE(governance_vote_wire_tests)

BOOST_AUTO_TEST_CASE(oversized_claim_rejected_before_allocation)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    WriteVoteHeader(ss);
    WriteCompactSize(ss, 1000);
    // No signature payload follows.

    std::string what;
    BOOST_CHECK(ThrowsOnRead(ss, &what));
    BOOST_CHECK_MESSAGE(what.find("Vector length limit exceeded") != std::string::npos,
                         "Expected a pre-allocation rejection, got: " + what);
}

BOOST_AUTO_TEST_CASE(wrong_exact_size_rejected)
{
    // 50 bytes: under the 96-byte cap, but neither of the two legitimate encodings (65 or 96).
    CDataStream ss = MakeVoteWire(50);
    std::string what;
    BOOST_CHECK(ThrowsOnRead(ss, &what));
    BOOST_CHECK_MESSAGE(what.find("bad governance vote signature size") != std::string::npos,
                         "Expected the exact-size rejection, got: " + what);
}

BOOST_AUTO_TEST_CASE(legitimate_ecdsa_signature_round_trips)
{
    CDataStream ss = MakeVoteWire(CGovernanceVote::COMPACT_SIG_SIZE);
    BOOST_CHECK(!ThrowsOnRead(ss));
}

BOOST_AUTO_TEST_CASE(legitimate_bls_signature_round_trips)
{
    CDataStream ss = MakeVoteWire(CGovernanceVote::BLS_SIG_SIZE);
    BOOST_CHECK(!ThrowsOnRead(ss));
}

BOOST_AUTO_TEST_SUITE_END()
