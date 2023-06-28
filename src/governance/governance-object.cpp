// Copyright (c) 2014-2020 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <governance/governance-object.h>

#include <chainparams.h>
#include <core_io.h>
#include <governance/governance.h>
#include <governance/governance-validators.h>
#include <smartnode/smartnode-meta.h>
#include <smartnode/smartnode-sync.h>
#include <evo/deterministicmns.h>
#include <messagesigner.h>
#include <spork.h>
#include <validation.h>
#include <validationinterface.h>

#include <string>

CGovernanceObject::CGovernanceObject() :
        cs(),
        nObjectType(GOVERNANCE_OBJECT_UNKNOWN),
        nHashParent(),
        nRevision(0),
        nTime(0),
        nDeletionTime(0),
        nCollateralHash(),
        vchData(),
        smartnodeOutpoint(),
        vchSig(),
        fCachedLocalValidity(false),
        strLocalValidityError(),
        fCachedFunding(false),
        fCachedValid(true),
        fCachedDelete(false),
        fCachedEndorsed(false),
        fDirtyCache(true),
        fExpired(false),
        fUnparsable(false),
        mapCurrentMNVotes(),
        fileVotes() {
    // PARSE JSON DATA STORAGE (VCHDATA)
    LoadData();
}

CGovernanceObject::CGovernanceObject(const uint256 &nHashParentIn, int nRevisionIn, int64_t nTimeIn,
                                     const uint256 &nCollateralHashIn, const std::string &strDataHexIn) :
        cs(),
        nObjectType(GOVERNANCE_OBJECT_UNKNOWN),
        nHashParent(nHashParentIn),
        nRevision(nRevisionIn),
        nTime(nTimeIn),
        nDeletionTime(0),
        nCollateralHash(nCollateralHashIn),
        vchData(ParseHex(strDataHexIn)),
        smartnodeOutpoint(),
        vchSig(),
        fCachedLocalValidity(false),
        strLocalValidityError(),
        fCachedFunding(false),
        fCachedValid(true),
        fCachedDelete(false),
        fCachedEndorsed(false),
        fDirtyCache(true),
        fExpired(false),
        fUnparsable(false),
        mapCurrentMNVotes(),
        fileVotes() {
    // PARSE JSON DATA STORAGE (VCHDATA)
    LoadData();
}

CGovernanceObject::CGovernanceObject(const CGovernanceObject &other) :
        cs(),
        nObjectType(other.nObjectType),
        nHashParent(other.nHashParent),
        nRevision(other.nRevision),
        nTime(other.nTime),
        nDeletionTime(other.nDeletionTime),
        nCollateralHash(other.nCollateralHash),
        vchData(other.vchData),
        smartnodeOutpoint(other.smartnodeOutpoint),
        vchSig(other.vchSig),
        fCachedLocalValidity(other.fCachedLocalValidity),
        strLocalValidityError(other.strLocalValidityError),
        fCachedFunding(other.fCachedFunding),
        fCachedValid(other.fCachedValid),
        fCachedDelete(other.fCachedDelete),
        fCachedEndorsed(other.fCachedEndorsed),
        fDirtyCache(other.fDirtyCache),
        fExpired(other.fExpired),
        fUnparsable(other.fUnparsable),
        mapCurrentMNVotes(other.mapCurrentMNVotes),
        fileVotes(other.fileVotes) {
}

