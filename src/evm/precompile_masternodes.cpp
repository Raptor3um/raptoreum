// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/precompiles.h>

#include <evo/deterministicmns.h>
#include <netaddress.h>
#include <sync.h>
#include <uint256.h>
#include <validation.h>

#include <cstring>

namespace evm {

// ----------------------------------------------------------------------
// Phase 4.4 — Masternode Registry precompile (address 0x00..00a04).
//
// Solidity-callable surface:
//
//   struct Masternode {
//       bytes32 proTxHash;
//       uint32  ip;        // IPv4 only for now; IPv6 needs bytes16
//       uint16  port;
//       bytes   pubKeyOperator;  // BLS 48 bytes
//       uint64  collateralAmount;
//       bool    isBanned;
//   }
//
//   interface IMasternodeRegistry {
//       function getCount()           external view returns (uint256);
//       function isMasternode(bytes32) external view returns (bool);
//       function getByProTxHash(bytes32) external view returns (Masternode);
//       function getByIndex(uint256)  external view returns (Masternode);
//   }
//
// Why: governance-on-chain, treasury-pays-masternodes, delegated-
// staking and similar composable patterns become straightforward
// when the MN list is a vanilla view from Solidity. No bridges, no
// off-chain trust assumptions.
//
// Backed by CDeterministicMNManager::GetListAtChainTip(). Iteration
// over the immer::map of MNs is read-only; cs_main is acquired.

namespace {

// keccak256 selectors (validated against eth_hash):
//   getCount()                       -> a87d942c
//   isMasternode(bytes32)            -> 47aebab3
//   getByProTxHash(bytes32)          -> 0402e163
//   getByIndex(uint256)              -> 2d883a73
constexpr uint32_t kSelGetCount        = 0xA87D942C;
constexpr uint32_t kSelIsMasternode    = 0x47AEBAB3;
constexpr uint32_t kSelGetByProTxHash  = 0x0402E163;
constexpr uint32_t kSelGetByIndex      = 0x2D883A73;

constexpr int64_t kPrecompileGasCost = 5000;

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

// Bitcoin uint256 (m_data[0] = LSB after the vector ctor; see uint256.cpp)
// versus the EVM-on-the-wire big-endian form. Reverse on cross.
uint256 EvmWordToBitcoinUint256(const uint256& evmWord)
{
    uint256 out;
    for (int i = 0; i < 32; ++i) {
        *(out.begin() + i) = *(evmWord.begin() + (31 - i));
    }
    return out;
}
uint256 BitcoinUint256ToEvmWord(const uint256& v)
{
    uint256 out;
    for (int i = 0; i < 32; ++i) {
        *(out.begin() + i) = *(v.begin() + (31 - i));
    }
    return out;
}

// Encode a CDeterministicMN as a Solidity-ABI Masternode struct.
// Layout:
//   head:
//     [0..32)   proTxHash (bytes32)
//     [32..64)  ip (uint32, padded)
//     [64..96)  port (uint16, padded)
//     [96..128) offset to pubKeyOperator tail
//     [128..160) collateralAmount (uint64, padded)
//     [160..192) isBanned (bool padded)
//   tail at offset 192:
//     [192..224) length = 48
//     [224..272) 48-byte BLS pubkey
//     [272..288) padding (48 bytes + 16 padding = 64 multiple-of-32)
//
// Total: 288 bytes per masternode encoded.
void EncodeMasternodeStruct(std::vector<uint8_t>& out,
                            const CDeterministicMN& mn)
{
    AbiWriteWord(out, BitcoinUint256ToEvmWord(mn.proTxHash));

    const CService& addr = mn.pdmnState->addr;
    uint32_t ip4 = 0;
    if (addr.IsIPv4()) {
        struct in_addr v4;
        if (addr.GetInAddr(&v4)) {
            ip4 = static_cast<uint32_t>(v4.s_addr); // network byte order
            // Convert to host-endian uint32 for ABI.
            ip4 = ((ip4 & 0xFF000000u) >> 24) |
                  ((ip4 & 0x00FF0000u) >> 8) |
                  ((ip4 & 0x0000FF00u) << 8) |
                  ((ip4 & 0x000000FFu) << 24);
        }
    }
    AbiWriteUint64(out, static_cast<uint64_t>(ip4));
    AbiWriteUint64(out, static_cast<uint64_t>(addr.GetPort()));

    // Head slot 3: offset to the pubKeyOperator tail. Head is six
    // 32-byte slots (= 192 bytes); tail starts at byte 192 within
    // the struct.
    AbiWriteDynamicHead(out, 192);
    AbiWriteUint64(out, static_cast<uint64_t>(mn.pdmnState->nCollateralAmount));
    AbiWriteBool(out, mn.pdmnState->IsBanned());

    // Tail: length-prefixed pubKeyOperator bytes (48 BLS bytes).
    std::vector<uint8_t> blsBytes;
    {
        // CBLSLazyPublicKey doesn't expose its raw bytes directly
        // via a stable API; we use ToBytes() if available, otherwise
        // serialize through the standard stream form.
        const CBLSLazyPublicKey& lazy = mn.pdmnState->pubKeyOperator;
        const CBLSPublicKey resolved = lazy.Get();
        blsBytes = resolved.ToByteVector();
        if (blsBytes.size() != 48) {
            // Pad/truncate defensively to keep the ABI shape stable;
            // a non-48-byte BLS key indicates corrupt state.
            blsBytes.resize(48, 0);
        }
    }
    AbiWriteDynamicBytesTail(out, blsBytes);
}

} // anonymous namespace

evmc::Result ExecuteMasternodeRegistryPrecompile(CEvmHost& /*host*/,
                                                const evmc_message& msg)
{
    const std::vector<uint8_t> input(msg.input_data,
                                    msg.input_data + msg.input_size);
    if (msg.gas < kPrecompileGasCost) {
        return PrecompileFailure(msg.gas);
    }

    const uint32_t selector = ReadSelector(input);
    const std::vector<uint8_t> args = ArgsSlice(input);
    std::vector<uint8_t> output;

    if (deterministicMNManager == nullptr) {
        return PrecompileFailure(msg.gas);
    }

    // Snapshot once per call to avoid races with chainstate
    // updates while we walk the list.
    CDeterministicMNList list;
    {
        LOCK(cs_main);
        list = deterministicMNManager->GetListAtChainTip();
    }

    switch (selector) {

    // -- getCount() returns (uint256) -------------------------------
    case kSelGetCount: {
        AbiWriteUint64(output, static_cast<uint64_t>(list.GetAllMNsCount()));
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    // -- isMasternode(bytes32) returns (bool) -----------------------
    case kSelIsMasternode: {
        if (args.size() < 32) return PrecompileFailure(msg.gas);
        uint256 evmHash;
        if (!AbiReadWord(args, 0, evmHash)) return PrecompileFailure(msg.gas);
        const uint256 proTxHash = EvmWordToBitcoinUint256(evmHash);
        AbiWriteBool(output, list.HasMN(proTxHash));
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    // -- getByProTxHash(bytes32) returns (Masternode) ---------------
    case kSelGetByProTxHash: {
        if (args.size() < 32) return PrecompileFailure(msg.gas);
        uint256 evmHash;
        if (!AbiReadWord(args, 0, evmHash)) return PrecompileFailure(msg.gas);
        const uint256 proTxHash = EvmWordToBitcoinUint256(evmHash);
        CDeterministicMNCPtr mn = list.GetMN(proTxHash);
        if (mn == nullptr) {
            return PrecompileFailure(msg.gas);
        }
        EncodeMasternodeStruct(output, *mn);
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    // -- getByIndex(uint256) returns (Masternode) -------------------
    // We iterate the MN list via ForEachMN and pick the Nth entry.
    // The order is the underlying immer::map iteration order, which
    // is stable across calls within a snapshot.
    case kSelGetByIndex: {
        if (args.size() < 32) return PrecompileFailure(msg.gas);
        uint64_t targetIdx = 0;
        if (!AbiReadUint64(args, 0, targetIdx)) return PrecompileFailure(msg.gas);
        if (targetIdx >= list.GetAllMNsCount()) {
            return PrecompileFailure(msg.gas);
        }
        size_t i = 0;
        const CDeterministicMN* found = nullptr;
        list.ForEachMN(/*onlyValid=*/ false,
            [&](const CDeterministicMNCPtr& mn) {
                if (found != nullptr) return;
                if (i == targetIdx) {
                    found = mn.get();
                }
                ++i;
            });
        if (found == nullptr) return PrecompileFailure(msg.gas);
        EncodeMasternodeStruct(output, *found);
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    default:
        return PrecompileFailure(msg.gas);
    }
}

} // namespace evm
