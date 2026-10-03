// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/rawtx.h>

#include <evm/hashing.h>
#include <evm/rlp.h>
#include <pubkey.h>
#include <uint256.h>

#include <cstring>

namespace evm {

namespace {

// Pull a uint256 (as a stripped big-endian value) out of an RLP byte
// item. Pads on the left with zero bytes to a full 32-byte word.
// Returns false on >32 bytes.
bool RlpValueAsUint256(const RlpValue& v, uint256& out)
{
    if (v.isList) return false;
    if (v.bytes.size() > 32) return false;
    out.SetNull();
    const size_t offset = 32 - v.bytes.size();
    for (size_t i = 0; i < v.bytes.size(); ++i) {
        *(out.begin() + offset + i) = v.bytes[i];
    }
    return true;
}

// 20-byte address: must be exactly 20 bytes or empty (= contract
// creation tx, "to" left blank).
bool RlpValueAsAddress(const RlpValue& v, uint160& outAddr, bool& outEmpty)
{
    if (v.isList) return false;
    if (v.bytes.empty()) { outEmpty = true; return true; }
    if (v.bytes.size() != 20) return false;
    outEmpty = false;
    std::memcpy(outAddr.begin(), v.bytes.data(), 20);
    return true;
}

// Recover the 20-byte EVM sender address from msgHash + (r, s, parity).
//
//   1. Build a compact-sig payload understood by CPubKey::RecoverCompact:
//        [recid_with_uncompressed_flag][32 bytes r][32 bytes s]
//   2. Recover the pubkey. Uncompressed form is 65 bytes
//      [0x04 || 32 X || 32 Y]; the EVM address is the last 20 bytes
//      of keccak256(X || Y) — i.e., keccak256(pubkey + 1, 64)[12:].
//   3. Return false on any recovery failure or short pubkey.
bool RecoverEvmSender(const uint256& msgHash,
                     uint8_t yParity,
                     const uint256& r,
                     const uint256& s,
                     uint160& outAddr)
{
    if (yParity > 1) return false;
    std::vector<unsigned char> compactSig(65);
    // CPubKey::RecoverCompact uses (vchSig[0] - 27) & 3 for recid,
    // and (vchSig[0] - 27) & 4 for the "compressed" flag. We want
    // the UNCOMPRESSED pubkey to derive an EVM address, so flag=0.
    compactSig[0] = static_cast<unsigned char>(yParity + 27);
    std::memcpy(compactSig.data() + 1, r.begin(), 32);
    std::memcpy(compactSig.data() + 33, s.begin(), 32);

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(msgHash, compactSig)) return false;
    if (pubkey.size() != 65) return false;
    if (pubkey.begin()[0] != 0x04) return false;