bool CGovernanceObject::ProcessVote(CNode *pfrom,
                                    const CGovernanceVote &vote,
                                    CGovernanceException &exception,
                                    CConnman &connman) {
    LOCK(cs);

    // do not process already known valid votes twice
    if (fileVotes.HasVote(vote.GetHash())) {
        // nothing to do here, not an error
        std::ostringstream ostr;
        ostr << "CGovernanceObject::ProcessVote -- Already known valid vote";
        LogPrint(BCLog::GOBJECT, "%s\n", ostr.str());
        exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_NONE);
        return false;
    }

    auto mnList = deterministicMNManager->GetListAtChainTip();
    auto dmn = mnList.GetMNByCollateral(vote.GetSmartnodeOutpoint());

    if (!dmn) {
        std::ostringstream ostr;
        ostr << "CGovernanceObject::ProcessVote -- Smartnode " << vote.GetSmartnodeOutpoint().ToStringShort()
             << " not found";
        exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_PERMANENT_ERROR, 20);
        return false;
    }

    auto it = mapCurrentMNVotes.emplace(vote_m_t::value_type(vote.GetSmartnodeOutpoint(), vote_rec_t())).first;
    vote_rec_t &voteRecordRef = it->second;
    vote_signal_enum_t eSignal = vote.GetSignal();
    if (eSignal == VOTE_SIGNAL_NONE) {
        std::ostringstream ostr;
        ostr << "CGovernanceObject::ProcessVote -- Vote signal: none";
        LogPrint(BCLog::GOBJECT, "%s\n", ostr.str());
        exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_WARNING);
        return false;
    }
    if (eSignal > MAX_SUPPORTED_VOTE_SIGNAL) {
        std::ostringstream ostr;
        ostr << "CGovernanceObject::ProcessVote -- Unsupported vote signal: "
             << CGovernanceVoting::ConvertSignalToString(vote.GetSignal());
        LogPrintf("%s\n", ostr.str());
        exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_PERMANENT_ERROR, 20);
        return false;
    }
    auto it2 = voteRecordRef.mapInstances.emplace(vote_instance_m_t::value_type(int(eSignal), vote_instance_t())).first;
    vote_instance_t &voteInstanceRef = it2->second;

    // Reject obsolete votes
    if (vote.GetTimestamp() < voteInstanceRef.nCreationTime) {
        std::ostringstream ostr;
        ostr << "CGovernanceObject::ProcessVote -- Obsolete vote";
        LogPrint(BCLog::GOBJECT, "%s\n", ostr.str());
        exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_NONE);
        return false;
    } else if (vote.GetTimestamp() == voteInstanceRef.nCreationTime) {
        // Someone is doing something fishy, there can be no two votes from the same smartnode
        // with the same timestamp for the same object and signal and yet different hash/outcome.
        std::ostringstream ostr;
        ostr << "CGovernanceObject::ProcessVote -- Invalid vote, same timestamp for the different outcome";
        if (vote.GetOutcome() < voteInstanceRef.eOutcome) {
            // This is an arbitrary comparison, we have to agree on some way
            // to pick the "winning" vote.
            ostr << ", rejected";
            LogPrint(BCLog::GOBJECT, "%s\n", ostr.str());
            exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_NONE);
            return false;
        }
        ostr << ", accepted";
        LogPrint(BCLog::GOBJECT, "%s\n", ostr.str());
    }

    int64_t nNow = GetAdjustedTime();
    int64_t nVoteTimeUpdate = voteInstanceRef.nTime;
    if (governance.AreRateChecksEnabled()) {
        int64_t nTimeDelta = nNow - voteInstanceRef.nTime;
        if (nTimeDelta < GOVERNANCE_UPDATE_MIN) {
            std::ostringstream ostr;
            ostr << "CGovernanceObject::ProcessVote -- Smartnode voting too often"
                 << ", MN outpoint = " << vote.GetSmartnodeOutpoint().ToStringShort()
                 << ", governance object hash = " << GetHash().ToString()
                 << ", time delta = " << nTimeDelta;
            LogPrint(BCLog::GOBJECT, "%s\n", ostr.str());
            exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_TEMPORARY_ERROR);
            return false;
        }
        nVoteTimeUpdate = nNow;
    }

    bool onlyVotingKeyAllowed = nObjectType == GOVERNANCE_OBJECT_PROPOSAL && vote.GetSignal() == VOTE_SIGNAL_FUNDING;

    // Finally check that the vote is actually valid (done last because of cost of signature verification)
    if (!vote.IsValid(onlyVotingKeyAllowed)) {
        std::ostringstream ostr;
        ostr << "CGovernanceObject::ProcessVote -- Invalid vote"
             << ", MN outpoint = " << vote.GetSmartnodeOutpoint().ToStringShort()
             << ", governance object hash = " << GetHash().ToString()
             << ", vote hash = " << vote.GetHash().ToString();
        LogPrintf("%s\n", ostr.str());
        exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_PERMANENT_ERROR, 20);
        governance.AddInvalidVote(vote);
        return false;
    }

    if (!mmetaman.AddGovernanceVote(dmn->proTxHash, vote.GetParentHash())) {
        std::ostringstream ostr;
        ostr << "CGovernanceObject::ProcessVote -- Unable to add governance vote"
             << ", MN outpoint = " << vote.GetSmartnodeOutpoint().ToStringShort()
             << ", governance object hash = " << GetHash().ToString();
        LogPrint(BCLog::GOBJECT, "%s\n", ostr.str());
        exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_PERMANENT_ERROR);
        return false;
    }

    voteInstanceRef = vote_instance_t(vote.GetOutcome(), nVoteTimeUpdate, vote.GetTimestamp());
    fileVotes.AddVote(vote);
    fDirtyCache = true;
    // SEND NOTIFICATION TO SCRIPT/ZMQ
    GetMainSignals().NotifyGovernanceVote(std::make_shared<const CGovernanceVote>(vote));
    return true;
}

