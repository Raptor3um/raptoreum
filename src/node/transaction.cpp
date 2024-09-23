// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/validation.h>
#include <net.h>
#include <net_processing.h>
#include <txmempool.h>
#include <util/validation.h>
#include <validation.h>
#include <validationinterface.h>
#include <node/context.h>
#include <node/transaction.h>

#include <future>

TransactionError
BroadcastTransaction(NodeContext &node, const CTransactionRef tx, std::string &err_string, const CAmount &max_tx_fee,
                     bool relay, bool wait_callback, bool bypass_limits) {
    assert(node.connman);
    assert(node.mempool);
    std::promise<void> promise;
    uint256 hashTx = tx->GetHash();
    bool callback_set = false;

    { // cs_main scope
        LOCK(cs_main);
        // If the transaction is already confirmed in the chain, don't do anything
        // and return early.
        CCoinsViewCache &view = ::ChainstateActive().CoinsTip();
        for (size_t o = 0; o < tx->vout.size(); o++) {
            const Coin &existingCoin = view.AccessCoin(COutPoint(hashTx, o));
            // IsSpent doesnt mean the coin is spent, it means the output doesnt' exist.
            // So if the output does exist, then this transaction exists in the chain.
            if (!existingCoin.IsSpent()) return TransactionError::ALREADY_IN_CHAIN;
        }
        if (!node.mempool->exists(hashTx)) {
            // Transaction is not already in the mempool. Submit it.
            CValidationState state;
            bool fMissingInputs;
            if (!AcceptToMemoryPool(*node.mempool, state, std::move(tx), &fMissingInputs,
                                    bypass_limits, max_tx_fee)) {
                if (state.IsInvalid()) {
                    err_string = FormatStateMessage(state);
                    return TransactionError::MEMPOOL_REJECTED;
                } else {
                    if (fMissingInputs) {
                        return TransactionError::MISSING_INPUTS;
                    }
                    err_string = FormatStateMessage(state);
                    return TransactionError::MEMPOOL_ERROR;
                }
            }

            // Transaction was accepted to the mempool.

            if (wait_callback) {
                // For transactions broadcast from outside the wallet, make sure
                // that the wallet has been notified of the transaction before
                // continuing.
                //
                // This prevents a race where a user might call sendrawtransaction
                // with a transaction to/from their wallet, immediately call some
                // wallet RPC, and get a stale result because callbacks have not
                // yet been processed.
                CallFunctionInValidationInterfaceQueue([&promise] {
                    promise.set_value();
                });
                callback_set = true;
            }
        }

    } // cs_main

    if (callback_set) {
        // Wait until Validation Interface clients have been
        // notified of the transaction entering the mempool.
        promise.get_future().wait();
    }

    if (relay) {
        RelayTransaction(hashTx, *node.connman);

    }

    return TransactionError::OK;
}
