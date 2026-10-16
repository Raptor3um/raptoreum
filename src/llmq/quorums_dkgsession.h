// Copyright (c) 2018-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LLMQ_QUORUMS_DKGSESSION_H
#define BITCOIN_LLMQ_QUORUMS_DKGSESSION_H

#include <batchedlogger.h>

#include <bls/bls_ies.h>
#include <bls/bls_worker.h>

#include <llmq/quorums_dkgabsence.h>
#include <llmq/quorums_utils.h>
#include <update/update.h>

class UniValue;

class CConnman;

class CInv;

namespace llmq {

    class CFinalCommitment;

    class CDKGSession;

    class CDKGSessionManager;

    class CDKGPendingMessages;

    class CDKGContribution {
    public:
        int32_t nVersion { 0 };
        Consensus::LLMQType llmqType;
        uint256 quorumHash;
        uint256 proTxHash;
        BLSVerificationVectorPtr vvec;
        std::shared_ptr <CBLSIESMultiRecipientObjects<CBLSSecretKey>> contributions;
        CBLSSignature sig;

        template<typename Stream>
        inline void SerializeWithoutSig(Stream &s) const {
            if (nVersion != 0) {
               s << (uint8_t)0; // Marker for new serialization (old serialization was not versioned)
               s << llmqType;
               s << nVersion;
            } else {
                s << llmqType;
            }
            s << quorumHash;
            s << proTxHash;
            s << *vvec;
            s << *contributions;
        }

        template<typename Stream>
        inline void Serialize(Stream &s) const {
            SerializeWithoutSig(s);
            s << sig;
        }

        template<typename Stream>
        inline void Unserialize(Stream &s) {
            BLSVerificationVector tmp1;
            CBLSIESMultiRecipientObjects <CBLSSecretKey> tmp2;

            s >> llmqType;
            if (llmqType == Consensus::LLMQType::LLMQ_INVALID) {
               // New version, deserialize the version, then the real llmqType
               s >> llmqType;
               s >> nVersion;
            }
            s >> quorumHash;
            s >> proTxHash;
            s >> tmp1;
            s >> tmp2;
            s >> sig;

            vvec = std::make_shared<BLSVerificationVector>(std::move(tmp1));
            contributions = std::make_shared < CBLSIESMultiRecipientObjects < CBLSSecretKey >> (std::move(tmp2));
        }

        [[nodiscard]] uint256 GetSignHash() const {
            CHashWriter hw(SER_GETHASH, 0);
            SerializeWithoutSig(hw);
            hw << CBLSSignature();
            return hw.GetHash();
        }
    };

    class CDKGComplaint {
    public:
        Consensus::LLMQType llmqType{Consensus::LLMQ_NONE};
        uint256 quorumHash;
        uint256 proTxHash;
        std::vector<bool> badMembers;
        std::vector<bool> complainForMembers;
        CBLSSignature sig;

        CDKGComplaint() = default;

        explicit CDKGComplaint(const Consensus::LLMQParams &params) : badMembers((size_t) params.size),
                                                                      complainForMembers((size_t) params.size) {};

        SERIALIZE_METHODS(CDKGComplaint, obj
        )
        {
            READWRITE(obj.llmqType, obj.quorumHash, obj.proTxHash, DYNBITSET(obj.badMembers),
                      DYNBITSET(obj.complainForMembers), obj.sig);
        }

        [[nodiscard]] uint256 GetSignHash() const {
            CDKGComplaint tmp(*this);
            tmp.sig = CBLSSignature();
            return ::SerializeHash(tmp);
        }
    };

    class CDKGJustification {
    public:
        Consensus::LLMQType llmqType;
        uint256 quorumHash;
        uint256 proTxHash;
        // TODO: make this pair a struct with named fieds
        std::vector <std::pair<uint32_t, CBLSSecretKey>> contributions;
        CBLSSignature sig;

        SERIALIZE_METHODS(CDKGJustification, obj
        )
        {
            READWRITE(obj.llmqType, obj.quorumHash, obj.proTxHash, obj.contributions, obj.sig);
        }

