// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/hashing.h>

#include <uint256.h>

#include <ethash/keccak.h>

#include <cstring>

namespace evm {

uint256 Keccak256(const uint8_t* data, size_t size)
{
    const auto h = ethash_keccak256(data, size);
    uint256 out;
    std::memcpy(out.begin(), h.bytes, 32);
    return out;
}

uint256 Keccak256(const std::vector<uint8_t>& data)
{
    return Keccak256(data.empty() ? nullptr : data.data(), data.size());
}

// ----------------------------------------------------------------------
// Minimal RLP encoding for the CREATE-address case.
//
// We only need to serialize a 2-element list [bytes20, uint64]. The
// encoded form fits well inside RLP's "short list" branch (<56 bytes),
// so we don't need the long-list extension path. This implementation
// is intentionally focused — a general-purpose RLP encoder lives with
// whatever future code needs eth_sendRawTransaction (Phase 3), not
// here.
// ----------------------------------------------------------------------

namespace {

// Encode a non-negative integer as big-endian, stripping leading zeros.
// rlp(0) is the empty byte string (NOT a single 0x00 byte).
std::vector<uint8_t> EncodeUintBE(uint64_t value)
{
    if (value == 0) return {};
    std::vector<uint8_t> out;
    while (value > 0) {
        out.insert(out.begin(), static_cast<uint8_t>(value & 0xFF));
        value >>= 8;
    }
    return out;
}

// RLP encoding of an arbitrary byte string. Used for both the
// 20-byte sender address and the nonce's big-endian encoding.
//
//   - Single byte in [0x00, 0x7F]:  encoded as itself.
//   - Empty string ("rlp 0"):       0x80.
//   - 1..55 bytes:                  0x80 + length, then the bytes.
//   - >= 56 bytes:                  long-string path; not needed here.
std::vector<uint8_t> RlpEncodeBytes(const uint8_t* bytes, size_t len)
{
    std::vector<uint8_t> out;
    if (len == 0) {
        out.push_back(0x80);
        return out;
    }
    if (len == 1 && bytes[0] < 0x80) {
        out.push_back(bytes[0]);
        return out;
    }
    if (len <= 55) {
        out.push_back(static_cast<uint8_t>(0x80 + len));
        out.insert(out.end(), bytes, bytes + len);
        return out;
    }
    // len > 55: long-string encoding. Not needed for our case
    // (sender is 20 bytes, nonce is at most 9 bytes after BE strip).
    // Defensive: emit a long-form encoding rather than truncate.
    std::vector<uint8_t> lenBytes = EncodeUintBE(static_cast<uint64_t>(len));
    out.push_back(static_cast<uint8_t>(0xB7 + lenBytes.size()));
    out.insert(out.end(), lenBytes.begin(), lenBytes.end());
    out.insert(out.end(), bytes, bytes + len);
    return out;
}

// RLP-encode a list whose elements are already RLP-encoded.
std::vector<uint8_t> RlpEncodeListPayload(const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> out;
    if (payload.size() <= 55) {
        out.push_back(static_cast<uint8_t>(0xC0 + payload.size()));
        out.insert(out.end(), payload.begin(), payload.end());
        return out;
    }
    // Long list (defensive — not reachable for our case).
    std::vector<uint8_t> lenBytes = EncodeUintBE(static_cast<uint64_t>(payload.size()));
    out.push_back(static_cast<uint8_t>(0xF7 + lenBytes.size()));
    out.insert(out.end(), lenBytes.begin(), lenBytes.end());
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

} // anonymous namespace

uint160 ContractAddressFromCreate(const uint160& sender, uint64_t nonce)
{
    // RLP-encode the two items.
    std::vector<uint8_t> rlpSender = RlpEncodeBytes(sender.begin(), 20);

    std::vector<uint8_t> nonceBE = EncodeUintBE(nonce);
    std::vector<uint8_t> rlpNonce = RlpEncodeBytes(
        nonceBE.empty() ? nullptr : nonceBE.data(), nonceBE.size());

    // Concatenate to form the list payload, then wrap as a list.
    std::vector<uint8_t> payload;
    payload.insert(payload.end(), rlpSender.begin(), rlpSender.end());
    payload.insert(payload.end(), rlpNonce.begin(), rlpNonce.end());

    std::vector<uint8_t> rlp = RlpEncodeListPayload(payload);

    // keccak256 of the RLP, then drop the high 12 bytes.
    uint256 hash = Keccak256(rlp);

    uint160 address;
    std::memcpy(address.begin(), hash.begin() + 12, 20);
    return address;
}

uint160 ContractAddressFromCreate2(const uint160& sender,
                                   const uint256& salt,
                                   const uint256& initCodeHash)
{
    // Preimage: 0xff || sender(20) || salt(32) || keccak256(init_code)(32)
    std::vector<uint8_t> preimage;
    preimage.reserve(1 + 20 + 32 + 32);
    preimage.push_back(0xFF);
    preimage.insert(preimage.end(), sender.begin(), sender.begin() + 20);
    preimage.insert(preimage.end(), salt.begin(), salt.begin() + 32);
    preimage.insert(preimage.end(), initCodeHash.begin(), initCodeHash.begin() + 32);

    uint256 hash = Keccak256(preimage);

    uint160 address;
    std::memcpy(address.begin(), hash.begin() + 12, 20);
    return address;
}

} // namespace evm
