// Copyright (c) 2014-2019 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <governance/governance.h>
#include <consensus/validation.h>
#include <governance/governance-classes.h>
#include <governance/governance-validators.h>
#include <init.h>
#include <smartnode/smartnode-meta.h>
#include <smartnode/smartnode-sync.h>
#include <messagesigner.h>
#include <net_processing.h>
#include <netfulfilledman.h>
#include <netmessagemaker.h>
#include <spork.h>
#include <validation.h>

CGovernanceManager governance;

int nSubmittedFinalBudget;

const std::string CGovernanceManager::SERIALIZATION_VERSION_STRING = "CGovernanceManager-Version-15";
const int CGovernanceManager::MAX_TIME_FUTURE_DEVIATION = 60 * 60;
const int CGovernanceManager::RELIABLE_PROPAGATION_TIME = 60;

CGovernanceManager::CGovernanceManager() :
    nTimeLastDiff(0),
    nCachedBlockHeight(0),
    mapObjects(),
    mapErasedGovernanceObjects(),
    cmapVoteToObject(MAX_CACHE_SIZE),
    cmapInvalidVotes(MAX_CACHE_SIZE),
    cmmapOrphanVotes(MAX_CACHE_SIZE),
    mapLastSmartnodeObject(),
    setRequestedObjects(),
    fRateChecksEnabled(true),
    cs()
{
}

// Accessors for thread-safe access to maps
bool CGovernanceManager::HaveObjectForHash(const uint256& nHash) const
{
    LOCK(cs);
    return (mapObjects.count(nHash) == 1 || mapPostponedObjects.count(nHash) == 1);
}

bool CGovernanceManager::SerializeObjectForHash(const uint256& nHash, CDataStream& ss) const
{
    LOCK(cs);
    auto it = mapObjects.find(nHash);
    if (it == mapObjects.end()) {
        it = mapPostponedObjects.find(nHash);
        if (it == mapPostponedObjects.end())
            return false;
    }
    ss << it->second;
    return true;
}

bool CGovernanceManager::HaveVoteForHash(const uint256& nHash) const
{
    LOCK(cs);

    CGovernanceObject* pGovobj = nullptr;
    return cmapVoteToObject.Get(nHash, pGovobj) && pGovobj->GetVoteFile().HasVote(nHash);
}

int CGovernanceManager::GetVoteCount() const
{
    LOCK(cs);
    return (int)cmapVoteToObject.GetSize();
}

bool CGovernanceManager::SerializeVoteForHash(const uint256& nHash, CDataStream& ss) const
{
    LOCK(cs);

    CGovernanceObject* pGovobj = nullptr;
    return cmapVoteToObject.Get(nHash, pGovobj) && pGovobj->GetVoteFile().SerializeVoteToStream(nHash, ss);
}

void CGovernanceManager::ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman& connman, bool enable_bip61)
{
    if (fDisableGovernance) return;
    if (!smartnodeSync.IsBlockchainSynced()) return;

    // ANOTHER USER IS ASKING US TO HELP THEM SYNC GOVERNANCE OBJECT DATA
    if (strCommand == NetMsgType::MNGOVERNANCESYNC) {
        if (pfrom->nVersion < MIN_GOVERNANCE_PEER_PROTO_VERSION) {
            LogPrint(BCLog::GOBJECT, "MNGOVERNANCESYNC -- peer=%d using obsolete version %i\n", pfrom->GetId(), pfrom->nVersion);
            if (enable_bip61) {
                connman.PushMessage(pfrom, CNetMsgMaker(pfrom->GetSendVersion()).Make(NetMsgType::REJECT, strCommand,
                                                                                      REJECT_OBSOLETE, strprintf(
                                "Version must be %d or greater", MIN_GOVERNANCE_PEER_PROTO_VERSION)));
            }
            return;
        }

        // Ignore such requests until we are fully synced.
        // We could start processing this after smartnode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!smartnodeSync.IsSynced()) return;

        uint256 nProp;
        CBloomFilter filter;

        vRecv >> nProp;

        vRecv >> filter;
        filter.UpdateEmptyFull();

        if (nProp == uint256()) {
            SyncObjects(pfrom, connman);
        } else {
            SyncSingleObjVotes(pfrom, nProp, filter, connman);
        }
        LogPrint(BCLog::GOBJECT, "MNGOVERNANCESYNC -- syncing governance objects to our peer %s\n", pfrom->GetLogString());
    }

    // A NEW GOVERNANCE OBJECT HAS ARRIVED
    else if (strCommand == NetMsgType::MNGOVERNANCEOBJECT) {
        // MAKE SURE WE HAVE A VALID REFERENCE TO THE TIP BEFORE CONTINUING

        CGovernanceObject govobj;
        vRecv >> govobj;

        uint256 nHash = govobj.GetHash();

        {
            LOCK(cs_main);
            EraseObjectRequest(pfrom->GetId(), CInv(MSG_GOVERNANCE_OBJECT, nHash));
        }

        if (pfrom->nVersion < MIN_GOVERNANCE_PEER_PROTO_VERSION) {
            LogPrint(BCLog::GOBJECT, "MNGOVERNANCEOBJECT -- peer=%d using obsolete version %i\n", pfrom->GetId(), pfrom->nVersion);
            if (enable_bip61) {
                connman.PushMessage(pfrom, CNetMsgMaker(pfrom->GetSendVersion()).Make(NetMsgType::REJECT, strCommand,
                                                                                      REJECT_OBSOLETE, strprintf(
                                "Version must be %d or greater", MIN_GOVERNANCE_PEER_PROTO_VERSION)));
            }
            return;
        }

        if (!smartnodeSync.IsBlockchainSynced()) {
            LogPrint(BCLog::GOBJECT, "MNGOVERNANCEOBJECT -- smartnode list not synced\n");
            return;
        }

        std::string strHash = nHash.ToString();

        LogPrint(BCLog::GOBJECT, "MNGOVERNANCEOBJECT -- Received object: %s\n", strHash);

        if (!AcceptObjectMessage(nHash)) {
            LogPrint(BCLog::GOBJECT, "MNGOVERNANCEOBJECT -- Received unrequested object: %s\n", strHash);
            return;
        }

        LOCK2(cs_main, cs);

        if (mapObjects.count(nHash) || mapPostponedObjects.count(nHash) || mapErasedGovernanceObjects.count(nHash)) {
            // TODO - print error code? what if it's GOVOBJ_ERROR_IMMATURE?
            LogPrint(BCLog::GOBJECT, "MNGOVERNANCEOBJECT -- Received already seen object: %s\n", strHash);
            return;
        }

        bool fRateCheckBypassed = false;
        if (!SmartnodeRateCheck(govobj, true, false, fRateCheckBypassed)) {
            LogPrint(BCLog::GOBJECT, "MNGOVERNANCEOBJECT -- smartnode rate check failed - %s - (current block height %d) \n", strHash, nCachedBlockHeight);
            return;
        }

        std::string strError = "";
        // CHECK OBJECT AGAINST LOCAL BLOCKCHAIN

        bool fMissingConfirmations = false;
        bool fIsValid = govobj.IsValidLocally(strError, fMissingConfirmations, true);

        if (fRateCheckBypassed && fIsValid) {
            if (!SmartnodeRateCheck(govobj, true)) {
                LogPrint(BCLog::GOBJECT, "MNGOVERNANCEOBJECT -- smartnode rate check failed (after signature verification) - %s - (current block height %d)\n", strHash, nCachedBlockHeight);
                return;
            }
        }

        if (!fIsValid) {
            if (fMissingConfirmations) {
                AddPostponedObject(govobj);
                LogPrintf("MNGOVERNANCEOBJECT -- Not enough fee confirmations for: %s, strError = %s\n", strHash, strError);
            } else {
                LogPrint(BCLog::GOBJECT, "MNGOVERNANCEOBJECT -- Governance object is invalid - %s\n", strError);
                // apply node's ban score
                Misbehaving(pfrom->GetId(), 20);
            }

            return;
        }

        AddGovernanceObject(govobj, connman, pfrom);
    }

    // A NEW GOVERNANCE OBJECT VOTE HAS ARRIVED
    else if (strCommand == NetMsgType::MNGOVERNANCEOBJECTVOTE) {
        CGovernanceVote vote;
        vRecv >> vote;

        uint256 nHash = vote.GetHash();

        {
            LOCK(cs_main);
            EraseObjectRequest(pfrom->GetId(), CInv(MSG_GOVERNANCE_OBJECT_VOTE, nHash));
        }

        if (pfrom->nVersion < MIN_GOVERNANCE_PEER_PROTO_VERSION) {
            LogPrint(BCLog::GOBJECT, "MNGOVERNANCEOBJECTVOTE -- peer=%d using obsolete version %i\n", pfrom->GetId(), pfrom->nVersion);
            if (enable_bip61) {
                connman.PushMessage(pfrom, CNetMsgMaker(pfrom->GetSendVersion()).Make(NetMsgType::REJECT, strCommand,
                                                                                      REJECT_OBSOLETE, strprintf(
                                "Version must be %d or greater", MIN_GOVERNANCE_PEER_PROTO_VERSION)));
            }
        }

        // Ignore such messages until smartnode list is synced
        if (!smartnodeSync.IsBlockchainSynced()) {
            LogPrint(BCLog::GOBJECT, "MNGOVERNANCEOBJECTVOTE -- smartnode list not synced\n");
            return;
        }

        LogPrint(BCLog::GOBJECT, "MNGOVERNANCEOBJECTVOTE -- Received vote: %s\n", vote.ToString());

        std::string strHash = nHash.ToString();

        if (!AcceptVoteMessage(nHash)) {
            LogPrint(BCLog::GOBJECT, "MNGOVERNANCEOBJECTVOTE -- Received unrequested vote object: %s, hash: %s, peer = %d\n",
                vote.ToString(), strHash, pfrom->GetId());
            return;
        }

        CGovernanceException exception;
        if (ProcessVote(pfrom, vote, exception, connman)) {
            LogPrint(BCLog::GOBJECT, "MNGOVERNANCEOBJECTVOTE -- %s new\n", strHash);
            smartnodeSync.BumpAssetLastTime("MNGOVERNANCEOBJECTVOTE");
            vote.Relay(connman);
        } else {
            LogPrint(BCLog::GOBJECT, "MNGOVERNANCEOBJECTVOTE -- Rejected vote, error = %s\n", exception.what());
            if ((exception.GetNodePenalty() != 0) && smartnodeSync.IsSynced()) {
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), exception.GetNodePenalty());
            }
            return;
        }
    }
}

