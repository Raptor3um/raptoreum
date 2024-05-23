// Copyright (c) 2018-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LLMQ_QUORUMS_H
#define BITCOIN_LLMQ_QUORUMS_H

#include <chain.h>
#include <consensus/params.h>
#include <saltedhasher.h>
#include <unordered_lru_cache.h>
#include <threadinterrupt.h>

#include <bls/bls.h>
#include <bls/bls_worker.h>

#include <evo/evodb.h>

class CNode;

class CConnman;

class CBlockIndex;

class CDeterministicMN;

using CDeterministicMNCPtr = std::shared_ptr<const CDeterministicMN>;

namespace llmq {

    class CDKGSessionManager;

// If true, we will connect to all new quorums and watch their communication
    static const bool DEFAULT_WATCH_QUORUMS = false;

/**
* Object used as a key to store CQuorumDataRequest
*/
    struct CQuorumDataRequestKey
    {
        uint256 proRegTx;
        //TODO: Investigate purpose of this flag and rename accordingly
        bool flag;
        uint256 quorumHash;
        Consensus::LLMQType llmqType;

        bool operator ==(const CQuorumDataRequestKey& obj) const
        {
            return (proRegTx == obj.proRegTx && flag == obj.flag && quorumHash == obj.quorumHash && llmqType == obj.llmqType);
        }
    };

/**
 * An object of this class represents a QGETDATA request or a QDATA response header
 */
    class CQuorumDataRequest {
    public:

        enum Flags : uint16_t {
            QUORUM_VERIFICATION_VECTOR = 0x0001,
            ENCRYPTED_CONTRIBUTIONS = 0x0002,
        };
        enum Errors : uint8_t {
            NONE = 0x00,
            QUORUM_TYPE_INVALID = 0x01,
            QUORUM_BLOCK_NOT_FOUND = 0x02,
            QUORUM_NOT_FOUND = 0x03,
            SMARTNODE_IS_NO_MEMBER = 0x04,
            QUORUM_VERIFICATION_VECTOR_MISSING = 0x05,
            ENCRYPTED_CONTRIBUTIONS_MISSING = 0x06,
            UNDEFINED = 0xFF,
        };

    private:
        Consensus::LLMQType llmqType{Consensus::LLMQType::LLMQ_NONE};
        uint256 quorumHash;
        uint16_t nDataMask{0};
        uint256 proTxHash;
        Errors nError{UNDEFINED};

        int64_t nTime{GetTime()};
        bool fProcessed{false};

        static const int64_t EXPIRATION_TIMEOUT{300};

    public:
        CQuorumDataRequest() : nTime(GetTime()) {}

        CQuorumDataRequest(const Consensus::LLMQType llmqTypeIn, const uint256 &quorumHashIn,
                           const uint16_t nDataMaskIn, const uint256 &proTxHashIn = uint256()) :
                llmqType(llmqTypeIn),
                quorumHash(quorumHashIn),
                nDataMask(nDataMaskIn),
                proTxHash(proTxHashIn) {}

        SERIALIZE_METHODS(CQuorumDataRequest, obj
        )
        {
            bool fRead{false};
            SER_READ(obj, fRead = true);
            READWRITE(obj.llmqType, obj.quorumHash, obj.nDataMask, obj.proTxHash);
            if (fRead) {
                try {
                    READWRITE(obj.nError);
                } catch (...) {
                    SER_READ(obj, obj.nError = UNDEFINED);
                }
            } else if (obj.nError != UNDEFINED) {
                READWRITE(obj.nError);
            }
        }

        Consensus::LLMQType GetLLMQType() const { return llmqType; }

        const uint256 &GetQuorumHash() const { return quorumHash; }

        uint16_t GetDataMask() const { return nDataMask; }

        const uint256 &GetProTxHash() const { return proTxHash; }

        void SetError(Errors nErrorIn) { nError = nErrorIn; }

        Errors GetError() const { return nError; }

        std::string GetErrorString() const;

