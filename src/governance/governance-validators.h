// Copyright (c) 2014-2019 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_GOVERNANCE_GOVERNANCE_VALIDATORS_H
#define BITCOIN_GOVERNANCE_GOVERNANCE_VALIDATORS_H

#include <string>

#include <univalue.h>

class CProposalValidator
{
private:
    UniValue objJSON;
    bool fJSONValid;
    bool fAllowLegacyFormat;
    std::string strErrorMessages;

public:
    explicit CProposalValidator(const std::string& strDataHexIn = std::string(), bool fAllowLegacyFormat = true);

    bool Validate(bool fCheckExpiration = true);

    const std::string& GetErrorMessages()
    {
        return strErrorMessages;
    }

private:
    void ParseStrHexData(const std::string& strHexData);
    void ParseJSONData(const std::string& strJSONData);

    bool GetDataValue(const std::string& strKey, std::string& strValueRet);
    bool GetDataValue(const std::string& strKey, int64_t& nValueRet);
    bool GetDataValue(const std::string& strKey, double& dValueRet);

    bool ValidateType();
    bool ValidateName();
    bool ValidateStartEndEpoch(bool fCheckExpiration = true);
    bool ValidatePaymentAmount();
    bool ValidatePaymentAddress();
    bool ValidateURL();

    bool CheckURL(const std::string& strURLIn);
};

#endif // BITCOIN_GOVERNANCE_GOVERNANCE_VALIDATORS_H