void CGovernanceManager::CheckOrphanVotes(CGovernanceObject& govobj, CGovernanceException& exception, CConnman& connman)
{
    uint256 nHash = govobj.GetHash();
    std::vector<vote_time_pair_t> vecVotePairs;
    cmmapOrphanVotes.GetAll(nHash, vecVotePairs);

    ScopedLockBool guard(cs, fRateChecksEnabled, false);

    int64_t nNow = GetAdjustedTime();
    for (auto& pairVote : vecVotePairs) {
        bool fRemove = false;
        CGovernanceVote& vote = pairVote.first;
        CGovernanceException exception;
        if (pairVote.second < nNow) {
            fRemove = true;
        } else if (govobj.ProcessVote(nullptr, vote, exception, connman)) {
            vote.Relay(connman);
            fRemove = true;
        }
        if (fRemove) {
            cmmapOrphanVotes.Erase(nHash, pairVote);
        }
    }
}

void CGovernanceManager::AddGovernanceObject(CGovernanceObject& govobj, CConnman& connman, CNode* pfrom)
{
    uint256 nHash = govobj.GetHash();
    std::string strHash = nHash.ToString();

    // UPDATE CACHED VARIABLES FOR THIS OBJECT AND ADD IT TO OUR MANAGED DATA

    govobj.UpdateSentinelVariables(); //this sets local vars in object

    LOCK2(cs_main, cs);
    std::string strError = "";

    // MAKE SURE THIS OBJECT IS OK

    if (!govobj.IsValidLocally(strError, true)) {
        LogPrint(BCLog::GOBJECT, "CGovernanceManager::AddGovernanceObject -- invalid governance object - %s - (nCachedBlockHeight %d) \n", strError, nCachedBlockHeight);
        return;
    }

    LogPrint(BCLog::GOBJECT, "CGovernanceManager::AddGovernanceObject -- Adding object: hash = %s, type = %d\n", nHash.ToString(), govobj.GetObjectType());

    // INSERT INTO OUR GOVERNANCE OBJECT MEMORY
    // IF WE HAVE THIS OBJECT ALREADY, WE DON'T WANT ANOTHER COPY
    auto objpair = mapObjects.emplace(nHash, govobj);

    if (!objpair.second) {
        LogPrint(BCLog::GOBJECT, "CGovernanceManager::AddGovernanceObject -- already have governance object %s\n", nHash.ToString());
        return;
    }

    // SHOULD WE ADD THIS OBJECT TO ANY OTHER MANAGERS?

    LogPrint(BCLog::GOBJECT, "CGovernanceManager::AddGovernanceObject -- Before trigger block, GetDataAsPlainString = %s, nObjectType = %d\n",
                govobj.GetDataAsPlainString(), govobj.GetObjectType());

    if (govobj.GetObjectType() == GOVERNANCE_OBJECT_TRIGGER) {
        if (!triggerman.AddNewTrigger(nHash)) {
            LogPrint(BCLog::GOBJECT, "CGovernanceManager::AddGovernanceObject -- undo adding invalid trigger object: hash = %s\n", nHash.ToString());
            objpair.first->second.PrepareDeletion(GetAdjustedTime());
            return;
        }
    }

    LogPrintf("CGovernanceManager::AddGovernanceObject -- %s new, received from peer %s\n", strHash, pfrom ? pfrom->GetLogString() : "nullptr");
    govobj.Relay(connman);

    // Update the rate buffer
    SmartnodeRateUpdate(govobj);

    smartnodeSync.BumpAssetLastTime("CGovernanceManager::AddGovernanceObject");

    // WE MIGHT HAVE PENDING/ORPHAN VOTES FOR THIS OBJECT

    CGovernanceException exception;
    CheckOrphanVotes(govobj, exception, connman);

    // SEND NOTIFICATION TO SCRIPT/ZMQ
    GetMainSignals().NotifyGovernanceObject(std::make_shared<const CGovernanceObject>(govobj));
}

