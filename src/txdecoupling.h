// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXDECOUPLING_H
#define BITCOIN_TXDECOUPLING_H

#include <primitives/txcertificate.h>
#include <script/interpreter.h>

#include <set>

class CBlock;
class CBlockIndex;
class CCoinsViewCache;
class CTransaction;
class CValidationState;

// The v1 statement fixes policy semantics independently of future policy changes.
static constexpr uint32_t TX_VALIDATION_POLICY_FLAGS_V1 =
    SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_STRICTENC |
    SCRIPT_VERIFY_MINIMALDATA | SCRIPT_VERIFY_NULLDUMMY |
    SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS | SCRIPT_VERIFY_CLEANSTACK |
    SCRIPT_VERIFY_NULLFAIL | SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY |
    SCRIPT_VERIFY_CHECKSEQUENCEVERIFY | SCRIPT_VERIFY_LOW_S |
    SCRIPT_ENABLE_DIP0020_OPCODES;

uint256 GetTxValidationRequestId(const CTxValidationCertificate& cert, const uint256& genesisHash);
uint256 GetTxValidationMessageHash(const CTxValidationCertificate& cert);
uint256 GetTxValidationSignHash(const CTxValidationCertificate& cert, const uint256& genesisHash);

bool IsTxDecouplingActive(const CBlockIndex* parent, const Consensus::Params& consensus);
// Requires cs_main. No temporary block index is inserted into update caches.
bool GetTxValidationNextScriptFlags(const CBlockIndex* parent, const Consensus::Params& consensus, uint32_t& flags);
bool GetTxValidationPrevoutsDigest(const CTransaction& tx, const CCoinsViewCache& view,
                                  const CBlockIndex* parent, uint256& digest, CValidationState& state);
// Requires cs_main and EvoDB at parent. A different local state is an Error, not an invalid certificate.
bool SelectTxValidationQuorum(const CBlockIndex* parent, const uint256& requestId,
                              uint256& quorumHash, CBLSPublicKey& publicKey, CValidationState& state);

// Checks the signed statement; callers still enforce every non-script transaction rule.
bool CheckTxValidationStatement(const CTxValidationCertificate& cert, const CTransaction& tx,
                                const CCoinsViewCache& view, const CBlockIndex* parent,
                                const Consensus::Params& consensus, uint8_t expectedResult,
                                CValidationState& state);
bool CheckTxValidationCertificate(const CTxValidationCertificate& cert, const CTransaction& tx,
                                  const CCoinsViewCache& view, const CBlockIndex* parent,
                                  const Consensus::Params& consensus, CValidationState& state);
bool CheckBlockTxCertificates(const CBlock& block, const CCoinsViewCache& view,
                              const CBlockIndex* parent, const Consensus::Params& consensus,
                              uint32_t blockFlags, std::set<uint16_t>& certified,
                              CValidationState& state);

#endif // BITCOIN_TXDECOUPLING_H
