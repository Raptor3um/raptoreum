// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <txdecoupling.h>

#include <chain.h>
#include <coins.h>
#include <consensus/validation.h>
#include <evo/cbtx.h>
#include <evo/evodb.h>
#include <evo/specialtx.h>
#include <hash.h>
#include <llmq/quorums_blockprocessor.h>
#include <llmq/quorums_commitment.h>
#include <llmq/quorums_utils.h>
#include <primitives/block.h>
#include <validation.h>

uint256 GetTxValidationRequestId(const CTxValidationCertificate& cert, const uint256& genesisHash)
{
    return (CHashWriter(SER_GETHASH, 0) << std::string("rtm-txvalidation-v1")
            << genesisHash << cert.parentHash << cert.txid).GetHash();
}

uint256 GetTxValidationMessageHash(const CTxValidationCertificate& cert)
{
    return (CHashWriter(SER_GETHASH, 0) << cert.version << cert.parentHash << cert.txid
            << cert.prevoutsDigest << cert.consensusFlags << cert.policyFlags << cert.result).GetHash();
}

uint256 GetTxValidationSignHash(const CTxValidationCertificate& cert, const uint256& genesisHash)
{
    return llmq::CLLMQUtils::BuildSignHash(cert.quorumType, cert.quorumHash,
            GetTxValidationRequestId(cert, genesisHash), GetTxValidationMessageHash(cert));
}

bool IsTxDecouplingActive(const CBlockIndex* parent, const Consensus::Params& consensus)
{
    return parent && consensus.fTxDecouplingAllowed && consensus.nTxDecouplingHeight >= 0 &&
           int64_t(parent->nHeight) + 1 >= consensus.nTxDecouplingHeight;
}

bool GetTxValidationPrevoutsDigest(const CTransaction& tx, const CCoinsViewCache& view,
                                  const CBlockIndex* parent, uint256& digest, CValidationState& state)
{
    if (!parent || view.GetBestBlock() != parent->GetBlockHash())
        return state.Error("txcert-coins-parent-unavailable");
    if (tx.IsCoinBase() || tx.nType != TRANSACTION_NORMAL || tx.vin.empty())
        return state.Invalid(false, REJECT_INVALID, "bad-txcert-ineligible");
    for (const auto& out : tx.vout) {
        if (out.scriptPubKey.IsAssetScript())
            return state.Invalid(false, REJECT_INVALID, "bad-txcert-asset");
    }
    CHashWriter hash(SER_GETHASH, 0);
    WriteCompactSize(hash, tx.vin.size());
    for (const auto& input : tx.vin) {
        const Coin& coin = view.AccessCoin(input.prevout);
        if (coin.IsSpent())
            return state.Invalid(false, REJECT_INVALID, "bad-txcert-missing-input");
        if (coin.nHeight > uint32_t(parent->nHeight) || coin.nType == TRANSACTION_FUTURE ||
            coin.nType == TRANSACTION_NEW_ASSET || coin.nType == TRANSACTION_UPDATE_ASSET ||
            coin.nType == TRANSACTION_MINT_ASSET ||
            coin.out.scriptPubKey.IsAssetScript())
            return state.Invalid(false, REJECT_INVALID, "bad-txcert-ineligible-input");
        hash << input.prevout << coin;
    }
    digest = hash.GetHash();
    return true;
}

bool SelectTxValidationQuorum(const CBlockIndex* parent, const uint256& requestId,
                              uint256& quorumHash, CBLSPublicKey& publicKey, CValidationState& state)
{
    AssertLockHeld(cs_main);
    quorumHash.SetNull();
    publicKey = CBLSPublicKey();
    if (!parent || !evoDb || !llmq::quorumBlockProcessor ||
        !evoDb->VerifyBestBlock(parent->GetBlockHash()))
        return state.Error("txcert-quorum-parent-unavailable");
    if (parent->nHeight < 8)
        return state.Invalid(false, REJECT_INVALID, "bad-txcert-no-quorum");
    const CBlockIndex* anchor = parent->GetAncestor(parent->nHeight - 8);
    const auto bases = llmq::quorumBlockProcessor->GetMinedCommitmentsUntilBlock(
            Consensus::LLMQ_5_60, anchor, 2);
    uint256 bestScore;
    for (const CBlockIndex* base : bases) {
        uint256 minedHash;
        const auto commitment = llmq::quorumBlockProcessor->GetMinedCommitment(
                Consensus::LLMQ_5_60, base->GetBlockHash(), minedHash);
        const CBlockIndex* mined = LookupBlockIndex(minedHash);
        if (!commitment || commitment->IsNull() || !commitment->quorumPublicKey.IsValid() ||
            commitment->llmqType != Consensus::LLMQ_5_60 ||
            commitment->quorumHash != base->GetBlockHash() || !mined ||
            base->nHeight > mined->nHeight || mined->nHeight > anchor->nHeight ||
            anchor->GetAncestor(base->nHeight) != base || anchor->GetAncestor(mined->nHeight) != mined)
            return state.Error("txcert-quorum-history-unavailable");
        const uint256 score = (CHashWriter(SER_GETHASH, 0) << Consensus::LLMQ_5_60
                << commitment->quorumHash << requestId).GetHash();
        if (quorumHash.IsNull() || score < bestScore ||
            (score == bestScore && commitment->quorumHash < quorumHash)) {
            bestScore = score;
            quorumHash = commitment->quorumHash;
            publicKey = commitment->quorumPublicKey;
        }
    }
    if (quorumHash.IsNull())
        return state.Invalid(false, REJECT_INVALID, "bad-txcert-no-quorum");
    return true;
}