void CGovernanceManager::UpdateCachesAndClean()
{
    // Return on initial sync, spammed the debug.log and provided no use
    if (!smartnodeSync.IsBlockchainSynced()) {
        return;
    }

    LogPrint(BCLog::GOBJECT, "CGovernanceManager::UpdateCachesAndClean\n");

    std::vector<uint256> vecDirtyHashes = mmetaman.GetAndClearDirtyGovernanceObjectHashes();

    LOCK2(cs_main, cs);

    for (const uint256& nHash : vecDirtyHashes) {
        auto it = mapObjects.find(nHash);
        if (it == mapObjects.end()) {
            continue;
        }
        it->second.ClearSmartnodeVotes();
    }

    ScopedLockBool guard(cs, fRateChecksEnabled, false);

    // Clean up any expired or invalid triggers
    triggerman.CleanAndRemove();

    auto it = mapObjects.begin();
    int64_t nNow = GetAdjustedTime();

    while (it != mapObjects.end()) {
        CGovernanceObject* pObj = &((*it).second);

        if (!pObj) {
            ++it;
            continue;
        }

        uint256 nHash = it->first;
        std::string strHash = nHash.ToString();

        // IF CACHE IS NOT DIRTY, WHY DO THIS?
        if (pObj->IsSetDirtyCache()) {
            // UPDATE LOCAL VALIDITY AGAINST CRYPTO DATA
            pObj->UpdateLocalValidity();

            // UPDATE SENTINEL SIGNALING VARIABLES
            pObj->UpdateSentinelVariables();
        }

        // IF DELETE=TRUE, THEN CLEAN THE MESS UP!

        int64_t nTimeSinceDeletion = nNow - pObj->GetDeletionTime();

        LogPrint(BCLog::GOBJECT, "CGovernanceManager::UpdateCachesAndClean -- Checking object for deletion: %s, deletion time = %d, time since deletion = %d, delete flag = %d, expired flag = %d\n",
            strHash, pObj->GetDeletionTime(), nTimeSinceDeletion, pObj->IsSetCachedDelete(), pObj->IsSetExpired());

        if ((pObj->IsSetCachedDelete() || pObj->IsSetExpired()) &&
            (nTimeSinceDeletion >= GOVERNANCE_DELETION_DELAY)) {
            LogPrintf("CGovernanceManager::UpdateCachesAndClean -- erase obj %s\n", (*it).first.ToString());
            mmetaman.RemoveGovernanceObject(pObj->GetHash());

            // Remove vote references
            const object_ref_cm_t::list_t& listItems = cmapVoteToObject.GetItemList();
            object_ref_cm_t::list_cit lit = listItems.begin();
            while (lit != listItems.end()) {
                if (lit->value == pObj) {
                    uint256 nKey = lit->key;
                    ++lit;
                    cmapVoteToObject.Erase(nKey);
                } else {
                    ++lit;
                }
            }

            int64_t nTimeExpired{0};

            if (pObj->GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL) {
                // keep hashes of deleted proposals forever
                nTimeExpired = std::numeric_limits<int64_t>::max();
            } else {
                int64_t nSuperblockCycleSeconds = Params().GetConsensus().nSuperblockCycle * Params().GetConsensus().nPowTargetSpacing;
                nTimeExpired = pObj->GetCreationTime() + 2 * nSuperblockCycleSeconds + GOVERNANCE_DELETION_DELAY;
            }

            mapErasedGovernanceObjects.insert(std::make_pair(nHash, nTimeExpired));
            mapObjects.erase(it++);
        } else {
            // NOTE: triggers are handled via triggerman
            if (pObj->GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL) {
                CProposalValidator validator(pObj->GetDataAsHexString(), true);
                if (!validator.Validate()) {
                    LogPrintf("CGovernanceManager::UpdateCachesAndClean -- set for deletion expired obj %s\n", strHash);
                    pObj->PrepareDeletion(nNow);
                }
            }
            ++it;
        }
    }

    // forget about expired deleted objects
    auto s_it = mapErasedGovernanceObjects.begin();
    while (s_it != mapErasedGovernanceObjects.end()) {
        if (s_it->second < nNow) {
            mapErasedGovernanceObjects.erase(s_it++);
        } else {
            ++s_it;
        }
    }

    LogPrintf("CGovernanceManager::UpdateCachesAndClean -- %s\n", ToString());
}

CGovernanceObject* CGovernanceManager::FindGovernanceObject(const uint256& nHash)
{
    LOCK(cs);

    if (mapObjects.count(nHash)) return &mapObjects[nHash];

    return nullptr;
}

std::vector<CGovernanceVote> CGovernanceManager::GetCurrentVotes(const uint256& nParentHash, const COutPoint& mnCollateralOutpointFilter) const
{
    LOCK(cs);
    std::vector<CGovernanceVote> vecResult;

    // Find the governance object or short-circuit.
    auto it = mapObjects.find(nParentHash);
    if (it == mapObjects.end()) return vecResult;
    const CGovernanceObject& govobj = it->second;

    auto mnList = deterministicMNManager->GetListAtChainTip();
    std::map<COutPoint, CDeterministicMNCPtr> mapSmartnodes;
    if (mnCollateralOutpointFilter.IsNull()) {
        mnList.ForEachMN(false, [&](const CDeterministicMNCPtr& dmn) {
            mapSmartnodes.emplace(dmn->collateralOutpoint, dmn);
        });
    } else {
        auto dmn = mnList.GetMNByCollateral(mnCollateralOutpointFilter);
        if (dmn) {
            mapSmartnodes.emplace(dmn->collateralOutpoint, dmn);
        }
    }

    // Loop thru each MN collateral outpoint and get the votes for the `nParentHash` governance object
    for (const auto& mnpair : mapSmartnodes) {
        // get a vote_rec_t from the govobj
        vote_rec_t voteRecord;
        if (!govobj.GetCurrentMNVotes(mnpair.first, voteRecord)) continue;

        for (const auto& voteInstancePair : voteRecord.mapInstances) {
            int signal = voteInstancePair.first;
            int outcome = voteInstancePair.second.eOutcome;
            int64_t nCreationTime = voteInstancePair.second.nCreationTime;

            CGovernanceVote vote = CGovernanceVote(mnpair.first, nParentHash, (vote_signal_enum_t)signal, (vote_outcome_enum_t)outcome);
            vote.SetTime(nCreationTime);

            vecResult.push_back(vote);
        }
    }

    return vecResult;
}

