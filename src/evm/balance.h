// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_BALANCE_H
#define RAPTOREUM_EVM_BALANCE_H

#include <uint256.h>

#include <cstdint>

namespace evm {

/**
 * Byte-level arithmetic on 32-byte big-endian uint256s used as EVM
 * account balances.
 *
 * Bitcoin Core's uint256 stores its data such that byte 0 of `begin()`
 * is the most significant byte (matching evmc::bytes32 and our on-wire
 * encoding). These helpers treat the buffer as a single 256-bit
 * unsigned integer, with the uint64 operand always interpreted as a
 * scalar to add or subtract from the low 64 bits.
 *
 * Arith_uint256 in Bitcoin Core uses a different internal layout
 * (little-endian 32-bit words). Going through arith_uint256 would
 * require a byteswap each way for every operation, so we do the
 * byte-level math directly here — same approach we used in Phase 2.3
 * apply.cpp's spend path, now centralised so process.cpp can reuse it.
 *
 * All functions are total (no exceptions); the bool returns are the
 * only failure signaling. uint256 inputs that need mutation are
 * passed by reference.
 */

/** Returns true if balance >= amount. */
bool Uint256GreaterOrEqualUint64(const uint256& balance, uint64_t amount);

/** balance -= amount, modular wrap on underflow. Returns false on
 *  underflow (caller should check Uint256GreaterOrEqualUint64 first
 *  to avoid the false-return path). */
bool Uint256SubUint64(uint256& balance, uint64_t amount);

/** balance += amount, modular wrap on overflow. Returns false if the
 *  high bits would have to carry past 256 bits — a real-balance
 *  overflow is impossible within RTM's supply, so this signals a
 *  consensus bug rather than a normal failure. */
bool Uint256AddUint64(uint256& balance, uint64_t amount);

/** Read the low 64 bits of a big-endian uint256 as a scalar.
 *  High 192 bits are ignored — callers that need to detect "would
 *  truncate" should first compare against (1<<64) via the helpers
 *  above. */
uint64_t Uint256ToLowUint64(const uint256& v);

/** Write a uint64 into the low 64 bits of a big-endian uint256.
 *  High 192 bits are zeroed. */
uint256 Uint256FromUint64(uint64_t v);

} // namespace evm

#endif // RAPTOREUM_EVM_BALANCE_H
