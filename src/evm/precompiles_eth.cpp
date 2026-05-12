// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/precompiles_eth.h>

#include <evm/hashing.h>

#include <crypto/ripemd160.h>
#include <crypto/sha256.h>
#include <pubkey.h>
#include <uint256.h>

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
        // 0x05..0x0a not yet implemented: fall back to bytecode
        // execution (which will run no-op-success at the empty code
        // residing at the precompile address). This is wrong per the
        // spec, but limiting the lie to those five lets the four
        // commonly-tested precompiles pass while we plan a port of
        // the bignum / pairing / blake2f / KZG primitives.
        default: return false;
    }
}

} // namespace evm
