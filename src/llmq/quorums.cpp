// Copyright (c) 2018-2019 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/quorums.h>
#include <llmq/quorums_commitment.h>
#include <llmq/quorums_blockprocessor.h>
#include <llmq/quorums_dkgsession.h>
#include <llmq/quorums_dkgsessionmgr.h>
#include <llmq/quorums_utils.h>

#include <evo/specialtx.h>
#include <evo/deterministicmns.h>

#include <smartnode/activesmartnode.h>
#include <chainparams.h>
#include <smartnode/smartnode-sync.h>
#include <net.h>
#include <net_processing.h>
#include <netmessagemaker.h>
#include <univalue.h>
#include <validation.h>

#include <cxxtimer.hpp>

namespace llmq {

    static const std::string DB_QUORUM_SK_SHARE = "q_Qsk";
    static const std::string DB_QUORUM_QUORUM_VVEC = "q_Qqvvec";

    CQuorumManager *quorumManager;

    RecursiveMutex cs_data_requests;
    static std::unordered_map <std::pair<uint256, bool>, CQuorumDataRequest, StaticSaltedHasher> mapQuorumDataRequests
    GUARDED_BY(cs_data_requests);

    static uint256 MakeQuorumKey(const CQuorum &q) {
        CHashWriter hw(SER_NETWORK, 0);
        hw << q.params.type;
        hw << q.qc->quorumHash;
        for (const auto &dmn: q.members) {
            hw << dmn->proTxHash;
        }
        return hw.GetHash();
    }

    CQuorum::CQuorum(const Consensus::LLMQParams &_params, CBLSWorker &_blsWorker) : params(_params),
                                                                                     blsCache(_blsWorker) {
    }

    void
    CQuorum::Init(CFinalCommitmentPtr _qc, const CBlockIndex *_pQuorumBaseBlockIndex, const uint256 &_minedBlockHash,
                  const std::vector <CDeterministicMNCPtr> &_members) {
        qc = std::move(_qc);
        m_quorum_base_block_index = _pQuorumBaseBlockIndex;
        members = _members;
        minedBlockHash = _minedBlockHash;
    }

    bool CQuorum::SetVerificationVector(const BLSVerificationVector &quorumVecIn) {
        const auto quorumVecInSerialized = ::SerializeHash(quorumVecIn);

        LOCK(cs);
        if (quorumVecInSerialized != qc->quorumVvecHash) {
            return false;
        }
        quorumVvec = std::make_shared<BLSVerificationVector>(quorumVecIn);
        return true;
    }

    bool CQuorum::SetSecretKeyShare(const CBLSSecretKey &secretKeyShare) {
        if (!secretKeyShare.IsValid() ||
            (secretKeyShare.GetPublicKey() != GetPubKeyShare(WITH_LOCK(activeSmartnodeInfoCs,
            return GetMemberIndex(activeSmartnodeInfo.proTxHash))))) {
            return false;
        }
        LOCK(cs);
        skShare = secretKeyShare;
        return true;
    }

    bool CQuorum::IsMember(const uint256 &proTxHash) const {
        return ranges::any_of(members, [&proTxHash](const auto &dmn) {
            return dmn->proTxHash == proTxHash;
        });
    }

    bool CQuorum::IsValidMember(const uint256 &proTxHash) const {
        for (size_t i = 0; i < members.size(); i++) {
            if (members[i]->proTxHash == proTxHash) {
                return qc->validMembers[i];
            }
        }
        return false;
    }

    CBLSPublicKey CQuorum::GetPubKeyShare(size_t memberIdx) const {
        LOCK(cs);
        if (!HasVerificationVector() || memberIdx >= members.size() || !qc->validMembers[memberIdx]) {
            return CBLSPublicKey();
        }
        auto &m = members[memberIdx];
        return blsCache.BuildPubKeyShare(m->proTxHash, quorumVvec, CBLSId(m->proTxHash));
    }

    bool CQuorum::HasVerificationVector() const {
        LOCK(cs);
        return quorumVvec != nullptr;
    }

    CBLSSecretKey CQuorum::GetSkShare() const {
        LOCK(cs);
        return skShare;
    }

    int CQuorum::GetMemberIndex(const uint256 &proTxHash) const {
        for (size_t i = 0; i < members.size(); i++) {
            if (members[i]->proTxHash == proTxHash) {
                return (int) i;
            }
        }
        return -1;
    }

    void CQuorum::WriteContributions(CEvoDB &evoDb) const {
        uint256 dbKey = MakeQuorumKey(*this);

        LOCK(cs);
        if (HasVerificationVector()) {
            evoDb.GetRawDB().Write(std::make_pair(DB_QUORUM_QUORUM_VVEC, dbKey), *quorumVvec);
        }
        if (skShare.IsValid()) {
            evoDb.GetRawDB().Write(std::make_pair(DB_QUORUM_SK_SHARE, dbKey), skShare);
        }
    }