        [[nodiscard]] uint256 GetSignHash() const {
            CDKGJustification tmp(*this);
            tmp.sig = CBLSSignature();
            return ::SerializeHash(tmp);
        }
    };

// each member commits to a single set of valid members with this message
// then each node aggregate all received premature commitments
// into a single CFinalCommitment, which is only valid if
// enough (>=minSize) premature commitments were aggregated
    class CDKGPrematureCommitment {
    public:
        Consensus::LLMQType llmqType{Consensus::LLMQ_NONE};
        uint256 quorumHash;
        uint256 proTxHash;
        std::vector<bool> validMembers;

        CBLSPublicKey quorumPublicKey;
        uint256 quorumVvecHash;

        bool roundVoting; // Is round voting active?
        Consensus::CQuorumUpdateVoteVec quorumUpdateVotes; // If empty, write a single zero byte (first bit number).

        CBLSSignature quorumSig; // threshold sig share of quorumHash+validMembers+pubKeyHash+vvecHash
        CBLSSignature sig; // single member sig of quorumHash+validMembers+pubKeyHash+vvecHash

        CDKGPrematureCommitment() : roundVoting(false) {}

        explicit CDKGPrematureCommitment(const Consensus::LLMQParams &params) : validMembers((size_t) params.size), roundVoting(false) {};

        [[nodiscard]] int CountValidMembers() const {
            return int(std::count(validMembers.begin(), validMembers.end(), true));
        }

         template<typename Stream>
        inline void Serialize(Stream &s) const {
            if (roundVoting) {
                s << (uint8_t)0; // Marker for new serialization (old serialization was not versioned)
                s << llmqType;
                s << roundVoting;
                s << quorumUpdateVotes;
            } else {
                s << llmqType;
            }
            s << quorumHash;
            s << proTxHash;
            s << DYNBITSET(validMembers);
            s << quorumPublicKey;
            s << quorumVvecHash;
            s << quorumSig;
            s << sig;
        }

        template<typename Stream>
        inline void Unserialize(Stream &s) {
            s >> llmqType;
            if (llmqType == Consensus::LLMQType::LLMQ_INVALID) {
               // New version, deserialize the version, then the real llmqType
               s >> llmqType;
               s >> roundVoting;
               s >> quorumUpdateVotes;
            }
            s >> quorumHash;
            s >> proTxHash;
            s >> DYNBITSET(validMembers);
            s >> quorumPublicKey;
            s >> quorumVvecHash;
            s >> quorumSig;
            s >> sig;
        }

        [[nodiscard]] uint256 GetSignHash() const {
            if (roundVoting) {
               return CLLMQUtils::BuildCommitmentHash(llmqType, quorumHash, validMembers, quorumUpdateVotes, quorumPublicKey, quorumVvecHash);
            }
            return CLLMQUtils::BuildCommitmentHash(llmqType, quorumHash, validMembers, quorumPublicKey, quorumVvecHash);
        }
    };

    class CDKGMember {
    public:
        CDKGMember(const CDeterministicMNCPtr &_dmn, size_t _idx);

        CDeterministicMNCPtr dmn;
        size_t idx;
        CBLSId id;

        std::set <uint256> contributions;
        std::set <uint256> complaints;
        std::set <uint256> justifications;
        std::set <uint256> prematureCommitments;

        std::set <uint256> badMemberVotes;
        std::set <uint256> complaintsFromOthers;

        bool bad{false};
        bool badConnection{false};
        bool weComplain{false};
        bool someoneComplain{false};
    };

/**
 * The DKG session is a single instance of the DKG process. It is owned and called by CDKGSessionHandler, which passes
 * received DKG messages to the session. The session is not persistent and will loose it's state (the whole object is
 * discarded) when it finishes (after the mining phase) or is aborted.
 *
 * When incoming contributions are received and the verification vector is valid, it is passed to CDKGSessionManager
 * which will store it in the evo DB. Secret key contributions which are meant for the local member are also passed
 * to CDKGSessionManager to store them in the evo DB. If verification of the SK contribution initially fails, it is
 * not passed to CDKGSessionManager. If the justification phase later gives a valid SK contribution from the same
 * member, it is then passed to CDKGSessionManager and after this handled the same way.
 *
 * The contributions stored by CDKGSessionManager are then later loaded by the quorum instances and used for signing
 * sessions, but only if the local node is a member of the quorum.
 */
    class CDKGSession {
        friend class CDKGSessionHandler;

        friend class CDKGSessionManager;

        friend class CDKGLogger;

    private:
        const Consensus::LLMQParams &params;

