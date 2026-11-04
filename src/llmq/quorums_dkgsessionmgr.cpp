// Copyright (c) 2018-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <llmq/quorums_dkgsessionmgr.h>
#include <llmq/quorums_debug.h>
#include <llmq/quorums_utils.h>

#include <evo/deterministicmns.h>

#include <chainparams.h>
#include <net_processing.h>
#include <spork.h>
#include <validation.h>

namespace llmq {

    CDKGSessionManager *quorumDKGSessionManager;

    static const std::string DB_VVEC = "qdkg_V";
    static const std::string DB_SKCONTRIB = "qdkg_S";
    static const std::string DB_ENC_CONTRIB = "qdkg_E";
    static const std::string DB_NODE_VOTE = "qdkg_U";

    CDKGSessionManager::CDKGSessionManager(CConnman &_connman, CBLSWorker &_blsWorker, bool unitTests, bool fWipe) :
            db(std::make_unique<CDBWrapper>(unitTests ? "" : (GetDataDir() / "llmq/dkgdb"), 1 << 20, unitTests, fWipe)),
            blsWorker(_blsWorker), connman(_connman) {
        MigrateDKG();

        for (const auto &qt: Params().GetConsensus().llmqs) {
            dkgSessionHandlers.emplace(std::piecewise_construct,
                                       std::forward_as_tuple(qt.first),
                                       std::forward_as_tuple(qt.second, blsWorker, *this, connman));
        }
    }

    void CDKGSessionManager::MigrateDKG() {
        LOCK(cs_db);

        if (!db->IsEmpty()) return;

        LogPrint(BCLog::LLMQ, "CDKGSessionManager::%d -- start\n", __func__);

        CDBBatch batch(*db);
        auto oldDb = std::make_unique<CDBWrapper>(GetDataDir() / "llmq", 8 << 20);
        std::unique_ptr <CDBIterator> pcursor(oldDb->NewIterator());

        auto start_vvec = std::make_tuple(DB_VVEC, (Consensus::LLMQType) 0, uint256(), uint256());
        pcursor->Seek(start_vvec);

        while (pcursor->Valid()) {
            decltype(start_vvec) k;
            BLSVerificationVector v;

            if (!pcursor->GetKey(k) || std::get<0>(k) != DB_VVEC) {
                break;
            }
            if (!pcursor->GetValue(v)) {
                break;
            }

            batch.Write(k, v);

            if (batch.SizeEstimate() >= (1 << 24)) {
                db->WriteBatch(batch);
                batch.Clear();
            }

            pcursor->Next();
        }

        auto start_contrib = std::make_tuple(DB_SKCONTRIB, (Consensus::LLMQType) 0, uint256(), uint256());
        pcursor->Seek(start_contrib);

        while (pcursor->Valid()) {
            decltype(start_contrib) k;
            CBLSSecretKey v;

            if (!pcursor->GetKey(k) || std::get<0>(k) != DB_SKCONTRIB) {
                break;
            }
            if (!pcursor->GetValue(v)) {
                break;
            }

            batch.Write(k, v);

            if (batch.SizeEstimate() >= (1 << 24)) {
                db->WriteBatch(batch);
                batch.Clear();
            }

            pcursor->Next();
        }

        auto start_enc_contrib = std::make_tuple(DB_ENC_CONTRIB, (Consensus::LLMQType) 0, uint256(), uint256());
        pcursor->Seek(start_enc_contrib);

        while (pcursor->Valid()) {
            decltype(start_enc_contrib)
            k;
            CBLSIESMultiRecipientObjects <CBLSSecretKey> v;

            if (!pcursor->GetKey(k) || std::get<0>(k) != DB_ENC_CONTRIB) {
                break;
            }
            if (!pcursor->GetValue(v)) {
                break;
            }

            batch.Write(k, v);

            if (batch.SizeEstimate() >= (1 << 24)) {
                db->WriteBatch(batch);
                batch.Clear();
            }

            pcursor->Next();
        }

        db->WriteBatch(batch);
        pcursor.reset();
        oldDb.reset();

        LogPrint(BCLog::LLMQ, "CDKGSessionManager::%d -- done\n", __func__);
    }

    void CDKGSessionManager::StartThreads() {
        for (auto &it: dkgSessionHandlers) {
            it.second.StartThread();
        }
    }

    void CDKGSessionManager::StopThreads() {
        for (auto &it: dkgSessionHandlers) {
            it.second.StopThread();
        }
    }