    bool CQuorum::ReadContributions(CEvoDB &evoDb) {
        uint256 dbKey = MakeQuorumKey(*this);

        BLSVerificationVector qv;
        if (evoDb.Read(std::make_pair(DB_QUORUM_QUORUM_VVEC, dbKey), qv)) {
            WITH_LOCK(cs, quorumVvec = std::make_shared<BLSVerificationVector>(std::move(qv)));
        } else {
            return false;
        }

        // We ignore the return value here as it is ok if this fails. If it fails, it usually means that we are not a
        // member of the quorum but observed the whole DKG process to have the quorum verification vector.
        WITH_LOCK(cs, evoDb.Read(std::make_pair(DB_QUORUM_SK_SHARE, dbKey), skShare));

        return true;
    }

    CQuorumManager::CQuorumManager(CEvoDB &_evoDb, CConnman &_connman, CBLSWorker &_blsWorker,
                                   CDKGSessionManager &_dkgManager) :
            connman(_connman),
            evoDb(_evoDb),
            blsWorker(_blsWorker),
            dkgManager(_dkgManager) {
        CLLMQUtils::InitQuorumsCache(mapQuorumsCache);
        CLLMQUtils::InitQuorumsCache(scanQuorumsCache);
        quorumThreadInterrupt.reset();
    }

    void CQuorumManager::Start() {
        int workerCount = std::thread::hardware_concurrency() / 2;
        workerCount = std::max(std::min(1, workerCount), 4);
        workerPool.resize(workerCount);
        RenameThreadPool(workerPool, "q-mngr");
    }

    void CQuorumManager::Stop() {
        quorumThreadInterrupt();
        workerPool.clear_queue();
        workerPool.stop(true);
    }

    void CQuorumManager::TriggerQuorumDataRecoveryThreads(const CBlockIndex *pIndex) const {
        if (!fSmartnodeMode || !CLLMQUtils::QuorumDataRecoveryEnabled() || pIndex == nullptr) {
            return;
        }

        const std::map <Consensus::LLMQType, QvvecSyncMode> mapQuorumVvecSync = CLLMQUtils::GetEnabledQuorumVvecSyncEntries();

        LogPrint(BCLog::LLMQ, "CQuorumManager::%s -- Process block %s\n", __func__, pIndex->GetBlockHash().ToString());

        for (auto &llmq: Params().GetConsensus().llmqs) {
            // Process signingActiveQuorumCount + 1 quorums for all available llmqTypes
            const auto vecQuorums = ScanQuorums(llmq.first, pIndex, llmq.second.signingActiveQuorumCount + 1);

            // First check if we are member of any quorum of this type
            auto proTxHash = WITH_LOCK(activeSmartnodeInfoCs,
            return activeSmartnodeInfo.proTxHash);
            bool fWeAreQuorumTypeMember = ranges::any_of(vecQuorums, [&proTxHash](const auto &pQuorum) {
                return pQuorum->IsValidMember(proTxHash);
            });

            for (const auto &pQuorum: vecQuorums) {
                // If there is already a thread running for this specific quorum skip it
                if (pQuorum->fQuorumDataRecoveryThreadRunning) {
                    continue;
                }

                uint16_t nDataMask{0};
                const bool fWeAreQuorumMember = pQuorum->IsValidMember(proTxHash);
                const bool fSyncForTypeEnabled = mapQuorumVvecSync.count(pQuorum->qc->llmqType) > 0;
                const QvvecSyncMode syncMode = fSyncForTypeEnabled ? mapQuorumVvecSync.at(pQuorum->qc->llmqType)
                                                                   : QvvecSyncMode::Invalid;
                const bool fSyncCurrent = syncMode == QvvecSyncMode::Always ||
                                          (syncMode == QvvecSyncMode::OnlyIfTypeMember && fWeAreQuorumTypeMember);

                if ((fWeAreQuorumMember || (fSyncForTypeEnabled && fSyncCurrent)) &&
                    !pQuorum->HasVerificationVector()) {
                    nDataMask |= llmq::CQuorumDataRequest::QUORUM_VERIFICATION_VECTOR;
                }

                if (fWeAreQuorumMember && !pQuorum->GetSkShare().IsValid()) {
                    nDataMask |= llmq::CQuorumDataRequest::ENCRYPTED_CONTRIBUTIONS;
                }

                if (nDataMask == 0) {
                    LogPrint(BCLog::LLMQ, "CQuorumManager::%s -- No data needed from (%d, %s) at height %d\n",
                             __func__, pQuorum->qc->llmqType, pQuorum->qc->quorumHash.ToString(), pIndex->nHeight);
                    continue;
                }

                // Finally start the thread which triggers the requests for this quorum
                StartQuorumDataRecoveryThread(pQuorum, pIndex, nDataMask);
            }
        }
    }