        CBLSWorker &blsWorker;
        CBLSWorkerCache cache;
        CDKGSessionManager &dkgManager;

        const CBlockIndex *m_quorum_base_block_index{nullptr};

        std::vector <std::unique_ptr<CDKGMember>> members;
        std::map <uint256, size_t> membersMap;
        std::set <uint256> relayMembers;
        BLSVerificationVectorPtr vvecContribution;
        BLSSecretKeyVector skContributions;

        BLSIdVector memberIds;
        std::vector <BLSVerificationVectorPtr> receivedVvecs;
        std::vector <uint32_t> receivedVersions; // Used to track node update voting
        // these are not necessarily verified yet. Only trust in what was written to the DB
        BLSSecretKeyVector receivedSkContributions;
        /// Contains the received unverified/encrypted DKG contributions
        std::vector <std::shared_ptr<CBLSIESMultiRecipientObjects < CBLSSecretKey>>>
        vecEncryptedContributions;

        uint256 myProTxHash;
        CBLSId myId;
        CConnman &connman;
        std::optional <size_t> myIdx;

        // all indexed by msg hash
        // we expect to only receive a single vvec and contribution per member, but we must also be able to relay
        // conflicting messages as otherwise an attacker might be able to broadcast conflicting (valid+invalid) messages
        // and thus split the quorum. Such members are later removed from the quorum.
        mutable RecursiveMutex invCs;
        std::map <uint256, CDKGContribution> contributions
        GUARDED_BY(invCs);
        std::map <uint256, CDKGComplaint> complaints
        GUARDED_BY(invCs);
        std::map <uint256, CDKGJustification> justifications
        GUARDED_BY(invCs);
        std::map <uint256, CDKGPrematureCommitment> prematureCommitments
        GUARDED_BY(invCs);

        mutable RecursiveMutex cs_pending;
        std::vector <size_t> pendingContributionVerifications
        GUARDED_BY(cs_pending);

        // filled by ReceivePrematureCommitment and used by FinalizeCommitments
        std::set <uint256> validCommitments
        GUARDED_BY(invCs);

        // Phase 6 (absence): every member's own vote on who sent no
        // contribution, keyed by message hash (same convention as
        // contributions/complaints/justifications/prematureCommitments
        // above, needed so GetAbsenceVote can serve GETDATA requests the
        // same way). One-vote-per-voter (first-wins) is enforced in
        // ReceiveMessage, under the same lock that guards this map --
        // PreVerifyMessage checks the same thing earlier, but only as a
        // cheap reject for the common case, since a whole batch is
        // pre-verified before any message in it is stored.
        std::map <uint256, CDKGAbsenceVote> absenceVotes
        GUARDED_BY(invCs);

        // Set once, by HandleDKGRound after the absence-vote drain window
        // closes, from whatever this node held at that point (minVotes=1 --
        // see BuildAbsenceRecord's own comment on why the caller, not
        // Verify(), decides that number). nullptr until then, and for a
        // session that was never a member of. This is "agreeing" in the
        // identify-agree-report pipeline: BuildAbsenceRecord already does the
        // intersection; this just keeps its result around long enough to be
        // reported (currently: quorum dkgstatus; later: the special
        // transaction) rather than discarding it the moment it's built.
        CDKGAbsenceRecordPtr builtAbsenceRecord
        GUARDED_BY(invCs);

    public:
        CDKGSession(const Consensus::LLMQParams &_params, CBLSWorker &_blsWorker, CDKGSessionManager &_dkgManager,
                    CConnman &_connman) :
                params(_params), blsWorker(_blsWorker), cache(_blsWorker), dkgManager(_dkgManager), connman(_connman) {}

        bool Init(const CBlockIndex *pQuorumBaseBlockReward, const std::vector <CDeterministicMNCPtr> &mns,
                  const uint256 &_myProTxHash);

        std::optional <size_t> GetMyMemberIndex() const { return myIdx; }

        /**
         * The following sets of methods are for the first 4 phases handled in the session. The flow of message calls
         * is identical for all phases:
         * 1. Execute local action (e.g. create/send own contributions)
         * 2. PreVerify incoming messages for this phase. Preverification means that everything from the message is checked
         *    that does not require too much resources for verification. This specifically excludes all CPU intensive BLS
         *    operations.
         * 3. CDKGSessionHandler will collect pre verified messages in batches and perform batched BLS signature verification
         *    on these.
         * 4. ReceiveMessage is called for each pre verified message with a valid signature. ReceiveMessage is also
         *    responsible for further verification of validity (e.g. validate vvecs and SK contributions).
         */

