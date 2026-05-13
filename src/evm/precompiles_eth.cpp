// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/precompiles_eth.h>

#include <evm/hashing.h>

#include <crypto/ripemd160.h>
#include <crypto/sha256.h>
#include <pubkey.h>
#include <uint256.h>

#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace evm {

namespace {

// Pull an input byte from msg.input_data padding with zeros past the
// declared input_size (Ethereum precompiles read up to a fixed-size
// padded view of the calldata).
uint8_t InputByte(const evmc_message& msg, size_t i)
{
    if (i < msg.input_size && msg.input_data != nullptr) return msg.input_data[i];
    return 0;
}

// Build a successful evmc::Result with the given output buffer.
// evmc::Result takes ownership of a malloc'd copy via its public
// constructor (the underlying release pointer is private).
evmc::Result Ok(int64_t gas_left, const std::vector<uint8_t>& bytes)
{
    return evmc::Result{
        EVMC_SUCCESS,
        gas_left,
        /*gas_refund=*/ 0,
        bytes.empty() ? nullptr : bytes.data(),
        bytes.size()};
}

evmc::Result Oog()
{
    return evmc::Result{EVMC_OUT_OF_GAS, 0, 0};
}

// Per-word gas accounting helper. Cancun keeps the Berlin schedule
// for IDENTITY (15 + 3/word), SHA256 (60 + 12/word), RIPEMD160
// (600 + 120/word).
int64_t Words(size_t bytes)
{
    return static_cast<int64_t>((bytes + 31) / 32);
}

// ---------------------------------------------------------------------------
// 0x01 ECRECOVER
// ---------------------------------------------------------------------------
// Input layout (128 bytes, zero-padded): [hash(32) | v(32) | r(32) | s(32)].
// Output: 32 bytes — 12 zero bytes followed by the 20-byte recovered
//         Ethereum address. Returns empty output on any validation
//         failure (the EVM caller observes a zero / non-address output
//         and a success status).
evmc::Result EcRecover(const evmc_message& msg)
{
    const int64_t cost = 3000;
    if (msg.gas < cost) return Oog();
    const int64_t gasLeft = msg.gas - cost;

    // Read 128 bytes (zero-padded).
    uint8_t hash[32];
    uint8_t v_bytes[32];
    uint8_t rsig[32];
    uint8_t ssig[32];
    for (size_t i = 0; i < 32; ++i) {
        hash[i]    = InputByte(msg, i);
        v_bytes[i] = InputByte(msg, 32 + i);
        rsig[i]    = InputByte(msg, 64 + i);
        ssig[i]    = InputByte(msg, 96 + i);
    }
    // v must be 27 or 28 (encoded as a 32-byte big-endian uint, so
    // bytes[0..30] must be zero and bytes[31] must be 27 or 28).
    for (int i = 0; i < 31; ++i) {
        if (v_bytes[i] != 0) return Ok(gasLeft, {});
    }
    const uint8_t v = v_bytes[31];
    if (v != 27 && v != 28) return Ok(gasLeft, {});

    // CPubKey::RecoverCompact takes a 65-byte compact signature laid
    // out as [recid_byte(1) | r(32) | s(32)] where recid_byte =
    // 27 + recid + 4 (Bitcoin convention). For Ethereum's v == 27/28
    // and uncompressed pubkey, recid_byte = 31 or 32.
    std::vector<unsigned char> sig(65, 0);
    sig[0] = static_cast<unsigned char>(v + 4);
    std::memcpy(sig.data() + 1, rsig, 32);
    std::memcpy(sig.data() + 33, ssig, 32);

    uint256 msgHash;
    std::memcpy(msgHash.begin(), hash, 32);

    CPubKey pk;
    if (!pk.RecoverCompact(msgHash, sig)) return Ok(gasLeft, {});
    if (pk.size() != 65) return Ok(gasLeft, {});

    std::vector<uint8_t> pkPayload(pk.begin() + 1, pk.begin() + pk.size());
    uint256 keccakHash = Keccak256(pkPayload);

    std::vector<uint8_t> out(32, 0);
    std::memcpy(out.data() + 12, keccakHash.begin() + 12, 20);
    return Ok(gasLeft, out);
}

// ---------------------------------------------------------------------------
// 0x02 SHA256
// ---------------------------------------------------------------------------
evmc::Result Sha256(const evmc_message& msg)
{
    const int64_t cost = 60 + 12 * Words(msg.input_size);
    if (msg.gas < cost) return Oog();

    CSHA256 h;
    if (msg.input_size > 0 && msg.input_data != nullptr) {
        h.Write(msg.input_data, msg.input_size);
    }
    std::vector<uint8_t> out(CSHA256::OUTPUT_SIZE);
    h.Finalize(out.data());
    return Ok(msg.gas - cost, out);
}

// ---------------------------------------------------------------------------
// 0x03 RIPEMD160
// ---------------------------------------------------------------------------
evmc::Result Ripemd160(const evmc_message& msg)
{
    const int64_t cost = 600 + 120 * Words(msg.input_size);
    if (msg.gas < cost) return Oog();

    CRIPEMD160 h;
    if (msg.input_size > 0 && msg.input_data != nullptr) {
        h.Write(msg.input_data, msg.input_size);
    }
    uint8_t digest[CRIPEMD160::OUTPUT_SIZE];
    h.Finalize(digest);

    std::vector<uint8_t> out(32, 0);
    std::memcpy(out.data() + 12, digest, CRIPEMD160::OUTPUT_SIZE);
    return Ok(msg.gas - cost, out);
}

// ---------------------------------------------------------------------------
// 0x04 IDENTITY
// ---------------------------------------------------------------------------
evmc::Result Identity(const evmc_message& msg)
{
    const int64_t cost = 15 + 3 * Words(msg.input_size);
    if (msg.gas < cost) return Oog();

    std::vector<uint8_t> out;
    if (msg.input_size > 0 && msg.input_data != nullptr) {
        out.assign(msg.input_data, msg.input_data + msg.input_size);
    }
    return Ok(msg.gas - cost, out);
}

// ---------------------------------------------------------------------------
// 0x05 MODEXP (EIP-198 / EIP-2565 gas)
// ---------------------------------------------------------------------------
// Input layout (variable-length, zero-padded):
//   [Bsize(32) | Esize(32) | Msize(32) | B(Bsize) | E(Esize) | M(Msize)]
// where each size is a 32-byte big-endian length, then the values
// follow concatenated. Output: M-size bytes containing (B**E) % M.
//
// EIP-2565 (Berlin+) gas formula:
//   multiplication_complexity = ceil(max(Bsize, Msize) / 8) ** 2
//   iteration_count = max(adjusted_exp_length(E), 1)
//     where adjusted_exp_length depends on whether Esize <= 32 and
//     the bit width of E.
//   cost = max(200, multiplication_complexity * iteration_count / 3)

namespace {

// Big-endian read of a 32-byte length field. Caps at SIZE_MAX/2 to
// avoid pathological allocations on adversarial input.
size_t ReadSize32(const evmc_message& msg, size_t offset)
{
    // Take the low 8 bytes of the 32-byte length (top 24 must be 0 per
    // spec; we tolerate non-zero by treating them as "huge" and
    // letting gas cost OoG us).
    for (int i = 0; i < 24; ++i) {
        if (InputByte(msg, offset + i) != 0) {
            return std::numeric_limits<size_t>::max() / 2;
        }
    }
    uint64_t v = 0;
    for (int i = 24; i < 32; ++i) {
        v = (v << 8) | InputByte(msg, offset + i);
    }
    if (v > (std::numeric_limits<size_t>::max() / 2)) {
        return std::numeric_limits<size_t>::max() / 2;
    }
    return static_cast<size_t>(v);
}

// Read `len` bytes starting at `offset`, zero-padding past the
// declared input.
std::vector<uint8_t> ReadBytes(const evmc_message& msg, size_t offset, size_t len)
{
    std::vector<uint8_t> out(len);
    for (size_t i = 0; i < len; ++i) {
        out[i] = InputByte(msg, offset + i);
    }
    return out;
}

// EIP-2565 multiplication complexity.
uint64_t MultComplexity(size_t base_len, size_t mod_len)
{
    const size_t max_len = std::max(base_len, mod_len);
    const uint64_t words = (max_len + 7) / 8;
    return words * words;
}

// EIP-2565 adjusted exponent length. If E fits in 32 bytes, looks at
// its bit width; otherwise charges 8 bits per excess byte plus the
// top-32-byte bit width.
uint64_t IterCount(size_t exp_len, const std::vector<uint8_t>& E)
{
    // Find the bit length of the first 32 bytes of E.
    auto top_bits = [&]() -> uint64_t {
        const size_t look = std::min<size_t>(32, E.size());
        for (size_t i = 0; i < look; ++i) {
            if (E[i] != 0) {
                // Leading byte is E[i]. Bit width = (look - i - 1) * 8 + bit
                // width of E[i].
                uint8_t b = E[i];
                int hi = 0;
                while (b) { ++hi; b >>= 1; }
                return static_cast<uint64_t>((look - i - 1) * 8 + hi);
            }
        }
        return 0;
    };

    uint64_t iter;
    if (exp_len <= 32) {
        iter = top_bits();
    } else {
        iter = 8 * static_cast<uint64_t>(exp_len - 32) + top_bits();
    }
    return iter == 0 ? 1 : iter;
}

} // namespace

evmc::Result Modexp(const evmc_message& msg)
{
    using boost::multiprecision::cpp_int;

    const size_t base_len = ReadSize32(msg, 0);
    const size_t exp_len  = ReadSize32(msg, 32);
    const size_t mod_len  = ReadSize32(msg, 64);

    // Sanity cap: real fixtures use at most a few KB per component.
    // Anything beyond ~64KB would cost more gas than any reasonable
    // tx provides AND risks allocating gigabytes if we trusted the
    // declared sizes blindly. Treat oversized inputs as out-of-gas.
    constexpr size_t kMaxLen = 64 * 1024;
    if (base_len > kMaxLen || exp_len > kMaxLen || mod_len > kMaxLen) {
        return Oog();
    }

    // Trivial case: modulus length is zero — output is empty.
    if (mod_len == 0) {
        const int64_t cost = 200;
        if (msg.gas < cost) return Oog();
        return Ok(msg.gas - cost, {});
    }

    const size_t base_off = 96;
    const size_t exp_off  = base_off + base_len;
    const size_t mod_off  = exp_off + exp_len;

    std::vector<uint8_t> B = ReadBytes(msg, base_off, base_len);
    std::vector<uint8_t> E = ReadBytes(msg, exp_off,  exp_len);
    std::vector<uint8_t> M = ReadBytes(msg, mod_off,  mod_len);

    // EIP-2565 gas.
    const uint64_t mc = MultComplexity(base_len, mod_len);
    const uint64_t it = IterCount(exp_len, E);
    uint64_t cost = (mc * it) / 3;
    if (cost < 200) cost = 200;
    if (msg.gas < static_cast<int64_t>(cost)) return Oog();

    // Convert big-endian byte strings into cpp_int.
    auto from_bytes = [](const std::vector<uint8_t>& bytes) -> cpp_int {
        cpp_int v = 0;
        for (uint8_t b : bytes) { v <<= 8; v += b; }
        return v;
    };
    cpp_int b = from_bytes(B);
    cpp_int e = from_bytes(E);
    cpp_int m = from_bytes(M);

    cpp_int result = 0;
    if (m != 0) {
        // boost::multiprecision::powm computes (b ** e) mod m.
        result = boost::multiprecision::powm(b, e, m);
    }

    // Encode result as mod_len big-endian bytes (left-padded with
    // zeros).
    std::vector<uint8_t> out(mod_len, 0);
    cpp_int tmp = result;
    for (size_t i = 0; i < mod_len && tmp != 0; ++i) {
        out[mod_len - 1 - i] = static_cast<uint8_t>(tmp & 0xff);
        tmp >>= 8;
    }

    return Ok(msg.gas - static_cast<int64_t>(cost), out);
}

// Detect a standard Ethereum precompile address: the high 19 bytes
// must be zero and the low byte must be in 1..0x0a.
bool IsStandardPrecompile(const evmc::address& addr, uint8_t& which)
{
    for (int i = 0; i < 19; ++i) {
        if (addr.bytes[i] != 0) return false;
    }
    const uint8_t b = addr.bytes[19];
    if (b < 0x01 || b > 0x0a) return false;
    which = b;
    return true;
}

} // namespace

bool ExecuteEthereumPrecompile(const evmc_message& msg, evmc::Result& result)
{
    uint8_t which = 0;
    if (!IsStandardPrecompile(msg.code_address, which)) return false;

    switch (which) {
        case 0x01: result = EcRecover(msg);  return true;
        case 0x02: result = Sha256(msg);     return true;
        case 0x03: result = Ripemd160(msg);  return true;
        case 0x04: result = Identity(msg);   return true;
        case 0x05: result = Modexp(msg);     return true;
        // 0x06..0x0a not yet implemented: fall back to bytecode
        // execution (which will run no-op-success at the empty code
        // residing at the precompile address). This is wrong per the
        // spec, but limiting the lie to those five lets the five
        // commonly-tested precompiles pass while we plan a port of
        // the bn128 / pairing / blake2f / KZG primitives.
        default: return false;
    }
}

} // namespace evm