    void CQuorumManager::UpdatedBlockTip(const CBlockIndex *pindexNew, bool fInitialDownload) const {
        if (!smartnodeSync.IsBlockchainSynced()) {
            return;
        }

        for (auto &p: Params().GetConsensus().llmqs) {
            EnsureQuorumConnections(p.second, pindexNew);
        }

        {
            // Cleanup expired data requests
            LOCK(cs_data_requests);
            auto it = mapQuorumDataRequests.begin();
            while (it != mapQuorumDataRequests.end()) {
                if (it->second.IsExpired()) {
                    it = mapQuorumDataRequests.erase(it);
                } else {
                    ++it;
                }
            }
        }

        TriggerQuorumDataRecoveryThreads(pindexNew);
    }

    void CQuorumManager::EnsureQuorumConnections(const Consensus::LLMQParams &llmqParams,
                                                 const CBlockIndex *pindexNew) const {
        auto lastQuorums = ScanQuorums(llmqParams.type, pindexNew, (size_t) llmqParams.keepOldConnections);

        auto connmanQuorumsToDelete = connman.GetSmartnodeQuorums(llmqParams.type);

        // don't remove connections for the currently in-progress DKG round
        int curDkgHeight = pindexNew->nHeight - (pindexNew->nHeight % llmqParams.dkgInterval);
        auto curDkgBlock = pindexNew->GetAncestor(curDkgHeight)->GetBlockHash();
        connmanQuorumsToDelete.erase(curDkgBlock);

        for (const auto &quorum: lastQuorums) {
            if (CLLMQUtils::EnsureQuorumConnections(llmqParams, quorum->m_quorum_base_block_index, connman,
                                                    WITH_LOCK(activeSmartnodeInfoCs,
                return activeSmartnodeInfo.proTxHash))) {
                continue;
            }
            if (connmanQuorumsToDelete.count(quorum->qc->quorumHash) > 0) {
                LogPrint(BCLog::LLMQ, "CQuorumManager::%s -- removing smartnodes quorum connections for quorum %s:\n",
                         __func__, quorum->qc->quorumHash.ToString());
                connman.RemoveSmartnodeQuorumNodes(llmqParams.type, quorum->qc->quorumHash);
            }
        }
    }

    CQuorumPtr CQuorumManager::BuildQuorumFromCommitment(const Consensus::LLMQType llmqType,
                                                         const CBlockIndex *pQuorumBaseBlockIndex) const {
        AssertLockHeld(cs_map_quorums);
        assert(pQuorumBaseBlockIndex);

        const uint256 &quorumHash{pQuorumBaseBlockIndex->GetBlockHash()};
        uint256 minedBlockHash;
        CFinalCommitmentPtr qc = quorumBlockProcessor->GetMinedCommitment(llmqType, quorumHash, minedBlockHash);
        if (qc == nullptr) {
            return nullptr;
        }
        assert(qc->quorumHash == pQuorumBaseBlockIndex->GetBlockHash());

        const auto &llmqParams = llmq::GetLLMQParams(llmqType);
        auto quorum = std::make_shared<CQuorum>(llmqParams, blsWorker);
        auto members = CLLMQUtils::GetAllQuorumMembers(llmqParams, pQuorumBaseBlockIndex);

        quorum->Init(std::move(qc), pQuorumBaseBlockIndex, minedBlockHash, members);

        bool hasValidVvec = false;
        if (quorum->ReadContributions(evoDb)) {
            hasValidVvec = true;
        } else {
            if (BuildQuorumContributions(quorum->qc, quorum)) {
                quorum->WriteContributions(evoDb);
                hasValidVvec = true;
            } else {
                LogPrint(BCLog::LLMQ,
                         "CQuorumManager::%s -- quorum.ReadContributions and BuildQuorumContributions for block %s failed\n",
                         __func__, quorum->qc->quorumHash.ToString());
            }
        }

        if (hasValidVvec) {
            // pre-populate caches in the background
            // recovering public key shares is quite expensive and would result in serious lags for the first few signing
            // sessions if the shares would be calculated on-demand
            StartCachePopulatorThread(quorum);
        }

        mapQuorumsCache[llmqType].insert(quorumHash, quorum);

        return quorum;
    }

    bool CQuorumManager::BuildQuorumContributions(const CFinalCommitmentPtr &fqc,
                                                  const std::shared_ptr <CQuorum> &quorum) const {
        std::vector <uint16_t> memberIndexes;
        std::vector <BLSVerificationVectorPtr> vvecs;
        BLSSecretKeyVector skContributions;
        if (!dkgManager.GetVerifiedContributions((Consensus::LLMQType) fqc->llmqType, quorum->m_quorum_base_block_index,
                                                 fqc->validMembers, memberIndexes, vvecs, skContributions)) {
            return false;
        }

        cxxtimer::Timer t2(true);
        LOCK(quorum->cs);
        quorum->quorumVvec = blsWorker.BuildQuorumVerificationVector(vvecs);
        if (!quorum->HasVerificationVector()) {
            LogPrint(BCLog::LLMQ, "CQuorumManager::%s -- failed to build quorumVvec\n", __func__);
            // without the quorum vvec, there can't be a skShare, so we fail here. Failure is not fatal here, as it still
            // allows to use the quorum as a non-member (verification through the quorum pub key)
            return false;
        }
        quorum->skShare = blsWorker.AggregateSecretKeys(skContributions);
        if (!quorum->skShare.IsValid()) {
            quorum->skShare.Reset();
            LogPrint(BCLog::LLMQ, "CQuorumManager::%s -- failed to build skShare\n", __func__);
            // We don't bail out here as this is not a fatal error and still allows us to recover public key shares (as we
            // have a valid quorum vvec at this point)
        }
        t2.stop();

        LogPrint(BCLog::LLMQ, "CQuorumManager::%s -- built quorum vvec and skShare. time=%d\n", __func__, t2.count());

        return true;
    }

