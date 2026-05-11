// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <evm/smoke.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <vector>

// EVMC_SUCCESS = 0 — see evmc/evmc.h. We don't include evmc here to keep
// the smoke API surface decoupled from the evmc C interface; the smoke
// API exposes status as int32_t on purpose.
static constexpr int32_t EVMC_SUCCESS_VALUE = 0;

BOOST_FIXTURE_TEST_SUITE(evm_smoke_tests, BasicTestingSetup)

/**
 * Test 1 — empty contract.
 *
 * An empty bytecode sequence is legal in the EVM: execution falls off
 * immediately with no gas consumed beyond the intrinsic cost. Status
 * should be EVMC_SUCCESS and the return data should be empty.
 */
BOOST_AUTO_TEST_CASE(empty_contract_succeeds)
{
    const std::vector<uint8_t> bytecode;
    const std::vector<uint8_t> calldata;

    evm::SmokeResult r = evm::EvmSmokeExecute(bytecode, calldata);

    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS_VALUE);
    BOOST_CHECK(r.return_data.empty());
}

/**
 * Test 2 — ADD then RETURN.
 *
 * Bytecode:
 *   PUSH1 0x05    (60 05)
 *   PUSH1 0x04    (60 04)
 *   ADD           (01)         ; stack: [9]
 *   PUSH1 0x00    (60 00)      ; stack: [9, 0]
 *   MSTORE        (52)         ; memory[0..32] = uint256(9)
 *   PUSH1 0x20    (60 20)      ; size = 32
 *   PUSH1 0x00    (60 00)      ; offset = 0
 *   RETURN        (F3)         ; return 32 bytes from memory[0]
 *
 * Expected: 32-byte return_data with a big-endian uint256(9), i.e. all zero
 * bytes except the last byte = 0x09.
 */
BOOST_AUTO_TEST_CASE(add_then_return_yields_nine)
{
    const std::vector<uint8_t> bytecode = {
        0x60, 0x05,
        0x60, 0x04,
        0x01,
        0x60, 0x00,
        0x52,
        0x60, 0x20,
        0x60, 0x00,
        0xF3,
    };
    const std::vector<uint8_t> calldata;

    evm::SmokeResult r = evm::EvmSmokeExecute(bytecode, calldata);

    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS_VALUE);
    BOOST_REQUIRE_EQUAL(r.return_data.size(), 32U);

    // First 31 bytes must be zero, last byte must be 0x09.
    for (size_t i = 0; i < 31; ++i) {
        BOOST_CHECK_EQUAL(static_cast<int>(r.return_data[i]), 0);
    }
    BOOST_CHECK_EQUAL(static_cast<int>(r.return_data[31]), 0x09);
}

/**
 * Test 3 — KECCAK256 of empty input.
 *
 * Bytecode:
 *   PUSH1 0x00    (60 00)      ; size of input  = 0
 *   PUSH1 0x00    (60 00)      ; offset of input = 0
 *   KECCAK256     (20)         ; stack: [keccak256("")]
 *   PUSH1 0x00    (60 00)      ; offset to MSTORE
 *   MSTORE        (52)         ; memory[0..32] = hash
 *   PUSH1 0x20    (60 20)      ; size = 32
 *   PUSH1 0x00    (60 00)      ; offset = 0
 *   RETURN        (F3)
 *
 * Expected: 32 bytes equal to the well-known keccak256("") constant
 *   c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470
 *
 * This validates that the SHA3 / KECCAK256 opcode (0x20) is wired and
 * produces the standard Keccak-256 (NOT FIPS-202 SHA3-256) result —
 * a common source of integration bugs.
 */
BOOST_AUTO_TEST_CASE(keccak256_empty_matches_known_constant)
{
    const std::vector<uint8_t> bytecode = {
        0x60, 0x00,
        0x60, 0x00,
        0x20,
        0x60, 0x00,
        0x52,
        0x60, 0x20,
        0x60, 0x00,
        0xF3,
    };
    const std::vector<uint8_t> calldata;

    // Well-known: keccak256(b"") = c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470
    const std::vector<uint8_t> expected_hash = {
        0xc5, 0xd2, 0x46, 0x01, 0x86, 0xf7, 0x23, 0x3c,
        0x92, 0x7e, 0x7d, 0xb2, 0xdc, 0xc7, 0x03, 0xc0,
        0xe5, 0x00, 0xb6, 0x53, 0xca, 0x82, 0x27, 0x3b,
        0x7b, 0xfa, 0xd8, 0x04, 0x5d, 0x85, 0xa4, 0x70,
    };

    evm::SmokeResult r = evm::EvmSmokeExecute(bytecode, calldata);

    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS_VALUE);
    BOOST_REQUIRE_EQUAL(r.return_data.size(), 32U);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        r.return_data.begin(), r.return_data.end(),
        expected_hash.begin(), expected_hash.end());
}

BOOST_AUTO_TEST_SUITE_END()
