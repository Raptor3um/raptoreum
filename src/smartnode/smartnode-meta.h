// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SMARTNODE_SMARTNODE_META_H
#define BITCOIN_SMARTNODE_SMARTNODE_META_H

#include <serialize.h>

#include <univalue.h>

#include <uint256.h>
#include <sync.h>

class CConnman;

static const int SMARTNODE_MAX_MIXING_TXES = 5;

// Holds extra (non-deterministic) information about smartnodes
// This is mostly local information, e.g. about mixing and governance
class CSmartnodeMetaInfo {
    friend class CSmartnodeMetaMan;

private:
    mutable RecursiveMutex cs;

    uint256 proTxHash;

    //the dsq count from the last dsq broadcast of this node
    int64_t nLastDsq = 0;
    int nMixingTxCount = 0;

    // KEEP TRACK OF GOVERNANCE ITEMS EACH SMARTNODE HAS VOTE UPON FOR RECALCULATION
    std::map<uint256, int> mapGovernanceObjectsVotedOn;

    int64_t lastOutboundAttempt = 0;
    int64_t lastOutboundSuccess = 0;

public:
    CSmartnodeMetaInfo() = default;

    explicit CSmartnodeMetaInfo(const uint256 &_proTxHash) : proTxHash(_proTxHash) {}

    CSmartnodeMetaInfo(const CSmartnodeMetaInfo &ref) :
            proTxHash(ref.proTxHash),
            nLastDsq(ref.nLastDsq),
            nMixingTxCount(ref.nMixingTxCount),
            mapGovernanceObjectsVotedOn(ref.mapGovernanceObjectsVotedOn),
            lastOutboundAttempt(ref.lastOutboundAttempt),
            lastOutboundSuccess(ref.lastOutboundSuccess) {
    }

    SERIALIZE_METHODS(CSmartnodeMetaInfo, obj
    )
    {
        LOCK(obj.cs);
        READWRITE(obj.proTxHash, obj.nLastDsq, obj.nMixingTxCount, obj.mapGovernanceObjectsVotedOn,
                  obj.lastOutboundAttempt, obj.lastOutboundSuccess);
    }

    UniValue ToJson() const;

public:
    const uint256 &GetProTxHash() const {
        LOCK(cs);
        return proTxHash;
    }

    int64_t GetLastDsq() const {
        LOCK(cs);
        return nLastDsq;
    }

    int GetMixingTxCount() const {
        LOCK(cs);
        return nMixingTxCount;
    }

    bool IsValidForMixingTxes() const { return GetMixingTxCount() <= SMARTNODE_MAX_MIXING_TXES; }

    // KEEP TRACK OF EACH GOVERNANCE ITEM IN CASE THIS NODE GOES OFFLINE, SO WE CAN RECALCULATE THEIR STATUS
    void AddGovernanceVote(const uint256 &nGovernanceObjectHash);

    void RemoveGovernanceObject(const uint256 &nGovernanceObjectHash);

    void SetLastOutboundAttempt(int64_t t) {
        LOCK(cs);
        lastOutboundAttempt = t;
    }

    int64_t GetLastOutboundAttempt() const {
        LOCK(cs);
        return lastOutboundAttempt;
    }

    void SetLastOutboundSuccess(int64_t t) {
        LOCK(cs);
        lastOutboundSuccess = t;
    }

    int64_t GetLastOutboundSuccess() const {
        LOCK(cs);
        return lastOutboundSuccess;
    }
};

using CSmartnodeMetaInfoPtr = std::shared_ptr<CSmartnodeMetaInfo>;

class CSmartnodeMetaMan {
private:
    static const std::string SERIALIZATION_VERSION_STRING;

    mutable RecursiveMutex cs;

    std::map <uint256, CSmartnodeMetaInfoPtr> metaInfos;
    std::vector <uint256> vecDirtyGovernanceObjectHashes;

    // keep track of dsq count to prevent smartnodes from gaming coinjoin queue
    int64_t nDsqCount = 0;

public:
    template<typename Stream>
    void Serialize(Stream &s) const {
        LOCK(cs);
        std::vector <CSmartnodeMetaInfo> tmpMetaInfo;
        for (auto &p: metaInfos) {
            tmpMetaInfo.emplace_back(*p.second);
        }
        s << SERIALIZATION_VERSION_STRING << tmpMetaInfo << nDsqCount;
    }

    template<typename Stream>
    void Unserialize(Stream &s) {
        LOCK(cs);
        Clear();
        std::string strVersion;
        s >> strVersion;
        if (strVersion != SERIALIZATION_VERSION_STRING) return;

        std::vector <CSmartnodeMetaInfo> tmpMetaInfo;
        s >> tmpMetaInfo >> nDsqCount;
        metaInfos.clear();
        for (auto &mm: tmpMetaInfo) {
            metaInfos.emplace(mm.GetProTxHash(), std::make_shared<CSmartnodeMetaInfo>(std::move(mm)));
        }
    }

public:
    CSmartnodeMetaInfoPtr GetMetaInfo(const uint256 &proTxHash, bool fCreate = true);

    int64_t GetDsqCount() {
        LOCK(cs);
        return nDsqCount;
    }

    int64_t GetDsqThreshold(const uint256 &proTxHash, int nMnCount);

    void AllowMixing(const uint256 &proTxHash);

    void DisallowMixing(const uint256 &proTxHash);

    bool AddGovernanceVote(const uint256 &proTxHash, const uint256 &nGovernanceObjectHash);

    void RemoveGovernanceObject(const uint256 &nGovernanceObjectHash);

    std::vector <uint256> GetAndClearDirtyGovernanceObjectHashes();

    void Clear();

    void CheckAndRemove();

    std::string ToString() const;
};

extern CSmartnodeMetaMan mmetaman;

#endif // BITCOIN_SMARTNODE_SMARTNODE_META_H