    bool CQuorumManager::HasQuorum(Consensus::LLMQType llmqType, const uint256 &quorumHash) {
        return quorumBlockProcessor->HasMinedCommitment(llmqType, quorumHash);
    }

    bool CQuorumManager::RequestQuorumData(CNode *pFrom, Consensus::LLMQType llmqType,
                                           const CBlockIndex *pQuorumBaseBlockIndex, uint16_t nDataMask,
                                           const uint256 &proTxHash) const {
        if (pFrom->nVersion < LLMQ_DATA_MESSAGES_VERSION) {
            LogPrint(BCLog::LLMQ, "CQuorumManager::%s -- Version must be %d or greater.\n", __func__,
                     LLMQ_DATA_MESSAGES_VERSION);
            return false;
        }
        if (pFrom == nullptr || (pFrom->GetVerifiedProRegTxHash().IsNull() && !pFrom->qwatch)) {
            LogPrint(BCLog::LLMQ,
                     "CQuorumManager::%s -- pFrom is neither a verified smartnode nor a qwatch connection\n", __func__);
            return false;
        }
        if (Params().GetConsensus().llmqs.count(llmqType) == 0) {
            LogPrint(BCLog::LLMQ, "CQuorumManager::%s -- Invalid llmqType: %d\n", __func__, llmqType);
            return false;
        }
        if (pQuorumBaseBlockIndex == nullptr) {
            LogPrint(BCLog::LLMQ, "CQuorumManager::%s -- Invalid pQuorumBaseBlockIndex: nullptr\n", __func__);
            return false;
        }
        if (GetQuorum(llmqType, pQuorumBaseBlockIndex) == nullptr) {
            LogPrint(BCLog::LLMQ, "CQuorumManager::%s -- Quorum not found: %s, %d\n", __func__,
                     pQuorumBaseBlockIndex->GetBlockHash().ToString(), llmqType);
            return false;
        }

        LOCK(cs_data_requests);
        auto key = std::make_pair(pFrom->GetVerifiedProRegTxHash(), true);
        auto it = mapQuorumDataRequests.emplace(key, CQuorumDataRequest(llmqType, pQuorumBaseBlockIndex->GetBlockHash(),
                                                                        nDataMask, proTxHash));
        if (!it.second && !it.first->second.IsExpired()) {
            LogPrint(BCLog::LLMQ, "CQuorumManager::%s -- Already requested\n", __func__);
            return false;
        }

        CNetMsgMaker msgMaker(pFrom->GetSendVersion());
        connman.PushMessage(pFrom, msgMaker.Make(NetMsgType::QGETDATA, it.first->second));

        return true;
    }

    std::vector <CQuorumCPtr> CQuorumManager::ScanQuorums(Consensus::LLMQType llmqType, size_t nCountRequested) const {
        const CBlockIndex *pindex = WITH_LOCK(cs_main,
        return ::ChainActive().Tip());
        return ScanQuorums(llmqType, pindex, nCountRequested);
    }

