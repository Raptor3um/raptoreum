// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/asset_ledger.h>

#include <evm/balance.h>
#include <evm/hashing.h>
#include <evm/state_cache.h>
#include <hash.h>

#include <cstring>
#include <vector>

namespace evm {

namespace {

// 0xA55E70_00_00_00_00_00 reservation marker (see precompiles.cpp).
const uint8_t kAssetMarker[8] = {0xA5, 0x5E, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00};

// Storage layout slot indices (Solidity-compatible).
constexpr uint64_t kSlotBalance       = 0;
constexpr uint64_t kSlotAllowance     = 1;
constexpr uint64_t kSlotWrappedSupply = 2;

uint160 ToUint160(const evmc::address& a)
{
    uint160 o;
    std::memcpy(o.begin(), a.bytes, 20);
    return o;
}

// 32-byte left-padded address (Solidity ABI / mapping-key encoding).
std::vector<uint8_t> Pad32Addr(const uint160& a)
{
    std::vector<uint8_t> v(32, 0);
    std::memcpy(v.data() + 12, a.begin(), 20);
    return v;
}

// 32-byte big-endian uint64 (high 192 bits zero).
std::vector<uint8_t> Pad32Uint(uint64_t n)
{
    std::vector<uint8_t> v(32, 0);
    for (int i = 0; i < 8; ++i) {
        v[31 - i] = static_cast<uint8_t>((n >> (8 * i)) & 0xFF);
    }
    return v;
}

// keccak256(a ++ b) as a uint256 (the Solidity mapping-slot rule).
uint256 KeccakKey(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
    std::vector<uint8_t> buf;
    buf.reserve(a.size() + b.size());
    buf.insert(buf.end(), a.begin(), a.end());
    buf.insert(buf.end(), b.begin(), b.end());
    return Keccak256(buf);
}

} // anonymous namespace

evmc::address AssetErc20Address(const std::string& assetId)
{
    const uint160 h = Hash160(
        std::vector<unsigned char>(assetId.begin(), assetId.end()));
    evmc::address a{};
    std::memcpy(a.bytes, kAssetMarker, 8);
    std::memcpy(a.bytes + 8, h.begin() + 8, 12);
    return a;
}

uint256 AssetBalanceSlot(const uint160& holder)
{
    return KeccakKey(Pad32Addr(holder), Pad32Uint(kSlotBalance));
}

uint256 AssetAllowanceSlot(const uint160& owner, const uint160& spender)
{
    const uint256 inner = KeccakKey(Pad32Addr(owner), Pad32Uint(kSlotAllowance));
    return KeccakKey(Pad32Addr(spender),
                     std::vector<uint8_t>(inner.begin(), inner.end()));
}

uint256 AssetWrappedSupplySlot()
{
    return uint256(Pad32Uint(kSlotWrappedSupply));
}

bool CreditAssetLedger(CEvmStateCache& cache, const std::string& assetId,
                       const uint160& holder, uint64_t amount)
{
    if (amount == 0) return true;  // no-op
    const uint160 c = ToUint160(AssetErc20Address(assetId));

    const uint256 balSlot = AssetBalanceSlot(holder);
    uint256 bal;
    if (!cache.GetStorage(c, balSlot, bal)) bal.SetNull();
    if (!Uint256AddUint64(bal, amount)) return false;  // overflow guard

    const uint256 supSlot = AssetWrappedSupplySlot();
    uint256 sup;
    if (!cache.GetStorage(c, supSlot, sup)) sup.SetNull();
    if (!Uint256AddUint64(sup, amount)) return false;

    cache.SetStorage(c, balSlot, bal);
    cache.SetStorage(c, supSlot, sup);
    return true;
}

bool DebitAssetLedger(CEvmStateCache& cache, const std::string& assetId,
                      const uint160& holder, uint64_t amount)
{
    if (amount == 0) return true;  // no-op
    const uint160 c = ToUint160(AssetErc20Address(assetId));

    const uint256 balSlot = AssetBalanceSlot(holder);
    uint256 bal;
    if (!cache.GetStorage(c, balSlot, bal)) bal.SetNull();
    if (!Uint256GreaterOrEqualUint64(bal, amount)) return false;

    const uint256 supSlot = AssetWrappedSupplySlot();
    uint256 sup;
    if (!cache.GetStorage(c, supSlot, sup)) sup.SetNull();
    // wrappedSupply must cover the debit too — a wrapped-supply shortfall
    // would mean the per-holder balances and the supply scalar diverged,
    // which is a consensus invariant violation.
    if (!Uint256GreaterOrEqualUint64(sup, amount)) return false;

    if (!Uint256SubUint64(bal, amount)) return false;
    if (!Uint256SubUint64(sup, amount)) return false;

    cache.SetStorage(c, balSlot, bal);
    cache.SetStorage(c, supSlot, sup);
    return true;
}

uint64_t AssetLedgerBalanceOf(CEvmStateCache& cache, const std::string& assetId,
                              const uint160& holder)
{
    const uint160 c = ToUint160(AssetErc20Address(assetId));
    uint256 bal;
    if (!cache.GetStorage(c, AssetBalanceSlot(holder), bal)) return 0;
    return Uint256ToLowUint64(bal);
}

uint64_t AssetLedgerWrappedSupply(CEvmStateCache& cache, const std::string& assetId)
{
    const uint160 c = ToUint160(AssetErc20Address(assetId));
    uint256 sup;
    if (!cache.GetStorage(c, AssetWrappedSupplySlot(), sup)) return 0;
    return Uint256ToLowUint64(sup);
}

} // namespace evm