bool CheckTxValidationStatement(const CTxValidationCertificate& cert, const CTransaction& tx,
                                const CCoinsViewCache& view, const CBlockIndex* parent,
                                const Consensus::Params& consensus, uint8_t expectedResult,
                                CValidationState& state)
{
    AssertLockHeld(cs_main);
    if (!IsTxDecouplingActive(parent, consensus))
        return state.Invalid(false, REJECT_INVALID, "bad-txcert-inactive");
    if (cert.version != CTxValidationCertificate::CURRENT_VERSION ||
        expectedResult > CTxValidationCertificate::POSITIVE || cert.result != expectedResult ||
        cert.quorumType != Consensus::LLMQ_5_60)
        return state.Invalid(false, REJECT_INVALID, "bad-txcert-format");
    if (cert.parentHash != parent->GetBlockHash() || cert.txid != tx.GetHash())
        return state.Invalid(false, REJECT_INVALID, "bad-txcert-context");
    if (!evoDb || !evoDb->VerifyBestBlock(parent->GetBlockHash()))
        return state.Error("txcert-quorum-parent-unavailable");
    uint32_t flags;
    if (!GetTxValidationNextScriptFlags(parent, consensus, flags) || cert.consensusFlags != flags ||
        cert.policyFlags != TX_VALIDATION_POLICY_FLAGS_V1)
        return state.Invalid(false, REJECT_INVALID, "bad-txcert-flags");
    uint256 digest;
    if (!GetTxValidationPrevoutsDigest(tx, view, parent, digest, state)) return false;
    if (cert.prevoutsDigest != digest)
        return state.Invalid(false, REJECT_INVALID, "bad-txcert-prevouts");
    uint256 quorumHash;
    CBLSPublicKey publicKey;
    if (!SelectTxValidationQuorum(parent, GetTxValidationRequestId(cert, consensus.hashGenesisBlock),
                                  quorumHash, publicKey, state)) return false;
    if (cert.quorumHash != quorumHash ||
        !cert.sig.VerifyInsecure(publicKey, GetTxValidationSignHash(cert, consensus.hashGenesisBlock)))
        return state.Invalid(false, REJECT_INVALID, "bad-txcert-signature");
    return true;
}

bool CheckTxValidationCertificate(const CTxValidationCertificate& cert, const CTransaction& tx,
                                  const CCoinsViewCache& view, const CBlockIndex* parent,
                                  const Consensus::Params& consensus, CValidationState& state)
{
    return CheckTxValidationStatement(cert, tx, view, parent, consensus,
                                      CTxValidationCertificate::POSITIVE, state);
}

static bool CheckBlockTxCertificatesImpl(const CBlock& block, const CCoinsViewCache& view,
                                         const CBlockIndex* parent, const Consensus::Params& consensus,
                                         uint32_t blockFlags, std::set<uint16_t>& certified,
                                         CValidationState& state)
{
    certified.clear();
    if (block.vtx.empty() || block.vtx[0]->nType != TRANSACTION_COINBASE) return true;
    CCbTx payload;
    if (!GetTxPayload(*block.vtx[0], payload))
        return state.Invalid(false, REJECT_INVALID, "bad-cbtx-payload");
    if (payload.nVersion != CCbTx::TX_CERTIFICATE_VERSION) return true;
    if (!IsTxDecouplingActive(parent, consensus))
        return state.Invalid(false, REJECT_INVALID, "bad-txcert-inactive");
    if (!CheckCbTx(*block.vtx[0], parent, state)) return false;
    for (const auto& entry : payload.txCertificates) {
        if (entry.index == 0 || entry.index >= block.vtx.size() || !certified.insert(entry.index).second)
            return state.Invalid(false, REJECT_INVALID, "bad-txcert-index");
        if (entry.certificate.consensusFlags != blockFlags)
            return state.Invalid(false, REJECT_INVALID, "bad-txcert-block-flags");
        if (!CheckTxValidationCertificate(entry.certificate, *block.vtx[entry.index], view,
                                          parent, consensus, state)) return false;
    }
    return true;
}

bool CheckBlockTxCertificates(const CBlock& block, const CCoinsViewCache& view,
                              const CBlockIndex* parent, const Consensus::Params& consensus,
                              uint32_t blockFlags, std::set<uint16_t>& certified,
                              CValidationState& state)
{
    if (CheckBlockTxCertificatesImpl(block, view, parent, consensus, blockFlags, certified, state)) return true;
    // A committed manifest cannot be mutated without changing the block hash, so an
    // invalid certificate makes the whole block invalid. The score reaches peers that
    // deliver full blocks; reconstructed blocks are processed without punishment, as
    // compact blocks are. Relay paths call CheckTxValidationStatement directly.
    int score = 0;
    if (state.IsInvalid(score) && score == 0) {
        const std::string reason = state.GetRejectReason(), debug = state.GetDebugMessage();
        state.DoS(100, false, REJECT_INVALID, reason, false, debug);
    }
    return false;
}