        bool IsExpired() const { return (GetTime() - nTime) >= EXPIRATION_TIMEOUT; }

        bool IsProcessed() const { return fProcessed; }

        void SetProcessed() { fProcessed = true; }

        bool operator==(const CQuorumDataRequest &other) const {
            return llmqType == other.llmqType &&
                   quorumHash == other.quorumHash &&
                   nDataMask == other.nDataMask &&
                   proTxHash == other.proTxHash;
        }

        bool operator!=(const CQuorumDataRequest &other) const {
            return !(*this == other);
        }
    };

/**
 * An object of this class represents a quorum which was mined on-chain (through a quorum commitment)
 * It at least contains information about the members and the quorum public key which is needed to verify recovered
 * signatures from this quorum.
 *
 * In case the local node is a member of the same quorum and successfully participated in the DKG, the quorum object
 * will also contain the secret key share and the quorum verification vector. The quorum vvec is then used to recover
 * the public key shares of individual members, which are needed to verify signature shares of these members.
 */

    class CQuorum;

    using CQuorumPtr = std::shared_ptr<CQuorum>;
    using CQuorumCPtr = std::shared_ptr<const CQuorum>;

    class CFinalCommitment;

    using CFinalCommitmentPtr = std::unique_ptr<CFinalCommitment>;

    class CQuorum {
        friend class CQuorumManager;

    public:
        const Consensus::LLMQParams &params;
        CFinalCommitmentPtr qc;
        const CBlockIndex *m_quorum_base_block_index;
        uint256 minedBlockHash;
        std::vector <CDeterministicMNCPtr> members;

    private:
        // Recovery of public key shares is very slow, so we start a background thread that pre-populates a cache so that
        // the public key shares are ready when needed later
        mutable CBLSWorkerCache blsCache;
        mutable std::atomic<bool> fQuorumDataRecoveryThreadRunning{false};

        mutable RecursiveMutex cs;
        // These are only valid when we either participated in the DKG or fully watched it.
        BLSVerificationVectorPtr quorumVvec
        GUARDED_BY(cs);
        CBLSSecretKey skShare
        GUARDED_BY(cs);

    public:
        CQuorum(const Consensus::LLMQParams &_params, CBLSWorker &_blsWorker);

        ~CQuorum() = default;

        void Init(CFinalCommitmentPtr _qc, const CBlockIndex *_pQuorumBaseBlockIndex, const uint256 &_minedBlockHash,
                  const std::vector <CDeterministicMNCPtr> &_members);

        bool SetVerificationVector(const BLSVerificationVector &quorumVecIn);

        bool SetSecretKeyShare(const CBLSSecretKey &secretKeyShare);

        bool HasVerificationVector() const;

        bool IsMember(const uint256 &proTxHash) const;

        bool IsValidMember(const uint256 &proTxHash) const;

        int GetMemberIndex(const uint256 &proTxHash) const;

        CBLSPublicKey GetPubKeyShare(size_t memberIdx) const;

        CBLSSecretKey GetSkShare() const;

    private:
        void WriteContributions(CEvoDB &evoDb) const;

        bool ReadContributions(CEvoDB &evoDb);
    };

/**
 * The quorum manager maintains quorums which were mined on chain. When a quorum is requested from the manager,
 * it will lookup the commitment (through CQuorumBlockProcessor) and build a CQuorum object from it.
 *
 * It is also responsible for initialization of the intra-quorum connections for new quorums.
 */
    class CQuorumManager {
    private:
        CEvoDB &evoDb;
        CConnman &connman;
        CBLSWorker &blsWorker;
        CDKGSessionManager &dkgManager;

        mutable RecursiveMutex cs_map_quorums;
        mutable std::map <Consensus::LLMQType, unordered_lru_cache<uint256, CQuorumPtr, StaticSaltedHasher>> mapQuorumsCache
        GUARDED_BY(cs_map_quorums);
        mutable RecursiveMutex cs_scan_quorums;
        mutable std::map <Consensus::LLMQType, unordered_lru_cache<uint256,
                std::vector < CQuorumCPtr>, StaticSaltedHasher>> scanQuorumsCache
        GUARDED_BY(cs_scan_quorums);