std::vector<const CGovernanceObject*> CGovernanceManager::GetAllNewerThan(int64_t nMoreThanTime) const
{
    LOCK(cs);

    std::vector<const CGovernanceObject*> vGovObjs;

    for (const auto& objPair : mapObjects) {
        // IF THIS OBJECT IS OLDER THAN TIME, CONTINUE
        if (objPair.second.GetCreationTime() < nMoreThanTime) {
            continue;
        }

        // ADD GOVERNANCE OBJECT TO LIST
        const CGovernanceObject* pGovObj = &(objPair.second);
        vGovObjs.push_back(pGovObj);
    }

    return vGovObjs;
}

//
// Sort by votes, if there's a tie sort by their feeHash TX
//
struct sortProposalsByVotes {
    bool operator()(const std::pair<CGovernanceObject*, int>& left, const std::pair<CGovernanceObject*, int>& right)
    {
        if (left.second != right.second) return (left.second > right.second);
        return (UintToArith256(left.first->GetCollateralHash()) > UintToArith256(right.first->GetCollateralHash()));
    }
};

void CGovernanceManager::DoMaintenance(CConnman& connman)
{
    if (fDisableGovernance || !smartnodeSync.IsSynced() || ShutdownRequested()) return;

    // CHECK OBJECTS WE'VE ASKED FOR, REMOVE OLD ENTRIES

    CleanOrphanObjects();

    RequestOrphanObjects(connman);

    // CHECK AND REMOVE - REPROCESS GOVERNANCE OBJECTS

    UpdateCachesAndClean();
}

bool CGovernanceManager::ConfirmInventoryRequest(const CInv& inv)
{
    // do not request objects until it's time to sync
    if (!smartnodeSync.IsBlockchainSynced()) return false;

    LOCK(cs);

    LogPrint(BCLog::GOBJECT, "CGovernanceManager::ConfirmInventoryRequest inv = %s\n", inv.ToString());

    // First check if we've already recorded this object
    switch (inv.type) {
    case MSG_GOVERNANCE_OBJECT: {
        if (mapObjects.count(inv.hash) == 1 || mapPostponedObjects.count(inv.hash) == 1) {
            LogPrint(BCLog::GOBJECT, "CGovernanceManager::ConfirmInventoryRequest already have governance object, returning false\n");
            return false;
        }
        break;
    }
    case MSG_GOVERNANCE_OBJECT_VOTE: {
        if (cmapVoteToObject.HasKey(inv.hash)) {
            LogPrint(BCLog::GOBJECT, "CGovernanceManager::ConfirmInventoryRequest already have governance vote, returning false\n");
            return false;
        }
        break;
    }
    default:
        LogPrint(BCLog::GOBJECT, "CGovernanceManager::ConfirmInventoryRequest unknown type, returning false\n");
        return false;
    }


    hash_s_t* setHash = nullptr;
    switch (inv.type) {
    case MSG_GOVERNANCE_OBJECT:
        setHash = &setRequestedObjects;
        break;
    case MSG_GOVERNANCE_OBJECT_VOTE:
        setHash = &setRequestedVotes;
        break;
    default:
        return false;
    }

    auto it = setHash->find(inv.hash);
    if (it == setHash->end()) {
        setHash->insert(inv.hash);
        LogPrint(BCLog::GOBJECT, "CGovernanceManager::ConfirmInventoryRequest added inv to requested set\n");
    }

    LogPrint(BCLog::GOBJECT, "CGovernanceManager::ConfirmInventoryRequest reached end, returning true\n");
    return true;
}

void CGovernanceManager::SyncSingleObjVotes(CNode* pnode, const uint256& nProp, const CBloomFilter& filter, CConnman& connman)
{
    // do not provide any data until our node is synced
    if (!smartnodeSync.IsSynced()) return;

    int nVoteCount = 0;

    // SYNC GOVERNANCE OBJECTS WITH OTHER CLIENT

    LogPrint(BCLog::GOBJECT, "CGovernanceManager::%s -- syncing single object to peer=%d, nProp = %s\n", __func__, pnode->GetId(), nProp.ToString());

    LOCK2(cs_main, cs);

    // single valid object and its valid votes
    auto it = mapObjects.find(nProp);
    if (it == mapObjects.end()) {
        LogPrint(BCLog::GOBJECT, "CGovernanceManager::%s -- no matching object for hash %s, peer=%d\n", __func__, nProp.ToString(), pnode->GetId());
        return;
    }
    CGovernanceObject& govobj = it->second;
    std::string strHash = it->first.ToString();

    LogPrint(BCLog::GOBJECT, "CGovernanceManager::%s -- attempting to sync govobj: %s, peer=%d\n", __func__, strHash, pnode->GetId());

    if (govobj.IsSetCachedDelete() || govobj.IsSetExpired()) {
        LogPrintf("CGovernanceManager::%s -- not syncing deleted/expired govobj: %s, peer=%d\n", __func__,
            strHash, pnode->GetId());
        return;
    }

    auto fileVotes = govobj.GetVoteFile();

    for (const auto& vote : fileVotes.GetVotes()) {
        uint256 nVoteHash = vote.GetHash();

        bool onlyVotingKeyAllowed = govobj.GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL && vote.GetSignal() == VOTE_SIGNAL_FUNDING;

        if (filter.contains(nVoteHash) || !vote.IsValid(onlyVotingKeyAllowed)) {
            continue;
        }
        pnode->PushInventory(CInv(MSG_GOVERNANCE_OBJECT_VOTE, nVoteHash));
        ++nVoteCount;
    }

    CNetMsgMaker msgMaker(pnode->GetSendVersion());
    connman.PushMessage(pnode, msgMaker.Make(NetMsgType::SYNCSTATUSCOUNT, SMARTNODE_SYNC_GOVOBJ_VOTE, nVoteCount));
    LogPrintf("CGovernanceManager::%s -- sent %d votes to peer=%d\n", __func__, nVoteCount, pnode->GetId());
}

