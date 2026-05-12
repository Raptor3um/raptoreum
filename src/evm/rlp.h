// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_RLP_H
#define RAPTOREUM_EVM_RLP_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace evm {

/**
 * Recursive-Length Prefix (RLP) — the canonical serialization used by
 * Ethereum for transactions, block headers, account state, and the
 * receipt trie. Phase 3.5 needs it to decode the raw bytes that
 * `eth_sendRawTransaction` accepts (a wallet-signed EIP-1559 or
 * legacy EIP-155 transaction).
 *
 * The Phase 2.3b hashing.cpp shipped a minimal *encoder* for the
 * single case of `rlp([sender, nonce])` (CREATE address derivation).
 * This module gives us the full encode + decode surface needed by
 * the JSON-RPC layer.
 *
 * Spec: https://ethereum.org/en/developers/docs/data-structures-
 *       and-encoding/rlp/ (yellow paper Appendix B).
 *
 * Design notes:
 *
 *   - Decoded values are either bytes (`std::vector<uint8_t>`) or
 *     lists of further `RlpValue`s. RLP itself doesn't distinguish
 *     "empty string" from "uint zero" — both serialise to 0x80 —
 *     so the caller decides on interpretation.
 *
 *   - Encoder helpers mirror what the Ethereum tx signing path
 *     needs: RlpEncodeBytes for byte strings, RlpEncodeUint for
 *     stripped big-endian integers, RlpEncodeList that takes the
 *     pre-encoded payload of the inner items.
 *
 *   - Decoder is bounds-checked end-to-end. Malformed input
 *     (truncation, oversized lengths, leading-zero length bytes
 *     where the short form would have worked) returns false from
 *     the entry points. We deliberately reject the non-canonical
 *     forms because consensus-affecting code downstream
 *     (sendRawTransaction → tx hash) needs determinism.
 *
 *   - There is no streaming API; eth_sendRawTransaction blobs are
 *     bounded (≤24 KiB) so a fully-materialised parse tree is fine.
 */

struct RlpValue
{
    bool isList{false};
    std::vector<uint8_t> bytes;   // valid when !isList
    std::vector<RlpValue> items;  // valid when isList
};

// ----------------------------------------------------------------------
// Encoder
// ----------------------------------------------------------------------

/** Encode a byte string per RLP rules. */
std::vector<uint8_t> RlpEncodeBytes(const std::vector<uint8_t>& data);
std::vector<uint8_t> RlpEncodeBytes(const uint8_t* data, size_t len);

/** Encode a non-negative integer per RLP rules.
 *  rlp(0) == 0x80 (empty byte string).
 *  rlp(N>0) == big-endian minimal-length bytes, then RlpEncodeBytes.
 */
std::vector<uint8_t> RlpEncodeUint(uint64_t value);

/** Encode a list whose pre-encoded payload is the concatenation of
 *  the inner items' RLP encodings. */
std::vector<uint8_t> RlpEncodeList(const std::vector<uint8_t>& payload);

/** Encode a list of pre-built RlpValues by recursively encoding
 *  each item then wrapping with RlpEncodeList. */
std::vector<uint8_t> RlpEncode(const RlpValue& value);

// ----------------------------------------------------------------------
// Decoder
// ----------------------------------------------------------------------

/** Decode a single RLP value from `data`. Returns false on any
 *  framing error (truncation, oversize, non-canonical-length form).
 *  On success, `consumed` is the number of bytes read and `out`
 *  holds the value. The caller checks `consumed == data.size()`
 *  when expecting a single top-level value with no trailing garbage. */
bool RlpDecode(const uint8_t* data, size_t size, RlpValue& out, size_t& consumed);

/** Convenience: top-level decode that also asserts the entire input
 *  was consumed. */
bool RlpDecode(const std::vector<uint8_t>& data, RlpValue& out);

/** Interpret an RlpValue's bytes as a stripped big-endian uint64.
 *  Returns false if the value is a list, larger than 8 bytes, or
 *  has a leading-zero byte (non-canonical). */
bool RlpValueAsUint64(const RlpValue& v, uint64_t& out);

} // namespace evm

#endif // RAPTOREUM_EVM_RLP_H
