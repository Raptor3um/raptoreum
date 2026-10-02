// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <buspool.h>

#include <chainparams.h>
#include <consensus/validation.h>
#include <core_memusage.h>
#include <llmq/quorums_instantsend.h>
#include <txdecoupling.h>
#include <txmempool.h>
#include <util/system.h>
#include <util/validation.h>
#include <validation.h>

#include <stdexcept>

std::shared_ptr<CBusPoolManager> busPoolManager;

bool IsBusPoolEnabled()
{
    return Params().GetConsensus().fTxDecouplingAllowed && gArgs.GetBoolArg("-txdecoupling", false);
}

CBusPoolManager::CBusPoolManager(CTxMemPool& poolIn, size_t maxCountIn, size_t maxBytesIn)
    : pool(poolIn), maxCount(maxCountIn), maxBytes(maxBytesIn)
{
    if (maxCount == 0 || maxCount > 100000 || maxBytes == 0 || maxBytes > 1024ULL * 1024 * 1024) {
        throw std::invalid_argument("Buspool limits must be 1..100000 records and 1..1073741824 bytes");
    }
}

void CBusPoolManager::Erase(const uint256& txid)
{
    AssertLockHeld(cs_main);
    const auto it = entries.find(txid);
    if (it == entries.end()) return;
    // A remote candidate must not outlive the only retained authorization for it.
    CValidationState state;
    if (pool.exists(txid) && !EnsureLocalScripts(txid, state)) {
        pool.removeRecursive(*it->second.tx, MemPoolRemovalReason::REORG);
    }
    if (it->second.hasStatement) {
        requests.erase(GetTxValidationRequestId(it->second.statement, Params().GetConsensus().hashGenesisBlock));
    }
    if (it->second.hasCertificate) certificateIndex.erase(SerializeHash(it->second.certificate));
    memoryUsage -= it->second.bytes;
    order.erase(it->second.position);
    entries.erase(it);
    pool.AddTransactionsUpdated(1);
}

bool CBusPoolManager::RetainTransaction(const CTransactionRef& tx)
{
    LOCK(cs_main);
    if (!tx) return false;
    if (entries.count(tx->GetHash())) return true;
    const size_t bytes = BodyMemoryUsage(tx);
    if (bytes > maxBytes) return false;
    while (entries.size() >= maxCount || memoryUsage > maxBytes - bytes) Erase(order.front());
    order.push_back(tx->GetHash());
    Entry entry;
    entry.tx = tx;
    entry.bytes = bytes;
    entry.position = std::prev(order.end());
    entries.emplace(tx->GetHash(), std::move(entry));
    memoryUsage += bytes;
    pool.AddTransactionsUpdated(1);
    return true;
}

size_t CBusPoolManager::BodyMemoryUsage(const CTransactionRef& tx) const
{
    // Reserve all index nodes even before a signing request exists. Signatures
    // have fixed storage, so later promotion cannot grow an already full cache.
    return RecursiveDynamicUsage(tx) + memusage::DynamicUsage(tx->vExtraPayload) +
        memusage::IncrementalDynamicUsage(entries) + memusage::IncrementalDynamicUsage(certificateIndex) +
        memusage::IncrementalDynamicUsage(requests) + memusage::MallocUsage(sizeof(uint256) + 2 * sizeof(void*));
}

CTransactionRef CBusPoolManager::GetTransaction(const uint256& txid) const
{
    LOCK(cs_main);
    const auto it = entries.find(txid);
    return it == entries.end() ? nullptr : it->second.tx;
}

size_t CBusPoolManager::GetCount() const
{
    LOCK(cs_main);
    return entries.size();
}

size_t CBusPoolManager::GetMemoryUsage() const
{
    LOCK(cs_main);
    return memoryUsage;
}

UniValue CBusPoolManager::GetInfo() const
{
    LOCK(cs_main);
    UniValue result(UniValue::VOBJ);
    result.pushKV("size", uint64_t(entries.size()));
    result.pushKV("bytes", uint64_t(memoryUsage));
    result.pushKV("maxcount", uint64_t(maxCount));
    result.pushKV("maxbytes", uint64_t(maxBytes));
    result.pushKV("pending", uint64_t(requests.size()));
    size_t eligible = 0;
    for (const auto& item : entries) eligible += IsEligible(item.first, ChainActive().Tip());
    result.pushKV("eligible", uint64_t(eligible));
    return result;
}