void CGovernanceManager::SyncObjects(CNode* pnode, CConnman& connman) const
{
    // do not provide any data until our node is synced
    if (!smartnodeSync.IsSynced()) return;

    if (netfulfilledman.HasFulfilledRequest(pnode->addr, NetMsgType::MNGOVERNANCESYNC)) {
        LOCK(cs_main);
        // Asking for the whole list multiple times in a short period of time is no good
        LogPrint(BCLog::GOBJECT, "CGovernanceManager::%s -- peer already asked me for the list\n", __func__);
        Misbehaving(pnode->GetId(), 20);
        return;
    }
    netfulfilledman.AddFulfilledRequest(pnode->addr, NetMsgType::MNGOVERNANCESYNC);

    int nObjCount = 0;

    // SYNC GOVERNANCE OBJECTS WITH OTHER CLIENT

    LogPrint(BCLog::GOBJECT, "CGovernanceManager::%s -- syncing all objects to peer=%d\n", __func__, pnode->GetId());

    LOCK2(cs_main, cs);

    // all valid objects, no votes
    for (const auto& objPair : mapObjects) {
        uint256 nHash = objPair.first;
        const CGovernanceObject& govobj = objPair.second;
        std::string strHash = nHash.ToString();

        LogPrint(BCLog::GOBJECT, "CGovernanceManager::%s -- attempting to sync govobj: %s, peer=%d\n", __func__, strHash, pnode->GetId());

        if (govobj.IsSetCachedDelete() || govobj.IsSetExpired()) {
            LogPrintf("CGovernanceManager::%s -- not syncing deleted/expired govobj: %s, peer=%d\n", __func__,
                strHash, pnode->GetId());
            continue;
        }

        // Push the inventory budget proposal message over to the other client
        LogPrint(BCLog::GOBJECT, "CGovernanceManager::%s -- syncing govobj: %s, peer=%d\n", __func__, strHash, pnode->GetId());
        pnode->PushInventory(CInv(MSG_GOVERNANCE_OBJECT, nHash));
        ++nObjCount;
    }

    CNetMsgMaker msgMaker(pnode->GetSendVersion());
    connman.PushMessage(pnode, msgMaker.Make(NetMsgType::SYNCSTATUSCOUNT, SMARTNODE_SYNC_GOVOBJ, nObjCount));
    LogPrintf("CGovernanceManager::%s -- sent %d objects to peer=%d\n", __func__, nObjCount, pnode->GetId());
}

void CGovernanceManager::SmartnodeRateUpdate(const CGovernanceObject& govobj)
{
    if (govobj.GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) return;

    const COutPoint& smartnodeOutpoint = govobj.GetSmartnodeOutpoint();
    auto it = mapLastSmartnodeObject.find(smartnodeOutpoint);

    if (it == mapLastSmartnodeObject.end()) {
        it = mapLastSmartnodeObject.insert(txout_m_t::value_type(smartnodeOutpoint, last_object_rec(true))).first;
    }

    int64_t nTimestamp = govobj.GetCreationTime();
    it->second.triggerBuffer.AddTimestamp(nTimestamp);

    if (nTimestamp > GetTime() + MAX_TIME_FUTURE_DEVIATION - RELIABLE_PROPAGATION_TIME) {
        // schedule additional relay for the object
        setAdditionalRelayObjects.insert(govobj.GetHash());
    }

    it->second.fStatusOK = true;
}

bool CGovernanceManager::SmartnodeRateCheck(const CGovernanceObject& govobj, bool fUpdateFailStatus)
{
    bool fRateCheckBypassed;
    return SmartnodeRateCheck(govobj, fUpdateFailStatus, true, fRateCheckBypassed);
}

bool CGovernanceManager::SmartnodeRateCheck(const CGovernanceObject& govobj, bool fUpdateFailStatus, bool fForce, bool& fRateCheckBypassed)
{
    LOCK(cs);

    fRateCheckBypassed = false;

    if (!smartnodeSync.IsSynced() || !fRateChecksEnabled) {
        return true;
    }

    if (govobj.GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) {
        return true;
    }

    const COutPoint& smartnodeOutpoint = govobj.GetSmartnodeOutpoint();
    int64_t nTimestamp = govobj.GetCreationTime();
    int64_t nNow = GetAdjustedTime();
    int64_t nSuperblockCycleSeconds = Params().GetConsensus().nSuperblockCycle * Params().GetConsensus().nPowTargetSpacing;

    std::string strHash = govobj.GetHash().ToString();

    if (nTimestamp < nNow - 2 * nSuperblockCycleSeconds) {
        LogPrint(BCLog::GOBJECT, "CGovernanceManager::SmartnodeRateCheck -- object %s rejected due to too old timestamp, smartnode = %s, timestamp = %d, current time = %d\n",
            strHash, smartnodeOutpoint.ToStringShort(), nTimestamp, nNow);
        return false;
    }

    if (nTimestamp > nNow + MAX_TIME_FUTURE_DEVIATION) {
        LogPrint(BCLog::GOBJECT, "CGovernanceManager::SmartnodeRateCheck -- object %s rejected due to too new (future) timestamp, smartnode = %s, timestamp = %d, current time = %d\n",
            strHash, smartnodeOutpoint.ToStringShort(), nTimestamp, nNow);
        return false;
    }

    auto it = mapLastSmartnodeObject.find(smartnodeOutpoint);
    if (it == mapLastSmartnodeObject.end()) return true;

    if (it->second.fStatusOK && !fForce) {
        fRateCheckBypassed = true;
        return true;
    }

    // Allow 1 trigger per mn per cycle, with a small fudge factor
    double dMaxRate = 2 * 1.1 / double(nSuperblockCycleSeconds);

    // Temporary copy to check rate after new timestamp is added
    CRateCheckBuffer buffer = it->second.triggerBuffer;

    buffer.AddTimestamp(nTimestamp);
    double dRate = buffer.GetRate();

    if (dRate < dMaxRate) {
        return true;
    }

    LogPrint(BCLog::GOBJECT, "CGovernanceManager::SmartnodeRateCheck -- Rate too high: object hash = %s, smartnode = %s, object timestamp = %d, rate = %f, max rate = %f\n",
        strHash, smartnodeOutpoint.ToStringShort(), nTimestamp, dRate, dMaxRate);

    if (fUpdateFailStatus) {
        it->second.fStatusOK = false;
    }

    return false;
}

