// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_PRECOMPILES_H
#define RAPTOREUM_EVM_PRECOMPILES_H

#include <evm/host.h>
#include <uint256.h>

#include <evmc/evmc.hpp>

#include <cstdint>
#include <vector>

class uint160;

namespace evm {

/**
 * RTM-native precompiles (Phase 4) — the chain's differentiator.
 *
 * Four precompile contracts live at deterministic, version-namespaced
 * addresses inside the EVM address space. Each surfaces a piece of
 * the Raptoreum subsystem (Smart Assets, LLMQ threshold sigs,
 * ChainLocks, masternode registry) as a Solidity-callable contract
 * with a standard ABI, letting any EVM dApp consume native state
 * that's otherwise locked behind RPC.
 *
 *   0x00..00a01  Smart Assets ERC-20 (one per assetId, derived
 *                from the Smart Asset id via a reserved prefix)
 *   0x00..0a01   "Asset registry" probe — TBD; the per-asset
 *                addresses encode assetId in the suffix
 *   0x00..0a02   LLMQ Oracle (threshold sigs, async)
 *   0x00..0a03   ChainLocks (sync read-only)
 *   0x00..0a04   Masternode Registry (sync read-only)
 *
 * Per-Asset address derivation (Phase 4.1):
 *
 *   address = 0xA55E700000000000_00000000_00000000_00000000 |
 *             (hash160(assetId) zero-extended into low 96 bits)
 *
 * The prefix `0xA55E70` reads as "ASSET" in leetspeak hex and is
 * blocked from user-EVM-CREATE per A11 collision rule.
 *
 * Dispatch entry:
 *
 *   IsPrecompileAddress(addr)  -> bool   (cheap address pattern check)
 *   ExecutePrecompile(host, msg, out)    (runs the precompile against
 *                                          the message's input data,
 *                                          fills `out` with status+gas+
 *                                          output bytes)
 *
 * CEvmHost::call() inserts a single check at the head of its
 * dispatch path:  if (IsPrecompileAddress(msg.code_address))
 *                     return ExecutePrecompile(*this, msg).
 *
 * All four precompiles are read-only with respect to the EVM state
 * cache *they receive*. The Smart Asset transfer/approve methods
 * mutate the surrounding CAssetsCache + a Phase 4.1 allowance map
 * that lives in the EVM storage trie of the precompile address
 * itself — kept inside the EVM state cache for clean reorg behavior.
 */

// ----------------------------------------------------------------------
// Address recognition + dispatch
// ----------------------------------------------------------------------

/** True if the address is in the RTM-native precompile range or in
 *  one of the per-asset Smart Asset ERC-20 prefixes. */
bool IsPrecompileAddress(const evmc::address& addr);

/** Execute the precompile dispatch. Returns false if the address
 *  isn't a precompile (caller falls through to normal CALL path).
 *  On true: fills `outResult` with the status code, gas_left, and
 *  output bytes. */
bool ExecutePrecompile(CEvmHost& host,
                       const evmc_message& msg,
                       evmc::Result& outResult);

// ----------------------------------------------------------------------
// ABI codec — narrow surface, only what the precompiles need.
// ----------------------------------------------------------------------
//
// Each Solidity function call is:
//   selector(4 bytes) || abi-encoded-args
//
// The selector is keccak256(signature)[0:4]. For deterministic
// dispatch we precompute the selector at compile time once per
// function. ABI encoding rules (Solidity v0.8 spec):
//   - Static types (uint256, address, bool, bytes32) pack into
//     32-byte slots.
//   - Dynamic types (bytes, string) encode as (offset, length,
//     payload-padded-to-32).
//
// We hand-roll the cases we need. A general-purpose Solidity ABI
// codec lives in the SDK (@raptoreum/evm-sdk) — server-side we keep
// the surface minimal.

/** Compute the function selector at runtime. Use sparingly; pin
 *  selectors as constants in each precompile header to avoid
 *  re-hashing at every call. */
uint32_t AbiFunctionSelector(const std::string& signature);

/** Read a 32-byte ABI word starting at `byteOffset` in `input`.
 *  Returns false if the read would overflow. */
bool AbiReadWord(const std::vector<uint8_t>& input, size_t byteOffset,
                uint256& outWord);

/** Read an `address` argument (32-byte word, address in low 20 bytes). */
bool AbiReadAddress(const std::vector<uint8_t>& input, size_t byteOffset,
                   uint160& outAddr);

/** Read a `uint64` (the low 8 bytes of a 32-byte word). Rejects
 *  values that would overflow uint64. */
bool AbiReadUint64(const std::vector<uint8_t>& input, size_t byteOffset,
                  uint64_t& outValue);

/** Read a `uint8` (low byte of a 32-byte word). */
bool AbiReadUint8(const std::vector<uint8_t>& input, size_t byteOffset,
                 uint8_t& outValue);

/** Read a `bool` — high 31 bytes must be zero. */
bool AbiReadBool(const std::vector<uint8_t>& input, size_t byteOffset,
                bool& outValue);

/** Read a `bytes`/`string` argument given the head-offset slot
 *  position. Resolves the dynamic-data pointer and returns the
 *  payload. */
bool AbiReadDynamicBytes(const std::vector<uint8_t>& input,
                        size_t headByteOffset,
                        std::vector<uint8_t>& outBytes);

/** Encode a single 32-byte word into the output buffer. */
void AbiWriteWord(std::vector<uint8_t>& out, const uint256& word);

/** Encode a uint64 as a 32-byte word (zero-padded high bytes). */
void AbiWriteUint64(std::vector<uint8_t>& out, uint64_t value);

/** Encode a uint8 as a 32-byte word. */
void AbiWriteUint8(std::vector<uint8_t>& out, uint8_t value);

/** Encode a bool as a 32-byte word (0 or 1). */
void AbiWriteBool(std::vector<uint8_t>& out, bool value);

/** Encode an address as a 32-byte word (zero-padded high 12 bytes). */
void AbiWriteAddress(std::vector<uint8_t>& out, const uint160& addr);

/** Encode a `bytes` payload at the *tail*: the call site is
 *  responsible for emitting the head-offset slot first. Pads the
 *  payload to a multiple of 32 bytes. */
void AbiWriteDynamicBytesTail(std::vector<uint8_t>& out,
                             const std::vector<uint8_t>& bytes);

/** Encode a `string`/`bytes` head slot containing the byte offset
 *  to the tail. */
void AbiWriteDynamicHead(std::vector<uint8_t>& out, uint64_t tailOffset);

/** Build a precompile-failure evmc::Result with all gas consumed.
 *  The Phase 4 precompiles use this for ABI decode errors,
 *  unknown selectors, and state-access misses. */
evmc::Result PrecompileFailure(int64_t gasLimit);

/** Build a precompile-success evmc::Result with the given output
 *  bytes and gas-left = gasLimit - gasUsed. */
evmc::Result PrecompileSuccess(int64_t gasLimit, int64_t gasUsed,
                               std::vector<uint8_t> output);

} // namespace evm

#endif // RAPTOREUM_EVM_PRECOMPILES_H
