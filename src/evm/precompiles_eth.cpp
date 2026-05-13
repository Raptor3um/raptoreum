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

// ---------------------------------------------------------------------------
// 0x09 BLAKE2F (EIP-152)
// ---------------------------------------------------------------------------
//
// Input: 213 bytes
//   rounds (4) | h (64) | m (128) | t (16) | f (1)
// h, m, t are little-endian uint64 arrays. f is 0 or 1.
// Gas: 1 per round.
// Output: 64 bytes — updated h.
namespace {

constexpr uint64_t kBlake2bIV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};

constexpr uint8_t kBlake2bSigma[10][16] = {
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
    { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 },
    { 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 },
    {  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 },
    {  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 },
    {  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 },
    { 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11 },
    { 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10 },
    {  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5 },
    { 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13,  0 },
};

inline uint64_t Rotr64(uint64_t x, unsigned n) { return (x >> n) | (x << (64 - n)); }

inline void Blake2bG(uint64_t v[16], int a, int b, int c, int d, uint64_t x, uint64_t y)
{
    v[a] = v[a] + v[b] + x;
    v[d] = Rotr64(v[d] ^ v[a], 32);
    v[c] = v[c] + v[d];
    v[b] = Rotr64(v[b] ^ v[c], 24);
    v[a] = v[a] + v[b] + y;
    v[d] = Rotr64(v[d] ^ v[a], 16);
    v[c] = v[c] + v[d];
    v[b] = Rotr64(v[b] ^ v[c], 63);
}

// Read a little-endian uint64 from `bytes` at offset `o`.
inline uint64_t LeU64(const uint8_t* bytes, size_t o)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | bytes[o + i];
    return v;
}

inline void WriteLeU64(uint8_t* bytes, size_t o, uint64_t v)
{
    for (int i = 0; i < 8; ++i) {
        bytes[o + i] = static_cast<uint8_t>(v & 0xff);
        v >>= 8;
    }
}

} // namespace

evmc::Result Blake2f(const evmc_message& msg)
{
    // Input must be exactly 213 bytes.
    if (msg.input_size != 213) {
        return evmc::Result{EVMC_FAILURE, 0, 0};
    }

    // Read rounds as a 32-bit big-endian unsigned integer.
    uint32_t rounds = 0;
    for (int i = 0; i < 4; ++i) {
        rounds = (rounds << 8) | msg.input_data[i];
    }

    // Read final flag (must be 0 or 1).
    const uint8_t f_byte = msg.input_data[212];
    if (f_byte != 0 && f_byte != 1) {
        return evmc::Result{EVMC_FAILURE, 0, 0};
    }
    const bool f = (f_byte != 0);

    // Gas cost: 1 per round (EIP-152).
    const int64_t cost = static_cast<int64_t>(rounds);
    if (msg.gas < cost) return Oog();

    // Parse h[8], m[16], t[2] from little-endian uint64 arrays.
    uint64_t h[8];
    for (int i = 0; i < 8; ++i) h[i] = LeU64(msg.input_data, 4 + i * 8);
    uint64_t m[16];
    for (int i = 0; i < 16; ++i) m[i] = LeU64(msg.input_data, 68 + i * 8);
    uint64_t t0 = LeU64(msg.input_data, 196);
    uint64_t t1 = LeU64(msg.input_data, 204);

    // F compression.
    uint64_t v[16];
    for (int i = 0; i < 8; ++i) {
        v[i] = h[i];
        v[8 + i] = kBlake2bIV[i];
    }
    v[12] ^= t0;
    v[13] ^= t1;
    if (f) v[14] = ~v[14];

    for (uint32_t r = 0; r < rounds; ++r) {
        const auto& s = kBlake2bSigma[r % 10];
        Blake2bG(v, 0, 4,  8, 12, m[s[ 0]], m[s[ 1]]);
        Blake2bG(v, 1, 5,  9, 13, m[s[ 2]], m[s[ 3]]);
        Blake2bG(v, 2, 6, 10, 14, m[s[ 4]], m[s[ 5]]);
        Blake2bG(v, 3, 7, 11, 15, m[s[ 6]], m[s[ 7]]);
        Blake2bG(v, 0, 5, 10, 15, m[s[ 8]], m[s[ 9]]);
        Blake2bG(v, 1, 6, 11, 12, m[s[10]], m[s[11]]);
        Blake2bG(v, 2, 7,  8, 13, m[s[12]], m[s[13]]);
        Blake2bG(v, 3, 4,  9, 14, m[s[14]], m[s[15]]);
    }
    for (int i = 0; i < 8; ++i) {
        h[i] ^= v[i] ^ v[i + 8];
    }

    // Serialize h[] as 64 little-endian bytes.
    std::vector<uint8_t> out(64);
    for (int i = 0; i < 8; ++i) WriteLeU64(out.data(), i * 8, h[i]);
    return Ok(msg.gas - cost, out);
}

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
        case 0x09: result = Blake2f(msg);    return true;
        // 0x06..0x08, 0x0a not yet implemented: fall back to bytecode
        // execution (which will run no-op-success at the empty code
        // residing at the precompile address). Limits the lie to:
        //   0x06 BN_ADD, 0x07 BN_MUL, 0x08 BN_PAIRING (need libff)
        //   0x0a KZG_POINT_EVALUATION (needs c-kzg-4844)
        default: return false;
    }
}

} // namespace evm
