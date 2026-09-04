// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/rlp.h>

#include <cstring>

namespace evm {

namespace {

// Encode `len` as a big-endian byte sequence with no leading zeros.
// Used to construct the long-form length prefix.
std::vector<uint8_t> LengthBytesBE(uint64_t len)
{
    if (len == 0) return {0};
    std::vector<uint8_t> out;
    while (len > 0) {
        out.insert(out.begin(), static_cast<uint8_t>(len & 0xFF));
        len >>= 8;
    }
    return out;
}

} // anonymous namespace

// ----------------------------------------------------------------------
// Encoder
// ----------------------------------------------------------------------

std::vector<uint8_t> RlpEncodeBytes(const uint8_t* data, size_t len)
{
    std::vector<uint8_t> out;
    if (len == 1 && data[0] < 0x80) {
        out.push_back(data[0]);
        return out;
    }
    if (len <= 55) {
        out.push_back(static_cast<uint8_t>(0x80 + len));
        out.insert(out.end(), data, data + len);
        return out;
    }
    std::vector<uint8_t> lenBytes = LengthBytesBE(len);
    out.push_back(static_cast<uint8_t>(0xB7 + lenBytes.size()));
    out.insert(out.end(), lenBytes.begin(), lenBytes.end());
    out.insert(out.end(), data, data + len);
    return out;
}

std::vector<uint8_t> RlpEncodeBytes(const std::vector<uint8_t>& data)
{
    return RlpEncodeBytes(data.empty() ? nullptr : data.data(), data.size());
}

std::vector<uint8_t> RlpEncodeUint(uint64_t value)
{
    if (value == 0) {
        // rlp(0) is the empty byte string -> 0x80.
        return RlpEncodeBytes(nullptr, 0);
    }
    std::vector<uint8_t> be;
    uint64_t v = value;
    while (v > 0) {
        be.insert(be.begin(), static_cast<uint8_t>(v & 0xFF));
        v >>= 8;
    }
    return RlpEncodeBytes(be);
}

std::vector<uint8_t> RlpEncodeList(const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> out;
    if (payload.size() <= 55) {
        out.push_back(static_cast<uint8_t>(0xC0 + payload.size()));
        out.insert(out.end(), payload.begin(), payload.end());
        return out;
    }
    std::vector<uint8_t> lenBytes = LengthBytesBE(payload.size());
    out.push_back(static_cast<uint8_t>(0xF7 + lenBytes.size()));
    out.insert(out.end(), lenBytes.begin(), lenBytes.end());
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<uint8_t> RlpEncode(const RlpValue& value)
{
    if (!value.isList) {
        return RlpEncodeBytes(value.bytes);
    }
    std::vector<uint8_t> payload;
    for (const auto& item : value.items) {
        std::vector<uint8_t> enc = RlpEncode(item);
        payload.insert(payload.end(), enc.begin(), enc.end());
    }
    return RlpEncodeList(payload);
}

// ----------------------------------------------------------------------
// Decoder
// ----------------------------------------------------------------------

namespace {

// Decode a big-endian unsigned integer from `len` bytes starting at
// `data`. Rejects leading-zero bytes (non-canonical) so the RLP we
// accept is unique-encoding-per-value.
bool DecodeLenBytesBE(const uint8_t* data, size_t lenBytesCount,
                     size_t& outLen)
{
    if (lenBytesCount == 0 || lenBytesCount > 8) return false;
    if (data[0] == 0x00) return false; // leading zero is non-canonical
    uint64_t n = 0;
    for (size_t i = 0; i < lenBytesCount; ++i) {
        n = (n << 8) | static_cast<uint64_t>(data[i]);
    }
    // Reject values that would have been representable in the short
    // form (<= 55), which is the canonical-form constraint.
    if (n <= 55) return false;
    outLen = static_cast<size_t>(n);
    return true;
}

} // anonymous namespace

bool RlpDecode(const uint8_t* data, size_t size, RlpValue& out, size_t& consumed)
{
    if (size == 0) return false;
    const uint8_t prefix = data[0];

    // Single-byte short form: 0x00..0x7F.
    if (prefix <= 0x7F) {
        out.isList = false;
        out.bytes = {prefix};
        consumed = 1;
        return true;
    }

    // Short string (0..55 bytes): 0x80..0xB7.
    if (prefix <= 0xB7) {
        const size_t strLen = static_cast<size_t>(prefix - 0x80);
        if (1 + strLen > size) return false;
        // Canonical: a 1-byte string with value < 0x80 must use the
        // single-byte short form, not the 0x81-prefixed form.
        if (strLen == 1 && data[1] < 0x80) return false;
        out.isList = false;
        out.bytes.assign(data + 1, data + 1 + strLen);
        consumed = 1 + strLen;
        return true;
    }

    // Long string: 0xB8..0xBF.
    if (prefix <= 0xBF) {
        const size_t lenLen = static_cast<size_t>(prefix - 0xB7);
        if (1 + lenLen > size) return false;
        size_t strLen = 0;
        if (!DecodeLenBytesBE(data + 1, lenLen, strLen)) return false;
        if (1 + lenLen + strLen > size) return false;
        out.isList = false;
        out.bytes.assign(data + 1 + lenLen, data + 1 + lenLen + strLen);
        consumed = 1 + lenLen + strLen;
        return true;
    }

    // Short list (0..55 byte payload): 0xC0..0xF7.
    if (prefix <= 0xF7) {
        const size_t listLen = static_cast<size_t>(prefix - 0xC0);
        if (1 + listLen > size) return false;
        out.isList = true;
        out.items.clear();
        size_t pos = 1;
        const size_t end = 1 + listLen;
        while (pos < end) {
            RlpValue inner;
            size_t innerConsumed = 0;
            if (!RlpDecode(data + pos, end - pos, inner, innerConsumed)) {
                return false;
            }
            out.items.push_back(std::move(inner));
            pos += innerConsumed;
        }
        if (pos != end) return false;
        consumed = end;
        return true;
    }

    // Long list: 0xF8..0xFF.
    const size_t lenLen = static_cast<size_t>(prefix - 0xF7);
    if (1 + lenLen > size) return false;
    size_t listLen = 0;
    if (!DecodeLenBytesBE(data + 1, lenLen, listLen)) return false;
    if (1 + lenLen + listLen > size) return false;
    out.isList = true;
    out.items.clear();
    size_t pos = 1 + lenLen;
    const size_t end = 1 + lenLen + listLen;
    while (pos < end) {
        RlpValue inner;
        size_t innerConsumed = 0;
        if (!RlpDecode(data + pos, end - pos, inner, innerConsumed)) {
            return false;
        }
        out.items.push_back(std::move(inner));
        pos += innerConsumed;
    }
    if (pos != end) return false;
    consumed = end;
    return true;
}

bool RlpDecode(const std::vector<uint8_t>& data, RlpValue& out)
{
    size_t consumed = 0;
    if (!RlpDecode(data.empty() ? nullptr : data.data(), data.size(),
                   out, consumed)) {
        return false;
    }
    return consumed == data.size();
}

bool RlpValueAsUint64(const RlpValue& v, uint64_t& out)
{
    if (v.isList) return false;
    if (v.bytes.size() > 8) return false;
    if (v.bytes.size() > 1 && v.bytes[0] == 0x00) return false;
    out = 0;
    for (uint8_t b : v.bytes) {
        out = (out << 8) | static_cast<uint64_t>(b);
    }
    return true;
}

} // namespace evm