void CGovernanceObject::ClearSmartnodeVotes() {
    LOCK(cs);

    auto mnList = deterministicMNManager->GetListAtChainTip();

    auto it = mapCurrentMNVotes.begin();
    while (it != mapCurrentMNVotes.end()) {
        if (!mnList.HasMNByCollateral(it->first)) {
            fileVotes.RemoveVotesFromSmartnode(it->first);
            mapCurrentMNVotes.erase(it++);
            fDirtyCache = true;
        } else {
            ++it;
        }
    }
}

std::set <uint256> CGovernanceObject::RemoveInvalidVotes(const COutPoint &mnOutpoint) {
    LOCK(cs);

    auto it = mapCurrentMNVotes.find(mnOutpoint);
    if (it == mapCurrentMNVotes.end()) {
        // don't even try as we don't have any votes from this MN
        return {};
    }

    auto removedVotes = fileVotes.RemoveInvalidVotes(mnOutpoint, nObjectType == GOVERNANCE_OBJECT_PROPOSAL);
    if (removedVotes.empty()) {
        return {};
    }

    auto nParentHash = GetHash();
    for (auto jt = it->second.mapInstances.begin(); jt != it->second.mapInstances.end();) {
        CGovernanceVote tmpVote(mnOutpoint, nParentHash, (vote_signal_enum_t) jt->first, jt->second.eOutcome);
        tmpVote.SetTime(jt->second.nCreationTime);
        if (removedVotes.count(tmpVote.GetHash())) {
            jt = it->second.mapInstances.erase(jt);
        } else {
            ++jt;
        }
    }
    if (it->second.mapInstances.empty()) {
        mapCurrentMNVotes.erase(it);
    }

    std::string removedStr;
    for (auto &h: removedVotes) {
        removedStr += strprintf("  %s\n", h.ToString());
    }
    LogPrintf("CGovernanceObject::%s -- Removed %d invalid votes for %s from MN %s:\n%s", __func__, removedVotes.size(),
              nParentHash.ToString(), mnOutpoint.ToString(), removedStr); /* Continued */
    fDirtyCache = true;

    return removedVotes;
}