UniValue CBusPoolManager::GetEntry(const uint256& txid) const
{
    LOCK2(cs_main, pool.cs);
    const auto it = entries.find(txid);
    if (it == entries.end()) return UniValue();
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txid.GetHex());
    result.pushKV("bytes", uint64_t(it->second.bytes));
    result.pushKV("size", uint64_t(it->second.tx->GetTotalSize()));
    result.pushKV("eligible", IsEligible(txid, ChainActive().Tip()));
    const auto candidate = pool.mapTx.find(txid);
    result.pushKV("candidate", candidate != pool.mapTx.end());
    result.pushKV("locallyvalidated", candidate != pool.mapTx.end() && candidate->AreScriptsLocallyValidated());
    result.pushKV("certificate", it->second.hasCertificate);
    if (it->second.hasCertificate) {
        result.pushKV("parent", it->second.certificate.parentHash.GetHex());
        result.pushKV("positive", it->second.certificate.result == CTxValidationCertificate::POSITIVE);
    }
    return result;
}

bool CBusPoolManager::GetCertificate(const uint256& txid, const CBlockIndex* parent,
                                    CTxValidationCertificate& certificate) const
{
    LOCK(cs_main);
    const auto it = entries.find(txid);
    if (!parent || parent != ChainActive().Tip() || !IsTxDecouplingActive(parent, Params().GetConsensus()) ||
        it == entries.end() || !it->second.hasCertificate || !pool.exists(txid)) return false;
    const auto& cert = it->second.certificate;
    if (cert.result != CTxValidationCertificate::POSITIVE || cert.parentHash != parent->GetBlockHash()) return false;
    certificate = cert;
    return true;
}

bool CBusPoolManager::GetCertificateByHash(const uint256& hash, CTransactionRef& tx,
                                          CTxValidationCertificate& certificate) const
{
    LOCK(cs_main);
    const auto index = certificateIndex.find(hash);
    if (index == certificateIndex.end() || !GetCertificate(index->second, ChainActive().Tip(), certificate)) return false;
    tx = entries.at(index->second).tx;
    return true;
}

std::vector<uint256> CBusPoolManager::GetRelayCertificateHashes() const
{
    LOCK(cs_main);
    std::vector<uint256> result;
    result.reserve(certificateIndex.size());
    CTxValidationCertificate certificate;
    for (const auto& item : certificateIndex) {
        if (GetCertificate(item.second, ChainActive().Tip(), certificate)) result.push_back(item.first);
    }
    return result;
}

bool CBusPoolManager::IsEligible(const uint256& txid, const CBlockIndex* parent) const
{
    LOCK2(cs_main, pool.cs);
    if (!parent || parent != ChainActive().Tip() || !entries.count(txid)) return false;
    const auto candidate = pool.mapTx.find(txid);
    if (candidate == pool.mapTx.end()) return false;
    CTxValidationCertificate certificate;
    return GetCertificate(txid, parent, certificate) ||
        (candidate->AreScriptsLocallyValidated() && llmq::quorumInstantSendManager &&
         llmq::quorumInstantSendManager->IsLocked(txid));
}

bool CBusPoolManager::EnsureLocalScripts(const uint256& txid, CValidationState& state)
{
    LOCK2(cs_main, pool.cs);
    const auto it = pool.mapTx.find(txid);
    if (it == pool.mapTx.end()) return state.Error("buspool-candidate-missing");
    if (it->AreScriptsLocallyValidated()) return true;
    // Ordinary admission rules: after a reorg, inputs may come from other mempool entries.
    if (!CheckMempoolTxScripts(it->GetTx(), pool, state)) return false;
    pool.mapTx.modify(it, [](CTxMemPoolEntry& entry) { entry.SetScriptsLocallyValidated(true); });
    pool.AddTransactionsUpdated(1);
    return true;
}

