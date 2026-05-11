// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/account.h>

#include <tinyformat.h>

namespace evm {

// Canonical Keccak-256 of the empty byte string, pinned per the Ethereum
// yellow paper. Every EOA (and every account that has never deployed code)
// uses this as its codeHash.
const uint256& CEvmAccount::EmptyCodeHash()
{
    static const uint256 kEmptyCodeHash = uint256S(
        "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
    return kEmptyCodeHash;
}

// Merkle-Patricia-Trie root of the empty trie. This is the storageRoot
// for any account that has never executed SSTORE.
const uint256& CEvmAccount::EmptyStorageRoot()
{
    static const uint256 kEmptyStorageRoot = uint256S(
        "56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421");
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
