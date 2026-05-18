// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <evo/cbtx.h>
#include <chainparams.h>
#include <streams.h>
#include <uint256.h>
#include <update/update.h>

#include <boost/test/unit_test.hpp>

#include <vector>

/**
 * D2 increment 1 — CCbTx v3 EVM-commitment fields.
 *
 * D2 commits the EVM consensus roots (stateRoot / receiptsRoot /
 * baseFee) in the coinbase special tx, NOT the 80-byte CBlockHeader,
 * reusing the version-gated additive serialization DIP0008 already
 * used to add merkleRootQuorums at v2.
 *
 * The single most important property of this increment is that it is
 * INERT pre-activation: a v2 CCbTx must serialize byte-for-byte
 * exactly as before (the v3 fields must not leak into the wire form),
 * so existing blocks are unchanged. v3 must round-trip all fields.
 */

namespace {

uint256 W(uint8_t fill)
{
    std::vector<unsigned char> r(32, fill);
    return uint256(r);
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(cbtx_evm_commit_tests, BasicTestingSetup)

// A v2 CCbTx must serialize to exactly the pre-v3 wire layout:
//   nVersion(2) + nHeight(4) + merkleRootMNList(32) +
//   merkleRootQuorums(32) = 70 bytes. The v3 fields must NOT appear.
BOOST_AUTO_TEST_CASE(v2_serialization_is_byte_identical_and_inert)
{
    CCbTx cb;
    cb.nVersion = 2;
    cb.nHeight = 123456;
    cb.merkleRootMNList = W(0xAA);
    cb.merkleRootQuorums = W(0xBB);
    // Set v3 fields to non-default to prove they are NOT written at v2.
    cb.evmStateRoot = W(0xCC);
    cb.evmReceiptsRoot = W(0xDD);
    cb.evmBaseFee = 0xDEADBEEF;
    cb.evmGasUsed = 0xCAFE;

    CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
    s << cb;
    BOOST_CHECK_EQUAL(s.size(), 2u + 4u + 32u + 32u);

    // Round-trips back, and the v3 fields come back DEFAULT (they were
    // never on the wire) — not the 0xCC/0xDD/0xDEADBEEF we set.
    CCbTx out;
    s >> out;
    BOOST_CHECK_EQUAL(out.nVersion, 2);
    BOOST_CHECK_EQUAL(out.nHeight, 123456);
    BOOST_CHECK(out.merkleRootMNList == W(0xAA));
    BOOST_CHECK(out.merkleRootQuorums == W(0xBB));
    BOOST_CHECK(out.evmStateRoot == uint256());
    BOOST_CHECK(out.evmReceiptsRoot == uint256());
    BOOST_CHECK_EQUAL(out.evmBaseFee, 0u);
    BOOST_CHECK_EQUAL(out.evmGasUsed, 0u);
}

// A v1 CCbTx (pre-DIP0008) must still be exactly nVersion+nHeight+
// merkleRootMNList = 38 bytes — guards the lower bound too.
BOOST_AUTO_TEST_CASE(v1_serialization_unaffected)
{
    CCbTx cb;
    cb.nVersion = 1;
    cb.nHeight = 7;
    cb.merkleRootMNList = W(0x11);

    CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
    s << cb;
    BOOST_CHECK_EQUAL(s.size(), 2u + 4u + 32u);
}

// v3 carries the EVM commitments and round-trips every field. Wire
// size = v2 (70) + evmStateRoot(32) + evmReceiptsRoot(32) +
// evmBaseFee(8) = 142 bytes.
BOOST_AUTO_TEST_CASE(v3_round_trips_evm_commitments)
{
    CCbTx cb;
    cb.nVersion = CCbTx::EVM_COMMIT_VERSION;  // 3
    cb.nHeight = 999;
    cb.merkleRootMNList = W(0x01);
    cb.merkleRootQuorums = W(0x02);
    cb.evmStateRoot = W(0x33);
    cb.evmReceiptsRoot = W(0x44);
    cb.evmBaseFee = 1234567890123ULL;
    cb.evmGasUsed = 9876543210ULL;

    CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
    s << cb;
    // v2 (70) + evmStateRoot(32) + evmReceiptsRoot(32) +
    // evmBaseFee(8) + evmGasUsed(8) = 150 bytes.
    BOOST_CHECK_EQUAL(s.size(), 2u + 4u + 32u + 32u + 32u + 32u + 8u + 8u);

    CCbTx out;
    s >> out;
    BOOST_CHECK_EQUAL((int)out.nVersion, (int)CCbTx::EVM_COMMIT_VERSION);
    BOOST_CHECK_EQUAL(out.nHeight, 999);
    BOOST_CHECK(out.merkleRootMNList == W(0x01));
    BOOST_CHECK(out.merkleRootQuorums == W(0x02));
    BOOST_CHECK(out.evmStateRoot == W(0x33));
    BOOST_CHECK(out.evmReceiptsRoot == W(0x44));
    BOOST_CHECK_EQUAL(out.evmBaseFee, 1234567890123ULL);
    BOOST_CHECK_EQUAL(out.evmGasUsed, 9876543210ULL);
}

// CURRENT_VERSION stays 2 in this increment: v3 is intentionally not
// yet the default / not yet accepted by CheckCbTx. This pins that so
// a premature bump (which would activate v3 before the hard-fork
// gate) fails loudly.
BOOST_AUTO_TEST_CASE(current_version_still_2_until_activation_increment)
{
    BOOST_CHECK_EQUAL((int)CCbTx::CURRENT_VERSION, 2);
    BOOST_CHECK_EQUAL((int)CCbTx::EVM_COMMIT_VERSION, 3);
    CCbTx fresh;
    BOOST_CHECK_EQUAL((int)fresh.nVersion, (int)CCbTx::CURRENT_VERSION);
}

// Increment 2: the EVM_COMMIT activation gate exists but is
// UNREGISTERED on every network → IsEvmCommitActive() is false, so
// CheckCbTx still caps at v2 (v3 rejected). This pins the inertness.
BOOST_AUTO_TEST_CASE(evm_commit_gate_is_inert_until_scheduled)
{
    BOOST_CHECK_EQUAL((int)EUpdate::EVM_COMMIT, 4);
    BOOST_CHECK_EQUAL((int)EUpdate::MAX_VERSION_BITS_DEPLOYMENTS, 5);
    // Unregistered on regtest (and everywhere) → never active.
    BOOST_CHECK(!Updates().IsEvmCommitActive(nullptr));
    // EVM execution gate and the D2 commitment gate are distinct
    // enum slots (regtest force-activates EVM at 0 but must NOT
    // thereby require committed roots).
    BOOST_CHECK((int)EUpdate::EVM != (int)EUpdate::EVM_COMMIT);
}

BOOST_AUTO_TEST_SUITE_END()