    std::vector <CQuorumCPtr> CQuorumManager::ScanQuorums(Consensus::LLMQType llmqType, const CBlockIndex *pindexStart,
                                                          size_t nCountRequested) const {
        if (pindexStart == nullptr || nCountRequested == 0) {
            return {};
        }

        const CBlockIndex *pIndexScanCommitments{pindexStart};
        size_t nScanCommitments{nCountRequested};
        std::vector <CQuorumCPtr> vecResultQuorums;

        {
            LOCK(cs_scan_quorums);
            auto &cache = scanQuorumsCache[llmqType];
            bool fCacheExists = cache.get(pindexStart->GetBlockHash(), vecResultQuorums);
            if (fCacheExists) {
                // We have exactly what requested so just return it
                if (vecResultQuorums.size() == nCountRequested) {
                    return vecResultQuorums;
                }
                // If we have more cached than requested return only a subvector
                if (vecResultQuorums.size() > nCountRequested) {
                    return {vecResultQuorums.begin(), vecResultQuorums.begin() + nCountRequested};
                }
                // If we have cached quorums but not enough, subtract what we have from the count and the set correct index where to start
                // scanning for the rests
                if (!vecResultQuorums.empty()) {
                    nScanCommitments -= vecResultQuorums.size();
                    pIndexScanCommitments = vecResultQuorums.back()->m_quorum_base_block_index->pprev;
                }
            } else {
                // If there is nothing in cache request at least cache.max_size() because this gets cached then later
                nScanCommitments = std::max(nCountRequested, cache.max_size());
            }
        }
        // Get the block indexes of the mined commitments to build the required quorums from
        std::vector<const CBlockIndex *> pQuorumBaseBlockIndexes{
                quorumBlockProcessor->GetMinedCommitmentsUntilBlock(llmqType, pIndexScanCommitments, nScanCommitments)};
        vecResultQuorums.reserve(vecResultQuorums.size() + pQuorumBaseBlockIndexes.size());

        for (auto &pQuorumBaseBlockIndex: pQuorumBaseBlockIndexes) {
            assert(pQuorumBaseBlockIndex);
            auto quorum = GetQuorum(llmqType, pQuorumBaseBlockIndex);
            assert(quorum != nullptr);
            vecResultQuorums.emplace_back(quorum);
        }

        const size_t nCountResult{vecResultQuorums.size()};
        if (nCountResult > 0) {
            LOCK(cs_scan_quorums);
            // Don't cache more than cache.max_size() elements
            auto &cache = scanQuorumsCache[llmqType];
            const size_t nCacheEndIndex = std::min(nCountResult, cache.max_size());
            cache.emplace(pindexStart->GetBlockHash(),
                          {vecResultQuorums.begin(), vecResultQuorums.begin() + nCacheEndIndex});
        }
        // Don't return more than nCountRequested elements
        const size_t nResultEndIndex = std::min(nCountResult, nCountRequested);
        return {vecResultQuorums.begin(), vecResultQuorums.begin() + nResultEndIndex};
    }

    CQuorumCPtr CQuorumManager::GetQuorum(Consensus::LLMQType llmqType, const uint256 &quorumHash) const {
        CBlockIndex * pQuorumBaseBlockIndex = WITH_LOCK(cs_main,
        return LookupBlockIndex(quorumHash));
        if (!pQuorumBaseBlockIndex) {
            LogPrint(BCLog::LLMQ, "CQuorumManager::%s -- block %s not found\n", __func__, quorumHash.ToString());
            return nullptr;
        }
        return GetQuorum(llmqType, pQuorumBaseBlockIndex);
    }

    CQuorumCPtr
    CQuorumManager::GetQuorum(Consensus::LLMQType llmqType, const CBlockIndex *pQuorumBaseBlockIndex) const {
        assert(pQuorumBaseBlockIndex);

        auto quorumHash = pQuorumBaseBlockIndex->GetBlockHash();

        // we must check this before we look into the cache. Reorgs might have happened which would mean we might have
        // cached quorums which are not in the active chain anymore
        if (!HasQuorum(llmqType, quorumHash)) {
            return nullptr;
        }

        LOCK(cs_map_quorums);
        CQuorumPtr pQuorum;
        if (mapQuorumsCache[llmqType].get(quorumHash, pQuorum)) {
            return pQuorum;
        }

        return BuildQuorumFromCommitment(llmqType, pQuorumBaseBlockIndex);
    }

    size_t CQuorumManager::GetQuorumRecoveryStartOffset(const CQuorumCPtr pQuorum, const CBlockIndex *pIndex) const {
        auto mns = deterministicMNManager->GetListForBlock(pIndex);
        std::vector <uint256> vecProTxHashes;
        vecProTxHashes.reserve(mns.GetValidMNsCount());
        mns.ForEachMN(true, [&](const CDeterministicMNCPtr &pSmartnode) {
            vecProTxHashes.emplace_back(pSmartnode->proTxHash);
        });
        std::sort(vecProTxHashes.begin(), vecProTxHashes.end());
        size_t nIndex{0};
        {
            LOCK(activeSmartnodeInfoCs);
            for (size_t i = 0; i < vecProTxHashes.size(); ++i) {
                if (activeSmartnodeInfo.proTxHash == vecProTxHashes[i]) {
                    nIndex = i;
                    break;
                }
            }
        }
        return nIndex % pQuorum->qc->validMembers.size();
    }