    void CDKGSessionManager::UpdatedBlockTip(const CBlockIndex *pindexNew, bool fInitialDownload) {
        CleanupCache();

        if (fInitialDownload)
            return;
        if (!deterministicMNManager->IsDIP3Enforced(pindexNew->nHeight))
            return;
        if (!IsQuorumDKGEnabled())
            return;

        for (auto &qt: dkgSessionHandlers) {
            qt.second.UpdatedBlockTip(pindexNew);
        }
    }

    void CDKGSessionManager::ProcessMessage(CNode *pfrom, const std::string &strCommand, CDataStream &vRecv) {
        if (!IsQuorumDKGEnabled())
            return;

        if (strCommand == NetMsgType::QABSENCERECORD) {
            // Handled here, before the dkgSessionHandlers.count(llmqType) gate a few lines
            // down: unlike every other message in this function, a relayed absence record
            // must be processable for a quorum type this node isn't actively running a DKG
            // session for at all -- that's the entire point of this message existing (see the
            // design doc's second-relay-hop section). Membership is resolved from chain state
            // inside ValidateAndRelayAbsenceRecord, never from a live local session object.
            if (vRecv.empty()) {
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), 100);
                return;
            }
            CDKGAbsenceRecord record;
            try {
                vRecv >> record;
            } catch (const std::exception &) {
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), 100);
                return;
            }
            ValidateAndRelayAbsenceRecord(pfrom, record);
            return;
        }

        if (strCommand != NetMsgType::QCONTRIB
            && strCommand != NetMsgType::QCOMPLAINT
            && strCommand != NetMsgType::QJUSTIFICATION
            && strCommand != NetMsgType::QPCOMMITMENT
            && strCommand != NetMsgType::QABSENCEVOTE
            && strCommand != NetMsgType::QWATCH) {
            return;
        }

        if (strCommand == NetMsgType::QWATCH) {
            pfrom->qwatch = true;
            LogPrint(BCLog::LLMQ_DKG, "CDKGSessionManager::%s --  dkg watch message received \n", __func__);
            return;
        }

        if (vRecv.empty()) {
            LOCK(cs_main);
            LogPrint(BCLog::LLMQ_DKG, "CDKGSessionManager::%s -- empty data stream received\n", __func__);
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        // Peek into the message and see which LLMQType it is. First byte of all messages is always the LLMQType
        Consensus::LLMQType llmqType = (Consensus::LLMQType) * vRecv.begin();
        if (llmqType == Consensus::LLMQType::LLMQ_INVALID && (strCommand == NetMsgType::QCONTRIB || strCommand == NetMsgType::QPCOMMITMENT)) {
            // These messages carry the real LLMQType in the second byte. Reject a
            // 1-byte message instead of reading past the end of the buffer.
            if (vRecv.size() < 2) {
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), 100);
                return;
            }
            llmqType = (Consensus::LLMQType) * (vRecv.begin() + 1);
        }

        if (!dkgSessionHandlers.count(llmqType)) {
            LOCK(cs_main);
            LogPrint(BCLog::LLMQ_DKG, "CDKGSessionManager::%s -- invalid llmqType received %d\n", __func__, llmqType);
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        dkgSessionHandlers.at(llmqType).ProcessMessage(pfrom, strCommand, vRecv);
    }

    bool CDKGSessionManager::AlreadyHave(const CInv &inv) const {
        if (!IsQuorumDKGEnabled())
            return false;

        if (inv.type == MSG_QUORUM_ABSENCE_RECORD) {
            // Unlike every other type below, a relayed CDKGAbsenceRecord has nothing to do with
            // dkgSessionHandlers (that map is this node's own live sessions, keyed by llmqType --
            // irrelevant to a non-member relaying data for a quorum it was never part of). The
            // actual seen-set is relayedAbsenceRecords, already keyed by this same hash.
            LOCK(cs_relayedAbsenceRecords);
            return relayedAbsenceRecords.count(inv.hash) != 0;
        }

        for (const auto &p: dkgSessionHandlers) {
            auto &dkgType = p.second;
            if (dkgType.pendingContributions.HasSeen(inv.hash)
                || dkgType.pendingComplaints.HasSeen(inv.hash)
                || dkgType.pendingJustifications.HasSeen(inv.hash)
                || dkgType.pendingPrematureCommitments.HasSeen(inv.hash)
                || dkgType.pendingAbsenceVotes.HasSeen(inv.hash)) {
                return true;
            }
        }
        return false;
    }

    bool CDKGSessionManager::GetContribution(const uint256 &hash, CDKGContribution &ret) const {
        if (!IsQuorumDKGEnabled())
            return false;

        for (const auto &p: dkgSessionHandlers) {
            auto &dkgType = p.second;
            LOCK(dkgType.cs);
            if (dkgType.phase < QuorumPhase_Initialized || dkgType.phase > QuorumPhase_Contribute) {
                continue;
            }
            LOCK(dkgType.curSession->invCs);
            auto it = dkgType.curSession->contributions.find(hash);
            if (it != dkgType.curSession->contributions.end()) {
                ret = it->second;
                return true;
            }
        }
        return false;
    }

    bool CDKGSessionManager::GetComplaint(const uint256 &hash, CDKGComplaint &ret) const {
        if (!IsQuorumDKGEnabled())
            return false;

        for (const auto &p: dkgSessionHandlers) {
            auto &dkgType = p.second;
            LOCK(dkgType.cs);
            if (dkgType.phase < QuorumPhase_Contribute || dkgType.phase > QuorumPhase_Complain) {
                continue;
            }
            LOCK(dkgType.curSession->invCs);
            auto it = dkgType.curSession->complaints.find(hash);
            if (it != dkgType.curSession->complaints.end()) {
                ret = it->second;
                return true;
            }
        }
        return false;
    }

    bool CDKGSessionManager::GetJustification(const uint256 &hash, CDKGJustification &ret) const {
        if (!IsQuorumDKGEnabled())
            return false;

        for (const auto &p: dkgSessionHandlers) {
            auto &dkgType = p.second;
            LOCK(dkgType.cs);
            if (dkgType.phase < QuorumPhase_Complain || dkgType.phase > QuorumPhase_Justify) {
                continue;
            }
            LOCK(dkgType.curSession->invCs);
            auto it = dkgType.curSession->justifications.find(hash);
            if (it != dkgType.curSession->justifications.end()) {
                ret = it->second;
                return true;
            }
        }
        return false;
    }

    bool CDKGSessionManager::GetPrematureCommitment(const uint256 &hash, CDKGPrematureCommitment &ret) const {
        if (!IsQuorumDKGEnabled())
            return false;

        for (const auto &p: dkgSessionHandlers) {
            auto &dkgType = p.second;
            LOCK(dkgType.cs);
            if (dkgType.phase < QuorumPhase_Justify || dkgType.phase > QuorumPhase_Commit) {
                continue;
            }
            LOCK(dkgType.curSession->invCs);
            auto it = dkgType.curSession->prematureCommitments.find(hash);
            if (it != dkgType.curSession->prematureCommitments.end() &&
                dkgType.curSession->validCommitments.count(hash)) {
                ret = it->second;
                return true;
            }
        }
        return false;
    }

    bool CDKGSessionManager::GetAbsenceVote(const uint256 &hash, CDKGAbsenceVote &ret) const {
        if (!IsQuorumDKGEnabled())
            return false;

        for (const auto &p: dkgSessionHandlers) {
            auto &dkgType = p.second;
            LOCK(dkgType.cs);
            // Built at/after Finalize, for the lifetime of the session that
            // built it -- once phase wraps back to Initialized for the next
            // round, curSession is already a different (fresh) object, so no
            // explicit upper bound is needed.
            if (dkgType.phase < QuorumPhase_Finalize) {
                continue;
            }
            LOCK(dkgType.curSession->invCs);
            auto it = dkgType.curSession->absenceVotes.find(hash);
            if (it != dkgType.curSession->absenceVotes.end()) {
                ret = it->second;
                return true;
            }
        }
        return false;
    }

    bool CDKGSessionManager::GetAbsenceRecord(Consensus::LLMQType llmqType, CDKGAbsenceRecord &ret) const {
        if (!IsQuorumDKGEnabled())
            return false;

        auto it = dkgSessionHandlers.find(llmqType);
        if (it == dkgSessionHandlers.end()) {
            return false;
        }
        auto &dkgType = it->second;
        LOCK(dkgType.cs);
        // Same lower bound as GetAbsenceVote, for the same reason: built
        // at/after Finalize, for the lifetime of the session that built it.
        if (dkgType.phase < QuorumPhase_Finalize) {
            return false;
        }
        LOCK(dkgType.curSession->invCs);
        if (!dkgType.curSession->builtAbsenceRecord) {
            return false;
        }
        ret = *dkgType.curSession->builtAbsenceRecord;
        return true;
    }

    bool CDKGSessionManager::GetRelayedAbsenceRecord(const uint256 &hash, CDKGAbsenceRecord &ret) const {
        LOCK(cs_relayedAbsenceRecords);
        auto it = relayedAbsenceRecords.find(hash);
        if (it == relayedAbsenceRecords.end()) {
            return false;
        }
        ret = it->second;
        return true;
    }

    bool CDKGSessionManager::ValidateAndRelayAbsenceRecord(CNode *pfrom, const CDKGAbsenceRecord &record) {
        const CBlockIndex *pQuorumBaseBlockIndex;
        int nTipHeight;
        {
            LOCK(cs_main);
            pQuorumBaseBlockIndex = LookupBlockIndex(record.quorumHash);
            nTipHeight = ::ChainActive().Tip()->nHeight;
        }

        const auto &llmqParamsMap = Params().GetConsensus().llmqs;
        auto paramsIt = llmqParamsMap.find(record.llmqType);
        if (paramsIt == llmqParamsMap.end()) {
            // Not a recognized llmqType at all -- cheapest possible rejection, same tier as
            // the missing-block case below, before anything else runs.
            return false;
        }
        const Consensus::LLMQParams &params = paramsIt->second;

        // Cheap gate, before any membership lookup: reject a record whose claimed quorumHash
        // isn't a real, sufficiently recent block. Not misbehavior-scored on its own -- a
        // record for a quorum that's simply aged out of the retention window isn't evidence
        // of an attack, just something no longer worth relaying further.
        if (ShouldRejectAbsenceRecordAsStale(pQuorumBaseBlockIndex != nullptr, nTipHeight,
                                              pQuorumBaseBlockIndex != nullptr ? pQuorumBaseBlockIndex->nHeight : 0,
                                              params)) {
            return false;
        }

        // Resolved once here -- reused for Verify() below AND, on success, for the
        // onAbsenceRecordRelayed callback, so a listener that needs this same candidate list
        // (the non-member accumulator) never pays for CLLMQUtils::GetAllQuorumMembers a
        // second time for the same (llmqType, quorumHash). This is the exact lookup the
        // staleness gate above exists to keep off the cheap-spam path; a record that reached
        // this point has earned it.
        auto members = CLLMQUtils::GetAllQuorumMembers(params, pQuorumBaseBlockIndex);

        // Full membership + signature + intersection check. A failure here, for a record that
        // passed the cheap gate above, is treated as real misbehavior -- it's claiming to be
        // about a genuine, recent quorum and failing anyway.
        if (!record.Verify(pQuorumBaseBlockIndex, params, members)) {
            if (pfrom != nullptr) {
                WITH_LOCK(cs_main, Misbehaving(pfrom->GetId(), 100));
            }
            return false;
        }

        const uint256 hash = ::SerializeHash(record);
        bool isNew;
        {
            LOCK(cs_relayedAbsenceRecords);
            isNew = TryMarkAbsenceRecordRelayed(relayedAbsenceRecords, hash, record);
        }
        if (!isNew) {
            // Already validated and relayed once -- a valid record, just nothing new to do.
            return true;
        }

        CInv inv(MSG_QUORUM_ABSENCE_RECORD, hash);
        connman.RelayInv(inv);

        if (onAbsenceRecordRelayed) {
            std::vector<uint256> candidateMembers;
            candidateMembers.reserve(members.size());
            for (const auto &m: members) {
                candidateMembers.push_back(m->proTxHash);
            }
            onAbsenceRecordRelayed(candidateMembers, record);
        }

        return true;
    }

    void CDKGSessionManager::WriteVerifiedVvecContribution(Consensus::LLMQType llmqType,
                                                           const CBlockIndex *pQuorumBaseBlockIndex,
                                                           const uint256 &proTxHash,
                                                           const BLSVerificationVectorPtr &vvec) {
        LOCK(cs_db);
        db->Write(std::make_tuple(DB_VVEC, llmqType, pQuorumBaseBlockIndex->GetBlockHash(), proTxHash), *vvec);
    }

    void CDKGSessionManager::WriteVerifiedSkContribution(Consensus::LLMQType llmqType,
                                                         const CBlockIndex *pQuorumBaseBlockIndex,
                                                         const uint256 &proTxHash,
                                                         const CBLSSecretKey &skContribution) {
        LOCK(cs_db);
        db->Write(std::make_tuple(DB_SKCONTRIB, llmqType, pQuorumBaseBlockIndex->GetBlockHash(), proTxHash),
                  skContribution);
    }

    void CDKGSessionManager::WriteEncryptedContributions(Consensus::LLMQType llmqType,
                                                         const CBlockIndex *pQuorumBaseBlockIndex,
                                                         const uint256 &proTxHash,
                                                         const CBLSIESMultiRecipientObjects <CBLSSecretKey> &contributions) {
        LOCK(cs_db);
        db->Write(std::make_tuple(DB_ENC_CONTRIB, llmqType, pQuorumBaseBlockIndex->GetBlockHash(), proTxHash),
                  contributions);
    }

    void CDKGSessionManager::WriteUpdateVote(Consensus::LLMQType llmqType,
                                                const CBlockIndex *pQuorumBaseBlockIndex,
                                                const uint256 &proTxHash,
                                                const uint32_t& updateVote) {
        LOCK(cs_db);
        db->Write(std::make_tuple(DB_NODE_VOTE, llmqType, pQuorumBaseBlockIndex->GetBlockHash(), proTxHash), updateVote);
    }

    bool
    CDKGSessionManager::GetVerifiedContributions(Consensus::LLMQType llmqType, const CBlockIndex *pQuorumBaseBlockIndex,
                                                 const std::vector<bool> &validMembers,
                                                 std::vector <uint16_t> &memberIndexesRet,
                                                 std::vector <BLSVerificationVectorPtr> &vvecsRet,
                                                 BLSSecretKeyVector &skContributionsRet,
                                                 Consensus::CQuorumUpdateVoteVec &updateVotesRet) const {
        LOCK(contributionsCacheCs);
        auto members = CLLMQUtils::GetAllQuorumMembers(GetLLMQParams(llmqType), pQuorumBaseBlockIndex);

        memberIndexesRet.clear();
        vvecsRet.clear();
        skContributionsRet.clear();
        memberIndexesRet.reserve(members.size());
        vvecsRet.reserve(members.size());
        skContributionsRet.reserve(members.size());
        updateVotesRet.clear();
        for (size_t i = 0; i < members.size(); i++) {
            if (validMembers[i]) {
                const uint256 &proTxHash = members[i]->proTxHash;
                ContributionsCacheKey cacheKey = {llmqType, pQuorumBaseBlockIndex->GetBlockHash(), proTxHash};
                auto it = contributionsCache.find(cacheKey);
                if (it == contributionsCache.end()) {
                    LOCK(cs_db);
                    auto vvecPtr = std::make_shared<BLSVerificationVector>();
                    CBLSSecretKey skContribution;
                    if (!db->Read(std::make_tuple(DB_VVEC, llmqType, pQuorumBaseBlockIndex->GetBlockHash(), proTxHash),
                                  *vvecPtr)) {
                        LogPrint(BCLog::LLMQ,
                                 "CDKGSessionManager::%s -- llmqDb.Read unable to read DB_VEC for block %s, protxHash %s\n",
                                 __func__, pQuorumBaseBlockIndex->GetBlockHash().ToString(), proTxHash.GetHex());
                        return false;
                    }
                    db->Read(std::make_tuple(DB_SKCONTRIB, llmqType, pQuorumBaseBlockIndex->GetBlockHash(), proTxHash),
                             skContribution);

                    uint32_t nVersion = 0;
                    if (Updates().IsActive(EUpdate::ROUND_VOTING, pQuorumBaseBlockIndex)) {
                        db->Read(std::make_tuple(DB_NODE_VOTE, llmqType, pQuorumBaseBlockIndex->GetBlockHash(), proTxHash),
                                nVersion);
                    }
                    it = contributionsCache.emplace(cacheKey, ContributionsCacheEntry{GetTimeMillis(), nVersion, vvecPtr,
                                                                                      skContribution}).first;
                }

                memberIndexesRet.emplace_back(i);
                vvecsRet.emplace_back(it->second.vvec);
                skContributionsRet.emplace_back(it->second.skContribution);
                if (it->second.nVersion != 0)
                {
                    updateVotesRet.AddVotes(it->second.nVersion);
                }
            }
        }
        return true;
    }

    bool CDKGSessionManager::GetEncryptedContributions(Consensus::LLMQType llmqType,
                                                       const CBlockIndex *pQuorumBaseBlockIndex,
                                                       const std::vector<bool> &validMembers, const uint256 &nProTxHash,
                                                       std::vector <CBLSIESEncryptedObject<CBLSSecretKey>> &vecRet) const {
        auto members = CLLMQUtils::GetAllQuorumMembers(GetLLMQParams(llmqType), pQuorumBaseBlockIndex);

        vecRet.clear();
        vecRet.reserve(members.size());

        size_t nRequestedMemberIdx{std::numeric_limits<size_t>::max()};
        for (size_t i = 0; i < members.size(); i++) {
            if (members[i]->proTxHash == nProTxHash) {
                nRequestedMemberIdx = i;
                break;
            }
        }
        if (nRequestedMemberIdx == std::numeric_limits<size_t>::max()) {
            return false;
        }

        for (size_t i = 0; i < members.size(); i++) {
            if (validMembers[i]) {
                LOCK(cs_db);
                CBLSIESMultiRecipientObjects <CBLSSecretKey> encryptedContributions;
                if (!db->Read(std::make_tuple(DB_ENC_CONTRIB, llmqType, pQuorumBaseBlockIndex->GetBlockHash(),
                                              members[i]->proTxHash), encryptedContributions)) {
                    return false;
                }
                vecRet.emplace_back(encryptedContributions.Get(nRequestedMemberIdx));
            }
        }
        return true;
    }

    void CDKGSessionManager::CleanupCache() const {
        LOCK(contributionsCacheCs);
        auto curTime = GetTimeMillis();
        for (auto it = contributionsCache.begin(); it != contributionsCache.end();) {
            if (curTime - it->second.entryTime > MAX_CONTRIBUTION_CACHE_TIME) {
                it = contributionsCache.erase(it);
            } else {
                ++it;
            }
        }
    }

    bool IsQuorumDKGEnabled() {
        return sporkManager.IsSporkActive(SPORK_17_QUORUM_DKG_ENABLED);
    }

    bool IsContributionExpired(int nTipHeight, int nQuorumHeight, const Consensus::LLMQParams &params) {
        return nTipHeight - nQuorumHeight > params.max_store_depth();
    }

    void CDKGSessionManager::CleanupOldContributions() const
    {
        LOCK(cs_db);
        if (db->IsEmpty()) {
            return;
        }

        const auto prefixes = {DB_VVEC, DB_SKCONTRIB, DB_ENC_CONTRIB, DB_NODE_VOTE};

        for (const auto& p : Params().GetConsensus().llmqs) {
            auto& params = p.second;
            LogPrint(BCLog::LLMQ, "CDKGSessionManager::%s -- looking for old entries for llmq type %d\n", __func__, params.type);

            CDBBatch batch(*db);
            size_t cnt_old{0}, cnt_all{0};
            for (const auto& prefix : prefixes) {
                std::unique_ptr<CDBIterator> pcursor(db->NewIterator());
                auto start = std::make_tuple(prefix, params.type, uint256(), uint256());
                decltype(start) k;

                pcursor->Seek(start);
                LOCK(cs_main);
                LogPrint(BCLog::LLMQ, "CDKGSessionManager::%s -- Valid: %d, PrefixMatch: %d, TypeMatch: %d\n", __func__, pcursor->Valid(), std::get<0>(k) == prefix, std::get<1>(k) == params.type);

                while (pcursor->Valid()) {
                    if (!pcursor->GetKey(k) || std::get<0>(k) != prefix || std::get<1>(k) != params.type) {
                        break;
                    }
                    cnt_all++;
                    const CBlockIndex* pindexQuorum = LookupBlockIndex(std::get<2>(k));
                    if (pindexQuorum == nullptr || IsContributionExpired(::ChainActive().Tip()->nHeight, pindexQuorum->nHeight, params)) {
                        LogPrint(BCLog::LLMQ, "CDKGSessionManager::%s -- removing element for llmq type %d\n", __func__, uint8_t(params.type));

                        // not found or too old
                        batch.Erase(k);
                        cnt_old++;
                    }
                    pcursor->Next();
                }
                pcursor.reset();
            }
            LogPrint(BCLog::LLMQ, "CDKGSessionManager::%s -- found %lld entries for llmq type %d, old: %d\n", __func__, cnt_all, uint8_t(params.type), cnt_old);
            if (cnt_old > 0) {
                db->WriteBatch(batch);
                LogPrint(BCLog::LLMQ, "CDKGSessionManager::%s -- removed %lld old entries for llmq type %d\n", __func__, cnt_old, uint8_t(params.type));
            }
        }
    }

} // namespace llmq