bool CGovernanceManager::ProcessVote(CNode* pfrom, const CGovernanceVote& vote, CGovernanceException& exception, CConnman& connman)
{
    ENTER_CRITICAL_SECTION(cs);
    uint256 nHashVote = vote.GetHash();
    uint256 nHashGovobj = vote.GetParentHash();

    if (cmapVoteToObject.HasKey(nHashVote)) {
        LogPrint(BCLog::GOBJECT, "CGovernanceObject::ProcessVote -- skipping known valid vote %s for object %s\n", nHashVote.ToString(), nHashGovobj.ToString());
        LEAVE_CRITICAL_SECTION(cs);
        return false;
    }

    if (cmapInvalidVotes.HasKey(nHashVote)) {
        std::ostringstream ostr;
        ostr << "CGovernanceManager::ProcessVote -- Old invalid vote "
             << ", MN outpoint = " << vote.GetSmartnodeOutpoint().ToStringShort()
             << ", governance object hash = " << nHashGovobj.ToString();
        LogPrint(BCLog::GOBJECT, "%s\n", ostr.str());
        exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_PERMANENT_ERROR, 20);
        LEAVE_CRITICAL_SECTION(cs);
        return false;
    }

    auto it = mapObjects.find(nHashGovobj);
    if (it == mapObjects.end()) {
        std::ostringstream ostr;
        ostr << "CGovernanceManager::ProcessVote -- Unknown parent object " << nHashGovobj.ToString()
             << ", MN outpoint = " << vote.GetSmartnodeOutpoint().ToStringShort();
        exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_WARNING);
        if (cmmapOrphanVotes.Insert(nHashGovobj, vote_time_pair_t(vote, GetAdjustedTime() + GOVERNANCE_ORPHAN_EXPIRATION_TIME))) {
            LEAVE_CRITICAL_SECTION(cs);
            RequestGovernanceObject(pfrom, nHashGovobj, connman);
            LogPrint(BCLog::GOBJECT, "%s\n", ostr.str());
            return false;
        }

        LogPrint(BCLog::GOBJECT, "%s\n", ostr.str());
        LEAVE_CRITICAL_SECTION(cs);
        return false;
    }

    CGovernanceObject& govobj = it->second;

    if (govobj.IsSetCachedDelete() || govobj.IsSetExpired()) {
        LogPrint(BCLog::GOBJECT, "CGovernanceObject::ProcessVote -- ignoring vote for expired or deleted object, hash = %s\n", nHashGovobj.ToString());
        LEAVE_CRITICAL_SECTION(cs);
        return false;
    }

    bool fOk = govobj.ProcessVote(pfrom, vote, exception, connman) && cmapVoteToObject.Insert(nHashVote, &govobj);
    LEAVE_CRITICAL_SECTION(cs);
    return fOk;
}

void CGovernanceManager::CheckPostponedObjects(CConnman& connman)
{
    if (!smartnodeSync.IsSynced()) return;

    LOCK2(cs_main, cs);

    // Check postponed proposals
    for (auto it = mapPostponedObjects.begin(); it != mapPostponedObjects.end();) {
        const uint256& nHash = it->first;
        CGovernanceObject& govobj = it->second;

        assert(govobj.GetObjectType() != GOVERNANCE_OBJECT_TRIGGER);

        std::string strError;
        bool fMissingConfirmations;
        if (govobj.IsCollateralValid(strError, fMissingConfirmations)) {
            if (govobj.IsValidLocally(strError, false)) {
                AddGovernanceObject(govobj, connman);
            } else {
                LogPrint(BCLog::GOBJECT, "CGovernanceManager::CheckPostponedObjects -- %s invalid\n", nHash.ToString());
            }

        } else if (fMissingConfirmations) {
            // wait for more confirmations
            ++it;
            continue;
        }

        // remove processed or invalid object from the queue
        mapPostponedObjects.erase(it++);
    }


    // Perform additional relays for triggers
    int64_t nNow = GetAdjustedTime();
    int64_t nSuperblockCycleSeconds = Params().GetConsensus().nSuperblockCycle * Params().GetConsensus().nPowTargetSpacing;

    for (auto it = setAdditionalRelayObjects.begin(); it != setAdditionalRelayObjects.end();) {
        auto itObject = mapObjects.find(*it);
        if (itObject != mapObjects.end()) {
            CGovernanceObject& govobj = itObject->second;

            int64_t nTimestamp = govobj.GetCreationTime();

            bool fValid = (nTimestamp <= nNow + MAX_TIME_FUTURE_DEVIATION) && (nTimestamp >= nNow - 2 * nSuperblockCycleSeconds);
            bool fReady = (nTimestamp <= nNow + MAX_TIME_FUTURE_DEVIATION - RELIABLE_PROPAGATION_TIME);

            if (fValid) {
                if (fReady) {
                    LogPrintf("CGovernanceManager::CheckPostponedObjects -- additional relay: hash = %s\n", govobj.GetHash().ToString());
                    govobj.Relay(connman);
                } else {
                    it++;
                    continue;
                }
            }

        } else {
            LogPrintf("CGovernanceManager::CheckPostponedObjects -- additional relay of unknown object: %s\n", it->ToString());
        }

        setAdditionalRelayObjects.erase(it++);
    }
}

void CGovernanceManager::RequestGovernanceObject(CNode* pfrom, const uint256& nHash, CConnman& connman, bool fUseFilter)
{
    if (!pfrom) {
        return;
    }

    LogPrint(BCLog::GOBJECT, "CGovernanceManager::RequestGovernanceObject -- nHash %s peer=%d\n", nHash.ToString(), pfrom->GetId());

    CNetMsgMaker msgMaker(pfrom->GetSendVersion());

    CBloomFilter filter;
    filter.clear();

    int nVoteCount = 0;
    if (fUseFilter) {
        LOCK(cs);
        CGovernanceObject* pObj = FindGovernanceObject(nHash);

        if (pObj) {
            filter = CBloomFilter(Params().GetConsensus().nGovernanceFilterElements, GOVERNANCE_FILTER_FP_RATE, GetRandInt(999999), BLOOM_UPDATE_ALL);
            std::vector<CGovernanceVote> vecVotes = pObj->GetVoteFile().GetVotes();
            nVoteCount = vecVotes.size();
            for (const auto& vote : vecVotes) {
                filter.insert(vote.GetHash());
            }
        }
    }

    LogPrint(BCLog::GOBJECT, "CGovernanceManager::RequestGovernanceObject -- nHash %s nVoteCount %d peer=%d\n", nHash.ToString(), nVoteCount, pfrom->GetId());
    connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::MNGOVERNANCESYNC, nHash, filter));
}

int CGovernanceManager::RequestGovernanceObjectVotes(CNode* pnode, CConnman& connman)
{
    if (pnode->nVersion < MIN_GOVERNANCE_PEER_PROTO_VERSION) return -3;
    std::vector<CNode*> vNodesCopy;
    vNodesCopy.push_back(pnode);
    return RequestGovernanceObjectVotes(vNodesCopy, connman);
}

