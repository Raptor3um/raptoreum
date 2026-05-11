// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_HASHING_H
#define RAPTOREUM_EVM_HASHING_H

#include <uint256.h>

#include <cstdint>
#include <vector>

class uint160;

namespace evm {

/**
 * Real Keccak-256 hashing (NOT FIPS SHA3-256).
 *
 * The two algorithms differ in their padding rules and produce
 * different outputs for the same input. Ethereum uses Keccak-256 for
 * everything (contract addresses, transaction hashes, storage trie
 * keys, EXTCODEHASH, etc.). We use the same primitive Ethereum does
 * by routing through ethash_keccak256, which is bundled into
 * libevmone-standalone.
 *
 * Returns a uint256 whose m_data layout matches the on-wire big-endian
 * form. That is, the high-order byte of the digest is at m_data[0].
 * This matches the layout we use throughout the EVM module for
 * compatibility with evmc::bytes32.
 *
 * Phase 0 already pinned the canonical value keccak256("") to
 *   c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470
 * which the Phase 0 smoke tests verify via the EVM's SHA3 opcode.
 * The Phase 2.3b unit tests will additionally pin this constant via
 * direct call to Keccak256({}) so we don't depend on the EVM round
 * trip for the basic sanity check.
 */
uint256 Keccak256(const uint8_t* data, size_t size);
uint256 Keccak256(const std::vector<uint8_t>& data);

/**
 * Derive the EVM contract address for a CREATE deployment.
 *
 *   address = last 20 bytes of keccak256( rlp([sender, nonce]) )
 *
 * RLP encoding rules used here (the subset relevant to this case):
 *
 *   - rlp(bytes20)              = 0x94 || 20 bytes
 *                                 (0x80 + length, where length=20)
 *
 *   - rlp(0)                    = 0x80
 *     rlp(N) where 0 < N < 128  = single byte equal to N
 *     rlp(N) where N >= 128     = (0x80 + L) || N-big-endian
 *                                 where L is the byte length of N
 *
 *   - rlp([a, b])               = 0xc0 + total_len || rlp(a) || rlp(b)
 *                                 if total_len < 56
 *                                 (we never exceed 56 for the
 *                                 (sender, nonce<=2^64-1) case;
 *                                 max is 22 + 9 = 31 bytes)
 *
 * This matches what the Ethereum yellow paper specifies for CREATE
 * address derivation. EVM clients (geth, nethermind, erigon) all use
 * this same formula. Our derivation must match byte-for-byte so a
 * contract deployed via our DEPLOY tx ends up at the same address it
 * would have on Ethereum mainnet with the same (sender, nonce) pair.
 *
 * CREATE2 derivation (with a user-provided salt and init code hash)
 * is exposed via ContractAddressFromCreate2 below; nested CREATE2
 * opcodes (Phase 2.3d) use it from within CEvmHost.
 */
uint160 ContractAddressFromCreate(const uint160& sender, uint64_t nonce);

/**
 * Derive the EVM contract address for a CREATE2 deployment (EIP-1014).
 *
 *   address = last 20 bytes of keccak256(
 *                 0xff || sender(20) || salt(32) || keccak256(init_code)
 *             )
 *
 * The user-supplied 32-byte salt lets the deployer pre-compute the
 * destination address before sending the create transaction (counter-
 * factual deployment), which is the headline use case for CREATE2.
 *
 * `initCodeHash` is the keccak256 of the init bytecode (the code that
 * RUNs at deploy time, not the runtime bytecode). Callers that have
 * the raw init code should compute Keccak256(initCode) first; we accept
 * the pre-computed hash so nested CREATE2 callers (evmone's call
 * dispatcher) can pass it through without re-hashing.
 */
uint160 ContractAddressFromCreate2(const uint160& sender,
                                   const uint256& salt,
                                   const uint256& initCodeHash);

} // namespace evm

#endif // RAPTOREUM_EVM_HASHING_H
