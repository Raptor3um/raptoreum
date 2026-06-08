// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/precompiles.h>

#include <assets/assets.h>
#include <evm/asset_ledger.h>
#include <evm/balance.h>
#include <evm/hashing.h>
#include <evm/host.h>
#include <hash.h>
#include <validation.h>

#include <cstring>
#include <string>
#include <vector>

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
// D4 mirror — ERC-20 write surface over the EVM-side ledger.
//   transfer(address,uint256)              -> a9059cbb
//   transferFrom(address,address,uint256)  -> 23b872dd
//   approve(address,uint256)               -> 095ea7b3
//   allowance(address,address)             -> dd62ed3e
constexpr uint32_t kSelTransfer     = 0xA9059CBB;
constexpr uint32_t kSelTransferFrom = 0x23B872DD;
constexpr uint32_t kSelApprove      = 0x095EA7B3;
constexpr uint32_t kSelAllowance    = 0xDD62ED3E;

// Canonical ERC-20 event topic0 hashes (keccak of the event signature).
//   Transfer(address,address,uint256)
//   Approval(address,address,uint256)
const uint8_t kTopicTransfer[32] = {
    0xdd,0xf2,0x52,0xad,0x1b,0xe2,0xc8,0x9b,0x69,0xc2,0xb0,0x68,0xfc,0x37,0x8d,0xaa,
    0x95,0x2b,0xa7,0xf1,0x63,0xc4,0xa1,0x16,0x28,0xf5,0x5a,0x4d,0xf5,0x23,0xb3,0xef};
const uint8_t kTopicApproval[32] = {
    0x8c,0x5b,0xe1,0xe5,0xeb,0xec,0x7d,0x5b,0xd1,0x4f,0x71,0x42,0x7d,0x1e,0x84,0xf3,
    0xdd,0x03,0x14,0xc0,0xf7,0xb2,0x29,0x1e,0x5b,0x20,0x0a,0xc8,0xc7,0xc3,0xb9,0x25};

uint160 AddrToUint160(const evmc::address& a)
{
    uint160 o;
    std::memcpy(o.begin(), a.bytes, 20);
    return o;
}
std::vector<uint8_t> Pad32Addr(const uint160& a)
{
    std::vector<uint8_t> v(32, 0);
    std::memcpy(v.data() + 12, a.begin(), 20);
    return v;
}
std::vector<uint8_t> Pad32Uint(uint64_t n)
{
    std::vector<uint8_t> v(32, 0);
    for (int i = 0; i < 8; ++i) v[31 - i] = static_cast<uint8_t>((n >> (8 * i)) & 0xFF);
    return v;
}
evmc::bytes32 ToBytes32(const uint256& u)
{
    evmc::bytes32 b{};
    std::memcpy(b.bytes, u.begin(), 32);
    return b;
}
// Storage slots delegate to evm/asset_ledger.h — the SINGLE SOURCE OF
// TRUTH shared with the wrap/unwrap apply path, so a balance credited by
// wrap is the exact slot balanceOf() reads here.
evmc::bytes32 BalanceSlot(const uint160& holder)
{
    return ToBytes32(AssetBalanceSlot(holder));
}
evmc::bytes32 AllowanceSlot(const uint160& owner, const uint160& spender)
{
    return ToBytes32(AssetAllowanceSlot(owner, spender));
}
evmc::bytes32 WrappedSupplySlotB32()
{
    return ToBytes32(AssetWrappedSupplySlot());
}
uint256 LoadU256(CEvmHost& host, const evmc::address& a, const evmc::bytes32& slot)
{
    const evmc::bytes32 w = host.get_storage(a, slot);
    uint256 u;
    std::memcpy(u.begin(), w.bytes, 32);
    return u;
}
void StoreU256(CEvmHost& host, const evmc::address& a, const evmc::bytes32& slot,
               const uint256& v)
{
    host.set_storage(a, slot, ToBytes32(v));
}

// Move `amount` units of the wrapped asset from `from` to `to` in the
// EVM-side ledger of contract `c`. Returns false (no mutation) if the
// sender's balance is insufficient. Self-transfer is a no-op net.
bool Erc20Move(CEvmHost& host, const evmc::address& c,
               const uint160& from, const uint160& to, uint64_t amount)
{
    uint256 fromBal = LoadU256(host, c, BalanceSlot(from));
    if (!Uint256GreaterOrEqualUint64(fromBal, amount)) {
        return false;
    }
    if (from == to) {
        return true;  // balance unchanged; avoids double load/store races
    }
    uint256 toBal = LoadU256(host, c, BalanceSlot(to));
    if (!Uint256SubUint64(fromBal, amount)) return false;
    if (!Uint256AddUint64(toBal, amount)) return false;  // overflow guard
    StoreU256(host, c, BalanceSlot(from), fromBal);
    StoreU256(host, c, BalanceSlot(to), toBal);
    return true;
}