uint256 CGovernanceObject::GetHash() const {
    // Note: doesn't match serialization

    // CREATE HASH OF ALL IMPORTANT PIECES OF DATA

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << nHashParent;
    ss << nRevision;
    ss << nTime;
    ss << GetDataAsHexString();
    ss << smartnodeOutpoint << uint8_t{} << 0xffffffff; // adding dummy values here to match old hashing
    ss << vchSig;
    // fee_tx is left out on purpose

    return ss.GetHash();
}

uint256 CGovernanceObject::GetSignatureHash() const {
    return SerializeHash(*this);
}

void CGovernanceObject::SetSmartnodeOutpoint(const COutPoint &outpoint) {
    smartnodeOutpoint = outpoint;
}

bool CGovernanceObject::Sign(const CBLSSecretKey &key) {
    CBLSSignature sig = key.Sign(GetSignatureHash());
    if (!sig.IsValid()) {
        return false;
    }
    vchSig = sig.ToByteVector();
    return true;
}

bool CGovernanceObject::CheckSignature(const CBLSPublicKey &pubKey) const {
    if (!CBLSSignature(vchSig).VerifyInsecure(pubKey, GetSignatureHash())) {
        LogPrintf("CGovernanceObject::CheckSignature -- VerifyInsecure() failed\n");
        return false;
    }
    return true;
}

/**
   Return the actual object from the vchData JSON structure.

   Returns an empty object on error.
 */
UniValue CGovernanceObject::GetJSONObject() {
    UniValue obj(UniValue::VOBJ);
    if (vchData.empty()) {
        return obj;
    }

    UniValue objResult(UniValue::VOBJ);
    GetData(objResult);

    if (objResult.isObject()) {
        obj = objResult;
    } else {
        std::vector <UniValue> arr1 = objResult.getValues();
        std::vector <UniValue> arr2 = arr1.at(0).getValues();
        obj = arr2.at(1);
    }

    return obj;
}

/**
*   LoadData
*   --------------------------------------------------------
*
*   Attempt to load data from vchData
*
*/

void CGovernanceObject::LoadData() {
    if (vchData.empty()) {
        return;
    }

    try {
        // ATTEMPT TO LOAD JSON STRING FROM VCHDATA
        UniValue objResult(UniValue::VOBJ);
        GetData(objResult);
        LogPrint(BCLog::GOBJECT, "CGovernanceObject::LoadData -- GetDataAsPlainString = %s\n", GetDataAsPlainString());
        UniValue obj = GetJSONObject();
        nObjectType = obj["type"].get_int();
    } catch (std::exception &e) {
        fUnparsable = true;
        std::ostringstream ostr;
        ostr << "CGovernanceObject::LoadData Error parsing JSON"
             << ", e.what() = " << e.what();
        LogPrintf("%s\n", ostr.str());
        return;
    } catch (...) {
        fUnparsable = true;
        std::ostringstream ostr;
        ostr << "CGovernanceObject::LoadData Unknown Error parsing JSON";
        LogPrintf("%s\n", ostr.str());
        return;
    }
}

/**
*   GetData - Example usage:
*   --------------------------------------------------------
*
*   Decode governance object data into UniValue(VOBJ)
*
*/

void CGovernanceObject::GetData(UniValue &objResult) {
    UniValue o(UniValue::VOBJ);
    std::string s = GetDataAsPlainString();
    o.read(s);
    objResult = o;
}

/**
*   GetData - As
*   --------------------------------------------------------
*
*/

std::string CGovernanceObject::GetDataAsHexString() const {
    return HexStr(vchData);
}

std::string CGovernanceObject::GetDataAsPlainString() const {
    return std::string(vchData.begin(), vchData.end());
}

