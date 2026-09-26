// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_BUSPOOL_H
#define RAPTOREUM_BUSPOOL_H

#include <llmq/quorums_signing.h>
#include <primitives/transaction.h>
#include <primitives/txcertificate.h>
#include <validationinterface.h>

#include <list>
#include <map>
#include <memory>

class CBlockIndex;
class CTxMemPool;
class CValidationState;

// One candidate graph remains in CTxMemPool. This cache retains only bodies and
// context-bound statements. All access uses cs_main, then mempool.cs.
class CBusPoolManager : public CValidationInterface, public llmq::CRecoveredSigsListener {
    struct Entry {
        CTransactionRef tx;
        CTxValidationCertificate statement;
        CTxValidationCertificate certificate;
        bool hasStatement{false};
        bool hasCertificate{false};
        size_t bytes{0};
        std::list<uint256>::iterator position;
    };

    CTxMemPool& pool;
    const size_t maxCount;
    const size_t maxBytes;
    size_t memoryUsage{0};
    std::list<uint256> order GUARDED_BY(cs_main);
    std::map<uint256, Entry> entries GUARDED_BY(cs_main);
    std::map<uint256, uint256> requests GUARDED_BY(cs_main);
    std::map<uint256, uint256> certificateIndex GUARDED_BY(cs_main);

    size_t BodyMemoryUsage(const CTransactionRef& tx) const;
    void Erase(const uint256& txid) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void RevalidateDelegated() EXCLUSIVE_LOCKS_REQUIRED(cs_main);

protected:
    void TransactionAddedToMempool(const CTransactionRef& tx, int64_t acceptTime) override;
    void NotifyTransactionLock(const CTransactionRef& tx,
                              const std::shared_ptr<const llmq::CInstantSendLock>& lock) override;
    void SynchronousUpdatedBlockTip(const CBlockIndex* tip, const CBlockIndex* fork, bool initialDownload) override;

public:
    static constexpr size_t DEFAULT_MAX_COUNT = 10000;
    static constexpr size_t DEFAULT_MAX_BYTES = 64 * 1024 * 1024;
    CBusPoolManager(CTxMemPool& poolIn, size_t maxCountIn = DEFAULT_MAX_COUNT,
                    size_t maxBytesIn = DEFAULT_MAX_BYTES);

    bool RetainTransaction(const CTransactionRef& tx);
    // Borrowers must charge the complete body to their own bounded lifetime
    // (template or reconstruction), even after this cache releases its copy.
    CTransactionRef GetTransaction(const uint256& txid) const;
    size_t GetCount() const;
    size_t GetMemoryUsage() const;
    size_t GetMaxCount() const { return maxCount; }
    size_t GetMaxBytes() const { return maxBytes; }
    bool GetCertificateByHash(const uint256& hash, CTransactionRef& tx, CTxValidationCertificate& certificate) const;
    std::vector<uint256> GetRelayCertificateHashes() const;
    UniValue GetInfo() const;
    UniValue GetEntry(const uint256& txid) const;

    bool GetCertificate(const uint256& txid, const CBlockIndex* parent,
                        CTxValidationCertificate& certificate) const;
    bool IsEligible(const uint256& txid, const CBlockIndex* parent) const;
    bool EnsureLocalScripts(const uint256& txid, CValidationState& state);
    bool RequestValidation(const CTransactionRef& tx, CTxValidationCertificate& statement,
                           bool& submitted, CValidationState& state);
    bool SubmitTransaction(const CTransactionRef& tx, const CTxValidationCertificate& certificate,
                           CValidationState& state);
    // Diagnostic retrieval includes negative certificates and unsigned requests.
    bool GetStatement(const uint256& txid, CTxValidationCertificate& statement, bool& recovered) const;
    void HandleNewRecoveredSig(const llmq::CRecoveredSig& recoveredSig) override;
};

bool IsBusPoolEnabled();
extern std::shared_ptr<CBusPoolManager> busPoolManager;

#endif
