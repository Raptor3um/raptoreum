// Copyright (c) 2018-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_QUORUMS_INIT_H
#define RAPTOREUM_QUORUMS_INIT_H

class CConnman;

class CDBWrapper;

class CEvoDB;

class CTxMemPool;

namespace llmq {

// Init/destroy LLMQ globals
    void InitLLMQSystem(CEvoDB &evoDb, CTxMemPool &mempool, CConnman &connman, bool unitTests, bool fWipe = false);

    void DestroyLLMQSystem();

// Manage scheduled tasks, threads, listeners etc.
    void StartLLMQSystem();

    void StopLLMQSystem();

    void InterruptLLMQSystem();

    void DoMaintenance();
} // namespace llmq

#endif //RAPTOREUM_QUORUMS_INIT_H
