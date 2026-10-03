// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/precompiles.h>

#include <bls/bls.h>
#include <consensus/params.h>
#include <llmq/quorums_parameters.h>
#include <llmq/quorums_signing.h>
#include <uint256.h>
#include <validation.h>

#include <cstring>

namespace evm {

// ----------------------------------------------------------------------
// Phase 4.2 — LLMQ Oracle precompile (address 0x00..00a02).
//
// Solidity-callable surface (per docs/evm/PROPOSAL-FOR-CORE-TEAM.md):
//
//   interface ILlmqOracle {
//       // Returns true if a recovered (threshold-signed) signature
//       // is on file for (llmqType, id).
//       function hasSignature(uint8 llmqType, bytes32 id)
//           external view returns (bool);
//
//       // Returns (true, sig 96 bytes) if a recovered signature is
//       // available; (false, "") otherwise. The signature is the
//       // serialised BLS aggregate produced by the LLMQ quorum.
//       function getSignature(uint8 llmqType, bytes32 id)
//           external view returns (bool, bytes);
//
//       // Statically verifies a BLS threshold signature without
//       // touching the signing manager's state. Useful for
//       // off-chain verification flows and bridge-side gates.
//       function verifySignature(uint8 llmqType, uint32 signedAtHeight,
//                                bytes32 id, bytes32 msgHash,
//                                bytes signature)
//           external view returns (bool);
//
//       // Note: requestSignature is the async-trigger half of the
//       // pair. It would call quorumSigningManager->AsyncSignIfMember,
//       // but doing so writes signing state and isn't safe inside a
//       // pure read-only EVM call (eth_call snapshots, no
//       // ConnectBlock involvement). For Phase 4.2 we expose the
//       // read + verify halves only; requestSignature lands when
//       // it can be wired into a state-mutating tx flow (FUP — gas
//       // escrow + timeout per A7).
//   }
//
// Why: oracles + cross-chain bridges become straightforward when
// dApps can ask the chain "is this message threshold-signed?"
// without trusting an external relayer. This is the Chainlink-killer
// piece of the design.

namespace {

constexpr int64_t kPrecompileGasCost = 8000; // BLS verify is heavier
                                             // than a state read

// keccak256 selectors (validated via eth_hash):
//   hasSignature(uint8,bytes32)                         -> 76f08249
//   getSignature(uint8,bytes32)                         -> 4e640cb2
//   verifySignature(uint8,uint32,bytes32,bytes32,bytes) -> 1805ce6b
constexpr uint32_t kSelHasSignature    = 0x76F08249;
constexpr uint32_t kSelGetSignature    = 0x4E640CB2;
constexpr uint32_t kSelVerifySignature = 0x1805CE6B;

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

uint256 EvmWordToBitcoinUint256(const uint256& evmWord)
{
    uint256 out;
    for (int i = 0; i < 32; ++i) {
        *(out.begin() + i) = *(evmWord.begin() + (31 - i));
    }
    return out;
}

// Validate a uint8 against the known LLMQ types. Unknown / reserved
// values reject so a dApp can't accidentally probe a non-existent
// quorum and silently get false.
bool IsKnownLlmqType(uint8_t v)
{
    switch (v) {
        case Consensus::LLMQ_50_60:
        case Consensus::LLMQ_400_60:
        case Consensus::LLMQ_400_85:
        case Consensus::LLMQ_100_67:
        case Consensus::LLMQ_5_60:
        case Consensus::LLMQ_TEST_V17:
            return true;
        default:
            return false;
    }
}

} // anonymous namespace

evmc::Result ExecuteLlmqOraclePrecompile(CEvmHost& /*host*/,
                                         const evmc_message& msg)
{
    if (msg.gas < kPrecompileGasCost) {
        return PrecompileFailure(msg.gas);
    }

    const std::vector<uint8_t> input(msg.input_data,
                                    msg.input_data + msg.input_size);
    const uint32_t selector = ReadSelector(input);
    const std::vector<uint8_t> args = ArgsSlice(input);
    std::vector<uint8_t> output;

    switch (selector) {

    // -- hasSignature(uint8 llmqType, bytes32 id) returns (bool) ----
    case kSelHasSignature: {
        if (args.size() < 64) return PrecompileFailure(msg.gas);
        uint8_t llmqType = 0;
        if (!AbiReadUint8(args, 0, llmqType)) return PrecompileFailure(msg.gas);
        if (!IsKnownLlmqType(llmqType)) return PrecompileFailure(msg.gas);
        uint256 evmId;
        if (!AbiReadWord(args, 32, evmId)) return PrecompileFailure(msg.gas);
        const uint256 id = EvmWordToBitcoinUint256(evmId);

        bool has = false;
        if (llmq::quorumSigningManager != nullptr) {
            has = llmq::quorumSigningManager->HasRecoveredSigForId(
                static_cast<Consensus::LLMQType>(llmqType), id);
        }
        AbiWriteBool(output, has);
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    // -- getSignature(uint8, bytes32) returns (bool, bytes) ---------
    case kSelGetSignature: {
        if (args.size() < 64) return PrecompileFailure(msg.gas);
        uint8_t llmqType = 0;
        if (!AbiReadUint8(args, 0, llmqType)) return PrecompileFailure(msg.gas);
        if (!IsKnownLlmqType(llmqType)) return PrecompileFailure(msg.gas);
        uint256 evmId;
        if (!AbiReadWord(args, 32, evmId)) return PrecompileFailure(msg.gas);
        const uint256 id = EvmWordToBitcoinUint256(evmId);

        // Two-value return: encode (bool available, bytes signature).
        // Static portion of `bool` + head-of-`bytes` = two 32-byte
        // slots; total static head = 64 bytes. Tail follows.
        std::vector<uint8_t> sigBytes;
        bool available = false;
        if (llmq::quorumSigningManager != nullptr) {
            llmq::CRecoveredSig recSig;
            available = llmq::quorumSigningManager->GetRecoveredSigForId(
                static_cast<Consensus::LLMQType>(llmqType), id, recSig);
            if (available) {
                const CBLSSignature s = recSig.sig.Get();
                sigBytes = s.ToByteVector();
            }
        }
        // Head: [0..32) = bool; [32..64) = offset-to-bytes-tail = 64.
        AbiWriteBool(output, available);
        AbiWriteDynamicHead(output, /*tailOffset=*/ 64);
        AbiWriteDynamicBytesTail(output, sigBytes);
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    // -- verifySignature(uint8, uint32, bytes32, bytes32, bytes)
    //    returns (bool)
    case kSelVerifySignature: {
        // Static head: 5 slots = 160 bytes.
        //   [0..32)   llmqType (uint8 padded)
        //   [32..64)  signedAtHeight (uint32 padded)
        //   [64..96)  id (bytes32)
        //   [96..128) msgHash (bytes32)
        //   [128..160) offset to `signature` tail
        if (args.size() < 160) return PrecompileFailure(msg.gas);
        uint8_t llmqType = 0;
        if (!AbiReadUint8(args, 0, llmqType)) return PrecompileFailure(msg.gas);
        if (!IsKnownLlmqType(llmqType)) return PrecompileFailure(msg.gas);
        uint64_t signedAt = 0;
        if (!AbiReadUint64(args, 32, signedAt)) return PrecompileFailure(msg.gas);
        uint256 evmId;
        if (!AbiReadWord(args, 64, evmId)) return PrecompileFailure(msg.gas);
        uint256 evmMsg;
        if (!AbiReadWord(args, 96, evmMsg)) return PrecompileFailure(msg.gas);
        std::vector<uint8_t> sigBytes;
        if (!AbiReadDynamicBytes(args, 128, sigBytes)) {
            return PrecompileFailure(msg.gas);
        }
        if (sigBytes.size() != 96) return PrecompileFailure(msg.gas);

        const uint256 id = EvmWordToBitcoinUint256(evmId);
        const uint256 msgHash = EvmWordToBitcoinUint256(evmMsg);

        CBLSSignature sig;
        sig.SetByteVector(sigBytes);
        if (!sig.IsValid()) {
            AbiWriteBool(output, false);
            return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
        }
        const bool ok = llmq::CSigningManager::VerifyRecoveredSig(
            static_cast<Consensus::LLMQType>(llmqType),
            static_cast<int>(signedAt),
            id, msgHash, sig);
        AbiWriteBool(output, ok);
        return PrecompileSuccess(msg.gas, kPrecompileGasCost, std::move(output));
    }

    default:
        return PrecompileFailure(msg.gas);
    }
}

} // namespace evm