    // Hash the 64-byte X||Y portion. Slice past the 0x04 prefix.
    std::vector<uint8_t> coordBytes(pubkey.begin() + 1, pubkey.begin() + 65);
    const uint256 h = Keccak256(coordBytes);
    std::memcpy(outAddr.begin(), h.begin() + 12, 20);
    return true;
}

// EIP-1559 envelope: 0x02 || rlp([
//   chain_id, nonce, max_priority_fee, max_fee, gas_limit, to, value,
//   data, access_list, y_parity, r, s
// ])
bool DecodeEip1559(const std::vector<uint8_t>& wire,
                   uint64_t expectedChainId,
                   DecodedRawTx& out)
{
    if (wire.size() < 2 || wire[0] != 0x02) return false;
    RlpValue body;
    {
        const uint8_t* payload = wire.data() + 1;
        const size_t payloadSize = wire.size() - 1;
        if (!RlpDecode(std::vector<uint8_t>(payload, payload + payloadSize),
                       body))
            return false;
    }
    if (!body.isList || body.items.size() != 12) return false;

    // Field 0: chain id.
    uint64_t chainId = 0;
    if (!RlpValueAsUint64(body.items[0], chainId)) return false;
    if (chainId != expectedChainId) return false;
    out.chainId = chainId;
    if (!RlpValueAsUint64(body.items[1], out.nonce)) return false;
    if (!RlpValueAsUint64(body.items[2], out.maxPriorityFeePerGas)) return false;
    if (!RlpValueAsUint64(body.items[3], out.maxFeePerGas)) return false;
    if (!RlpValueAsUint64(body.items[4], out.gasLimit)) return false;
    if (!RlpValueAsAddress(body.items[5], out.to, out.emptyTo)) return false;
    if (!RlpValueAsUint64(body.items[6], out.value)) return false;
    if (body.items[7].isList) return false;
    out.data = body.items[7].bytes;
    // Field 8: access list — accept empty; reject non-empty for now
    // (we'd need to track warm slots through to evmone; FUP).
    if (!body.items[8].isList) return false;
    if (!body.items[8].items.empty()) return false;

    // Field 9: y_parity (0 or 1).
    uint64_t yParity64 = 0;
    if (!RlpValueAsUint64(body.items[9], yParity64)) return false;
    if (yParity64 > 1) return false;
    out.yParity = static_cast<uint8_t>(yParity64);
    if (!RlpValueAsUint256(body.items[10], out.r)) return false;
    if (!RlpValueAsUint256(body.items[11], out.s)) return false;

    // Build the signing payload: 0x02 || rlp(first 9 fields).
    std::vector<uint8_t> innerPayload;
    for (size_t i = 0; i < 9; ++i) {
        std::vector<uint8_t> enc = RlpEncode(body.items[i]);
        innerPayload.insert(innerPayload.end(), enc.begin(), enc.end());
    }
    std::vector<uint8_t> sigPayload;
    sigPayload.push_back(0x02);
    std::vector<uint8_t> rlpList = RlpEncodeList(innerPayload);
    sigPayload.insert(sigPayload.end(), rlpList.begin(), rlpList.end());

    const uint256 msgHash = Keccak256(sigPayload);
    if (!RecoverEvmSender(msgHash, out.yParity, out.r, out.s, out.sender)) {
        return false;
    }

    out.txType = 2;
    out.wire = wire;
    return true;
}

// Legacy EIP-155 envelope: rlp([
//   nonce, gas_price, gas_limit, to, value, data, v, r, s
// ])
//   where v = chain_id * 2 + 35 + y_parity (for chain_id != 0).
//
// Pre-EIP-155 legacy (v in {27, 28}) is NOT accepted; modern wallets
// don't emit it and accepting it would mean re-using a chainId-less
// signature on another chain.
bool DecodeLegacy(const std::vector<uint8_t>& wire,
                  uint64_t expectedChainId,
                  DecodedRawTx& out)
{
    RlpValue body;
    if (!RlpDecode(wire, body)) return false;
    if (!body.isList || body.items.size() != 9) return false;

    if (!RlpValueAsUint64(body.items[0], out.nonce)) return false;
    uint64_t gasPrice = 0;
    if (!RlpValueAsUint64(body.items[1], gasPrice)) return false;
    out.maxFeePerGas = gasPrice;
    out.maxPriorityFeePerGas = gasPrice;
    if (!RlpValueAsUint64(body.items[2], out.gasLimit)) return false;
    if (!RlpValueAsAddress(body.items[3], out.to, out.emptyTo)) return false;
    if (!RlpValueAsUint64(body.items[4], out.value)) return false;
    if (body.items[5].isList) return false;
    out.data = body.items[5].bytes;

    uint64_t v = 0;
    if (!RlpValueAsUint64(body.items[6], v)) return false;
    if (v < 35) return false; // pre-EIP-155 not accepted
    const uint64_t chainId = (v - 35) / 2;
    if (chainId != expectedChainId) return false;
    out.chainId = chainId;
    out.yParity = static_cast<uint8_t>((v - 35) % 2);
    if (!RlpValueAsUint256(body.items[7], out.r)) return false;
    if (!RlpValueAsUint256(body.items[8], out.s)) return false;

    // Signing payload: rlp(nonce, gas_price, gas_limit, to, value,
    //                      data, chain_id, 0, 0).
    std::vector<uint8_t> innerPayload;
    for (size_t i = 0; i < 6; ++i) {
        std::vector<uint8_t> enc = RlpEncode(body.items[i]);
        innerPayload.insert(innerPayload.end(), enc.begin(), enc.end());
    }
    {
        std::vector<uint8_t> chainEnc = RlpEncodeUint(chainId);
        innerPayload.insert(innerPayload.end(), chainEnc.begin(), chainEnc.end());
        std::vector<uint8_t> zeroEnc = RlpEncodeUint(0);
        innerPayload.insert(innerPayload.end(), zeroEnc.begin(), zeroEnc.end());
        innerPayload.insert(innerPayload.end(), zeroEnc.begin(), zeroEnc.end());
    }
    std::vector<uint8_t> sigPayload = RlpEncodeList(innerPayload);
    const uint256 msgHash = Keccak256(sigPayload);
    if (!RecoverEvmSender(msgHash, out.yParity, out.r, out.s, out.sender)) {
        return false;
    }

    out.txType = 0;
    out.wire = wire;
    return true;
}

} // anonymous namespace

bool DecodeRawEthTx(const std::vector<uint8_t>& wire,
                    uint64_t expectedChainId,
                    DecodedRawTx& out)
{
    if (wire.empty()) return false;
    // Typed-tx envelope: first byte < 0x80 (per EIP-2718 the type
    // byte is in [0x00, 0x7F]; 0x80..0xBF would be valid RLP-string
    // prefixes but RLP transactions are LISTS, so they start at
    // 0xC0+). EIP-1559 specifically uses type 0x02.
    if (wire[0] == 0x02) {
        return DecodeEip1559(wire, expectedChainId, out);
    }
    if (wire[0] >= 0xC0) {
        return DecodeLegacy(wire, expectedChainId, out);
    }
    return false;
}

uint256 EthTxHash(const std::vector<uint8_t>& wire)
{
    return Keccak256(wire);
}

} // namespace evm
