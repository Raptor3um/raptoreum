// Copyright (c) 2014-2019 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SMARTNODE_SMARTNODE_PAYMENTS_H
#define BITCOIN_SMARTNODE_SMARTNODE_PAYMENTS_H

#include <amount.h>

#include <string>
#include <vector>
#include <map>

class CSmartnodePayments;

class CBlock;

class CTransaction;

struct CMutableTransaction;

class CTxOut;

/// TODO: all 4 functions do not belong here really, they should be refactored/moved somewhere (main.cpp ?)
bool IsBlockValueValid(const CBlock &block, int nBlockHeight, CAmount blockReward, std::string &strErrorRet);

bool IsBlockPayeeValid(const CTransaction &txNew, int nBlockHeight, CAmount blockReward, CAmount specialTxFees);

void FillBlockPayments(CMutableTransaction &txNew, int nBlockHeight, CAmount blockReward,
                       std::vector <CTxOut> &voutSmartnodePaymentsRet, std::vector <CTxOut> &voutSuperblockPaymentsRet,
                       CAmount specialTxFees = 0);

std::map<int, std::string> GetRequiredPaymentsStrings(int nStartHeight, int nEndHeight);

extern CSmartnodePayments mnpayments;

//
// Smartnode Payments Class
// Keeps track of who should get paid for which blocks
//

class CSmartnodePayments {
public:
    static bool GetBlockTxOuts(int nBlockHeight, CAmount blockReward, std::vector <CTxOut> &voutSmartnodePaymentsRet,
                               CAmount specialTxFee);

    static bool
    IsTransactionValid(const CTransaction &txNew, int nBlockHeight, CAmount blockReward, CAmount specialTxFee);

    static bool
    GetSmartnodeTxOuts(int nBlockHeight, CAmount blockReward, std::vector <CTxOut> &voutSmartnodePaymentsRet,
                       CAmount specialTxFee);
};

#endif // BITCOIN_SMARTNODE_SMARTNODE_PAYMENTS_H