int CGovernanceManager::RequestGovernanceObjectVotes(const std::vector<CNode*>& vNodesCopy, CConnman& connman)
{
    static std::map<uint256, std::map<CService, int64_t> > mapAskedRecently;

    if (vNodesCopy.empty()) return -1;

    int64_t nNow = GetTime();
    int nTimeout = 60 * 60;
    size_t nPeersPerHashMax = 3;

    std::vector<uint256> vTriggerObjHashes;
    std::vector<uint256> vOtherObjHashes;

    // This should help us to get some idea about an impact this can bring once deployed on mainnet.
    // Testnet is ~40 times smaller in smartnode count, but only ~1000 smartnodes usually vote,
    // so 1 obj on mainnet == ~10 objs or ~1000 votes on testnet. However we want to test a higher
    // number of votes to make sure it's robust enough, so aim at 2000 votes per smartnode per request.
    // On mainnet nMaxObjRequestsPerNode is always set to 1.
    int nMaxObjRequestsPerNode = 1;
    size_t nProjectedVotes = 2000;
    if (Params().NetworkIDString() != CBaseChainParams::MAIN) {
        nMaxObjRequestsPerNode = std::max(1, int(nProjectedVotes / std::max(1, (int)deterministicMNManager->GetListAtChainTip().GetValidMNsCount())));
    }

    {
        LOCK2(cs_main, cs);

        if (mapObjects.empty()) return -2;

        for (const auto& objPair : mapObjects) {
            uint256 nHash = objPair.first;
            if (mapAskedRecently.count(nHash)) {
                auto it = mapAskedRecently[nHash].begin();
                while (it != mapAskedRecently[nHash].end()) {
                    if (it->second < nNow) {
                        mapAskedRecently[nHash].erase(it++);
                    } else {
                        ++it;
                    }
                }
                if (mapAskedRecently[nHash].size() >= nPeersPerHashMax) continue;
            }

            if (objPair.second.GetObjectType() == GOVERNANCE_OBJECT_TRIGGER) {
                vTriggerObjHashes.push_back(nHash);
            } else {
                vOtherObjHashes.push_back(nHash);
            }
        }
    }

    LogPrint(BCLog::GOBJECT, "CGovernanceManager::RequestGovernanceObjectVotes -- start: vTriggerObjHashes %d vOtherObjHashes %d mapAskedRecently %d\n",
        vTriggerObjHashes.size(), vOtherObjHashes.size(), mapAskedRecently.size());

    Shuffle(vTriggerObjHashes.begin(), vTriggerObjHashes.end(), FastRandomContext());
    Shuffle(vOtherObjHashes.begin(), vOtherObjHashes.end(), FastRandomContext());

    for (int i = 0; i < nMaxObjRequestsPerNode; ++i) {
        uint256 nHashGovobj;

        // ask for triggers first
        if (vTriggerObjHashes.size()) {
            nHashGovobj = vTriggerObjHashes.back();
        } else {
            if (vOtherObjHashes.empty()) break;
            nHashGovobj = vOtherObjHashes.back();
        }
        bool fAsked = false;
        for (const auto& pnode : vNodesCopy) {
            // Don't try to sync any data from outbound non-relay "masternode" connections.
            // Inbound connection this early is most likely a "masternode" connection
            // initiated from another node, so skip it too.
            if (!pnode->CanRelay() || (fSmartnodeMode && pnode->fInbound)) continue;
            // only use up to date peers
            if (pnode->nVersion < MIN_GOVERNANCE_PEER_PROTO_VERSION) continue;
            // stop early to prevent setAskFor overflow
            {
                LOCK(cs_main);
                size_t nProjectedSize = GetRequestedObjectCount(pnode->GetId()) + nProjectedVotes;
                if (nProjectedSize > MAX_INV_SZ) continue;
                // to early to ask the same node
                if (mapAskedRecently[nHashGovobj].count(pnode->addr)) continue;
            }

            RequestGovernanceObject(pnode, nHashGovobj, connman, true);
            mapAskedRecently[nHashGovobj][pnode->addr] = nNow + nTimeout;
            fAsked = true;
            // stop loop if max number of peers per obj was asked
            if (mapAskedRecently[nHashGovobj].size() >= nPeersPerHashMax) break;
        }
        // NOTE: this should match `if` above (the one before `while`)
        if (vTriggerObjHashes.size()) {
            vTriggerObjHashes.pop_back();
        } else {
            vOtherObjHashes.pop_back();
        }
        if (!fAsked) i--;
    }
    LogPrint(BCLog::GOBJECT, "CGovernanceManager::RequestGovernanceObjectVotes -- end: vTriggerObjHashes %d vOtherObjHashes %d mapAskedRecently %d\n",
        vTriggerObjHashes.size(), vOtherObjHashes.size(), mapAskedRecently.size());

    return int(vTriggerObjHashes.size() + vOtherObjHashes.size());
}

bool CGovernanceManager::AcceptObjectMessage(const uint256& nHash)
{
    LOCK(cs);
    return AcceptMessage(nHash, setRequestedObjects);
}

bool CGovernanceManager::AcceptVoteMessage(const uint256& nHash)
{
    LOCK(cs);
    return AcceptMessage(nHash, setRequestedVotes);
}

bool CGovernanceManager::AcceptMessage(const uint256& nHash, hash_s_t& setHash)
{
    auto it = setHash.find(nHash);
    if (it == setHash.end()) {
        // We never requested this
        return false;
    }
    // Only accept one response
    setHash.erase(it);
    return true;
}

void CGovernanceManager::RebuildIndexes()
{
    LOCK(cs);

    cmapVoteToObject.Clear();
    for (auto& objPair : mapObjects) {
        CGovernanceObject& govobj = objPair.second;
        std::vector<CGovernanceVote> vecVotes = govobj.GetVoteFile().GetVotes();
        for (size_t i = 0; i < vecVotes.size(); ++i) {
            cmapVoteToObject.Insert(vecVotes[i].GetHash(), &govobj);
        }
    }
}

void CGovernanceManager::AddCachedTriggers()
{
    LOCK(cs);

    for (auto& objpair : mapObjects) {
        CGovernanceObject& govobj = objpair.second;

        if (govobj.GetObjectType() != GOVERNANCE_OBJECT_TRIGGER) {
            continue;
        }

        if (!triggerman.AddNewTrigger(govobj.GetHash())) {
            govobj.PrepareDeletion(GetAdjustedTime());
        }
    }
}

