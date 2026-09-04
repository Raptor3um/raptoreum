// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/precompiles.h>

#include <evm/hashing.h>

#include <cstring>

namespace evm {

// ----------------------------------------------------------------------
// Address recognition
// ----------------------------------------------------------------------

namespace {

// Compare the high N bytes of `addr` against a prefix template.
// Returns true on exact match.
bool AddrHasPrefix(const evmc::address& addr,
                   const std::vector<uint8_t>& prefix)
{
    if (prefix.size() > 20) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (addr.bytes[i] != prefix[i]) return false;
    }
    return true;
}

// The fixed-address precompiles: 0x00..00a0X, X in {1, 2, 3, 4}.
bool IsFixedPrecompile(const evmc::address& addr)
{
    // Bytes 0..17 must be zero; byte 18 == 0x0A; byte 19 in {1..4}.
    for (size_t i = 0; i < 18; ++i) {
        if (addr.bytes[i] != 0x00) return false;
    }
    if (addr.bytes[18] != 0x0A) return false;
    const uint8_t suffix = addr.bytes[19];
    return suffix >= 0x01 && suffix <= 0x04;
}

// Smart-Asset ERC-20 prefix (A11 collision rule): the high 8 bytes
// spell "A55E7000_00000000" — anything in this range is a Phase 4.1
// per-asset precompile.
bool IsAssetErc20PrecompileAddress(const evmc::address& addr)
{
    static const std::vector<uint8_t> kPrefix = {
        0xA5, 0x5E, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    return AddrHasPrefix(addr, kPrefix);
}

} // anonymous namespace

bool IsPrecompileAddress(const evmc::address& addr)
{
    return IsFixedPrecompile(addr) || IsAssetErc20PrecompileAddress(addr);
}

// ----------------------------------------------------------------------
// ABI codec
// ----------------------------------------------------------------------

uint32_t AbiFunctionSelector(const std::string& signature)
{
    const std::vector<uint8_t> bytes(signature.begin(), signature.end());
    const uint256 h = Keccak256(bytes);
    uint32_t sel = 0;
    sel |= static_cast<uint32_t>(*(h.begin() + 0)) << 24;
    sel |= static_cast<uint32_t>(*(h.begin() + 1)) << 16;
    sel |= static_cast<uint32_t>(*(h.begin() + 2)) << 8;
    sel |= static_cast<uint32_t>(*(h.begin() + 3));
    return sel;
}

bool AbiReadWord(const std::vector<uint8_t>& input, size_t byteOffset,
                uint256& outWord)
{
    if (byteOffset + 32 > input.size()) return false;
    outWord.SetNull();
    std::memcpy(outWord.begin(), input.data() + byteOffset, 32);
    return true;
}

bool AbiReadAddress(const std::vector<uint8_t>& input, size_t byteOffset,
                   uint160& outAddr)
{
    uint256 word;
    if (!AbiReadWord(input, byteOffset, word)) return false;
    // High 12 bytes must be zero per the address ABI rule.
    for (int i = 0; i < 12; ++i) {
        if (*(word.begin() + i) != 0) return false;
    }
    std::memcpy(outAddr.begin(), word.begin() + 12, 20);
    return true;
}

bool AbiReadUint64(const std::vector<uint8_t>& input, size_t byteOffset,
                  uint64_t& outValue)
{
    uint256 word;
    if (!AbiReadWord(input, byteOffset, word)) return false;
    // High 24 bytes must be zero — anything else is > uint64 max.
    for (int i = 0; i < 24; ++i) {
        if (*(word.begin() + i) != 0) return false;
    }
    outValue = 0;
    for (int i = 24; i < 32; ++i) {
        outValue = (outValue << 8) | static_cast<uint64_t>(*(word.begin() + i));
    }
    return true;
}

bool AbiReadUint8(const std::vector<uint8_t>& input, size_t byteOffset,
                 uint8_t& outValue)
{
    uint256 word;
    if (!AbiReadWord(input, byteOffset, word)) return false;
    for (int i = 0; i < 31; ++i) {
        if (*(word.begin() + i) != 0) return false;
    }
    outValue = *(word.begin() + 31);
    return true;
}

bool AbiReadBool(const std::vector<uint8_t>& input, size_t byteOffset,
                bool& outValue)
{
    uint8_t v = 0;
    if (!AbiReadUint8(input, byteOffset, v)) return false;
    if (v != 0 && v != 1) return false;
    outValue = (v == 1);
    return true;
}

bool AbiReadDynamicBytes(const std::vector<uint8_t>& input,
                        size_t headByteOffset,
                        std::vector<uint8_t>& outBytes)
{
    uint64_t tailOffset = 0;
    if (!AbiReadUint64(input, headByteOffset, tailOffset)) return false;
    // The tail offset is measured from the start of the call's
    // argument region (i.e., from byte 4 of the input — after the
    // selector). The caller passes input minus the selector, so
    // we treat input.data() as offset 0.
    if (tailOffset > input.size()) return false;
    uint64_t length = 0;
    if (!AbiReadUint64(input, tailOffset, length)) return false;
    const size_t dataStart = tailOffset + 32;
    if (dataStart + length > input.size()) return false;
    outBytes.assign(input.data() + dataStart,
                    input.data() + dataStart + length);
    return true;
}

void AbiWriteWord(std::vector<uint8_t>& out, const uint256& word)
{
    out.insert(out.end(), word.begin(), word.begin() + 32);
}

void AbiWriteUint64(std::vector<uint8_t>& out, uint64_t value)
{
    std::vector<uint8_t> word(32, 0);
    for (int i = 0; i < 8; ++i) {
        word[31 - i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
    }
    out.insert(out.end(), word.begin(), word.end());
}

void AbiWriteUint8(std::vector<uint8_t>& out, uint8_t value)
{
    std::vector<uint8_t> word(32, 0);
    word[31] = value;
    out.insert(out.end(), word.begin(), word.end());
}

void AbiWriteBool(std::vector<uint8_t>& out, bool value)
{
    AbiWriteUint8(out, value ? 1 : 0);
}

void AbiWriteAddress(std::vector<uint8_t>& out, const uint160& addr)
{
    std::vector<uint8_t> word(32, 0);
    std::memcpy(word.data() + 12, addr.begin(), 20);
    out.insert(out.end(), word.begin(), word.end());
}

void AbiWriteDynamicHead(std::vector<uint8_t>& out, uint64_t tailOffset)
{
    AbiWriteUint64(out, tailOffset);
}

void AbiWriteDynamicBytesTail(std::vector<uint8_t>& out,
                             const std::vector<uint8_t>& bytes)
{
    AbiWriteUint64(out, static_cast<uint64_t>(bytes.size()));
    out.insert(out.end(), bytes.begin(), bytes.end());
    // Pad to multiple of 32.
    const size_t pad = (32 - (bytes.size() % 32)) % 32;
    out.insert(out.end(), pad, 0);
}

evmc::Result PrecompileFailure(int64_t gasLimit)
{
    evmc::Result r;
    r.status_code = EVMC_FAILURE;
    r.gas_left = 0;
    (void)gasLimit;
    return r;
}

evmc::Result PrecompileSuccess(int64_t gasLimit, int64_t gasUsed,
                               std::vector<uint8_t> output)
{
    int64_t gasLeft = gasLimit - gasUsed;
    if (gasLeft < 0) gasLeft = 0;
    // The evmc::Result(status, gas_left, gas_refund, output_data,
    // output_size) constructor copies the output bytes internally
    // and installs its own release thunk, so we don't have to
    // touch the private release field.
    return evmc::Result(EVMC_SUCCESS, gasLeft, /*gas_refund=*/ 0,
                        output.empty() ? nullptr : output.data(),
                        output.size());
}

// ----------------------------------------------------------------------
// Top-level dispatch
// ----------------------------------------------------------------------
//
// Forward declarations for each precompile's entrypoint. Definitions
// live in their own files (precompile_chainlocks.cpp etc.).

evmc::Result ExecuteChainLocksPrecompile(CEvmHost& host, const evmc_message& msg);
evmc::Result ExecuteMasternodeRegistryPrecompile(CEvmHost& host, const evmc_message& msg);
evmc::Result ExecuteAssetErc20Precompile(CEvmHost& host, const evmc_message& msg);
evmc::Result ExecuteLlmqOraclePrecompile(CEvmHost& host, const evmc_message& msg);

bool ExecutePrecompile(CEvmHost& host,
                       const evmc_message& msg,
                       evmc::Result& outResult)
{
    if (IsAssetErc20PrecompileAddress(msg.code_address)) {
        outResult = ExecuteAssetErc20Precompile(host, msg);
        return true;
    }
    if (!IsFixedPrecompile(msg.code_address)) {
        return false;
    }
    switch (msg.code_address.bytes[19]) {
        case 0x02:
            outResult = ExecuteLlmqOraclePrecompile(host, msg);
            return true;
        case 0x03:
            outResult = ExecuteChainLocksPrecompile(host, msg);
            return true;
        case 0x04:
            outResult = ExecuteMasternodeRegistryPrecompile(host, msg);
            return true;
        default:
            outResult = PrecompileFailure(msg.gas);
            return true;
    }
}

} // namespace evm
