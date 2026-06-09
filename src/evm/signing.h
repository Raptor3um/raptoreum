// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_SIGNING_H
#define RAPTOREUM_EVM_SIGNING_H

#include <key.h>
#include <uint256.h>

#include <cstdint>
#include <vector>

class uint160;

namespace evm {

/**
 * Phase 5 — server-side EIP-1559 transaction signing.
 *
 * Two pure primitives:
 *
 *   1. EvmAddressForKey(CKey) -> uint160
 *      Derives the EVM-style 20-byte address from a secp256k1
 *      private key:
 *          address = keccak256(uncompressed_pubkey[1..65])[12..32]
 *      The leading 0x04 byte of the uncompressed-form pubkey is
 *      dropped before hashing (the standard form).
 *
 *   2. SignEip1559Tx(privKey, fields) -> wire bytes
 *      Builds + signs an EIP-1559 (type 0x02) transaction. The
 *      output bytes are directly consumable by the existing
 *      eth_sendRawTransaction handler — no client-side post-
 *      processing needed. Same RLP framing and signing payload
 *      that ethers.js / eth-account produce.
 *
 * Use cases:
 *   - The wallet-side Phase 5.x RPC commands (evm_signTransaction,
 *     evm_sendTransaction) call these to sign for users.
 *   - Future Phase 6 functional tests construct signed txs via
 *     these primitives without depending on Python eth-account.
 *
 * Wallet-keystore integration (HD path / encrypted vault access)
 * is intentionally NOT part of this module. The caller supplies
 * a CKey; sourcing that CKey from a wallet, a hardware device,
 * or stdin is the caller's concern.
 */

/** Fields the caller must supply to sign an EIP-1559 transaction. */
struct Eip1559TxFields
{
    uint64_t chainId{0};
    uint64_t nonce{0};
    uint64_t maxPriorityFeePerGas{0};
    uint64_t maxFeePerGas{0};
    uint64_t gasLimit{0};
    bool emptyTo{false};      // true = contract creation (CEvmDeployTx)
    uint160 to;               // valid when !emptyTo
    uint64_t value{0};        // in weis
    std::vector<uint8_t> data; // calldata or init bytecode
};

/** Fields the caller must supply to sign a legacy (pre-EIP-1559,
 *  EIP-155-replay-protected, type 0x0) transaction. */
struct LegacyTxFields
{
    uint64_t chainId{0};      // EIP-155 replay protection; must be > 0 here
    uint64_t nonce{0};
    uint64_t gasPrice{0};     // single gas price (no base/priority split)
    uint64_t gasLimit{0};
    bool emptyTo{false};      // true = contract creation
    uint160 to;               // valid when !emptyTo
    uint64_t value{0};        // in weis
    std::vector<uint8_t> data; // calldata or init bytecode
};

/** Compute the 20-byte EVM address for a secp256k1 private key. */
uint160 EvmAddressForKey(const CKey& privKey);

/** Build a signed EIP-1559 (type-0x02) wire-format transaction.
 *
 * Returns the full envelope (0x02 || rlp([12 fields])) suitable for
 * eth_sendRawTransaction. On signing failure returns empty.
 */
std::vector<uint8_t> SignEip1559Tx(const CKey& privKey,
                                   const Eip1559TxFields& fields);

/** Build a signed legacy (EIP-155, type-0x0) wire-format transaction.
 *
 * Returns rlp([nonce, gasPrice, gasLimit, to, value, data, v, r, s]) with
 * v = chainId*2 + 35 + y_parity (EIP-155), NO type-envelope byte — the
 * form older tooling / hardware wallets emit. Directly consumable by
 * eth_sendRawTransaction (which already decodes legacy). Empty on failure
 * or chainId == 0 (this build requires EIP-155 replay protection). */
std::vector<uint8_t> SignLegacyTx(const CKey& privKey,
                                  const LegacyTxFields& fields);

} // namespace evm

#endif // RAPTOREUM_EVM_SIGNING_H
