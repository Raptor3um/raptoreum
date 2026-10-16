// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/balance.h>

namespace evm {

bool Uint256GreaterOrEqualUint64(const uint256& balance, uint64_t amount)
{
    // Any non-zero byte in the high 24 bytes means balance > 2^64,
    // so unambiguously >= any uint64.
    for (int i = 0; i < 24; ++i) {
        if (*(balance.begin() + i) != 0) return true;
    }
    return Uint256ToLowUint64(balance) >= amount;
}

bool Uint256SubUint64(uint256& balance, uint64_t amount)
{
    uint64_t low = Uint256ToLowUint64(balance);

    if (low >= amount) {
        low -= amount;
        for (int i = 31; i >= 24; --i) {
            *(balance.begin() + i) = static_cast<uint8_t>(low & 0xFF);
            low >>= 8;
        }
        return true;
    }

    // Borrow from the high 24 bytes.
    int borrowFrom = -1;
    for (int i = 23; i >= 0; --i) {
        if (*(balance.begin() + i) != 0) {
            borrowFrom = i;
            break;
        }
    }
    if (borrowFrom == -1) {
        return false; // underflow
    }
    *(balance.begin() + borrowFrom) -= 1;
    for (int i = borrowFrom + 1; i < 24; ++i) {
        *(balance.begin() + i) = 0xFF;
    }
    const uint64_t new_low = low - amount; // unsigned wrap is defined
    uint64_t v = new_low;
    for (int i = 31; i >= 24; --i) {
        *(balance.begin() + i) = static_cast<uint8_t>(v & 0xFF);
        v >>= 8;
    }
    return true;
}

bool Uint256AddUint64(uint256& balance, uint64_t amount)
{
    // Add into the low 64 bits; propagate carry through the high
    // 24 bytes one byte at a time.
    uint64_t low = Uint256ToLowUint64(balance);
    const uint64_t sum = low + amount;
    const bool carryFromLow = sum < low; // unsigned overflow detect
    for (int i = 31; i >= 24; --i) {
        *(balance.begin() + i) = static_cast<uint8_t>(sum & 0xFF);
    }
    // Rewrite low bytes explicitly to make the loop above correct.
    {
        uint64_t v = sum;
        for (int i = 31; i >= 24; --i) {
            *(balance.begin() + i) = static_cast<uint8_t>(v & 0xFF);
            v >>= 8;
        }
    }
    if (!carryFromLow) return true;

    int carry = 1;
    for (int i = 23; i >= 0; --i) {
        const int next = static_cast<int>(*(balance.begin() + i)) + carry;
        *(balance.begin() + i) = static_cast<uint8_t>(next & 0xFF);
        carry = next >> 8;
        if (carry == 0) return true;
    }
    return carry == 0;
}

uint64_t Uint256ToLowUint64(const uint256& v)
{
    uint64_t low = 0;
    for (int i = 24; i < 32; ++i) {
        low = (low << 8) | static_cast<uint64_t>(*(v.begin() + i));
    }
    return low;
}

uint256 Uint256FromUint64(uint64_t v)
{
    uint256 out;
    for (int i = 0; i < 8; ++i) {
        *(out.begin() + 31 - i) = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
    }
    return out;
}

} // namespace evm