UniValue CGovernanceObject::ToJson() const {
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("objectHash", GetHash().ToString());
    obj.pushKV("parentHash", nHashParent.ToString());
    obj.pushKV("collateralHash", GetCollateralHash().ToString());
    obj.pushKV("createdAt", GetCreationTime());
    obj.pushKV("revision", nRevision);
    UniValue data;
    if (!data.read(GetDataAsPlainString())) {
        data.clear();
        data.setObject();
        data.pushKV("plain", GetDataAsPlainString());
        data.pushKV("hex", GetDataAsHexString());
    } else {
        data.pushKV("hex", GetDataAsHexString());
    }
    obj.pushKV("data", data);
    return obj;
}

void CGovernanceObject::UpdateLocalValidity() {
    AssertLockHeld(cs_main);
    // THIS DOES NOT CHECK COLLATERAL, THIS IS CHECKED UPON ORIGINAL ARRIVAL
    fCachedLocalValidity = IsValidLocally(strLocalValidityError, false);
}


bool CGovernanceObject::IsValidLocally(std::string &strError, bool fCheckCollateral) const {
    bool fMissingConfirmations = false;

    return IsValidLocally(strError, fMissingConfirmations, fCheckCollateral);
}

bool
CGovernanceObject::IsValidLocally(std::string &strError, bool &fMissingConfirmations, bool fCheckCollateral) const {
    fMissingConfirmations = false;

    if (fUnparsable) {
        strError = "Object data unparseable";
        return false;
    }

    switch (nObjectType) {
        case GOVERNANCE_OBJECT_PROPOSAL: {
            CProposalValidator validator(GetDataAsHexString(), true);
            // Note: It's ok to have expired proposals
            // they are going to be cleared by CGovernanceManager::UpdateCachesAndClean()
            // TODO: should they be tagged as "expired" to skip vote downloading?
            if (!validator.Validate(false)) {
                strError = strprintf("Invalid proposal data, error messages: %s", validator.GetErrorMessages());
                return false;
            }
            if (fCheckCollateral && !IsCollateralValid(strError, fMissingConfirmations)) {
                strError = "Invalid proposal collateral";
                return false;
            }
            return true;
        }
        case GOVERNANCE_OBJECT_TRIGGER: {
            if (!fCheckCollateral) {
                // nothing else we can check here (yet?)
                return true;
            }

            auto mnList = deterministicMNManager->GetListAtChainTip();

            std::string strOutpoint = smartnodeOutpoint.ToStringShort();
            auto dmn = mnList.GetMNByCollateral(smartnodeOutpoint);
            if (!dmn) {
                strError = "Failed to find Smartnode by UTXO, missing smartnode=" + strOutpoint;
                return false;
            }

            // Check that we have a valid MN signature
            if (!CheckSignature(dmn->pdmnState->pubKeyOperator.Get())) {
                strError = "Invalid smartnode signature for: " + strOutpoint + ", pubkey = " +
                           dmn->pdmnState->pubKeyOperator.Get().ToString();
                return false;
            }

            return true;
        }
        default: {
            strError = strprintf("Invalid object type %d", nObjectType);
            return false;
        }
    }
}

CAmount CGovernanceObject::GetMinCollateralFee() const {
    // Only 1 type has a fee for the moment but switch statement allows for future object types
    switch (nObjectType) {
        case GOVERNANCE_OBJECT_PROPOSAL:
            return GOVERNANCE_PROPOSAL_FEE_TX;
        case GOVERNANCE_OBJECT_TRIGGER:
            return 0;
        default:
            return MAX_MONEY;
    }
}