// Emit a standard ERC-20 Transfer/Approval log:
//   topics = [topic0, indexed addr1, indexed addr2], data = abi(amount).
void EmitErc20Event(CEvmHost& host, const evmc::address& c,
                    const uint8_t topic0[32], const uint160& a1,
                    const uint160& a2, uint64_t amount)
{
    evmc::bytes32 topics[3];
    std::memcpy(topics[0].bytes, topic0, 32);
    const std::vector<uint8_t> p1 = Pad32Addr(a1);
    const std::vector<uint8_t> p2 = Pad32Addr(a2);
    std::memcpy(topics[1].bytes, p1.data(), 32);
    std::memcpy(topics[2].bytes, p2.data(), 32);
    const std::vector<uint8_t> data = Pad32Uint(amount);
    host.emit_log(c, data.data(), data.size(), topics, 3);
}

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

evmc::Result ExecuteAssetErc20Precompile(CEvmHost& host,
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
    // D4 mirror: totalSupply is the WRAPPED supply (sum of EVM-side
    // balances), so sum(balanceOf) == totalSupply per ERC-20. It is 0
    // until units are wrapped in (M2 wrap/unwrap); the UTXO-side
    // circulatingSupply is a separate quantity (the asset's true
    // total) and is not the ERC-20 totalSupply of the wrapped token.
    case kSelTotalSupply: {
        const uint256 wrapped =
            LoadU256(host, msg.code_address, WrappedSupplySlotB32());
        std::vector<uint8_t> w(wrapped.begin(), wrapped.end());
        output.insert(output.end(), w.begin(), w.end());
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    // -- balanceOf(address) returns (uint256) — EVM-side ledger ------
    case kSelBalanceOf: {
        if (args.size() < 32) return PrecompileFailure(msg.gas);
        uint160 addr;
        if (!AbiReadAddress(args, 0, addr)) {
            return PrecompileFailure(msg.gas);
        }
        const uint256 bal = LoadU256(host, msg.code_address, BalanceSlot(addr));
        std::vector<uint8_t> b(bal.begin(), bal.end());
        output.insert(output.end(), b.begin(), b.end());
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    // -- allowance(address owner, address spender) returns (uint256) --
    case kSelAllowance: {
        uint160 owner, spender;
        if (!AbiReadAddress(args, 0, owner) ||
            !AbiReadAddress(args, 32, spender)) {
            return PrecompileFailure(msg.gas);
        }
        const uint256 a =
            LoadU256(host, msg.code_address, AllowanceSlot(owner, spender));
        std::vector<uint8_t> v(a.begin(), a.end());
        output.insert(output.end(), v.begin(), v.end());
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    // -- approve(address spender, uint256 amount) returns (bool) -----
    case kSelApprove: {
        if (msg.flags & EVMC_STATIC) return PrecompileFailure(msg.gas);
        uint160 spender; uint64_t amount = 0;
        if (!AbiReadAddress(args, 0, spender) ||
            !AbiReadUint64(args, 32, amount)) {
            return PrecompileFailure(msg.gas);
        }
        const uint160 owner = AddrToUint160(msg.sender);
        StoreU256(host, msg.code_address, AllowanceSlot(owner, spender),
                  evm::Uint256FromUint64(amount));
        EmitErc20Event(host, msg.code_address, kTopicApproval, owner,
                       spender, amount);
        AbiWriteBool(output, true);
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    // -- transfer(address to, uint256 amount) returns (bool) --------
    case kSelTransfer: {
        if (msg.flags & EVMC_STATIC) return PrecompileFailure(msg.gas);
        uint160 to; uint64_t amount = 0;
        if (!AbiReadAddress(args, 0, to) ||
            !AbiReadUint64(args, 32, amount)) {
            return PrecompileFailure(msg.gas);
        }
        const uint160 from = AddrToUint160(msg.sender);
        if (!Erc20Move(host, msg.code_address, from, to, amount)) {
            return PrecompileFailure(msg.gas);  // insufficient balance
        }
        EmitErc20Event(host, msg.code_address, kTopicTransfer, from, to,
                       amount);
        AbiWriteBool(output, true);
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    // -- transferFrom(address from, address to, uint256 amount) ------
    case kSelTransferFrom: {
        if (msg.flags & EVMC_STATIC) return PrecompileFailure(msg.gas);
        uint160 from, to; uint64_t amount = 0;
        if (!AbiReadAddress(args, 0, from) ||
            !AbiReadAddress(args, 32, to) ||
            !AbiReadUint64(args, 64, amount)) {
            return PrecompileFailure(msg.gas);
        }
        const uint160 spender = AddrToUint160(msg.sender);
        // Spend the allowance first (require >= amount), then move.
        const evmc::bytes32 aslot = AllowanceSlot(from, spender);
        uint256 allow = LoadU256(host, msg.code_address, aslot);
        if (!Uint256GreaterOrEqualUint64(allow, amount)) {
            return PrecompileFailure(msg.gas);  // allowance too low
        }
        if (!Erc20Move(host, msg.code_address, from, to, amount)) {
            return PrecompileFailure(msg.gas);  // insufficient balance
        }
        // Decrement the allowance (skip the unlimited-approval sentinel
        // max-uint64 convention is not used here; always decrement).
        Uint256SubUint64(allow, amount);
        StoreU256(host, msg.code_address, aslot, allow);
        EmitErc20Event(host, msg.code_address, kTopicTransfer, from, to,
                       amount);
        AbiWriteBool(output, true);
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    default:
        return PrecompileFailure(msg.gas);
    }
}

} // namespace evm
