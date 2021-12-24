// Copyright (c) 2014-2019 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <smartnode/smartnode-meta.h>

#include <timedata.h>

CSmartnodeMetaMan mmetaman;

const std::string CSmartnodeMetaMan::SERIALIZATION_VERSION_STRING = "CSmartnodeMetaMan-Version-2";

UniValue CSmartnodeMetaInfo::ToJson() const
{
    UniValue ret(UniValue::VOBJ);

    auto now = GetAdjustedTime();

    ret.push_back(Pair("lastDSQ", nLastDsq));
    ret.push_back(Pair("mixingTxCount", nMixingTxCount));
    ret.push_back(Pair("lastOutboundAttempt", lastOutboundAttempt));
    ret.push_back(Pair("lastOutboundAttemptElapsed", now - lastOutboundAttempt));
    ret.push_back(Pair("lastOutboundSuccess", lastOutboundSuccess));
    ret.push_back(Pair("lastOutboundSuccessElapsed", now - lastOutboundSuccess));

    return ret;
}

void CSmartnodeMetaInfo::AddGovernanceVote(const uint256& nGovernanceObjectHash)
{
    LOCK(cs);
    // Insert a zero value, or not. Then increment the value regardless. This
    // ensures the value is in the map.
    const auto& pair = mapGovernanceObjectsVotedOn.emplace(nGovernanceObjectHash, 0);
    pair.first->second++;
}

void CSmartnodeMetaInfo::RemoveGovernanceObject(const uint256& nGovernanceObjectHash)
{
    LOCK(cs);
    // Whether or not the govobj hash exists in the map first is irrelevant.
    mapGovernanceObjectsVotedOn.erase(nGovernanceObjectHash);
}

CSmartnodeMetaInfoPtr CSmartnodeMetaMan::GetMetaInfo(const uint256& proTxHash, bool fCreate)
{
    LOCK(cs);
    auto it = metaInfos.find(proTxHash);
    if (it != metaInfos.end()) {
        return it->second;
    }
    if (!fCreate) {
        return nullptr;
    }
    it = metaInfos.emplace(proTxHash, std::make_shared<CSmartnodeMetaInfo>(proTxHash)).first;
    return it->second;
}

// We keep track of dsq (mixing queues) count to avoid using same smartnodes for mixing too often.
// This threshold is calculated as the last dsq count this specific smartnode was used in a mixing
// session plus a margin of 20% of smartnode count. In other words we expect at least 20% of unique
// smartnodes before we ever see a smartnode that we know already mixed someone's funds ealier.
int64_t CSmartnodeMetaMan::GetDsqThreshold(const uint256& proTxHash, int nMnCount)
{
    LOCK(cs);
    auto metaInfo = GetMetaInfo(proTxHash);
    if (metaInfo == nullptr) {
        // return a threshold which is slightly above nDsqCount i.e. a no-go
        return nDsqCount + 1;
    }
    return metaInfo->GetLastDsq() + nMnCount / 5;
}

void CSmartnodeMetaMan::AllowMixing(const uint256& proTxHash)
{
    LOCK(cs);
    auto mm = GetMetaInfo(proTxHash);
    nDsqCount++;
    LOCK(mm->cs);
    mm->nLastDsq = nDsqCount;
    mm->nMixingTxCount = 0;
}

void CSmartnodeMetaMan::DisallowMixing(const uint256& proTxHash)
{
    LOCK(cs);
    auto mm = GetMetaInfo(proTxHash);

    LOCK(mm->cs);
    mm->nMixingTxCount++;
}

bool CSmartnodeMetaMan::AddGovernanceVote(const uint256& proTxHash, const uint256& nGovernanceObjectHash)
{
    LOCK(cs);
    auto mm = GetMetaInfo(proTxHash);
    mm->AddGovernanceVote(nGovernanceObjectHash);
    return true;
}

void CSmartnodeMetaMan::RemoveGovernanceObject(const uint256& nGovernanceObjectHash)
{
    LOCK(cs);
    for(auto& p : metaInfos) {
        p.second->RemoveGovernanceObject(nGovernanceObjectHash);
    }
}

std::vector<uint256> CSmartnodeMetaMan::GetAndClearDirtyGovernanceObjectHashes()
{
    LOCK(cs);
    std::vector<uint256> vecTmp = std::move(vecDirtyGovernanceObjectHashes);
    vecDirtyGovernanceObjectHashes.clear();
    return vecTmp;
}

void CSmartnodeMetaMan::Clear()
{
    LOCK(cs);
    metaInfos.clear();
    vecDirtyGovernanceObjectHashes.clear();
}

void CSmartnodeMetaMan::CheckAndRemove()
{

}

std::string CSmartnodeMetaMan::ToString() const
{
    std::ostringstream info;

    info << "Smartnodes: meta infos object count: " << (int)metaInfos.size() <<
         ", nDsqCount: " << (int)nDsqCount;
    return info.str();
}
