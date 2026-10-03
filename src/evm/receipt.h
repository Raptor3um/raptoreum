// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_RECEIPT_H
#define RAPTOREUM_EVM_RECEIPT_H

#include <evm/host.h>     // for CEvmHost::Log
#include <serialize.h>
#include <uint256.h>

#include <cstdint>
#include <vector>

namespace evm {

/**
 * Per-tx execution receipt — what eth_getTransactionReceipt returns
 * and the input to log indexers / wallets / block explorers.
 *
 * Receipts are produced during ConnectTip (Phase 3.6) from the
 * BlockProcessResult that ProcessEvmTransactionsInBlock returns, and
 * persisted to pevmstatedb keyed by the wrapper's sha256d hash
 * (the Raptoreum-side tx hash that ConnectTip already has at hand).
 * A secondary index 'X' (eth_hash -> rtm_hash) lets dApps look up
 * receipts by the Ethereum-style keccak256 they got back from
 * eth_sendRawTransaction.
 *
 * Each emitted log gets its own per-tx index (`logIndex`) and shares
 * the receipt's block/tx coordinates. Logs are stored inline inside
 * the receipt rather than in a separate space — small per-tx counts
 * and a single read covers eth_getTransactionReceipt + eth_getLogs.
 *
 * Receipts are NOT consensus-critical artefacts on their own (the
 * stateRoot+receiptsRoot header field is FUP-1) so we don't bind
 * them to the block undo trail.  On a reorg we simply re-derive
 * receipts for the new chain. Mainnet activation of D2 makes the
 * receiptsRoot a consensus field — that's when CEvmReceipt
 * serialization becomes a stability concern.
 */

struct CEvmLog
{
    uint160 address;
    std::vector<uint256> topics;
    std::vector<uint8_t> data;

    SERIALIZE_METHODS(CEvmLog, obj)
    {
        READWRITE(obj.address);
        READWRITE(obj.topics);
        READWRITE(obj.data);
    }
};

struct CEvmReceipt
{
    /** Ethereum-style tx hash (keccak256 of the original signed wire
     *  bytes). Populated when the tx arrived via
     *  eth_sendRawTransaction; otherwise zero. */
    uint256 ethTxHash;

    /** Raptoreum wrapper tx hash (sha256d). The receipt's primary
     *  key in the DB and the natural index for ConnectTip. */
    uint256 rtmTxHash;

    /** Block coordinates. */
    uint256 blockHash;
    uint64_t blockHeight{0};

    /** Index of the wrapper tx within block.vtx. */
    uint32_t txIndex{0};

    /** Per-tx fields. */
    uint8_t status{0};            // 1 = success, 0 = failure
    uint64_t gasUsed{0};
    uint64_t cumulativeGasUsed{0}; // sum across this tx and prior EVM
                                  // txs in the block
    uint64_t effectiveGasPrice{0};

    /** Sender (recovered EVM 20-byte address). */
    uint160 sender;

    /** Recipient (zero for contract-creation txs; same convention
     *  as Ethereum where eth_getTransactionReceipt returns null
     *  for `to` on a deploy). */
    uint160 to;
    bool isContractCreation{false};

    /** Contract address — populated when isContractCreation. */
    uint160 contractAddress;

    /** Logs emitted during execution. */
    std::vector<CEvmLog> logs;

    SERIALIZE_METHODS(CEvmReceipt, obj)
    {
        READWRITE(obj.ethTxHash);
        READWRITE(obj.rtmTxHash);
        READWRITE(obj.blockHash);
        READWRITE(obj.blockHeight);
        READWRITE(obj.txIndex);
        READWRITE(obj.status);
        READWRITE(obj.gasUsed);
        READWRITE(obj.cumulativeGasUsed);
        READWRITE(obj.effectiveGasPrice);
        READWRITE(obj.sender);
        READWRITE(obj.to);
        READWRITE(obj.isContractCreation);
        READWRITE(obj.contractAddress);
        READWRITE(obj.logs);
    }
};

/** Convert a CEvmHost::Log (from the apply-layer result) into the
 *  serializable receipt-side form. */
CEvmLog ConvertHostLog(const struct ::evm::CEvmHost::Log& hostLog);

} // namespace evm

#endif // RAPTOREUM_EVM_RECEIPT_H
