// Copyright (c) 2018-2019 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "evodb.h"

CEvoDB* evoDb;

CEvoDBScopedCommitter::CEvoDBScopedCommitter(CEvoDB &_evoDB) :
    evoDB(_evoDB)
{
}

CEvoDBScopedCommitter::~CEvoDBScopedCommitter()
{
    if (!didCommitOrRollback)
        Rollback();
}

void CEvoDBScopedCommitter::Commit()
{
    assert(!didCommitOrRollback);
    didCommitOrRollback = true;
    evoDB.CommitCurTransaction();
}

void CEvoDBScopedCommitter::Rollback()
{
    assert(!didCommitOrRollback);
    didCommitOrRollback = true;
    evoDB.RollbackCurTransaction();
}

CEvoDB::CEvoDB(size_t nCacheSize, bool fMemory, bool fWipe) :
    db(fMemory ? "" : (GetDataDir() / "evodb"), nCacheSize, fMemory, fWipe),
    rootBatch(db),
    rootDBTransaction(db, rootBatch),
    curDBTransaction(rootDBTransaction, rootDBTransaction)
{
}

void CEvoDB::CommitCurTransaction()
{
    LOCK(cs);
    curDBTransaction.Commit();
}

void CEvoDB::RollbackCurTransaction()
{
    LOCK(cs);
    curDBTransaction.Clear();
}

bool CEvoDB::CommitRootTransaction()
{
    assert(curDBTransaction.IsClean());
    rootDBTransaction.Commit();
    bool ret = db.WriteBatch(rootBatch);
    rootBatch.Clear();
    return ret;
}

bool CEvoDB::VerifyBestBlock(const uint256& hash)
{
    // Make sure evodb is consistent.
    // If we already have best block hash saved, the previous block should match it.
    uint256 hashBestBlock;
    bool fHasBestBlock = Read(EVODB_BEST_BLOCK, hashBestBlock);
    uint256 hashBlockIndex = fHasBestBlock ? hash : uint256();
    assert(hashBestBlock == hashBlockIndex);

    return fHasBestBlock || hashBestBlock == uint256();
}

void CEvoDB::WriteBestBlock(const uint256& hash)
{
    Write(EVODB_BEST_BLOCK, hash);
}