bool CGovernanceObject::IsCollateralValid(std::string &strError, bool &fMissingConfirmations) const {
    AssertLockHeld(cs_main);
    AssertLockHeld(::mempool.cs);

    strError = "";
    fMissingConfirmations = false;
    CAmount nMinFee = GetMinCollateralFee();
    uint256 nExpectedHash = GetHash();

    // RETRIEVE TRANSACTION IN QUESTION
    uint256 nBlockHash;
    CTransactionRef txCollateral = GetTransaction(/* block_index */ nullptr, /* mempool */ nullptr, nCollateralHash,
                                                                    Params().GetConsensus(), nBlockHash);
    if (!txCollateral) {
        strError = strprintf("Can't find collateral tx %s", nCollateralHash.ToString());
        LogPrintf("CGovernanceObject::IsCollateralValid -- %s\n", strError);
        return false;
    }

    if (nBlockHash == uint256()) {
        strError = strprintf("Collateral tx %s is not mined yet", txCollateral->ToString());
        LogPrintf("CGovernanceObject::IsCollateralValid -- %s\n", strError);
        return false;
    }

    if (txCollateral->vout.size() < 1) {
        strError = strprintf("tx vout size less than 1 | %d", txCollateral->vout.size());
        LogPrintf("CGovernanceObject::IsCollateralValid -- %s\n", strError);
        return false;
    }

    // LOOK FOR SPECIALIZED GOVERNANCE SCRIPT (PROOF OF BURN)

    CScript findScript;
    findScript << OP_RETURN << ToByteVector(nExpectedHash);

    LogPrint(BCLog::GOBJECT,
             "CGovernanceObject::IsCollateralValid -- txCollateral->vout.size() = %s, findScript = %s, nMinFee = %lld\n",
             txCollateral->vout.size(), ScriptToAsmStr(findScript, false), nMinFee);

    bool foundOpReturn = false;
    for (const auto &output: txCollateral->vout) {
        LogPrint(BCLog::GOBJECT,
                 "CGovernanceObject::IsCollateralValid -- txout = %s, output.nValue = %lld, output.scriptPubKey = %s\n",
                 output.ToString(), output.nValue, ScriptToAsmStr(output.scriptPubKey, false));
        if (!output.scriptPubKey.IsPayToPublicKeyHash() && !output.scriptPubKey.IsUnspendable()) {
            strError = strprintf("Invalid Script %s", txCollateral->ToString());
            LogPrintf("CGovernanceObject::IsCollateralValid -- %s\n", strError);
            return false;
        }
        if (output.scriptPubKey == findScript && output.nValue >= nMinFee) {
            foundOpReturn = true;
        }
    }

    if (!foundOpReturn) {
        strError = strprintf("Couldn't find opReturn %s in %s", nExpectedHash.ToString(), txCollateral->ToString());
        LogPrintf("CGovernanceObject::IsCollateralValid -- %s\n", strError);
        return false;
    }

    // GET CONFIRMATIONS FOR TRANSACTION

    AssertLockHeld(cs_main);
    int nConfirmationsIn = 0;
    if (nBlockHash != uint256()) {
        CBlockIndex *pindex = LookupBlockIndex(nBlockHash);
        if (pindex && ::ChainActive().Contains(pindex)) {
            nConfirmationsIn += ::ChainActive().Height() - pindex->nHeight + 1;
        }
    }

    if ((nConfirmationsIn < GOVERNANCE_FEE_CONFIRMATIONS)) {
        strError = strprintf(
                "Collateral requires at least %d confirmations to be relayed throughout the network (it has only %d)",
                GOVERNANCE_FEE_CONFIRMATIONS, nConfirmationsIn);
        if (nConfirmationsIn >= GOVERNANCE_MIN_RELAY_FEE_CONFIRMATIONS) {
            fMissingConfirmations = true;
            strError += ", pre-accepted -- waiting for required confirmations";
        } else {
            strError += ", rejected -- try again later";
        }
        LogPrintf("CGovernanceObject::IsCollateralValid -- %s\n", strError);

        return false;
    }

    strError = "valid";
    return true;
}

int CGovernanceObject::CountMatchingVotes(vote_signal_enum_t eVoteSignalIn, vote_outcome_enum_t eVoteOutcomeIn) const {
    LOCK(cs);

    int nCount = 0;
    for (const auto &votepair: mapCurrentMNVotes) {
        const vote_rec_t &recVote = votepair.second;
        auto it2 = recVote.mapInstances.find(eVoteSignalIn);
        if (it2 != recVote.mapInstances.end() && it2->second.eOutcome == eVoteOutcomeIn) {
            ++nCount;
        }
    }
    return nCount;
}

