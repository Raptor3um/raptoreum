// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <evm/hashing.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

/**
 * Phase 2.3b hashing primitives:
 *
 *   - Keccak256 produces the canonical Ethereum digest (NOT FIPS
 *     SHA3-256). Pinned against well-known constants.
 *
 *   - ContractAddressFromCreate produces the standard Ethereum CREATE
 *     address from (sender, nonce). Pinned against the test vectors
 *     widely cited in the Ethereum wiki (sender
 *     0x6ac7ea33f8831ea9dcc53393aaa88b25a785dbf0 with nonces 0..3).
 *     This matches what geth / erigon / nethermind produce, so a
 *     contract we deploy ends up at the same address it would on
 *     Ethereum mainnet for the same (sender, nonce) pair.
 */

namespace {

uint160 AddrFromHex20(const std::string& hex)
{
    BOOST_REQUIRE_EQUAL(hex.size(), 40U);
    std::vector<unsigned char> bytes(20);
    for (size_t i = 0; i < 20; ++i) {
        unsigned int b;
        BOOST_REQUIRE_EQUAL(std::sscanf(hex.c_str() + 2 * i, "%2x", &b), 1);
        bytes[i] = static_cast<unsigned char>(b);
    }
    return uint160(bytes);
}

std::string Hex20From(const uint160& addr)
{
    char buf[41]{};
    for (int i = 0; i < 20; ++i) {
        std::sprintf(buf + 2 * i, "%02x", *(addr.begin() + i));
    }
    return std::string(buf);
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(evm_hashing_tests, BasicTestingSetup)

// ----------------------------------------------------------------------------
// Keccak256: pinned constants
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(keccak256_of_empty_matches_canonical)
{
    // The single most-cited Keccak-256 constant in Ethereum's
    // documentation. EVM accounts that have never deployed code carry
    // this as their codeHash. Phase 0 already pinned it from the
    // EVM-side via SHA3 opcode; this pins it from the C++ wrapper.
    uint256 h = evm::Keccak256(std::vector<uint8_t>{});
    char buf[65]{};
    for (int i = 0; i < 32; ++i) {
        std::sprintf(buf + 2 * i, "%02x", *(h.begin() + i));
    }
    BOOST_CHECK_EQUAL(
        std::string(buf),
        "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
}

BOOST_AUTO_TEST_CASE(keccak256_of_abc_matches_canonical)
{
    // From the Ethereum yellow paper test vectors: keccak256("abc") =
    //   4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45
    const std::string text = "abc";
    uint256 h = evm::Keccak256(
        reinterpret_cast<const uint8_t*>(text.data()), text.size());
    char buf[65]{};
    for (int i = 0; i < 32; ++i) {
        std::sprintf(buf + 2 * i, "%02x", *(h.begin() + i));
    }
    BOOST_CHECK_EQUAL(
        std::string(buf),
        "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45");
}

// ----------------------------------------------------------------------------
// CREATE address derivation: Ethereum-standard test vectors
// ----------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(create_address_nonce_0)
{
    // From the Ethereum wiki / yellow paper test vectors.
    uint160 sender = AddrFromHex20("6ac7ea33f8831ea9dcc53393aaa88b25a785dbf0");
    uint160 addr = evm::ContractAddressFromCreate(sender, 0);
    BOOST_CHECK_EQUAL(Hex20From(addr),
                      "cd234a471b72ba2f1ccf0a70fcaba648a5eecd8d");
}

BOOST_AUTO_TEST_CASE(create_address_nonce_1)
{
    uint160 sender = AddrFromHex20("6ac7ea33f8831ea9dcc53393aaa88b25a785dbf0");
    uint160 addr = evm::ContractAddressFromCreate(sender, 1);
    BOOST_CHECK_EQUAL(Hex20From(addr),
                      "343c43a37d37dff08ae8c4a11544c718abb4fcf8");
}

BOOST_AUTO_TEST_CASE(create_address_nonce_2)
{
    uint160 sender = AddrFromHex20("6ac7ea33f8831ea9dcc53393aaa88b25a785dbf0");
    uint160 addr = evm::ContractAddressFromCreate(sender, 2);
    BOOST_CHECK_EQUAL(Hex20From(addr),
                      "f778b86fa74e846c4f0a1fbd1335fe81c00a0c91");
}

BOOST_AUTO_TEST_CASE(create_address_nonce_3)
{
    uint160 sender = AddrFromHex20("6ac7ea33f8831ea9dcc53393aaa88b25a785dbf0");
    uint160 addr = evm::ContractAddressFromCreate(sender, 3);
    BOOST_CHECK_EQUAL(Hex20From(addr),
                      "fffd933a0bc612844eaf0c6fe3e5b8e9b6c1d19c");
}

BOOST_AUTO_TEST_SUITE_END()