        mutable ctpl::thread_pool workerPool;
        mutable CThreadInterrupt quorumThreadInterrupt;

    public:
        CQuorumManager(CEvoDB &_evoDb, CConnman &_connman, CBLSWorker &_blsWorker, CDKGSessionManager &_dkgManager);

        ~CQuorumManager() { Stop(); };

        void Start();

        void Stop();

        void TriggerQuorumDataRecoveryThreads(const CBlockIndex *pIndex) const;

        void UpdatedBlockTip(const CBlockIndex *pindexNew, bool fInitialDownload) const;

        void ProcessMessage(CNode *pfrom, const std::string &strCommand, CDataStream &vRecv);

        static bool HasQuorum(Consensus::LLMQType llmqType, const uint256 &quorumHash);

        bool RequestQuorumData(CNode *pFrom, Consensus::LLMQType llmqType, const CBlockIndex *pQuorumBaseBlockIndex,
                               uint16_t nDataMask, const uint256 &proTxHash = uint256()) const;

        // all these methods will lock cs_main for a short period of time
        CQuorumCPtr GetQuorum(Consensus::LLMQType llmqType, const uint256 &quorumHash) const;

        std::vector <CQuorumCPtr> ScanQuorums(Consensus::LLMQType llmqType, size_t nCountRequested) const;

        // this one is cs_main-free
        std::vector <CQuorumCPtr>
        ScanQuorums(Consensus::LLMQType llmqType, const CBlockIndex *pindexStart, size_t nCountRequested) const;

    private:
        // all private methods here are cs_main-free
        void EnsureQuorumConnections(const Consensus::LLMQParams &llmqParams, const CBlockIndex *pindexNew) const;

        CQuorumPtr
        BuildQuorumFromCommitment(Consensus::LLMQType llmqType, const CBlockIndex *pQuorumBaseBlockIndex) const

        EXCLUSIVE_LOCKS_REQUIRED(cs_map_quorums);

        bool BuildQuorumContributions(const CFinalCommitmentPtr &fqc, const std::shared_ptr <CQuorum> &quorum) const;

        CQuorumCPtr GetQuorum(Consensus::LLMQType llmqType, const CBlockIndex *pindex) const;

        /// Returns the start offset for the smartnode with the given proTxHash. This offset is applied when picking data recovery members of a quorum's
        /// memberlist and is calculated based on a list of all member of all active quorums for the given llmqType in a way that each member
        /// should receive the same number of request if all active llmqType members requests data from one llmqType quorum.
        size_t GetQuorumRecoveryStartOffset(const CQuorumCPtr pQuorum, const CBlockIndex *pIndex) const;

        void StartCachePopulatorThread(const CQuorumCPtr pQuorum) const;

        void
        StartQuorumDataRecoveryThread(const CQuorumCPtr pQuorum, const CBlockIndex *pIndex, uint16_t nDataMask) const;
    };

    extern CQuorumManager *quorumManager;

} // namespace llmq

template<typename T> struct SaltedHasherImpl;
template<>
struct SaltedHasherImpl<llmq::CQuorumDataRequestKey>
{
    static std::size_t CalcHash(const llmq::CQuorumDataRequestKey& v, uint64_t k0, uint64_t k1)
    {
        CSipHasher c(k0, k1);
        c.Write((unsigned char*)&(v.proRegTx), sizeof(v.proRegTx));
        c.Write((unsigned char*)&(v.flag), sizeof(v.flag));
        c.Write((unsigned char*)&(v.quorumHash), sizeof(v.quorumHash));
        c.Write((unsigned char*)&(v.llmqType), sizeof(v.llmqType));
        return c.Finalize();
    }
};

template<> struct is_serializable_enum<llmq::CQuorumDataRequest::Errors> : std::true_type {
};

#endif // BITCOIN_LLMQ_QUORUMS_H
