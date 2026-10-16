// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/account.h>

#include <tinyformat.h>

#include <cstring>

namespace evm {

namespace {

// Build a uint256 from a 32-byte big-endian byte sequence (byte[0] = MSB).
// This is the byte-order convention our EVM stack uses everywhere
// (CEvmHost::FromUint256 / get_balance / Keccak256 all memcpy directly into
// the uint256's bytes, treating byte[0] as MSB). uint256S, by contrast,
// parses for the Bitcoin Core "hash" convention which is byte-reversed
// for display — the two conventions disagree on codeHash equality, so we
// avoid uint256S here.
uint256 BigEndianFromBytes(const uint8_t (&src)[32])
{
    uint256 out;
    std::memcpy(out.begin(), src, 32);
    return out;
}

} // namespace

// Canonical Keccak-256 of the empty byte string, pinned per the Ethereum
// yellow paper. Every EOA (and every account that has never deployed code)
// uses this as its codeHash. Stored in big-endian byte order so it
// compares equal to evm::Keccak256(empty) output.
const uint256& CEvmAccount::EmptyCodeHash()
{
    static constexpr uint8_t kBytes[32] = {
        0xc5, 0xd2, 0x46, 0x01, 0x86, 0xf7, 0x23, 0x3c,
        0x92, 0x7e, 0x7d, 0xb2, 0xdc, 0xc7, 0x03, 0xc0,
        0xe5, 0x00, 0xb6, 0x53, 0xca, 0x82, 0x27, 0x3b,
        0x7b, 0xfa, 0xd8, 0x04, 0x5d, 0x85, 0xa4, 0x70,
    };
    static const uint256 kEmptyCodeHash = BigEndianFromBytes(kBytes);
    return kEmptyCodeHash;
}

// Merkle-Patricia-Trie root of the empty trie. Same byte-order rationale
// as EmptyCodeHash above.
const uint256& CEvmAccount::EmptyStorageRoot()
{
    static constexpr uint8_t kBytes[32] = {
        0x56, 0xe8, 0x1f, 0x17, 0x1b, 0xcc, 0x55, 0xa6,
        0xff, 0x83, 0x45, 0xe6, 0x92, 0xc0, 0xf8, 0x6e,
        0x5b, 0x48, 0xe0, 0x1b, 0x99, 0x6c, 0xad, 0xc0,
        0x01, 0x62, 0x2f, 0xb5, 0xe3, 0x63, 0xb4, 0x21,
    };
    static const uint256 kEmptyStorageRoot = BigEndianFromBytes(kBytes);
    return kEmptyStorageRoot;
}

bool CEvmAccount::IsEmpty() const
{
    return nonce == 0 &&
           balance.IsNull() &&
           codeHash == EmptyCodeHash() &&
           storageRoot == EmptyStorageRoot();
}

std::string CEvmAccount::ToString() const
{
    return strprintf(
        "CEvmAccount(nonce=%u, balance=%s, codeHash=%s..., storageRoot=%s...)",
        nonce,
        balance.ToString(),
        codeHash.ToString().substr(0, 16),
        storageRoot.ToString().substr(0, 16));
}

} // namespace evm