void CGovernanceManager::InitOnLoad()
{
    LOCK(cs);
    int64_t nStart = GetTimeMillis();
    LogPrintf("Preparing smartnode indexes and governance triggers...\n");
    RebuildIndexes();
    AddCachedTriggers();
    LogPrintf("Smartnode indexes and governance triggers prepared  %dms\n", GetTimeMillis() - nStart);
    LogPrintf("     %s\n", ToString());
}

std::string CGovernanceManager::ToString() const
{
    LOCK(cs);

    int nProposalCount = 0;
    int nTriggerCount = 0;
    int nOtherCount = 0;

    for (const auto& objPair : mapObjects) {
        switch (objPair.second.GetObjectType()) {
        case GOVERNANCE_OBJECT_PROPOSAL:
            nProposalCount++;
            break;
        case GOVERNANCE_OBJECT_TRIGGER:
            nTriggerCount++;
            break;
        default:
            nOtherCount++;
            break;
        }
    }

    return strprintf("Governance Objects: %d (Proposals: %d, Triggers: %d, Other: %d; Erased: %d), Votes: %d",
        (int)mapObjects.size(),
        nProposalCount, nTriggerCount, nOtherCount, (int)mapErasedGovernanceObjects.size(),
        (int)cmapVoteToObject.GetSize());
}

UniValue CGovernanceManager::ToJson() const
{
    LOCK(cs);

    int nProposalCount = 0;
    int nTriggerCount = 0;
    int nOtherCount = 0;

    for (const auto& objpair : mapObjects) {
        switch (objpair.second.GetObjectType()) {
        case GOVERNANCE_OBJECT_PROPOSAL:
            nProposalCount++;
            break;
        case GOVERNANCE_OBJECT_TRIGGER:
            nTriggerCount++;
            break;
        default:
            nOtherCount++;
            break;
        }
    }

    UniValue jsonObj(UniValue::VOBJ);
    jsonObj.pushKV("objects_total", (int)mapObjects.size());
    jsonObj.pushKV("proposals", nProposalCount);
    jsonObj.pushKV("triggers", nTriggerCount);
    jsonObj.pushKV("other", nOtherCount);
    jsonObj.pushKV("erased", (int)mapErasedGovernanceObjects.size());
    jsonObj.pushKV("votes", (int)cmapVoteToObject.GetSize());
    return jsonObj;
}

void CGovernanceManager::UpdatedBlockTip(const CBlockIndex* pindex, CConnman& connman)
{
    // Note this gets called from ActivateBestChain without cs_main being held
    // so it should be safe to lock our mutex here without risking a deadlock
    // On the other hand it should be safe for us to access pindex without holding a lock
    // on cs_main because the CBlockIndex objects are dynamically allocated and
    // presumably never deleted.
    if (!pindex) {
        return;
    }

    nCachedBlockHeight = pindex->nHeight;
    LogPrint(BCLog::GOBJECT, "CGovernanceManager::UpdatedBlockTip -- nCachedBlockHeight: %d\n", nCachedBlockHeight);

    if (deterministicMNManager->IsDIP3Enforced(pindex->nHeight)) {
        RemoveInvalidVotes();
    }

    CheckPostponedObjects(connman);

    CSuperblockManager::ExecuteBestSuperblock(pindex->nHeight);
}

void CGovernanceManager::RequestOrphanObjects(CConnman& connman)
{
    std::vector<CNode*> vNodesCopy = connman.CopyNodeVector(CConnman::FullyConnectedOnly);

    std::vector<uint256> vecHashesFiltered;
    {
        std::vector<uint256> vecHashes;
        LOCK(cs);
        cmmapOrphanVotes.GetKeys(vecHashes);
        for (const uint256& nHash : vecHashes) {
            if (mapObjects.find(nHash) == mapObjects.end()) {
                vecHashesFiltered.push_back(nHash);
            }
        }
    }

    LogPrint(BCLog::GOBJECT, "CGovernanceObject::RequestOrphanObjects -- number objects = %d\n", vecHashesFiltered.size());
    for (const uint256& nHash : vecHashesFiltered) {
        for (CNode* pnode : vNodesCopy) {
            if (!pnode->CanRelay()) {
                continue;
            }
            RequestGovernanceObject(pnode, nHash, connman);
        }
    }

    connman.ReleaseNodeVector(vNodesCopy);
}

void CGovernanceManager::CleanOrphanObjects()
{
    LOCK(cs);
    const vote_cmm_t::list_t& items = cmmapOrphanVotes.GetItemList();

    int64_t nNow = GetAdjustedTime();

    vote_cmm_t::list_cit it = items.begin();
    while (it != items.end()) {
        vote_cmm_t::list_cit prevIt = it;
        ++it;
        const vote_time_pair_t& pairVote = prevIt->value;
        if (pairVote.second < nNow) {
            cmmapOrphanVotes.Erase(prevIt->key, prevIt->value);
        }
    }
}

void CGovernanceManager::RemoveInvalidVotes()
{
    if (!smartnodeSync.IsSynced()) {
        return;
    }

    LOCK(cs);

    auto curMNList = deterministicMNManager->GetListAtChainTip();
    auto diff = lastMNListForVotingKeys.BuildDiff(curMNList);

    std::vector<COutPoint> changedKeyMNs;
    for (const auto& p : diff.updatedMNs) {
        auto oldDmn = lastMNListForVotingKeys.GetMNByInternalId(p.first);
        if ((p.second.fields & CDeterministicMNStateDiff::Field_keyIDVoting) && p.second.state.keyIDVoting != oldDmn->pdmnState->keyIDVoting) {
            changedKeyMNs.emplace_back(oldDmn->collateralOutpoint);
        } else if ((p.second.fields & CDeterministicMNStateDiff::Field_pubKeyOperator) && p.second.state.pubKeyOperator != oldDmn->pdmnState->pubKeyOperator) {
            changedKeyMNs.emplace_back(oldDmn->collateralOutpoint);
        }
    }
    for (const auto& id : diff.removedMns) {
        auto oldDmn = lastMNListForVotingKeys.GetMNByInternalId(id);
        changedKeyMNs.emplace_back(oldDmn->collateralOutpoint);
    }

    for (const auto& outpoint : changedKeyMNs) {
        for (auto& p : mapObjects) {
            auto removed = p.second.RemoveInvalidVotes(outpoint);
            if (removed.empty()) {
                continue;
            }
            for (auto& voteHash : removed) {
                cmapVoteToObject.Erase(voteHash);
                cmapInvalidVotes.Erase(voteHash);
                cmmapOrphanVotes.Erase(voteHash);
                setRequestedVotes.erase(voteHash);
            }
        }
    }

    // store current MN list for the next run so that we can determine which keys changed
    lastMNListForVotingKeys = curMNList;
}

bool AreSuperblocksEnabled()
{
    return sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED);
}
