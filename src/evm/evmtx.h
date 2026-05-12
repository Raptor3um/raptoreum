// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_EVMTX_H
#define RAPTOREUM_EVM_EVMTX_H

#include <amount.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

#include <cstdint>
#include <vector>

class CBlockIndex;
class CValidationState;

/**
 * Payload structures for EVM transaction types (Phase 1 scaffolding).
 *
 * These structs are serialized into the special transaction's vExtraPayload
 * field, similar to how Smart Assets and ProRegTx use the pattern. Validation
 * is structure-only in Phase 1 (no execution, no state). Phase 2 adds EVM
 * execution against the state trie inside ConnectBlock.
 *
 * See docs/evm/PLAN.md § "Fase 1 — AAL: Account Abstraction Layer" for the
 * full design rationale.
 *
 * Gas/fee fields follow EIP-1559 (per design decision D6: target Cancun).
 */

namespace evm {

/** Common payload version constant for all EVM tx types. */
static constexpr uint16_t EVM_TX_PAYLOAD_VERSION = 1;

/**
 * One entry of an EIP-2930 access list: an address plus a set of
 * storage slots within that address that the transaction will
 * touch. The Ethereum spec pre-warms both when the tx enters the
 * EVM, so subsequent reads cost the warm price (100) instead of
 * cold (2600 / 2100).
 *
 * For Phase 1/2 the access list is NOT wire-serialised — it sits on
 * the in-memory payload only and is consumed by ApplyEvmCallTx /
 * ApplyEvmDeployTx for pre-warming. When Phase 2.4 finalises the
 * full Cancun-compatible tx envelope, this will graduate into the
 * SERIALIZE_METHODS block (and bump EVM_TX_PAYLOAD_VERSION). Until
 * then the field is callers' responsibility to populate; production
 * code can leave it empty.
 */
struct AccessListEntry
{
    uint160 address;
    std::vector<uint256> storageKeys;
};

/** Maximum EVM contract bytecode size in bytes. Matches Ethereum's EIP-170
 *  contract code size limit (24576 bytes = 24KB). */
static constexpr size_t MAX_EVM_CONTRACT_CODE_SIZE = 24576;

/** Maximum EVM call data size. We use the same 24KB ceiling as bytecode for
 *  Phase 1; revisit in Phase 2 when gas-pricing for calldata is finalized. */
static constexpr size_t MAX_EVM_CALLDATA_SIZE = 24576;

/**
 * Payload for TRANSACTION_EVM_DEPLOY.
 *
 * Deploys a new EVM contract. The deployed contract's address is derived
 * deterministically from (senderHash, sender nonce) per standard EVM CREATE
 * semantics. Phase 1 does NOT compute or validate this address; Phase 2 will.
 */
struct CEvmDeployTx
{
    uint16_t nVersion{EVM_TX_PAYLOAD_VERSION};

    /** EVM bytecode of the contract to deploy. Limited by MAX_EVM_CONTRACT_CODE_SIZE. */
    std::vector<uint8_t> code;

    /** Gas limit reserved for execution (intrinsic + creation). */
    uint64_t gasLimit{0};

    /** EIP-1559 fields. */
    uint64_t maxFeePerGas{0};
    uint64_t maxPriorityFeePerGas{0};

    /** Hash of the deploying EVM account's public key (20-byte EVM address
     *  semantics are computed from this; concrete derivation defined in
     *  Phase 2). Stored as uint256 to allow future flexibility. */
    uint256 senderHash;

    /** Sender's nonce at the time of deploy. */
    uint64_t nonce{0};

    /** EIP-2930 access list (off-wire for now — see AccessListEntry). */
    std::vector<AccessListEntry> accessList;

    SERIALIZE_METHODS(CEvmDeployTx, obj)
    {
        READWRITE(obj.nVersion);
        READWRITE(obj.code);
        READWRITE(obj.gasLimit);
        READWRITE(obj.maxFeePerGas);
        READWRITE(obj.maxPriorityFeePerGas);
        READWRITE(obj.senderHash);
        READWRITE(obj.nonce);
    }

