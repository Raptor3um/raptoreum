// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/precompiles.h>

#include <chain.h>
#include <llmq/quorums_chainlocks.h>
#include <llmq/quorums_instantsend.h>
#include <sync.h>
#include <uint256.h>
#include <validation.h>

#include <cstring>

namespace evm {

// ----------------------------------------------------------------------
// Phase 4.3 — ChainLocks precompile (address 0x00..00a03).
//
// Solidity-callable surface (matches docs/evm/PROPOSAL-FOR-CORE-TEAM.md):
//
//   interface IChainLocks {
//       function isChainLocked(uint32 height, bytes32 blockHash)
//           external view returns (bool);
//       function isTxInstantLocked(bytes32 txid)
//           external view returns (bool);
//       function latestChainLockedHeight()
//           external view returns (uint32);
//   }
//
// Why this exists: gives dApps a deterministic on-chain answer to
// "is this txid / block locked by the LLMQ quorum?" which Ethereum
// L1 cannot natively provide. Bridges, DEX UIs, and exchanges can
// treat ChainLocked / InstantLocked transactions as final in ~2s
// instead of waiting for probabilistic-finality block confirmations.
//
// All three methods are sync + read-only. The precompile reads
// directly from the LLMQ handler globals — these are populated
// during normal block validation (LLMQ recovered sigs ingestion)
// and are safe to read here under cs_main (we acquire it for the
// reads).
//
// Gas cost: a fixed 5000 per call (cheap; comparable to STATICCALL
// + EXTCODESIZE on Ethereum). Real cost-model calibration is FUP.

namespace {

// keccak256("isChainLocked(uint32,bytes32)")[0:4]
constexpr uint32_t kSelIsChainLocked = 0x59D66E82;
// keccak256("isTxInstantLocked(bytes32)")[0:4]
constexpr uint32_t kSelIsTxInstantLocked = 0x51C37046;
// keccak256("latestChainLockedHeight()")[0:4]
constexpr uint32_t kSelLatestChainLockedHeight = 0x2A433BE9;

constexpr int64_t kPrecompileGasCost = 5000;

// Read the 4-byte selector from the head of the input. Returns 0
// (a selector that no method should match) on too-short input.
uint32_t ReadSelector(const std::vector<uint8_t>& input)
{
    if (input.size() < 4) return 0;
    return (uint32_t(input[0]) << 24) | (uint32_t(input[1]) << 16) |
           (uint32_t(input[2]) << 8)  |  uint32_t(input[3]);
}

// Split out the args region (input minus the 4-byte selector).
std::vector<uint8_t> ArgsSlice(const std::vector<uint8_t>& input)
{
    if (input.size() < 4) return {};
    return std::vector<uint8_t>(input.begin() + 4, input.end());
}

// Convert a 32-byte big-endian uint256 (Bitcoin Core layout uses
// little-endian internally — see Uint256ToEthHex) into our EVM-
// module big-endian uint256. The input here arrives from ABI
// decode which is already big-endian, so no swap is needed; we
// just need to interpret the bytes as Bitcoin-Core uint256 for
// downstream LLMQ calls. Bitcoin uint256 stores bytes such that
// begin() is the least-significant byte; so we reverse on the
// way in.
uint256 EvmWordToBitcoinUint256(const uint256& evmWord)
{
    uint256 out;
    for (int i = 0; i < 32; ++i) {
        *(out.begin() + i) = *(evmWord.begin() + (31 - i));
    }
    return out;
}

} // anonymous namespace

evmc::Result ExecuteChainLocksPrecompile(CEvmHost& /*host*/,
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

    switch (selector) {

    // -- isChainLocked(uint32, bytes32) returns (bool) --------------
    case kSelIsChainLocked: {
        if (args.size() < 64) return PrecompileFailure(msg.gas);
        uint64_t height = 0;
        if (!AbiReadUint64(args, 0, height)) return PrecompileFailure(msg.gas);
        uint256 evmBlockHash;
        if (!AbiReadWord(args, 32, evmBlockHash)) return PrecompileFailure(msg.gas);
        const uint256 blockHash = EvmWordToBitcoinUint256(evmBlockHash);

        bool locked = false;
        if (llmq::chainLocksHandler != nullptr) {
            LOCK(cs_main);
            locked = llmq::chainLocksHandler->HasChainLock(
                static_cast<int>(height), blockHash);
        }
        AbiWriteBool(output, locked);
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    // -- isTxInstantLocked(bytes32) returns (bool) ------------------
    case kSelIsTxInstantLocked: {
        if (args.size() < 32) return PrecompileFailure(msg.gas);
        uint256 evmTxid;
        if (!AbiReadWord(args, 0, evmTxid)) return PrecompileFailure(msg.gas);
        const uint256 txid = EvmWordToBitcoinUint256(evmTxid);

        bool locked = false;
        if (llmq::quorumInstantSendManager != nullptr) {
            locked = llmq::quorumInstantSendManager->IsLocked(txid);
        }
        AbiWriteBool(output, locked);
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    // -- latestChainLockedHeight() returns (uint32) -----------------
    case kSelLatestChainLockedHeight: {
        // Walk back from the chain tip to find the highest height
        // that's ChainLocked. The LLMQ chainlocks handler doesn't
        // expose this directly, but the tip itself is the natural
        // candidate — if the tip is ChainLocked, that's the answer;
        // otherwise walk back. For the MVP we return the tip if
        // any ChainLock exists at all, else 0 — a refinement that
        // reflects "is the chain currently ChainLocked" rather
        // than the exact highest-locked height. Sufficient for
        // bridges; refine when needed (FUP).
        uint64_t height = 0;
        LOCK(cs_main);
        if (::ChainActive().Tip() != nullptr &&
            llmq::chainLocksHandler != nullptr)
        {
            // Try the tip first; if it's locked, that's our answer.
            // Otherwise walk back a bounded distance.
            constexpr int kScanDepth = 256;
            const CBlockIndex* p = ::ChainActive().Tip();
            for (int i = 0; i < kScanDepth && p != nullptr; ++i) {
                if (llmq::chainLocksHandler->HasChainLock(
                        p->nHeight, p->GetBlockHash()))
                {
                    height = static_cast<uint64_t>(p->nHeight);
                    break;
                }
                p = p->pprev;
            }
        }
        // Per the ABI declared above the field is uint32 — clamp
        // defensively at uint32_max.
        if (height > 0xFFFFFFFFULL) height = 0xFFFFFFFFULL;
        AbiWriteUint64(output, height);
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    default:
        return PrecompileFailure(msg.gas);
    }
}

} // namespace evm