        // Phase 1: contribution
        void Contribute(CDKGPendingMessages &pendingMessages);

        void SendContributions(CDKGPendingMessages &pendingMessages);

        bool PreVerifyMessage(const CDKGContribution &qc, bool &retBan) const;

        void ReceiveMessage(const CDKGContribution &qc, bool &retBan);

        void VerifyPendingContributions()

        EXCLUSIVE_LOCKS_REQUIRED(cs_pending);

        // Phase 2: complaint
        void VerifyAndComplain(CDKGPendingMessages &pendingMessages);

        void VerifyConnectionAndMinProtoVersions() const;

        void SendComplaint(CDKGPendingMessages &pendingMessages);

        bool PreVerifyMessage(const CDKGComplaint &qc, bool &retBan) const;

        void ReceiveMessage(const CDKGComplaint &qc, bool &retBan);

        // Phase 3: justification
        void VerifyAndJustify(CDKGPendingMessages &pendingMessages);

        void SendJustification(CDKGPendingMessages &pendingMessages, const std::set <uint256> &forMembers);

        bool PreVerifyMessage(const CDKGJustification &qj, bool &retBan) const;

        void ReceiveMessage(const CDKGJustification &qj, bool &retBan);

        // Phase 4: commit
        void VerifyAndCommit(CDKGPendingMessages &pendingMessages);

        void SendCommitment(CDKGPendingMessages &pendingMessages);

        bool PreVerifyMessage(const CDKGPrematureCommitment &qc, bool &retBan) const;

        void ReceiveMessage(const CDKGPrematureCommitment &qc, bool &retBan);

        // Phase 5: aggregate/finalize
        std::vector <CFinalCommitment> FinalizeCommitments();

        // Phase 6: absence voting. Runs regardless of whether Phase 5
        // produced anything -- its whole purpose is to leave evidence behind
        // for sessions that never finalize. Ordered after FinalizeCommitments()
        // in HandleDKGRound() because that's naturally where a round's
        // outcome already gets handled, not because anything after the
        // Contribute phase still mutates members[i].contributions.
        void CheckAbsences(CDKGPendingMessages &pendingMessages);

        bool PreVerifyMessage(const CDKGAbsenceVote &qv, bool &retBan) const;

        void ReceiveMessage(const CDKGAbsenceVote &qv, bool &retBan);

        // Bundles every currently-held absence vote (this node's own plus
        // whatever it received) into a record. minVotes is the caller's own
        // choice of how many independent votes it wants before trusting the
        // result -- this method does not decide that number, it just builds
        // and self-verifies whatever is asked for. Returns nullptr if fewer
        // than minVotes distinct votes are held.
        CDKGAbsenceRecordPtr BuildAbsenceRecord(size_t minVotes) const;

        bool AreWeMember() const { return !myProTxHash.IsNull(); }

        void MarkBadMember(size_t idx);

        void RelayInvToParticipants(const CInv &inv) const;

        CDKGMember *GetMember(const uint256 &proTxHash) const;

    private:
        bool ShouldSimulateError(const std::string &type) const;
    };

    class CDKGLogger : public CBatchedLogger {
    public:
        CDKGLogger(const CDKGSession &_quorumDkg, std::string_view _func) :
                CDKGLogger(_quorumDkg.params.name, _quorumDkg.m_quorum_base_block_index->GetBlockHash(),
                           _quorumDkg.m_quorum_base_block_index->nHeight, _quorumDkg.AreWeMember(), _func) {};

        CDKGLogger(std::string_view _llmqTypeName, const uint256 &_quorumHash, int _height, bool _areWeMember,
                   std::string_view _func) :
                CBatchedLogger(BCLog::LLMQ_DKG,
                               strprintf("QuorumDKG(type=%s, height=%d, member=%d, func=%s)", _llmqTypeName, _height,
                                         _areWeMember, _func)) {};
    };

    void SetSimulatedDKGErrorRate(const std::string &type, double rate);

} // namespace llmq

#endif // BITCOIN_LLMQ_QUORUMS_DKGSESSION_H
