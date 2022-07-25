// Copyright (c) 2018-2021 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
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

namespace llmq
{

CBLSWorker* blsWorker;

void InitLLMQSystem(CEvoDB& evoDb, CConnman& connman, bool unitTests, bool fWipe)
{
    blsWorker = new CBLSWorker();

    quorumDKGDebugManager = new CDKGDebugManager();
    quorumBlockProcessor = new CQuorumBlockProcessor(evoDb, connman);
    quorumDKGSessionManager = new CDKGSessionManager(connman, *blsWorker, unitTests, fWipe);
    quorumManager = new CQuorumManager(evoDb, connman, *blsWorker, *quorumDKGSessionManager);
    quorumSigSharesManager = new CSigSharesManager(connman);
    quorumSigningManager = new CSigningManager(connman, unitTests, fWipe);
    chainLocksHandler = new CChainLocksHandler(connman);
    quorumInstantSendManager = new CInstantSendManager(connman, unitTests, fWipe);

    // TODO: remove at some point of future upgrades. it is used only to wipe old db.
    auto llmqDbTmp = std::make_unique<CDBWrapper>(unitTests ? "" : (GetDataDir() / "llmq"), 1 << 20, unitTests, true);
}

void DestroyLLMQSystem()
{
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
    LOCK(cs_llmq_vbc);
    llmq_versionbitscache.Clear();
}

void StartLLMQSystem()
{
    if (blsWorker) {
        blsWorker->Start();
    }
    if (quorumDKGSessionManager) {
        quorumDKGSessionManager->StartThreads();
    }
    if (quorumManager) {
        quorumManager->Start();
    }
    if (quorumSigSharesManager) {
        quorumSigSharesManager->RegisterAsRecoveredSigsListener();
        quorumSigSharesManager->StartWorkerThread();
    }
    if (chainLocksHandler) {
        chainLocksHandler->Start();
    }
    if (quorumInstantSendManager) {
        quorumInstantSendManager->Start();
    }
}

void StopLLMQSystem()
{
    if (quorumInstantSendManager) {
        quorumInstantSendManager->Stop();
    }
    if (chainLocksHandler) {
        chainLocksHandler->Stop();
    }
    if (quorumSigSharesManager) {
        quorumSigSharesManager->StopWorkerThread();
        quorumSigSharesManager->UnregisterAsRecoveredSigsListener();
    }
    if (quorumManager) {
        quorumManager->Stop();
    }
    if (quorumDKGSessionManager) {
        quorumDKGSessionManager->StopThreads();
    }
    if (blsWorker) {
        blsWorker->Stop();
    }
}

void InterruptLLMQSystem()
{
    if (quorumSigSharesManager) {
        quorumSigSharesManager->InterruptWorkerThread();
    }
    if (quorumInstantSendManager) {
        quorumInstantSendManager->InterruptWorkerThread();
    }
}

} // namespace llmq
