// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_PRIMITIVES_TXCERTIFICATE_H
#define RAPTOREUM_PRIMITIVES_TXCERTIFICATE_H

#include <bls/bls.h>
#include <consensus/params.h>
#include <serialize.h>
#include <uint256.h>

class CTxValidationCertificate {
public:
    static constexpr uint16_t CURRENT_VERSION = 1;
    static constexpr uint8_t NEGATIVE = 0;
    static constexpr uint8_t POSITIVE = 1;

    uint16_t version{CURRENT_VERSION};
    uint256 txid;
    uint256 parentHash;
    uint256 prevoutsDigest;
    uint32_t consensusFlags{0};
    uint32_t policyFlags{0};
    Consensus::LLMQType quorumType{Consensus::LLMQ_5_60};
    uint256 quorumHash;
    uint8_t result{POSITIVE};
    CBLSSignature sig;

    SERIALIZE_METHODS(CTxValidationCertificate, obj)
    {
        READWRITE(obj.version, obj.txid, obj.parentHash, obj.prevoutsDigest,
                  obj.consensusFlags, obj.policyFlags, obj.quorumType,
                  obj.quorumHash, obj.result, obj.sig);
    }
};

struct CTxCertificateEntry {
    uint16_t index{0};
    CTxValidationCertificate certificate;

    SERIALIZE_METHODS(CTxCertificateEntry, obj)
    {
        READWRITE(obj.index, obj.certificate);
    }
};

#endif
