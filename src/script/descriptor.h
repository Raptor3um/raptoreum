// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_DESCRIPTOR_H
#define BITCOIN_SCRIPT_DESCRIPTOR_H

#include <script/script.h>
#include <script/sign.h>

#include <vector>

// Descriptors are strings that describe a set of scriptPubKeys, together with
// all information necessary to solve them. By combining all information into
// one, they avoid the need to separately import keys and scripts.
//
// Descriptors may be ranged, which occurs when the public keys inside are
// specified in the form of HD chains (xpubs).
//
// Descriptors always represent public information - public keys and scripts -
// but in cases where private keys need to be conveyed along with a descriptor,
// they can be included inside by changing public keys to private keys (WIF
// format), and changing xpubs by xprvs.
//
// 1. Examples
//
// A P2PK descriptor with a fixed public key:
// - pk(0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798)
//
// A P2SH-P2WSH-P2PKH descriptor with a fixed public key:
// - sh(wsh(pkh(02e493dbf1c10d80f3581e4904930b1404cc6c13900ee0758474fa94abe8c4cd13)))
//
// A bare 1-of-2 multisig descriptor:
// - multi(1,022f8bde4d1a07209355b4a7250a5c5128e88b84bddc619ab7cba8d569b240efe4,025cbdf0646e5db4eaa398f365f2ea7a0e3d419b7e0330e39ce92bddedcac4f9bc)
//
// A chain of P2PKH outputs (this needs the corresponding private key to derive):
// - pkh(xpub68Gmy5EdvgibQVfPdqkBBCHxA5htiqg55crXYuXoQRKfDBFA1WEjWgP6LHhwBZeNK1VTsfTFUHCdrfp1bgwQ9xv5ski8PX9rL2dZXvgGDnw/1'/2/*)
//
// 2. Grammar description:
//
// X: xpub or xprv encoded extended key
// I: decimal encoded integer
// H: Hex encoded byte array
// A: Address in P2PKH, P2SH, or Bech32 encoding
//
// S (Scripts):
// * pk(P): Pay-to-pubkey (P2PK) output for public key P.
// * pkh(P): Pay-to-pubkey-hash (P2PKH) output for public key P.
// * sh(S): Pay-to-script-hash (P2SH) output for script S
// * combo(P): combination of P2PK and P2PKH for public key P.
// * multi(I,L): k-of-n multisig for given public keys
// * addr(A): Output to address
// * raw(H): scriptPubKey with raw bytes
//
// P (Public keys):
// * H: fixed public key (or WIF-encoded private key)
// * E: extended public key
// * E/*: (ranged) all unhardened direct children of an extended public key
// * E/*': (ranged) all hardened direct children of an extended public key
//
// L (Comma-separated lists of public keys):
// * P
// * L,P
//
// E (Extended public keys):
// * X
// * E/I: unhardened child
// * E/I': hardened child
// * E/Ih: hardened child (alternative notation)
//
// The top level is S.

/** Interface for parsed descriptor objects. */
struct Descriptor {
    virtual ~Descriptor() = default;

    /** Whether the expansion of this descriptor depends on the position. */
    virtual bool IsRange() const = 0;

    /** Whether this descriptor has all information about signing ignoring lack of private keys.
     *  This is true for all descriptors except ones that use `raw` or `addr` constructions. */
    virtual bool IsSolvable() const = 0;

    /** Convert the descriptor back to a string, undoing parsing. */
    virtual std::string ToString() const = 0;

    /** Convert the descriptor to a private string. This fails if the provided provider does not have the relevant private keys. */
    virtual bool ToPrivateString(const SigningProvider &provider, std::string &out) const = 0;

    /** Expand a descriptor at a specified position.
     *
     * pos: the position at which to expand the descriptor. If IsRange() is false, this is ignored.
     * provider: the provider to query for private keys in case of hardened derivation.
     * output_script: the expanded scriptPubKeys will be put here.
     * out: scripts and public keys necessary for solving the expanded scriptPubKeys will be put here (may be equal to provider).
     * cache: vector which will be overwritten with cache data necessary to-evaluate the descriptor at this point without access to private keys.
     */
    virtual bool
    Expand(int pos, const SigningProvider &provider, std::vector <CScript> &output_scripts, FlatSigningProvider &out,
           std::vector<unsigned char> *cache = nullptr) const = 0;

    /** Expand a descriptor at a specified position using cached expansion data.
     *
     * pos: the position at which to expand the descriptor. If IsRange() is false, this is ignored.
     * cache: vector from which cached expansion data will be read.
     * output_script: the expanded scriptPubKeys will be put here.
     * out: scripts and public keys necessary for solving the expanded scriptPubKeys will be put here (may be equal to provider).
     */
    virtual bool
    ExpandFromCache(int pos, const std::vector<unsigned char> &cache, std::vector <CScript> &output_scripts,
                    FlatSigningProvider &out) const = 0;
};

/** Parse a descriptor string. Included private keys are put in out.
 *
 * If the descriptor has a checksum, it must be valid. If require_checksum
 * is set, the checksum is mandatory - otherwise it is optional.
 *
 * If a parse error occurs, or the checksum is missing/invalid, or anything
 * else is wrong, nullptr is returned.
 */
std::unique_ptr <Descriptor>
Parse(const std::string &descriptor, FlatSigningProvider &out, std::string &error, bool require_checksum = false);

/** Get the checksum for a descriptor.
 *
 * If it already has one, and it is correct, return the checksum in the input.
 * If it already has one that is wrong, return "".
 * If it does not already have one, return the checksum that would need to be added.
 */
std::string GetDescriptorChecksum(const std::string &descriptor);

/** Find a descriptor for the specified script, using information from provider where possible.
 *
 * A non-ranged descriptor which only generates the specified script will be returned in all
 * circumstances.
 *
 * For public keys with key origin information, this information will be preserved in the returned
 * descriptor.
 *
 * - If all information for solving `script` is present in `provider`, a descriptor will be returned
 *   which is `IsSolvable()` and encapsulates said information.
 * - Failing that, if `script` corresponds to a known address type, an "addr()" descriptor will be
 *   returned (which is not `IsSolvable()`).
 * - Failing that, a "raw()" descriptor is returned.
 */
std::unique_ptr <Descriptor> InferDescriptor(const CScript &script, const SigningProvider &provider);

#endif // BITCOIN_SCRIPT_DESCRIPTOR_H
