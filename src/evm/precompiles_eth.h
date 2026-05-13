// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_PRECOMPILES_ETH_H
#define RAPTOREUM_EVM_PRECOMPILES_ETH_H

#include <evmc/evmc.hpp>

namespace evm {

/**
 * Dispatch a CALL/STATICCALL/CALLCODE/DELEGATECALL whose target is one
 * of the standard Ethereum precompiles (0x01..0x0a). Returns true if
 * the message addressed a known precompile and `result` was populated
 * with the appropriate evmc::Result; returns false otherwise (the
 * caller should fall back to normal bytecode execution).
 *
 * Implemented precompiles (Cancun-relevant subset):
 *   0x01 ECRECOVER  — secp256k1 signature recovery -> 20-byte address
 *   0x02 SHA256     — SHA-256 hash
 *   0x03 RIPEMD160  — RIPEMD-160 hash
 *   0x04 IDENTITY   — pass-through copy
 *   0x05 MODEXP     — modular exponentiation via boost::multiprecision
 *
 * Not yet implemented (the dispatcher returns false so CEvmHost::call
 * falls back to bytecode execution on empty code — wrong per spec,
 * but limits the lie to those four):
 *   0x06 BN_ADD, 0x07 BN_MUL, 0x08 BN_PAIRING — bn128 elliptic curve;
 *     would need libff / hand-rolled pairing arithmetic.
 *   0x09 BLAKE2F      — Blake2 compression function; possible to
 *     hand-implement (small code surface).
 *   0x0a KZG_POINT_EVALUATION — BLS12-381 + KZG commitments;
 *     would need c-kzg-4844 or equivalent.
 *
 * Gas charging follows the Cancun schedule. If msg.gas is insufficient
 * we return EVMC_OUT_OF_GAS.
 */
bool ExecuteEthereumPrecompile(const evmc_message& msg, evmc::Result& result);

} // namespace evm

#endif // RAPTOREUM_EVM_PRECOMPILES_ETH_H
