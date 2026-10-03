// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/signing.h>

#include <evm/hashing.h>
#include <evm/rlp.h>
#include <key.h>
#include <pubkey.h>
#include <uint256.h>

#include <cstring>

namespace evm {

namespace {

// Drop the 0x04 prefix byte from an uncompressed pubkey and return
// the raw 64-byte X||Y coordinates suitable for keccak hashing into
// an EVM address.
std::vector<uint8_t> UncompressedPubkeyCoords(const CKey& key)
{
    // Derive the *uncompressed* pubkey. CKey::GetPubKey honors the
    // key's fCompressed flag, so we Decompress() after fetch to be
    // sure we get the 65-byte form.
    CPubKey pubkey = key.GetPubKey();
    if (pubkey.IsCompressed()) {
        pubkey.Decompress();
    }
    if (pubkey.size() != 65) return {};
    if (pubkey.begin()[0] != 0x04) return {};
    return std::vector<uint8_t>(pubkey.begin() + 1, pubkey.begin() + 65);
}

// Encode a 32-byte big-endian quantity as an RLP byte string with
// leading-zero bytes stripped — required for signing payload r/s
// to round-trip with the canonical form that ethers/eth-account
// emit.
std::vector<uint8_t> Bytes32Stripped(const uint256& word)
{
    int start = 0;
    while (start < 32 && *(word.begin() + start) == 0x00) ++start;
    if (start == 32) return {};
    return std::vector<uint8_t>(word.begin() + start, word.begin() + 32);
}

// Build the unsigned signing-payload RLP body: 9 fields without the
// trailing y_parity / r / s. The caller wraps with the 0x02 envelope
// byte before hashing.
std::vector<uint8_t> BuildEip1559UnsignedRlp(const Eip1559TxFields& f)
{
    std::vector<uint8_t> inner;
    auto append = [&](const std::vector<uint8_t>& enc) {
        inner.insert(inner.end(), enc.begin(), enc.end());
    };
    append(RlpEncodeUint(f.chainId));
    append(RlpEncodeUint(f.nonce));
    append(RlpEncodeUint(f.maxPriorityFeePerGas));
    append(RlpEncodeUint(f.maxFeePerGas));
    append(RlpEncodeUint(f.gasLimit));
    // `to`: 20-byte address for CALL, empty string for CREATE.
    if (f.emptyTo) {
        append(RlpEncodeBytes(nullptr, 0));
    } else {
        append(RlpEncodeBytes(f.to.begin(), 20));
    }
    append(RlpEncodeUint(f.value));
    append(RlpEncodeBytes(f.data));
    // Empty access list -> rlp([]) -> 0xC0.
    append(RlpEncodeList(std::vector<uint8_t>{}));
    return RlpEncodeList(inner);
}

// Build the EIP-155 signing-payload RLP body:
//   [nonce, gasPrice, gasLimit, to, value, data, chainId, 0, 0].
std::vector<uint8_t> BuildLegacyUnsignedRlp(const LegacyTxFields& f)
{
    std::vector<uint8_t> inner;
    auto append = [&](const std::vector<uint8_t>& enc) {
        inner.insert(inner.end(), enc.begin(), enc.end());
    };
    append(RlpEncodeUint(f.nonce));
    append(RlpEncodeUint(f.gasPrice));
    append(RlpEncodeUint(f.gasLimit));
    if (f.emptyTo) {
        append(RlpEncodeBytes(nullptr, 0));
    } else {
        append(RlpEncodeBytes(f.to.begin(), 20));
    }
    append(RlpEncodeUint(f.value));
    append(RlpEncodeBytes(f.data));
    append(RlpEncodeUint(f.chainId));  // EIP-155
    append(RlpEncodeUint(0));          // empty r placeholder
    append(RlpEncodeUint(0));          // empty s placeholder
    return RlpEncodeList(inner);
}

} // anonymous namespace

uint160 EvmAddressForKey(const CKey& privKey)
{
    const std::vector<uint8_t> coords = UncompressedPubkeyCoords(privKey);
    if (coords.size() != 64) return uint160{};
    const uint256 h = Keccak256(coords);
    uint160 out;
    std::memcpy(out.begin(), h.begin() + 12, 20);
    return out;
}

std::vector<uint8_t> SignEip1559Tx(const CKey& privKey,
                                   const Eip1559TxFields& fields)
{
    // 1. Build the unsigned payload and compute the signing hash
    //    msgHash = keccak256(0x02 || rlp([9 fields])).
    const std::vector<uint8_t> innerRlp = BuildEip1559UnsignedRlp(fields);
    std::vector<uint8_t> sigPayload;
    sigPayload.reserve(1 + innerRlp.size());
    sigPayload.push_back(0x02);
    sigPayload.insert(sigPayload.end(), innerRlp.begin(), innerRlp.end());
    const uint256 msgHash = Keccak256(sigPayload);

    // 2. Sign via secp256k1 compact ECDSA. CKey::SignCompact emits
    //    a 65-byte (header, r, s) blob where the header is
    //    27 + recid + (compressed ? 4 : 0). We strip the compressed
    //    bit and convert recid {0,1} into the EIP-1559 y_parity.
    std::vector<unsigned char> compact;
    if (!privKey.SignCompact(msgHash, compact)) return {};
    if (compact.size() != 65) return {};
    const uint8_t header = compact[0];
    // SignCompact derives recid from the compressed-keyed signature
    // path; we want the recid as the y_parity for the *uncompressed*
    // pubkey since the EVM address is from the uncompressed form.
    // recid extraction: (header - 27) & 3 — same as CPubKey::Recover.
    const int recid = (header - 27) & 3;
    if (recid > 1) {
        // EIP-1559 y_parity is restricted to {0, 1}; values 2/3 are
        // not used for the secp256k1 curves Ethereum signs with.
        return {};
    }
    const uint8_t yParity = static_cast<uint8_t>(recid);
    uint256 r, s;
    std::memcpy(r.begin(), compact.data() + 1,  32);
    std::memcpy(s.begin(), compact.data() + 33, 32);

    // 3. Build the full signed RLP body: the 9 unsigned fields plus
    //    y_parity, r, s. Then 0x02 || rlp([12 fields]).
    std::vector<uint8_t> signedInner;
    auto append = [&](const std::vector<uint8_t>& enc) {
        signedInner.insert(signedInner.end(), enc.begin(), enc.end());
    };
    append(RlpEncodeUint(fields.chainId));
    append(RlpEncodeUint(fields.nonce));
    append(RlpEncodeUint(fields.maxPriorityFeePerGas));
    append(RlpEncodeUint(fields.maxFeePerGas));
    append(RlpEncodeUint(fields.gasLimit));
    if (fields.emptyTo) {
        append(RlpEncodeBytes(nullptr, 0));
    } else {
        append(RlpEncodeBytes(fields.to.begin(), 20));
    }
    append(RlpEncodeUint(fields.value));
    append(RlpEncodeBytes(fields.data));
    append(RlpEncodeList(std::vector<uint8_t>{}));
    append(RlpEncodeUint(static_cast<uint64_t>(yParity)));
    {
        const std::vector<uint8_t> rStripped = Bytes32Stripped(r);
        append(RlpEncodeBytes(rStripped));
    }
    {
        const std::vector<uint8_t> sStripped = Bytes32Stripped(s);
        append(RlpEncodeBytes(sStripped));
    }
    const std::vector<uint8_t> finalRlp = RlpEncodeList(signedInner);
    std::vector<uint8_t> wire;
    wire.reserve(1 + finalRlp.size());
    wire.push_back(0x02);
    wire.insert(wire.end(), finalRlp.begin(), finalRlp.end());
    return wire;
}

std::vector<uint8_t> SignLegacyTx(const CKey& privKey,
                                  const LegacyTxFields& fields)
{
    // EIP-155 requires a non-zero chainId for replay protection.
    if (fields.chainId == 0) return {};

    // 1. msgHash = keccak256(rlp([nonce,gasPrice,gasLimit,to,value,data,
    //    chainId,0,0])). Unlike type-0x02 there is NO envelope byte.
    const uint256 msgHash = Keccak256(BuildLegacyUnsignedRlp(fields));

    // 2. secp256k1 compact ECDSA, recid -> y_parity {0,1}.
    std::vector<unsigned char> compact;
    if (!privKey.SignCompact(msgHash, compact)) return {};
    if (compact.size() != 65) return {};
    const int recid = (compact[0] - 27) & 3;
    if (recid > 1) return {};
    uint256 r, s;
    std::memcpy(r.begin(), compact.data() + 1,  32);
    std::memcpy(s.begin(), compact.data() + 33, 32);

    // EIP-155: v = chainId*2 + 35 + y_parity.
    const uint64_t v = fields.chainId * 2 + 35 + static_cast<uint64_t>(recid);

    // 3. Signed body: [nonce, gasPrice, gasLimit, to, value, data, v, r, s].
    std::vector<uint8_t> signedInner;
    auto append = [&](const std::vector<uint8_t>& enc) {
        signedInner.insert(signedInner.end(), enc.begin(), enc.end());
    };
    append(RlpEncodeUint(fields.nonce));
    append(RlpEncodeUint(fields.gasPrice));
    append(RlpEncodeUint(fields.gasLimit));
    if (fields.emptyTo) {
        append(RlpEncodeBytes(nullptr, 0));
    } else {
        append(RlpEncodeBytes(fields.to.begin(), 20));
    }
    append(RlpEncodeUint(fields.value));
    append(RlpEncodeBytes(fields.data));
    append(RlpEncodeUint(v));
    append(RlpEncodeBytes(Bytes32Stripped(r)));
    append(RlpEncodeBytes(Bytes32Stripped(s)));
    // No type-envelope byte for legacy.
    return RlpEncodeList(signedInner);
}

} // namespace evm