bool CBusPoolManager::RequestValidation(const CTransactionRef& tx, CTxValidationCertificate& statement,
                                       bool& submitted, CValidationState& state)
{
    LOCK(cs_main);
    submitted = false;
    const auto* parent = ChainActive().Tip();
    const auto& consensus = Params().GetConsensus();
    if (!tx || !IsTxDecouplingActive(parent, consensus)) return state.Error("tx-decoupling-inactive");
    statement = CTxValidationCertificate();
    statement.txid = tx->GetHash();
    statement.parentHash = parent->GetBlockHash();
    statement.policyFlags = TX_VALIDATION_POLICY_FLAGS_V1;
    if (!GetTxValidationNextScriptFlags(parent, consensus, statement.consensusFlags)) {
        return state.Error("buspool-script-context-unavailable");
    }
    if (!GetTxValidationPrevoutsDigest(*tx, ChainstateActive().CoinsTip(), parent, statement.prevoutsDigest, state)) {
        return false;
    }
    CBLSPublicKey publicKey;
    const auto requestId = GetTxValidationRequestId(statement, consensus.hashGenesisBlock);
    if (!SelectTxValidationQuorum(parent, requestId, statement.quorumHash, publicKey, state)) return false;
    CValidationState localState;
    if (!CheckTxForCertificate(*tx, localState, statement.consensusFlags)) {
        if (!localState.IsInvalid()) return state.Error(FormatStateMessage(localState));
        statement.result = CTxValidationCertificate::NEGATIVE;
    } else {
        LOCK(pool.cs);
        const auto candidate = pool.mapTx.find(tx->GetHash());
        if (candidate != pool.mapTx.end() && !candidate->AreScriptsLocallyValidated()) {
            pool.mapTx.modify(candidate, [](CTxMemPoolEntry& entry) { entry.SetScriptsLocallyValidated(true); });
            pool.AddTransactionsUpdated(1);
        }
    }
    if (!RetainTransaction(tx)) return state.Error("buspool-body-too-large");
    auto& entry = entries.at(tx->GetHash());
    if (entry.hasStatement) requests.erase(GetTxValidationRequestId(entry.statement, consensus.hashGenesisBlock));
    entry.statement = statement;
    entry.hasStatement = true;
    requests[requestId] = tx->GetHash();
    const auto message = GetTxValidationMessageHash(statement);
    llmq::CRecoveredSig recovered;
    if (llmq::quorumSigningManager->GetRecoveredSigForId(statement.quorumType, requestId, recovered)) {
        HandleNewRecoveredSig(recovered);
        requests.erase(requestId);
    } else {
        submitted = llmq::quorumSigningManager->AsyncSignIfMember(statement.quorumType, requestId, message,
                                                                 statement.quorumHash);
    }
    return true;
}

bool CBusPoolManager::SubmitTransaction(const CTransactionRef& tx, const CTxValidationCertificate& certificate,
                                       CValidationState& state)
{
    LOCK(cs_main);
    if (!tx) return state.Error("buspool-missing-transaction");
    if (BodyMemoryUsage(tx) > maxBytes) return state.Error("buspool-body-too-large");
    // Admission verifies the certificate after cheaper checks; verify it here only for an existing candidate.
    if (pool.exists(tx->GetHash())) {
        if (!CheckTxValidationCertificate(certificate, *tx, ChainstateActive().CoinsTip(), ChainActive().Tip(),
                                          Params().GetConsensus(), state)) return false;
    } else {
        bool missingInputs = false;
        if (!AcceptToMemoryPool(pool, state, tx, &missingInputs, false, 0, false, &certificate)) {
            // Keep a diagnostic reason: admission reports missing inputs without one.
            if (missingInputs && state.IsValid())
                state.Invalid(false, REJECT_INVALID, "bad-txns-inputs-missingorspent");
            return false;
        }
    }
    const bool retained = RetainTransaction(tx);
    assert(retained); // Capacity was checked before admission; tx is immutable and cs_main is held.
    auto& entry = entries.at(tx->GetHash());
    if (entry.hasCertificate) certificateIndex.erase(SerializeHash(entry.certificate));
    entry.certificate = certificate;
    entry.hasCertificate = true;
    certificateIndex[SerializeHash(certificate)] = tx->GetHash();
    pool.AddTransactionsUpdated(1);
    return true;
}