/**
*   Get specific vote counts for each outcome (funding, validity, etc)
*/

int CGovernanceObject::GetAbsoluteYesCount(vote_signal_enum_t eVoteSignalIn) const {
    return GetYesCount(eVoteSignalIn) - GetNoCount(eVoteSignalIn);
}

int CGovernanceObject::GetAbsoluteNoCount(vote_signal_enum_t eVoteSignalIn) const {
    return GetNoCount(eVoteSignalIn) - GetYesCount(eVoteSignalIn);
}

int CGovernanceObject::GetYesCount(vote_signal_enum_t eVoteSignalIn) const {
    return CountMatchingVotes(eVoteSignalIn, VOTE_OUTCOME_YES);
}

int CGovernanceObject::GetNoCount(vote_signal_enum_t eVoteSignalIn) const {
    return CountMatchingVotes(eVoteSignalIn, VOTE_OUTCOME_NO);
}

int CGovernanceObject::GetAbstainCount(vote_signal_enum_t eVoteSignalIn) const {
    return CountMatchingVotes(eVoteSignalIn, VOTE_OUTCOME_ABSTAIN);
}

bool CGovernanceObject::GetCurrentMNVotes(const COutPoint &mnCollateralOutpoint, vote_rec_t &voteRecord) const {
    LOCK(cs);

    auto it = mapCurrentMNVotes.find(mnCollateralOutpoint);
    if (it == mapCurrentMNVotes.end()) {
        return false;
    }
    voteRecord = it->second;
    return true;
}

void CGovernanceObject::Relay(CConnman &connman) {
    // Do not relay until fully synced
    if (!smartnodeSync.IsSynced()) {
        LogPrint(BCLog::GOBJECT, "CGovernanceObject::Relay -- won't relay until fully synced\n");
        return;
    }

    CInv inv(MSG_GOVERNANCE_OBJECT, GetHash());
    connman.RelayInv(inv, MIN_GOVERNANCE_PEER_PROTO_VERSION);
}

void CGovernanceObject::UpdateSentinelVariables() {
    // CALCULATE MINIMUM SUPPORT LEVELS REQUIRED

    int nMnCount = (int) deterministicMNManager->GetListAtChainTip().GetValidMNsCount();
    if (nMnCount == 0) return;

    // CALCULATE THE MINIMUM VOTE COUNT REQUIRED FOR FULL SIGNAL

    int nAbsVoteReq = std::max(Params().GetConsensus().nGovernanceMinQuorum, nMnCount / 10);
    int nAbsDeleteReq = std::max(Params().GetConsensus().nGovernanceMinQuorum, (2 * nMnCount) / 3);

    // SET SENTINEL FLAGS TO FALSE

    fCachedFunding = false;
    fCachedValid = true; //default to valid
    fCachedEndorsed = false;
    fDirtyCache = false;

    // SET SENTINEL FLAGS TO TRUE IF MINIMUM SUPPORT LEVELS ARE REACHED
    // ARE ANY OF THESE FLAGS CURRENTLY ACTIVATED?

    if (GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING) >= nAbsVoteReq) fCachedFunding = true;
    if ((GetAbsoluteYesCount(VOTE_SIGNAL_DELETE) >= nAbsDeleteReq) && !fCachedDelete) {
        fCachedDelete = true;
        if (nDeletionTime == 0) {
            nDeletionTime = GetAdjustedTime();
        }
    }
    if (GetAbsoluteYesCount(VOTE_SIGNAL_ENDORSED) >= nAbsVoteReq) fCachedEndorsed = true;

    if (GetAbsoluteNoCount(VOTE_SIGNAL_VALID) >= nAbsVoteReq) fCachedValid = false;
}