    std::string ToString() const;
};

/**
 * Payload for TRANSACTION_EVM_CALL.
 *
 * Invokes an existing EVM contract at address `to` with the given calldata
 * and value. Equivalent to Ethereum's standard CALL transaction.
 */
struct CEvmCallTx
{
    uint16_t nVersion{EVM_TX_PAYLOAD_VERSION};

    /** Target contract address (20-byte EVM address as the trailing 160 bits
     *  of a uint256; full 32-byte type is used for serialization regularity). */
    uint256 toAddress;

    /** Amount of RTM (in weis: 1 RTM = 10^18 weis) to transfer with the call. */
    uint64_t value{0};

    /** Calldata passed to the contract. Limited by MAX_EVM_CALLDATA_SIZE. */
    std::vector<uint8_t> data;

    uint64_t gasLimit{0};
    uint64_t maxFeePerGas{0};
    uint64_t maxPriorityFeePerGas{0};

    uint256 senderHash;
    uint64_t nonce{0};

    /** EIP-2930 access list (off-wire for now — see AccessListEntry). */
    std::vector<AccessListEntry> accessList;

    SERIALIZE_METHODS(CEvmCallTx, obj)
    {
        READWRITE(obj.nVersion);
        READWRITE(obj.toAddress);
        READWRITE(obj.value);
        READWRITE(obj.data);
        READWRITE(obj.gasLimit);
        READWRITE(obj.maxFeePerGas);
        READWRITE(obj.maxPriorityFeePerGas);
        READWRITE(obj.senderHash);
        READWRITE(obj.nonce);
    }

    std::string ToString() const;
};

/**
 * Payload for TRANSACTION_EVM_SPEND.
 *
 * Moves RTM from an EVM account back into the UTXO subsystem by creating
 * a regular UTXO output with the specified script and amount. The EVM
 * account's balance is debited by `amount` (plus gas).
 *
 * This is the inverse of the implicit transfer when sending RTM from a UTXO
 * to an EVM address (which happens via OP_EVMCALL with value > 0).
 */
struct CEvmSpendTx
{
    uint16_t nVersion{EVM_TX_PAYLOAD_VERSION};

    /** Source EVM account address (low 160 bits used). */
    uint256 fromAddress;

    /** Amount in RTM weis to move into a UTXO output. */
    uint64_t amount{0};

    /** Destination UTXO script (where RTM lands as a normal output). */
    CScript outputScript;

    uint64_t gasLimit{0};
    uint64_t maxFeePerGas{0};
    uint64_t maxPriorityFeePerGas{0};

    /** Sender's nonce at the time of spend. */
    uint64_t nonce{0};

    SERIALIZE_METHODS(CEvmSpendTx, obj)
    {
        READWRITE(obj.nVersion);
        READWRITE(obj.fromAddress);
        READWRITE(obj.amount);
        READWRITE(obj.outputScript);
        READWRITE(obj.gasLimit);
        READWRITE(obj.maxFeePerGas);
        READWRITE(obj.maxPriorityFeePerGas);
        READWRITE(obj.nonce);
    }

    std::string ToString() const;
};

// ----------------------------------------------------------------------------
// Validation entry points (Phase 1: structure-only).
// ----------------------------------------------------------------------------
//
// These are called from src/evo/specialtx.cpp::CheckSpecialTx() via the
// switch on tx.nType.
//
// In Phase 1 they validate:
//   - vExtraPayload deserializes cleanly into the corresponding struct.
//   - nVersion is supported.
//   - Sizes are within limits (code/calldata bounds; non-zero gasLimit).
//   - Activation gate Updates().IsEvmActive() is honored.
//
// They do NOT yet:
//   - Execute the EVM (Phase 2).
//   - Check sender balance or nonce (Phase 2).
//   - Validate signatures (Phase 2 — wallet integration).
//   - Apply state changes (Phase 2).

bool CheckEvmDeployTx(const CTransaction& tx,
                     const CBlockIndex* pindexPrev,
                     CValidationState& state);

bool CheckEvmCallTx(const CTransaction& tx,
                   const CBlockIndex* pindexPrev,
                   CValidationState& state);

bool CheckEvmSpendTx(const CTransaction& tx,
                    const CBlockIndex* pindexPrev,
                    CValidationState& state);

} // namespace evm

#endif // RAPTOREUM_EVM_EVMTX_H
