// Copyright (c) 2018-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/quorums_init.h>

#include <llmq/quorums.h>
#include <llmq/quorums_blockprocessor.h>
#include <llmq/quorums_commitment.h>
#include <llmq/quorums_chainlocks.h>
#include <llmq/quorums_debug.h>
#include <llmq/quorums_dkgsessionmgr.h>
#include <llmq/quorums_instantsend.h>
#include <llmq/quorums_signing.h>
#include <llmq/quorums_signing_shares.h>
#include <llmq/quorums_utils.h>

#include <dbwrapper.h>

namespace llmq {

    CBLSWorker *blsWorker;

    void InitLLMQSystem(CEvoDB &evoDb, CTxMemPool &mempool, CConnman &connman, bool unitTests, bool fWipe) {
        blsWorker = new CBLSWorker();

        quorumDKGDebugManager = new CDKGDebugManager();
        quorumBlockProcessor = new CQuorumBlockProcessor(evoDb, connman);
        quorumDKGSessionManager = new CDKGSessionManager(connman, *blsWorker, unitTests, fWipe);
        quorumManager = new CQuorumManager(evoDb, connman, *blsWorker, *quorumDKGSessionManager);
        quorumSigSharesManager = new CSigSharesManager(connman);
        quorumSigningManager = new CSigningManager(connman, unitTests, fWipe);
        chainLocksHandler = new CChainLocksHandler(mempool, connman);
        quorumInstantSendManager = new CInstantSendManager(mempool, connman, unitTests, fWipe);

        // TODO: remove at some point of future upgrades. it is used only to wipe old db.
        auto llmqDbTmp = std::make_unique<CDBWrapper>(unitTests ? "" : (GetDataDir() / "llmq"), 1 << 20, unitTests,
                                                      true);
    }

    void DestroyLLMQSystem() {
        delete quorumInstantSendManager;
        quorumInstantSendManager = nullptr;
        delete chainLocksHandler;
        chainLocksHandler = nullptr;
        delete quorumSigningManager;
        quorumSigningManager = nullptr;
        delete quorumSigSharesManager;
        quorumSigSharesManager = nullptr;
        delete quorumManager;
        quorumManager = nullptr;
        delete quorumDKGSessionManager;
        quorumDKGSessionManager = nullptr;
        delete quorumBlockProcessor;
        quorumBlockProcessor = nullptr;
        delete quorumDKGDebugManager;
        quorumDKGDebugManager = nullptr;
        delete blsWorker;
        blsWorker = nullptr;
    }

    void StartLLMQSystem() {
        if (blsWorker != nullptr) {
            blsWorker->Start();
        }
        if (quorumDKGSessionManager != nullptr) {
            quorumDKGSessionManager->StartThreads();
        }
        if (quorumManager != nullptr) {
            quorumManager->Start();
        }
        if (quorumSigSharesManager != nullptr) {
            quorumSigSharesManager->RegisterAsRecoveredSigsListener();
            quorumSigSharesManager->StartWorkerThread();
        }
        if (chainLocksHandler != nullptr) {
            chainLocksHandler->Start();
        }
        if (quorumInstantSendManager != nullptr) {
            quorumInstantSendManager->Start();
        }
    }

    void StopLLMQSystem() {
        if (quorumInstantSendManager != nullptr) {
            quorumInstantSendManager->Stop();
        }
        if (chainLocksHandler != nullptr) {
            chainLocksHandler->Stop();
        }
        if (quorumSigSharesManager != nullptr) {
            quorumSigSharesManager->StopWorkerThread();
            quorumSigSharesManager->UnregisterAsRecoveredSigsListener();
        }
        if (quorumManager != nullptr) {
            quorumManager->Stop();
        }
        if (quorumDKGSessionManager != nullptr) {
            quorumDKGSessionManager->StopThreads();
        }
        if (blsWorker != nullptr) {
            blsWorker->Stop();
        }
    }

    void InterruptLLMQSystem() {
        if (quorumSigSharesManager != nullptr) {
            quorumSigSharesManager->InterruptWorkerThread();
        }
        if (quorumInstantSendManager != nullptr) {
            quorumInstantSendManager->InterruptWorkerThread();
        }
    }

    void DoMaintenance() {
         if (quorumDKGSessionManager != nullptr) {
            quorumDKGSessionManager->CleanupOldContributions();
        }
    }

} // namespace llmq