bool CBusPoolManager::GetStatement(const uint256& txid, CTxValidationCertificate& statement, bool& recovered) const
{
    LOCK(cs_main);
    const auto it = entries.find(txid);
    if (it == entries.end()) return false;
    if (it->second.hasCertificate) {
        statement = it->second.certificate;
        recovered = true;
        return true;
    }
    if (!it->second.hasStatement) return false;
    statement = it->second.statement;
    recovered = false;
    return true;
}

void CBusPoolManager::HandleNewRecoveredSig(const llmq::CRecoveredSig& recoveredSig)
{
    LOCK(cs_main);
    const auto request = requests.find(recoveredSig.getId());
    if (request == requests.end()) return;
    const auto entry = entries.find(request->second);
    if (entry == entries.end()) return;
    CTxValidationCertificate certificate = entry->second.statement;
    if (recoveredSig.getLlmqType() != certificate.quorumType ||
        recoveredSig.getQuorumHash() != certificate.quorumHash ||
        recoveredSig.getMsgHash() != GetTxValidationMessageHash(certificate)) return;
    certificate.sig = recoveredSig.sig.Get();
    CValidationState state;
    if (!CheckTxValidationStatement(certificate, *entry->second.tx, ChainstateActive().CoinsTip(), ChainActive().Tip(),
                                    Params().GetConsensus(), certificate.result, state)) return;
    if (entry->second.hasCertificate) certificateIndex.erase(SerializeHash(entry->second.certificate));
    entry->second.certificate = certificate;
    entry->second.hasCertificate = true;
    if (certificate.result == CTxValidationCertificate::POSITIVE)
        certificateIndex[SerializeHash(certificate)] = entry->first;
    requests.erase(request);
    pool.AddTransactionsUpdated(1);
}

void CBusPoolManager::RevalidateDelegated()
{
    AssertLockHeld(cs_main);
    LOCK(pool.cs);
    // Scans the bounded mempool on each tip change; index delegated entries if profiling shows this dominates.
    std::vector<CTransactionRef> delegated;
    for (const auto& entry : pool.mapTx) {
        if (!entry.AreScriptsLocallyValidated()) delegated.emplace_back(entry.GetSharedTx());
    }
    for (const auto& tx : delegated) {
        CValidationState state;
        if (pool.exists(tx->GetHash()) && !EnsureLocalScripts(tx->GetHash(), state)) {
            pool.removeRecursive(*tx, MemPoolRemovalReason::REORG);
        }
    }
}

void CBusPoolManager::SynchronousUpdatedBlockTip(const CBlockIndex* tip, const CBlockIndex*, bool)
{
    LOCK(cs_main);
    // A rejected or invalidated side block reports an unchanged tip; certificates stay valid.
    if (tip == lastTip) return;
    lastTip = tip;
    RevalidateDelegated();
    requests.clear();
    certificateIndex.clear();
    for (auto& item : entries) {
        item.second.hasStatement = false;
        item.second.hasCertificate = false;
    }
    pool.AddTransactionsUpdated(1);
}

void CBusPoolManager::TransactionAddedToMempool(const CTransactionRef& tx, int64_t)
{
    LOCK2(cs_main, pool.cs);
    if (!pool.exists(tx->GetHash())) return;
    RetainTransaction(tx);
    // Receiving a remote certificate is not a request for an ordinary peer to
    // execute scripts. Smartnodes always validate locally before voting.
    const auto candidate = pool.mapTx.find(tx->GetHash());
    if (candidate == pool.mapTx.end() || (!fSmartnodeMode && !candidate->AreScriptsLocallyValidated())) return;
    CTxValidationCertificate statement;
    CValidationState state;
    bool submitted;
    RequestValidation(tx, statement, submitted, state);
}

void CBusPoolManager::NotifyTransactionLock(const CTransactionRef& tx,
                                          const std::shared_ptr<const llmq::CInstantSendLock>&)
{
    LOCK(cs_main);
    if (pool.exists(tx->GetHash()) && RetainTransaction(tx)) pool.AddTransactionsUpdated(1);
}
