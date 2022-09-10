// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SMARTNODE_MASTERNODE_UTILS_H
#define BITCOIN_SMARTNODE_MASTERNODE_UTILS_H

#include <evo/deterministicmns.h>

class CConnman;

class CSmartnodeUtils
{
public:
    static void ProcessSmartnodeConnections(CConnman& connman);
    static void DoMaintenance(CConnman &connman);
};

#endif // BITCOIN_SMARTNODE_MASTERNODE_UTILS_H
