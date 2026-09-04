// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_ASSET_LEDGER_H
#define RAPTOREUM_EVM_ASSET_LEDGER_H

#include <uint256.h>

#include <evmc/evmc.hpp>

#include <cstdint>
#include <string>

namespace evm {

class CEvmStateCache;

// ----------------------------------------------------------------------
// D4 Smart-Asset bidirectional mirror — EVM-side ERC-20 ledger layout.
//
// This is the SINGLE SOURCE OF TRUTH for how a wrapped Smart Asset is
// represented in EVM state. Both the read/write precompile surface
// (precompile_asset_erc20.cpp) and the wrap/unwrap consensus apply path
// (apply.cpp) compute slots through these helpers, so the mirror cannot
// drift: a balance credited by wrap is the exact slot balanceOf() reads.
//
// The wrapped token lives in the per-asset precompile's own storage trie
// with a Solidity-compatible layout, so standard tooling can inspect it:
//
//   mapping(address => uint256) balanceOf                  at slot 0
//   mapping(address => mapping(address => uint256)) allowance at slot 1
//   uint256 wrappedSupply (== ERC-20 totalSupply)          at slot 2
//
// Amounts are uint64-capped (asset supply is int64), reusing balance.h.
// ----------------------------------------------------------------------

// Forward map: assetId -> per-asset ERC-20 precompile address
//   0xA55E70 00 00 00 00 00 || hash160(assetId)[8:20].
// This is the inverse of the linear-scan resolver in
// precompile_asset_erc20.cpp (ResolveAssetIdFromAddress).
evmc::address AssetErc20Address(const std::string& assetId);

// Storage-slot keys (as uint256 cache keys). The precompile uses the
// evmc::bytes32 form via the host; these are byte-identical (the host
// converts bytes32<->uint256 with a raw copy), so the apply path can
// drive CEvmStateCache::Get/SetStorage with these directly.
uint256 AssetBalanceSlot(const uint160& holder);
uint256 AssetAllowanceSlot(const uint160& owner, const uint160& spender);
uint256 AssetWrappedSupplySlot();

// Credit `amount` wrapped units to `holder` in `assetId`'s EVM ledger and
// bump wrappedSupply by the same amount (sum(balanceOf) == totalSupply).
// Returns false only on a uint64 overflow — impossible within RTM supply,
// so a false return signals a consensus bug, not a normal failure.
bool CreditAssetLedger(CEvmStateCache& cache, const std::string& assetId,
                       const uint160& holder, uint64_t amount);

// Debit `amount` from `holder` (require balance >= amount) and reduce
// wrappedSupply (require >= amount). Returns false without mutating any
// slot if either is insufficient — the unwrap caller turns this into a
// block-level rejection.
bool DebitAssetLedger(CEvmStateCache& cache, const std::string& assetId,
                      const uint160& holder, uint64_t amount);

// Read helpers (used by tests and conservation checks).
uint64_t AssetLedgerBalanceOf(CEvmStateCache& cache, const std::string& assetId,
                              const uint160& holder);
uint64_t AssetLedgerWrappedSupply(CEvmStateCache& cache, const std::string& assetId);

} // namespace evm

#endif // RAPTOREUM_EVM_ASSET_LEDGER_H
