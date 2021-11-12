// Copyright (c) 2014-2019 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PRIVATESENDUTIL_H
#define PRIVATESENDUTIL_H

#include "wallet/wallet.h"

class CKeyHolder
{
private:
    CReserveKey reserveKey;
    CPubKey pubKey;

public:
    CKeyHolder(CWallet* pwalletIn);
    CKeyHolder(CKeyHolder&&) = delete;
    CKeyHolder& operator=(CKeyHolder&&) = delete;
    void KeepKey();
    void ReturnKey();

    CScript GetScriptForDestination() const;
};

class CKeyHolderStorage
{
private:
    std::vector<std::unique_ptr<CKeyHolder> > storage;
    mutable CCriticalSection cs_storage;

public:
    CScript AddKey(CWallet* pwalletIn);
    void KeepAll();
    void ReturnAll();
};
#endif //PRIVATESENDUTIL_H
