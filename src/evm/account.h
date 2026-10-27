// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_ACCOUNT_H
#define RAPTOREUM_EVM_ACCOUNT_H

#include <serialize.h>
#include <uint256.h>

#include <cstdint>

namespace evm {

/**
 * Persistent state of a single EVM account (Phase 2.1).
 *
 * Mirrors the standard Ethereum account record:
 *
 *   account = (nonce, balance, codeHash, storageRoot)
 *
 * Fields use 32-byte big-endian encoding for the 256-bit quantities so
 * the on-disk form is identical to what an Ethereum geth/erigon node
 * would write. The arithmetic happens elsewhere (evmone via intx)
 * where the bytes are interpreted as little- or big-endian as needed.
 *
 * An "empty" account — newly observed but never written — has:
 *   nonce       = 0
 *   balance     = 0
 *   codeHash    = keccak256("")  (well-known constant
 *                                  c5d2460186f7233c...85a470)
 *   storageRoot = keccak256(rlp(empty trie))
 *
 * EOAs (externally-owned accounts) keep codeHash at keccak256("") and
 * storageRoot at the empty-trie root for their entire lifetime.
 * Contracts diverge from these values on deployment.
 */
class CEvmAccount
{
public:
    /** Sequence number; incremented on each tx originated from this account
     *  (CALL or CREATE). Used for replay protection (EIP-155) and CREATE
     *  address derivation. */
    uint64_t nonce{0};

    /** Account balance in RTM weis (1 RTM = 10^18 weis), 256-bit. */
    uint256 balance;

    /** Keccak-256 hash of the deployed bytecode at this address, or
     *  keccak256("") for accounts that never deployed code (EOAs). */
    uint256 codeHash;

    /** Merkle-Patricia-Trie root of the contract's storage, or the
     *  empty-trie root for accounts with no SSTORE'd slots. */
    uint256 storageRoot;

    CEvmAccount() = default;

    CEvmAccount(uint64_t n, const uint256& bal,
                const uint256& ch, const uint256& sr)
        : nonce(n), balance(bal), codeHash(ch), storageRoot(sr) {}

    SERIALIZE_METHODS(CEvmAccount, obj)
    {
        READWRITE(obj.nonce);
        READWRITE(obj.balance);
        READWRITE(obj.codeHash);
        READWRITE(obj.storageRoot);
    }

    bool operator==(const CEvmAccount& other) const
    {
        return nonce == other.nonce &&
               balance == other.balance &&
               codeHash == other.codeHash &&
               storageRoot == other.storageRoot;
    }

    bool operator!=(const CEvmAccount& other) const
    {
        return !(*this == other);
    }

    /** Returns the canonical Keccak-256 of the empty byte string,
     *  c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470.
     *  This is the codeHash of every account that has never deployed
     *  code (i.e., every EOA). */
    static const uint256& EmptyCodeHash();

    /** Returns the Merkle-Patricia-Trie root of the empty trie,
     *  56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421.
     *  Used as the initial storageRoot for any account with no
     *  SSTORE'd slots. */
    static const uint256& EmptyStorageRoot();

    /** Returns true when this record has never been written:
     *  default-constructed nonce/balance and the canonical
     *  empty-code/empty-storage hashes. */
    bool IsEmpty() const;

    std::string ToString() const;
};

} // namespace evm

#endif // RAPTOREUM_EVM_ACCOUNT_H
