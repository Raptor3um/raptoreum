// Copyright (c) 2009-2010 Satoshi Nakamowo
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c)      2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php

#ifndef BITCOIN_SHUTDOWN_H
#define BITCOIN_SHUTDOWN_H

bool InitShutdownState();

void StartShutdown();

void StartRestart();

void AbortShutdown();

bool ShutdownRequested();

void WaitForShutdown();

bool RestartRequested();

#endif