    void CQuorumManager::ProcessMessage(CNode *pFrom, const std::string &strCommand, CDataStream &vRecv) {
        auto strFunc = __func__;
        auto errorHandler = [&](const std::string &strError, int nScore = 10) {
            LogPrint(BCLog::LLMQ, "CQuorumManager::%s -- %s: %s, from peer=%d\n", strFunc, strCommand, strError,
                     pFrom->GetId());
            if (nScore > 0) {
                LOCK(cs_main);
                Misbehaving(pFrom->GetId(), nScore);
            }
        };

        if (strCommand == NetMsgType::QGETDATA) {

            if (!fSmartnodeMode || pFrom == nullptr || (pFrom->GetVerifiedProRegTxHash().IsNull() && !pFrom->qwatch)) {
                errorHandler("Not a verified smartnode or a qwatch connection");
                return;
            }

            CQuorumDataRequest request;
            vRecv >> request;

            auto sendQDATA = [&](CQuorumDataRequest::Errors nError = CQuorumDataRequest::Errors::UNDEFINED,
                                 const CDataStream &body = CDataStream(SER_NETWORK, PROTOCOL_VERSION)) {
                request.SetError(nError);
                CDataStream ssResponse(SER_NETWORK, pFrom->GetSendVersion(), request, body);
                connman.PushMessage(pFrom, CNetMsgMaker(pFrom->GetSendVersion()).Make(NetMsgType::QDATA, ssResponse));
            };

            {
                LOCK2(cs_main, cs_data_requests);
                auto key = std::make_pair(pFrom->GetVerifiedProRegTxHash(), false);
                auto it = mapQuorumDataRequests.find(key);
                if (it == mapQuorumDataRequests.end()) {
                    it = mapQuorumDataRequests.emplace(key, request).first;
                } else if (it->second.IsExpired()) {
                    it->second = request;
                } else {
                    errorHandler("Request limit exceeded", 25);
                }
            }

            if (Params().GetConsensus().llmqs.count(request.GetLLMQType()) == 0) {
                sendQDATA(CQuorumDataRequest::Errors::QUORUM_TYPE_INVALID);
                return;
            }

            const CBlockIndex *pQuorumBaseBlockIndex = WITH_LOCK(cs_main,
            return LookupBlockIndex(request.GetQuorumHash()));
            if (pQuorumBaseBlockIndex == nullptr) {
                sendQDATA(CQuorumDataRequest::Errors::QUORUM_BLOCK_NOT_FOUND);
                return;
            }

            const CQuorumCPtr pQuorum = GetQuorum(request.GetLLMQType(), pQuorumBaseBlockIndex);
            if (pQuorum == nullptr) {
                sendQDATA(CQuorumDataRequest::Errors::QUORUM_NOT_FOUND);
                return;
            }

            CDataStream ssResponseData(SER_NETWORK, pFrom->GetSendVersion());

            // Check if request wants QUORUM_VERIFICATION_VECTOR data
            if (request.GetDataMask() & CQuorumDataRequest::QUORUM_VERIFICATION_VECTOR) {
                if (!pQuorum->HasVerificationVector()) {
                    sendQDATA(CQuorumDataRequest::Errors::QUORUM_VERIFICATION_VECTOR_MISSING);
                    return;
                }

                WITH_LOCK(pQuorum->cs, ssResponseData << *pQuorum->quorumVvec);
            }

            // Check if request wants ENCRYPTED_CONTRIBUTIONS data
            if (request.GetDataMask() & CQuorumDataRequest::ENCRYPTED_CONTRIBUTIONS) {

                int memberIdx = pQuorum->GetMemberIndex(request.GetProTxHash());
                if (memberIdx == -1) {
                    sendQDATA(CQuorumDataRequest::Errors::SMARTNODE_IS_NO_MEMBER);
                    return;
                }

                std::vector <CBLSIESEncryptedObject<CBLSSecretKey>> vecEncrypted;
                if (!quorumDKGSessionManager->GetEncryptedContributions(request.GetLLMQType(), pQuorumBaseBlockIndex,
                                                                        pQuorum->qc->validMembers,
                                                                        request.GetProTxHash(), vecEncrypted)) {
                    sendQDATA(CQuorumDataRequest::Errors::ENCRYPTED_CONTRIBUTIONS_MISSING);
                    return;
                }

                ssResponseData << vecEncrypted;
            }

            sendQDATA(CQuorumDataRequest::Errors::NONE, ssResponseData);
            return;
        }

        if (strCommand == NetMsgType::QDATA) {

            auto verifiedProRegTxHash = pFrom->GetVerifiedProRegTxHash();
            if ((!fSmartnodeMode && !CLLMQUtils::IsWatchQuorumsEnabled()) || pFrom == nullptr ||
                (verifiedProRegTxHash.IsNull() && !pFrom->qwatch)) {
                errorHandler("Not a verified masternode or a qwatch connection");
                return;
            }

            CQuorumDataRequest request;
            vRecv >> request;

            {
                LOCK2(cs_main, cs_data_requests);
                auto it = mapQuorumDataRequests.find(std::make_pair(verifiedProRegTxHash, true));
                if (it == mapQuorumDataRequests.end()) {
                    errorHandler("Not requested");
                    return;
                }
                if (it->second.IsProcessed()) {
                    errorHandler("Already received");
                    return;
                }
                if (request != it->second) {
                    errorHandler("Not like requested");
                    return;
                }
                it->second.SetProcessed();
            }

            if (request.GetError() != CQuorumDataRequest::Errors::NONE) {
                errorHandler(strprintf("Error %d", request.GetError()), 0);
                return;
            }

            CQuorumPtr pQuorum;
            {
                LOCK(cs_map_quorums);
                if (!mapQuorumsCache[request.GetLLMQType()].get(request.GetQuorumHash(), pQuorum)) {
                    errorHandler("Quorum not found", 0); // Don't bump score because we asked for it
                    return;
                }
            }

            // Check if request has QUORUM_VERIFICATION_VECTOR data
            if (request.GetDataMask() & CQuorumDataRequest::QUORUM_VERIFICATION_VECTOR) {

                BLSVerificationVector verificationVector;
                vRecv >> verificationVector;

                if (pQuorum->SetVerificationVector(verificationVector)) {
                    StartCachePopulatorThread(pQuorum);
                } else {
                    errorHandler("Invalid quorum verification vector");
                    return;
                }
            }

            // Check if request has ENCRYPTED_CONTRIBUTIONS data
            if (request.GetDataMask() & CQuorumDataRequest::ENCRYPTED_CONTRIBUTIONS) {


                if (WITH_LOCK(pQuorum->cs, return pQuorum->quorumVvec->size() != pQuorum->params.threshold)) {
                    errorHandler("No valid quorum verification vector available",
                                 0); // Don't bump score because we asked for it
                    return;
                }


/*            BLSVerificationVectorPtr quorumVvecCopy;
            int thresholdCopy;
            {
                LOCK(pQuorum->cs);
                // work on copy (keep this simple)
                quorumVvecCopy = pQuorum->quorumVvec;
                // If the quorum vector ptr isn't here we know the issue is with pQuorum
                thresholdCopy = pQuorum->params.threshold;
                // Never say never :)
            }

            // This is 100% thread-safe so we can rule out threading issues
            if (quorumVvecCopy) {
                if (quorumVvecCopy->size() != thresholdCopy) {
                    errorHandler("No valid quorum verification vector available", 0); // Don't bump score because we asked for it
                    return;
                }
            }

            if (pQuorum->qc.validMembers.size() != pQuorum->params.size) {
                errorHandler("Quorum not fully qualified", 0); // Don't bump score because we asked for it
                return;
            }
*/
                int memberIdx = pQuorum->GetMemberIndex(request.GetProTxHash());
                if (memberIdx == -1) {
                    errorHandler("Not a member of the quorum", 0); // Don't bump score because we asked for it
                    return;
                }

                std::vector <CBLSIESEncryptedObject<CBLSSecretKey>> vecEncrypted;
                vRecv >> vecEncrypted;

                BLSSecretKeyVector vecSecretKeys;
                vecSecretKeys.resize(vecEncrypted.size());
                auto secret = WITH_LOCK(activeSmartnodeInfoCs,
                return *activeSmartnodeInfo.blsKeyOperator);
                int minSmartnodeProtoVersion = Params().IsFutureActive(::ChainActive().Tip())
                                               ? MIN_SMARTNODE_PROTO_VERSION : OLD_MIN_SMARTNODE_PROTO_VERSION;
                for (size_t i = 0; i < vecEncrypted.size(); ++i) {
                    if (!vecEncrypted[i].Decrypt(memberIdx, secret, vecSecretKeys[i], minSmartnodeProtoVersion)) {
                        errorHandler("Failed to decrypt");
                        return;
                    }
                }

                CBLSSecretKey secretKeyShare = blsWorker.AggregateSecretKeys(vecSecretKeys);
                if (!pQuorum->SetSecretKeyShare(secretKeyShare)) {
                    errorHandler("Invalid secret key share received");
                    return;
                }
            }
            pQuorum->WriteContributions(evoDb);
            return;
        }
    }

