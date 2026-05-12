// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/precompiles.h>

#include <assets/assets.h>
#include <hash.h>
#include <validation.h>

#include <cstring>
#include <string>

namespace evm {

// ----------------------------------------------------------------------
// Phase 4.1 — Smart Assets ERC-20 precompile (per-asset addresses).
//
// One precompile address PER Smart Asset, derived deterministically
// from the assetId:
//
//   precompile_address = 0xA55E70<6 bytes 0> || hash160(assetId)
//
// The high 8 bytes (the 0xA55E70_00_00_00_00_00 marker) reserve the
// address space; per A11 the CREATE-collision rule refuses to deploy
// user contracts into this range. The low 12 bytes encode hash160
// of the assetId string, giving a 1:1 mapping (modulo cryptographic
// collisions, which require finding ripemd160-of-sha256 preimages).
//
// Solidity-callable surface — standard ERC-20 reads:
//
//   interface IRtmAsset {
//       function name()         external view returns (string);
//       function symbol()       external view returns (string);
//       function decimals()     external view returns (uint8);
//       function totalSupply()  external view returns (uint256);
//       function balanceOf(address) external view returns (uint256);
//   }
//
// MVP scope: metadata reads (name/symbol/decimals/totalSupply) +
// balanceOf returning 0. The bidirectional Smart Asset ↔ EVM balance
// mirror per D4-revised lives in a separate iteration (the "T-mirror"
// test in the project plan is its acceptance gate). transfer /
// transferFrom / approve / allowance arrive together with the
// mirror — they require coordinated mutations to BOTH the
// CAssetsCache AND an EVM-side allowance map, and the consensus
// rules for ordering. For Phase 4.1 we ship the read surface so
// MetaMask "Add Token" works against a Smart Asset address even
// before transfers cross the bridge.
//
// Lookup strategy from precompile address -> assetId:
//   * Cheap version (current): linear scan over passetsCache->mapAsset,
//     computing hash160 of each assetId. O(N) per call. Fine for
//     <1000 assets on regtest/testnet.
//   * Production-ready: a hash160 -> assetId index in CAssetsCache,
//     maintained at asset-creation time. Tracked as a FUP under
//     D4-revised.

namespace {

constexpr int64_t kPrecompileGasCost = 5000;

// keccak256 selectors:
//   name()         -> 06fdde03
//   symbol()       -> 95d89b41
//   decimals()     -> 313ce567
//   totalSupply()  -> 18160ddd
//   balanceOf(address) -> 70a08231
constexpr uint32_t kSelName        = 0x06FDDE03;
constexpr uint32_t kSelSymbol      = 0x95D89B41;
constexpr uint32_t kSelDecimals    = 0x313CE567;
constexpr uint32_t kSelTotalSupply = 0x18160DDD;
constexpr uint32_t kSelBalanceOf   = 0x70A08231;

uint32_t ReadSelector(const std::vector<uint8_t>& input)
{
    if (input.size() < 4) return 0;
    return (uint32_t(input[0]) << 24) | (uint32_t(input[1]) << 16) |
           (uint32_t(input[2]) << 8)  |  uint32_t(input[3]);
}

std::vector<uint8_t> ArgsSlice(const std::vector<uint8_t>& input)
{
    if (input.size() < 4) return {};
    return std::vector<uint8_t>(input.begin() + 4, input.end());
}

// Extract the low 12 bytes of the precompile address — this is the
// truncated hash160(assetId) tag. (Yes, only 12 of the 20 hash160
// bytes; the high 8 bytes hold the 0xA55E70 reservation marker.
// Collisions in the 12-byte tag are extremely unlikely for the
// asset population sizes we care about; if a clash ever happens,
// asset creation registers a longer suffix — a follow-up.)
std::vector<uint8_t> AddressToAssetTag(const evmc::address& addr)
{
    return std::vector<uint8_t>(addr.bytes + 8, addr.bytes + 20);
}

// Resolve the precompile address to an assetId via linear scan
// over passetsCache->mapAsset. Returns "" on miss.
std::string ResolveAssetIdFromAddress(const evmc::address& addr)
{
    if (!passetsCache) return {};
    const std::vector<uint8_t> tag = AddressToAssetTag(addr);
    LOCK(cs_main);
    for (const auto& kv : passetsCache->mapAsset) {
        const std::string& assetId = kv.first;
        const std::vector<unsigned char> bytes(assetId.begin(), assetId.end());
        const uint160 h160 = Hash160(bytes);
        // h160 layout: 20 bytes; we want bytes [8..20) to match the tag.
        if (std::memcmp(h160.begin() + 8, tag.data(), 12) == 0) {
            return assetId;
        }
    }
    return {};
}

// ABI-encode a string: head slot containing offset to tail, then
// at tail the length and the bytes (padded). The single-arg-returns-
// string case has the head at offset 0 and tail at offset 32.
std::vector<uint8_t> AbiEncodeStringResult(const std::string& s)
{
    std::vector<uint8_t> out;
    AbiWriteDynamicHead(out, /*tailOffset=*/ 32);
    AbiWriteDynamicBytesTail(out,
        std::vector<uint8_t>(s.begin(), s.end()));
    return out;
}

} // anonymous namespace

evmc::Result ExecuteAssetErc20Precompile(CEvmHost& /*host*/,
                                         const evmc_message& msg)
{
    if (msg.gas < kPrecompileGasCost) {
        return PrecompileFailure(msg.gas);
    }

    const std::vector<uint8_t> input(msg.input_data,
                                    msg.input_data + msg.input_size);
    const uint32_t selector = ReadSelector(input);
    const std::vector<uint8_t> args = ArgsSlice(input);

    const std::string assetId = ResolveAssetIdFromAddress(msg.code_address);
    if (assetId.empty()) {
        // Address in the asset prefix but no matching asset.
        return PrecompileFailure(msg.gas);
    }
    CAssetMetaData meta;
    {
        LOCK(cs_main);
        if (!passetsCache || !passetsCache->GetAssetMetaData(assetId, meta)) {
            return PrecompileFailure(msg.gas);
        }
    }

    std::vector<uint8_t> output;

    switch (selector) {

    // -- name() returns (string) ------------------------------------
    case kSelName: {
        output = AbiEncodeStringResult(meta.name);
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    // -- symbol() returns (string) ----------------------------------
    // Smart Assets don't carry a distinct "symbol" field; we surface
    // the asset name in both name() and symbol() to keep Solidity
    // ERC-20 clients happy. A future asset-metadata extension can
    // add a separate ticker.
    case kSelSymbol: {
        output = AbiEncodeStringResult(meta.name);
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    // -- decimals() returns (uint8) ---------------------------------
    case kSelDecimals: {
        AbiWriteUint8(output, meta.decimalPoint);
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    // -- totalSupply() returns (uint256) ----------------------------
    case kSelTotalSupply: {
        // CAssetMetaData::circulatingSupply is a CAmount (int64). For
        // ABI uint256 we widen via the uint64 writer (sufficient
        // until per-asset supply exceeds 2^63; CAmount is signed
        // int64 so anything above that is already invalid).
        const int64_t supply = meta.circulatingSupply;
        AbiWriteUint64(output, supply < 0 ? 0 : static_cast<uint64_t>(supply));
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    // -- balanceOf(address) returns (uint256) -----------------------
    // MVP: always 0 until the D4-revised bidirectional mirror lands.
    // The map key in CAssets::mapAssetAddressAmount is a base58
    // string (UTXO-style P2PKH) — EVM 20-byte addresses derive from
    // a different hash of the same pubkey and don't appear in that
    // map. Real bridging happens via the wrap/unwrap pattern (Q-A2).
    case kSelBalanceOf: {
        if (args.size() < 32) return PrecompileFailure(msg.gas);
        uint160 addr;
        if (!AbiReadAddress(args, 0, addr)) {
            return PrecompileFailure(msg.gas);
        }
        AbiWriteUint64(output, 0);
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    default:
        return PrecompileFailure(msg.gas);
    }
}

} // namespace evm
