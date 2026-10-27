// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_RAWTX_H
#define RAPTOREUM_EVM_RAWTX_H

#include <uint256.h>

#include <cstdint>
#include <vector>

class uint160;

namespace evm {

/**
 * Decoded Ethereum-format transaction (eth_sendRawTransaction input).
 *
 * Supports two envelope formats:
 *
 *   - EIP-1559 (type 0x02): the modern wallet default. Carries
 *     maxFeePerGas / maxPriorityFeePerGas instead of a single
 *     gasPrice. Signing payload is `0x02 || rlp([...])`.
 *
 *   - Legacy EIP-155: pre-EIP-1559 format with `gasPrice`. Detected
 *     when the input doesn't start with a typed-tx envelope byte
 *     (0x00..0x7F is RLP-payload territory only when the byte is
 *     a single-byte short string, which never happens for a
 *     transaction-shaped list — so the discriminator is "first
 *     byte >= 0xC0").
 *
 * Common fields decoded into `DecodedRawTx`:
 *
 *   - nonce, gasLimit, value, data (calldata or init code).
 *   - to: 20-byte address; emptyTo=true on a contract-creation tx.
 *   - chainId: must match the active EVM chain id (EIP-155 replay
 *     protection). EIP-1559 tx envelope carries it explicitly;
 *     legacy infers from `v = chainId * 2 + 35 + y_parity`.
 *   - maxFeePerGas / maxPriorityFeePerGas — EIP-1559; for legacy
 *     both default to `gasPrice`.
 *   - sender: recovered from the signature against the signing
 *     payload hash. Set by the parser (`DecodeRawEthTx`).
 */

struct DecodedRawTx
{
    // Public payload fields:
    uint64_t chainId{0};
    uint64_t nonce{0};
    uint64_t gasLimit{0};
    uint64_t maxFeePerGas{0};
    uint64_t maxPriorityFeePerGas{0};
    uint64_t value{0};        // in weis; uint64 reflects current ceiling
    std::vector<uint8_t> data;
    bool emptyTo{false};      // true for a contract creation tx
    uint160 to;               // valid when !emptyTo

    // Sig + recovered sender.
    uint8_t yParity{0};
    uint256 r;
    uint256 s;
    uint160 sender;           // computed by DecodeRawEthTx

    // For tx-hash construction the caller may need the original
    // wire bytes; we keep them for callers that want the canonical
    // 32-byte keccak hash of the serialised signed payload.
    std::vector<uint8_t> wire;

    // Envelope type. 2 == EIP-1559; 0 == legacy.
    uint8_t txType{0};
};

/**
 * Decode + verify an `eth_sendRawTransaction` wire blob.
 *
 *   - Validates RLP framing.
 *   - Validates the EIP-1559 / legacy field count.
 *   - Validates chainId == expectedChainId (EIP-155 replay protection
 *     check). A mismatch returns false; the caller surfaces a
 *     descriptive RPC error.
 *   - Recovers `sender` from the signature using secp256k1.
 *
 * Returns false on any malformed envelope or sig-recovery failure.
 * On success `out` is fully populated and `out.sender` is the
 * 20-byte address that signed the transaction.
 */
bool DecodeRawEthTx(const std::vector<uint8_t>& wire,
                    uint64_t expectedChainId,
                    DecodedRawTx& out);

/**
 * Compute the Ethereum-style tx hash: keccak256 of the full signed
 * wire bytes (including the typed-tx envelope byte for EIP-1559).
 * That's what `eth_sendRawTransaction` returns to the caller and
 * what dApps key receipts/logs by.
 */
uint256 EthTxHash(const std::vector<uint8_t>& wire);

} // namespace evm

#endif // RAPTOREUM_EVM_RAWTX_H