    void CQuorumManager::StartCachePopulatorThread(const CQuorumCPtr pQuorum) const {
        if (!pQuorum->HasVerificationVector()) {
            return;
        }

        cxxtimer::Timer t(true);
        LogPrint(BCLog::LLMQ, "CQuorumManager::StartCachePopulatorThread -- start\n");

        // when then later some other thread tries to get keys, it will be much faster
        workerPool.push([pQuorum, t, this](int threadId) {
            for (size_t i = 0; i < pQuorum->members.size() && !quorumThreadInterrupt; i++) {
                if (pQuorum->qc->validMembers[i]) {
                    pQuorum->GetPubKeyShare(i);
                }
            }
            LogPrint(BCLog::LLMQ, "CQuorumManager::StartCachePopulatorThread -- done. time=%d\n", t.count());
        });
    }

    void CQuorumManager::StartQuorumDataRecoveryThread(const CQuorumCPtr pQuorum, const CBlockIndex *pIndex,
                                                       uint16_t nDataMaskIn) const {
        if (pQuorum->fQuorumDataRecoveryThreadRunning) {
            LogPrint(BCLog::LLMQ, "CQuorumManager::%s -- Already running\n", __func__);
            return;
        }
        pQuorum->fQuorumDataRecoveryThreadRunning = true;

        workerPool.push([pQuorum, pIndex, nDataMaskIn, this](int threadId) {
            size_t nTries{0};
            uint16_t nDataMask{nDataMaskIn};
            int64_t nTimeLastSuccess{0};
            uint256 *pCurrentMemberHash{nullptr};
            std::vector <uint256> vecMemberHashes;
            const size_t nMyStartOffset{GetQuorumRecoveryStartOffset(pQuorum, pIndex)};
            const int64_t nRequestTimeout{10};

            auto printLog = [&](const std::string &strMessage) {
                const std::string strMember{pCurrentMemberHash == nullptr ? "nullptr" : pCurrentMemberHash->ToString()};
                LogPrint(BCLog::LLMQ,
                         "CQuorumManager::StartQuorumDataRecoveryThread -- %s - for llmqType %d, quorumHash %s, nDataMask (%d/%d), pCurrentMemberHash %s, nTries %d\n",
                         strMessage, pQuorum->qc->llmqType, pQuorum->qc->quorumHash.ToString(), nDataMask, nDataMaskIn,
                         strMember, nTries);
            };
            printLog("Start");

            while (!smartnodeSync.IsBlockchainSynced() && !quorumThreadInterrupt) {
                quorumThreadInterrupt.sleep_for(std::chrono::seconds(nRequestTimeout));
            }

            if (quorumThreadInterrupt) {
                printLog("Aborted");
                return;
            }

            vecMemberHashes.reserve(pQuorum->qc->validMembers.size());
            for (auto &member: pQuorum->members) {
                if (pQuorum->IsValidMember(member->proTxHash) &&
                    member->proTxHash != WITH_LOCK(activeSmartnodeInfoCs,
                    return activeSmartnodeInfo.proTxHash)) {
                    vecMemberHashes.push_back(member->proTxHash);
                }
            }
            std::sort(vecMemberHashes.begin(), vecMemberHashes.end());

            printLog("Try to request");

            while (nDataMask > 0 && !quorumThreadInterrupt) {

                if (nDataMask & llmq::CQuorumDataRequest::QUORUM_VERIFICATION_VECTOR &&
                    pQuorum->HasVerificationVector()) {
                    nDataMask &= ~llmq::CQuorumDataRequest::QUORUM_VERIFICATION_VECTOR;
                    printLog("Received quorumVvec");
                }

                if (nDataMask & llmq::CQuorumDataRequest::ENCRYPTED_CONTRIBUTIONS && pQuorum->GetSkShare().IsValid()) {
                    nDataMask &= ~llmq::CQuorumDataRequest::ENCRYPTED_CONTRIBUTIONS;
                    printLog("Received skShare");
                }

                if (nDataMask == 0) {
                    printLog("Success");
                    break;
                }

                if ((GetAdjustedTime() - nTimeLastSuccess) > nRequestTimeout) {
                    if (nTries >= vecMemberHashes.size()) {
                        printLog("All tried but failed");
                        break;
                    }
                    // Access the member list of the quorum with the calculated offset applied to balance the load equally
                    pCurrentMemberHash = &vecMemberHashes[(nMyStartOffset + nTries++) % vecMemberHashes.size()];
                    {
                        LOCK(cs_data_requests);
                        auto it = mapQuorumDataRequests.find(std::make_pair(*pCurrentMemberHash, true));
                        if (it != mapQuorumDataRequests.end() && !it->second.IsExpired()) {
                            printLog("Already asked");
                            continue;
                        }
                    }
                    // Sleep a bit depending on the start offset to balance out multiple requests to same smartnode
                    quorumThreadInterrupt.sleep_for(std::chrono::milliseconds(nMyStartOffset * 100));
                    nTimeLastSuccess = GetAdjustedTime();
                    connman.AddPendingSmartnode(*pCurrentMemberHash);
                    printLog("Connect");
                }

                auto proTxHash = WITH_LOCK(activeSmartnodeInfoCs,
                return activeSmartnodeInfo.proTxHash);
                connman.ForEachNode([&](CNode *pNode) {
                    auto verifiedProRegTxHash = pNode->GetVerifiedProRegTxHash();
                    if (pCurrentMemberHash == nullptr || verifiedProRegTxHash != *pCurrentMemberHash) {
                        return;
                    }

                    if (quorumManager->RequestQuorumData(pNode, pQuorum->qc->llmqType,
                                                         pQuorum->m_quorum_base_block_index, nDataMask, proTxHash)) {
                        nTimeLastSuccess = GetAdjustedTime();
                        printLog("Requested");
                    } else {
                        LOCK(cs_data_requests);
                        auto it = mapQuorumDataRequests.find(std::make_pair(verifiedProRegTxHash, true));
                        if (it == mapQuorumDataRequests.end()) {
                            printLog("Failed");
                            pNode->fDisconnect = true;
                            pCurrentMemberHash = nullptr;
                            return;
                        } else if (it->second.IsProcessed()) {
                            printLog("Processed");
                            pNode->fDisconnect = true;
                            pCurrentMemberHash = nullptr;
                            return;
                        } else {
                            printLog("Waiting");
                            return;
                        }
                    }
                });
                quorumThreadInterrupt.sleep_for(std::chrono::seconds(1));
            }
            pQuorum->fQuorumDataRecoveryThreadRunning = false;
            printLog("Done");
        });
    }

} // namespace llmq
