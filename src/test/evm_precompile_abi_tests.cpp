// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <evm/precompiles.h>

#include <evmc/evmc.hpp>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <vector>

/**
 * Phase-4 precompile ABI codec + address-recognition tests.
 *
 * The ABI read/write codec and IsPrecompileAddress are the shared
 * foundation of ALL four RTM-native precompiles (Smart-Asset ERC-20,
 * LLMQ oracle, ChainLocks, masternode registry). They are
 * consensus-reachable (a contract CALLs a precompile address) yet had
 * zero coverage. A bug here — a wrong keccak selector slice, a missing
 * high-byte validation in AbiReadAddress (address spoofing), an
 * off-by-one in the dynamic-bytes pointer — would silently corrupt
 * every precompile. These tests pin the codec, including the function
 * selectors against the canonical Ethereum ERC-20 values (proving the
 * Smart-Asset precompile is genuinely ABI-compatible with
 * ethers/Solidity).
 */

namespace {

evmc::address AddrFromBytes(const std::vector<uint8_t>& b)
{
    evmc::address a{};
    for (size_t i = 0; i < 20 && i < b.size(); ++i) a.bytes[i] = b[i];
    return a;
}

// RTM-native fixed precompiles live at 0x00..00_0A_0X (byte[18]=0x0A,
// byte[19] in {1..4}) — i.e. 0x...0a01 .. 0x...0a04, per the plan.
evmc::address FixedPrecompile(uint8_t suffix)
{
    evmc::address a{};
    a.bytes[18] = 0x0A;
    a.bytes[19] = suffix;
    return a;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(evm_precompile_abi_tests, BasicTestingSetup)

// --- Function selectors match canonical Ethereum ERC-20 values -------
// These are the well-known, spec-fixed 4-byte selectors. Matching them
// proves AbiFunctionSelector computes keccak256(sig)[0:4] exactly as
// Solidity/ethers do — the precondition for ERC-20 ABI compatibility.
BOOST_AUTO_TEST_CASE(selectors_match_canonical_erc20)
{
    BOOST_CHECK_EQUAL(evm::AbiFunctionSelector("transfer(address,uint256)"),
                      0xa9059cbbU);
    BOOST_CHECK_EQUAL(evm::AbiFunctionSelector("balanceOf(address)"),
                      0x70a08231U);
    BOOST_CHECK_EQUAL(evm::AbiFunctionSelector("approve(address,uint256)"),
                      0x095ea7b3U);
    BOOST_CHECK_EQUAL(evm::AbiFunctionSelector("totalSupply()"),
                      0x18160dddU);
    BOOST_CHECK_EQUAL(
        evm::AbiFunctionSelector("transferFrom(address,address,uint256)"),
        0x23b872ddU);
    BOOST_CHECK_EQUAL(evm::AbiFunctionSelector("allowance(address,address)"),
                      0xdd62ed3eU);
    BOOST_CHECK_EQUAL(evm::AbiFunctionSelector("decimals()"), 0x313ce567U);
    BOOST_CHECK_EQUAL(evm::AbiFunctionSelector("symbol()"), 0x95d89b41U);
}

// --- Address recognition (A11 collision rule) ------------------------
BOOST_AUTO_TEST_CASE(precompile_address_recognition)
{
    // RTM-native fixed precompiles 0x...0a01..0x...0a04 are recognised.
    for (uint8_t s = 1; s <= 4; ++s) {
        BOOST_CHECK(evm::IsPrecompileAddress(FixedPrecompile(s)));
    }
    // 0x...0a05 (one past the range) is NOT a fixed precompile.
    BOOST_CHECK(!evm::IsPrecompileAddress(FixedPrecompile(5)));
    // Smart-Asset ERC-20 prefix 0xA55E7000_00000000... is recognised.
    BOOST_CHECK(evm::IsPrecompileAddress(AddrFromBytes(
        {0xA5, 0x5E, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
         0x99, 0xAA, 0xBB, 0xCC})));
    // A plain EOA-looking address is NOT a precompile.
    BOOST_CHECK(!evm::IsPrecompileAddress(AddrFromBytes(
        std::vector<uint8_t>(20, 0xAB))));
    // The all-zero address is not a precompile (suffix 0 < 0x01).
    BOOST_CHECK(!evm::IsPrecompileAddress(evmc::address{}));
    // Near-miss prefix (last prefix byte differs) is NOT asset-erc20.
    BOOST_CHECK(!evm::IsPrecompileAddress(AddrFromBytes(
        {0xA5, 0x5E, 0x70, 0x00, 0x00, 0x00, 0x00, 0x01,
         0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
         0x99, 0xAA, 0xBB, 0xCC})));
}

// --- Static word round-trips -----------------------------------------
BOOST_AUTO_TEST_CASE(uint64_read_write_round_trip)
{
    std::vector<uint8_t> buf;
    evm::AbiWriteUint64(buf, 0);
    evm::AbiWriteUint64(buf, 0x0123456789abcdefULL);
    BOOST_REQUIRE_EQUAL(buf.size(), 64U);

    uint64_t v0 = 1, v1 = 0;
    BOOST_CHECK(evm::AbiReadUint64(buf, 0, v0));
    BOOST_CHECK_EQUAL(v0, 0U);
    BOOST_CHECK(evm::AbiReadUint64(buf, 32, v1));
    BOOST_CHECK_EQUAL(v1, 0x0123456789abcdefULL);
}

BOOST_AUTO_TEST_CASE(address_read_write_round_trip)
{
    uint160 addr(std::vector<uint8_t>(20, 0x5A));
    std::vector<uint8_t> buf;
    evm::AbiWriteAddress(buf, addr);
    BOOST_REQUIRE_EQUAL(buf.size(), 32U);
    // High 12 bytes are zero padding.
    for (int i = 0; i < 12; ++i) BOOST_CHECK_EQUAL(buf[i], 0);

    uint160 out;
    BOOST_CHECK(evm::AbiReadAddress(buf, 0, out));
    BOOST_CHECK(out == addr);
}

BOOST_AUTO_TEST_CASE(bool_and_uint8_round_trip)
{
    std::vector<uint8_t> buf;
    evm::AbiWriteBool(buf, true);
    evm::AbiWriteBool(buf, false);
    evm::AbiWriteUint8(buf, 0xC3);
    bool b = false; uint8_t u = 0;
    BOOST_CHECK(evm::AbiReadBool(buf, 0, b) && b == true);
    BOOST_CHECK(evm::AbiReadBool(buf, 32, b) && b == false);
    BOOST_CHECK(evm::AbiReadUint8(buf, 64, u) && u == 0xC3);
}

// --- Validation: malformed words are rejected (security-critical) -----
BOOST_AUTO_TEST_CASE(read_address_rejects_dirty_high_bytes)
{
    // A 32-byte word whose high 12 bytes are NOT zero is not a valid
    // ABI address — must be rejected (else a caller could smuggle data
    // in the padding / spoof a different value).
    std::vector<uint8_t> buf(32, 0);
    buf[0] = 0x01;  // dirty high byte
    for (int i = 12; i < 32; ++i) buf[i] = 0x5A;
    uint160 out;
    BOOST_CHECK(!evm::AbiReadAddress(buf, 0, out));
}

BOOST_AUTO_TEST_CASE(read_uint64_rejects_overflow)
{
    // A word with any of the high 24 bytes set exceeds uint64 — reject.
    std::vector<uint8_t> buf(32, 0);
    buf[23] = 0x01;  // bit just above the uint64 range
    for (int i = 24; i < 32; ++i) buf[i] = 0xFF;
    uint64_t v = 0;
    BOOST_CHECK(!evm::AbiReadUint64(buf, 0, v));
}

BOOST_AUTO_TEST_CASE(read_word_rejects_out_of_bounds)
{
    std::vector<uint8_t> buf(20, 0);  // shorter than a 32-byte word
    uint256 w;
    BOOST_CHECK(!evm::AbiReadWord(buf, 0, w));
    uint64_t v; uint160 a;
    BOOST_CHECK(!evm::AbiReadUint64(buf, 0, v));
    BOOST_CHECK(!evm::AbiReadAddress(buf, 0, a));
}

// --- Dynamic bytes round-trip (offset, length, padded payload) -------
BOOST_AUTO_TEST_CASE(dynamic_bytes_round_trip)
{
    const std::vector<uint8_t> payload = {0xde, 0xad, 0xbe, 0xef, 0x01};

    // Encode as a single dynamic arg: head slot (offset=32) + tail.
    std::vector<uint8_t> buf;
    evm::AbiWriteDynamicHead(buf, 32);             // head: offset to tail
    evm::AbiWriteDynamicBytesTail(buf, payload);   // tail: len + padded
    // 32 (head) + 32 (length) + 32 (payload padded to 32) = 96.
    BOOST_REQUIRE_EQUAL(buf.size(), 96U);

    std::vector<uint8_t> out;
    BOOST_CHECK(evm::AbiReadDynamicBytes(buf, 0, out));
    BOOST_CHECK(out == payload);
}

BOOST_AUTO_TEST_CASE(empty_dynamic_bytes_round_trip)
{
    std::vector<uint8_t> buf;
    evm::AbiWriteDynamicHead(buf, 32);
    evm::AbiWriteDynamicBytesTail(buf, {});
    std::vector<uint8_t> out{0x01};  // pre-fill to prove it clears
    BOOST_CHECK(evm::AbiReadDynamicBytes(buf, 0, out));
    BOOST_CHECK(out.empty());
}

// --- Result builders --------------------------------------------------
BOOST_AUTO_TEST_CASE(precompile_result_builders)
{
    const evmc::Result fail = evm::PrecompileFailure(100000);
    BOOST_CHECK(fail.status_code == EVMC_FAILURE);
    BOOST_CHECK_EQUAL(fail.gas_left, 0);  // failure consumes all gas

    std::vector<uint8_t> out = {0x01, 0x02, 0x03};
    const evmc::Result ok = evm::PrecompileSuccess(100000, 5000, out);
    BOOST_CHECK(ok.status_code == EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(ok.gas_left, 95000);  // gasLimit - gasUsed
    BOOST_REQUIRE_EQUAL(ok.output_size, 3U);
    BOOST_CHECK_EQUAL(ok.output_data[0], 0x01);
}

BOOST_AUTO_TEST_SUITE_END()
