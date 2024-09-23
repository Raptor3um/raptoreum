// Copyright (c) 2018-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_EVO_SPECIALTX_H
#define BITCOIN_EVO_SPECIALTX_H

#include <primitives/transaction.h>
#include <streams.h>
#include <sync.h>
#include <threadsafety.h>
#include <version.h>

class CBlock;

class CBlockIndex;

class CCoinsViewCache;

class CValidationState;

class CAssetsCache;

extern RecursiveMutex cs_main;

bool CheckSpecialTx(const CTransaction &tx, const CBlockIndex *pindexPrev, CValidationState &state,
                    const CCoinsViewCache &view, CAssetsCache *assetsCache, bool check_sigs);

bool ProcessSpecialTxsInBlock(const CBlock &block, const CBlockIndex *pindex, CValidationState &state,
                              const CCoinsViewCache &view, CAssetsCache *assetsCache, bool fJustCheck,
                              bool fCheckCbTxMerleRoots)

EXCLUSIVE_LOCKS_REQUIRED(cs_main);

bool UndoSpecialTxsInBlock(const CBlock &block, const CBlockIndex *pindex)

EXCLUSIVE_LOCKS_REQUIRED(cs_main);

template<typename T>
inline bool GetTxPayload(const std::vector<unsigned char> &payload, T &obj) {
    CDataStream ds(payload, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ds >> obj;
    } catch (std::exception &e) {
        return false;
    }
    return ds.empty();
}

template<typename T>
inline bool GetTxPayload(const CMutableTransaction &tx, T &obj) {
    return GetTxPayload(tx.vExtraPayload, obj);
}

template<typename T>
inline bool GetTxPayload(const CTransaction &tx, T &obj) {
    return GetTxPayload(tx.vExtraPayload, obj);
}

template<typename T>
void SetTxPayload(CMutableTransaction &tx, const T &payload) {
    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << payload;
    tx.vExtraPayload.assign(ds.begin(), ds.end());
}

uint256 CalcTxInputsHash(const CTransaction &tx);

#endif // BITCOIN_EVO_SPECIALTX_H
