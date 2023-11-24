// Copyright (c) 2018-2019 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <base58.h>
#include <chainparams.h>
#include <index/txindex.h>
#include <keystore.h>
#include <messagesigner.h>
#include <netbase.h>
#include <policy/policy.h>
#include <script/interpreter.h>
#include <script/sign.h>
#include <script/standard.h>
#include <spork.h>
#include <txmempool.h>
#include <validation.h>

#include <assets/assets.h>
#include <assets/assetstype.h>
#include <core_io.h>
#include <evo/providertx.h>
#include <evo/specialtx.h>

#include <boost/test/unit_test.hpp>

using SimpleUTXOMap = std::map<COutPoint, std::pair<int, CAmount>>;

static SimpleUTXOMap BuildSimpleUtxoMap(const std::vector<CTransactionRef>& txs)
{
    SimpleUTXOMap utxos;
    for (size_t i = 0; i < txs.size(); i++) {
        auto& tx = txs[i];
        for (size_t j = 0; j < tx->vout.size(); j++) {
            if (tx->vout[j].nValue > 0)
                utxos.emplace(COutPoint(tx->GetHash(), j), std::make_pair((int)i + 1, tx->vout[j].nValue));
        }
    }
    return utxos;
}

static std::vector<COutPoint> SelectUTXOs(SimpleUTXOMap& utoxs, CAmount amount, CAmount& changeRet)
{
    changeRet = 0;

    std::vector<COutPoint> selectedUtxos;
    CAmount selectedAmount = 0;
    while (!utoxs.empty()) {
        bool found = false;
        for (auto it = utoxs.begin(); it != utoxs.end(); ++it) {
            if (::ChainActive().Height() - it->second.first < 101) {
                continue;
            }

            found = true;
            selectedAmount += it->second.second;
            selectedUtxos.emplace_back(it->first);
            utoxs.erase(it);
            break;
        }
        BOOST_ASSERT(found);
        if (selectedAmount >= amount) {
            changeRet = selectedAmount - amount;
            break;
        }
    }

    return selectedUtxos;
}

static void FundTransaction(CMutableTransaction& tx, SimpleUTXOMap& utoxs, const CScript& scriptPayout, CAmount amount, const CKey& coinbaseKey)
{
    CAmount change;
    auto inputs = SelectUTXOs(utoxs, amount, change);
    for (size_t i = 0; i < inputs.size(); i++) {
        tx.vin.emplace_back(CTxIn(inputs[i]));
    }
    if (change != 0) {
        tx.vout.emplace_back(CTxOut(change, scriptPayout));
    }
}

static bool SignTransaction(const CTxMemPool& mempool, CMutableTransaction& tx, const CKey& coinbaseKey)
{
    CBasicKeyStore tempKeystore;
    tempKeystore.AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey());

    for (size_t i = 0; i < tx.vin.size(); i++) {
        uint256 hashBlock;
        CTransactionRef txFrom = GetTransaction(/* block_index */ nullptr, &mempool, tx.vin[i].prevout.hash,
            Params().GetConsensus(), hashBlock);
        BOOST_CHECK_MESSAGE(txFrom, "SignTransaction: GetTransaction");
        if (!txFrom)
            return false;
        bool ret = SignSignature(tempKeystore, *txFrom, tx, i, SIGHASH_ALL);
        BOOST_CHECK_MESSAGE(ret, "SignTransaction: SignSignature");
        if (!ret)
           return false;
    }
    return true;
}

static CMutableTransaction
CreateNewAssetTx(const CTxMemPool& mempool, SimpleUTXOMap& utxos, const CKey& coinbaseKey, std::string name, bool updatable, bool is_unique, uint8_t type, uint8_t decimalPoint, CAmount amount)
{
    CKeyID ownerKey = coinbaseKey.GetPubKey().GetID();
    CNewAssetTx newAsset;

    newAsset.name = name;
    newAsset.updatable = updatable;
    newAsset.isUnique = is_unique;
    newAsset.decimalPoint = decimalPoint;
    newAsset.referenceHash = "";
    newAsset.type = type;
    newAsset.maxMintCount = 10;
    newAsset.fee = getAssetsFees();
    newAsset.targetAddress = ownerKey;
    newAsset.ownerAddress = ownerKey;
    newAsset.amount = amount * COIN;
    //newAsset.collateralAddress = CKey();

    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_NEW_ASSET;
    FundTransaction(tx, utxos, GetScriptForDestination(coinbaseKey.GetPubKey().GetID()), newAsset.fee * COIN + 1 * COIN,
        coinbaseKey);
    newAsset.inputsHash = CalcTxInputsHash(tx);
    SetTxPayload(tx, newAsset);
    BOOST_ASSERT(SignTransaction(mempool, tx, coinbaseKey));

    return tx;
}

static CMutableTransaction
CreateUpdateAssetTx(const CTxMemPool& mempool, SimpleUTXOMap& utxos, const CKey& coinbaseKey, const CKey& newowner, std::string assetId, bool updatable, uint8_t type, CAmount amount)
{
    CKeyID ownerKey = newowner.GetPubKey().GetID();
    CUpdateAssetTx upAsset;

    CAssetMetaData asset;
    passetsCache->GetAssetMetaData(assetId, asset);

    upAsset.assetId = assetId;
    upAsset.updatable = updatable;
    upAsset.referenceHash = "";
    upAsset.fee = getAssetsFees();
    upAsset.type = type;
    upAsset.targetAddress = asset.targetAddress;
    upAsset.issueFrequency = asset.issueFrequency;
    upAsset.maxMintCount = asset.maxMintCount;
    upAsset.amount = amount * COIN;
    upAsset.ownerAddress = ownerKey;
    upAsset.collateralAddress = asset.collateralAddress;

    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_UPDATE_ASSET;
    FundTransaction(tx, utxos, GetScriptForDestination(coinbaseKey.GetPubKey().GetID()), upAsset.fee * COIN + 1 * COIN,
        coinbaseKey);
    upAsset.inputsHash = CalcTxInputsHash(tx);
    SetTxPayload(tx, upAsset);
    BOOST_ASSERT(SignTransaction(mempool, tx, coinbaseKey));

    return tx;
}

static CMutableTransaction
CreateMintAssetTx(const CTxMemPool& mempool, SimpleUTXOMap& utxos, const CKey& coinbaseKey, std::string assetId)
{
    CKeyID ownerKey = coinbaseKey.GetPubKey().GetID();
    CMintAssetTx mint;

    CAssetMetaData asset;
    BOOST_ASSERT(passetsCache->GetAssetMetaData(assetId, asset));

    mint.assetId = assetId;
    mint.fee = getAssetsFees();

    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_MINT_ASSET;


    if (asset.isUnique) {
        CScript scriptPubKey = GetScriptForDestination(asset.targetAddress);
        CAssetTransfer assetTransfer(asset.assetId, asset.amount, 0);
        assetTransfer.BuildAssetTransaction(scriptPubKey);
        CTxOut out(0, scriptPubKey);
        tx.vout.push_back(out);
    } else {
        CScript scriptPubKey = GetScriptForDestination(asset.targetAddress);
        CAssetTransfer assetTransfer(assetId, asset.amount);
        assetTransfer.BuildAssetTransaction(scriptPubKey);
        CTxOut out(0, scriptPubKey);
        tx.vout.push_back(out);
    }

    FundTransaction(tx, utxos, GetScriptForDestination(coinbaseKey.GetPubKey().GetID()), mint.fee * COIN + 1 * COIN,
        coinbaseKey);
    mint.inputsHash = CalcTxInputsHash(tx);
    SetTxPayload(tx, mint);
    BOOST_ASSERT(SignTransaction(mempool, tx, coinbaseKey));

    return tx;
}


static CScript GenerateRandomAddress()
{
    CKey key;
    key.MakeNewKey(false);
    return GetScriptForDestination(key.GetPubKey().GetID());
}

BOOST_AUTO_TEST_SUITE(assets_creation_tests)

BOOST_FIXTURE_TEST_CASE(assets_creation, TestChainDIP3BeforeActivationSetup)
{
    CKey sporkKey;
    sporkKey.MakeNewKey(false);
    sporkManager.SetSporkAddress(EncodeDestination(sporkKey.GetPubKey().GetID()));
    sporkManager.SetPrivKey(EncodeSecret(sporkKey));
    sporkManager.UpdateSpork(SPORK_22_SPECIAL_TX_FEE, 2560, *m_node.connman);

    auto utxos = BuildSimpleUtxoMap(m_coinbase_txns);

    auto tx = CreateNewAssetTx(*m_node.mempool, utxos, coinbaseKey, "Test Asset", true, false, 0, 8, 1000);
    std::vector<CMutableTransaction> txns = {tx};

    int nHeight = ::ChainActive().Height();

    // Mining a block with a asset create transaction
    auto block = std::make_shared<CBlock>(CreateBlock(txns, coinbaseKey));
    EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

    BOOST_ASSERT(::ChainActive().Height() == nHeight + 1);
    BOOST_ASSERT(block->GetHash() == ::ChainActive().Tip()->GetBlockHash());

    //invalid asset name
    tx = CreateNewAssetTx(*m_node.mempool, utxos, coinbaseKey, "*Test_Asset*", true, false, 0, 8, 1000);
    txns = {tx};
    block = std::make_shared<CBlock>(CreateBlock(txns, coinbaseKey));
    //block should be rejected
    EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

    BOOST_ASSERT(::ChainActive().Height() == nHeight + 1);

    //invalid distribution type
    tx = CreateNewAssetTx(*m_node.mempool, utxos, coinbaseKey, "Test Asset", true, false, 5, 8, 1000);
    txns = {tx};
    block = std::make_shared<CBlock>(CreateBlock(txns, coinbaseKey));
    //block should be rejected
    EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

    BOOST_ASSERT(::ChainActive().Height() == nHeight + 1);

    //invalid decimalPoint
    tx = CreateNewAssetTx(*m_node.mempool, utxos, coinbaseKey, "Test Asset", true, false, 0, 9, 1000);
    txns = {tx};
    block = std::make_shared<CBlock>(CreateBlock(txns, coinbaseKey));
    //block should be rejected
    EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

    BOOST_ASSERT(::ChainActive().Height() == nHeight + 1);
}

BOOST_FIXTURE_TEST_CASE(assets_update, TestChainDIP3BeforeActivationSetup)
{
    CKey sporkKey;
    sporkKey.MakeNewKey(false);
    sporkManager.SetSporkAddress(EncodeDestination(sporkKey.GetPubKey().GetID()));
    sporkManager.SetPrivKey(EncodeSecret(sporkKey));
    sporkManager.UpdateSpork(SPORK_22_SPECIAL_TX_FEE,
        2560, *m_node.connman);

    auto utxos = BuildSimpleUtxoMap(m_coinbase_txns);

    auto tx = CreateNewAssetTx(*m_node.mempool, utxos, coinbaseKey, "Test Asset", true, false, 0, 8, 1000);
    std::vector<CMutableTransaction> txns = {tx};

    int nHeight = ::ChainActive().Height();

    // Mining a block with a asset create transaction
    {
        auto block = std::make_shared<CBlock>(CreateBlock(txns, coinbaseKey));
        EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(::ChainActive().Height() == nHeight + 1);
        BOOST_ASSERT(block->GetHash() == ::ChainActive().Tip()->GetBlockHash());
    }

    CAssetMetaData asset;
    BOOST_ASSERT(passetsCache->GetAssetMetaData(tx.GetHash().ToString(), asset));

    //change asset owner
    CKey key;
    key.MakeNewKey(false);
    std::string assetId = tx.GetHash().ToString();
    tx = CreateUpdateAssetTx(*m_node.mempool, utxos, coinbaseKey, key, assetId, true, 0, 1000);
    {
        auto block = std::make_shared<CBlock>(CreateBlock({tx}, coinbaseKey));
        EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(::ChainActive().Height() == nHeight + 2);
        BOOST_ASSERT(block->GetHash() == ::ChainActive().Tip()->GetBlockHash());
    }

    BOOST_ASSERT(passetsCache->GetAssetMetaData(assetId, asset));
    BOOST_ASSERT(asset.ownerAddress == key.GetPubKey().GetID());

    //any atemp to update with the coinbaseKey should fail
    tx = CreateUpdateAssetTx(*m_node.mempool, utxos, coinbaseKey, coinbaseKey, assetId, true, 0, 10000);
    {
        auto block = std::make_shared<CBlock>(CreateBlock({tx}, coinbaseKey));
        EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(::ChainActive().Height() == nHeight + 2);
        BOOST_ASSERT(block->GetHash() != ::ChainActive().Tip()->GetBlockHash());
    }
}

BOOST_FIXTURE_TEST_CASE(assets_mint, TestChainDIP3BeforeActivationSetup)
{
    CKey sporkKey;
    sporkKey.MakeNewKey(false);
    sporkManager.SetSporkAddress(EncodeDestination(sporkKey.GetPubKey().GetID()));
    sporkManager.SetPrivKey(EncodeSecret(sporkKey));
    sporkManager.UpdateSpork(SPORK_22_SPECIAL_TX_FEE, 2560, *m_node.connman);

    auto utxos = BuildSimpleUtxoMap(m_coinbase_txns);

    auto tx = CreateNewAssetTx(*m_node.mempool, utxos, coinbaseKey, "Test Asset", true, false, 0, 8, 1000);
    std::vector<CMutableTransaction> txns = {tx};

    int nHeight = ::ChainActive().Height();

    // Mining a block with a asset create transaction
    {
        auto block = std::make_shared<CBlock>(CreateBlock(txns, coinbaseKey));
        EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(::ChainActive().Height() == nHeight + 1);
        BOOST_ASSERT(block->GetHash() == ::ChainActive().Tip()->GetBlockHash());
    }

    std::string assetId = tx.GetHash().ToString();
    tx = CreateMintAssetTx(*m_node.mempool, utxos, coinbaseKey, assetId);

    {
        auto block = std::make_shared<CBlock>(CreateBlock({tx}, coinbaseKey));
        EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(::ChainActive().Height() == nHeight + 2);
        BOOST_ASSERT(block->GetHash() == ::ChainActive().Tip()->GetBlockHash());
    }

    CAssetMetaData asset;
    BOOST_ASSERT(passetsCache->GetAssetMetaData(assetId, asset));

    BOOST_ASSERT(asset.circulatingSupply == 1000);

    // Allow TX index to catch up with the block index.
    g_txindex->BlockUntilSyncedToCurrentChain();

    //transfer asset
    CMutableTransaction tx2;

    CKey key;
    key.MakeNewKey(false);
    CScript scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
    CAssetTransfer assetTransfer(assetId, 100 * COIN);
    assetTransfer.BuildAssetTransaction(scriptPubKey);
    CTxOut out(0, scriptPubKey);
    tx2.vout.push_back(out);

    //change
    CScript scriptPubKey2 = GetScriptForDestination(coinbaseKey.GetPubKey().GetID());
    CAssetTransfer assetTransfer2(assetId, 900 * COIN);
    assetTransfer2.BuildAssetTransaction(scriptPubKey2);
    CTxOut out2(0, scriptPubKey2);
    tx2.vout.push_back(out2);
    tx2.vin.push_back(CTxIn(COutPoint(tx.GetHash(), 0)));

    FundTransaction(tx2, utxos, GetScriptForDestination(coinbaseKey.GetPubKey().GetID()), 1 * COIN, coinbaseKey);
    BOOST_ASSERT(SignTransaction(*m_node.mempool, tx2, coinbaseKey));

    {
        auto block = std::make_shared<CBlock>(CreateBlock({tx2}, coinbaseKey));
        EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(::ChainActive().Height() == nHeight + 3);
        BOOST_ASSERT(block->GetHash() == ::ChainActive().Tip()->GetBlockHash());
    }
}

BOOST_FIXTURE_TEST_CASE(assets_invalid_cases, TestChainDIP3BeforeActivationSetup)
{
    CKey sporkKey;
    sporkKey.MakeNewKey(false);
    sporkManager.SetSporkAddress(EncodeDestination(sporkKey.GetPubKey().GetID()));
    sporkManager.SetPrivKey(EncodeSecret(sporkKey));
    sporkManager.UpdateSpork(SPORK_22_SPECIAL_TX_FEE, 2560, *m_node.connman);

    auto utxos = BuildSimpleUtxoMap(m_coinbase_txns);

    //create a asset
    auto tx = CreateNewAssetTx(*m_node.mempool, utxos, coinbaseKey, "Test Asset", false, false, 0, 2, 100);
    std::vector<CMutableTransaction> txns = {tx};

    int nHeight = ::ChainActive().Height();

    // Mining a block with a asset create transaction
    {
        auto block = std::make_shared<CBlock>(CreateBlock(txns, coinbaseKey));
        EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(::ChainActive().Height() == nHeight + 1);
        BOOST_ASSERT(block->GetHash() == ::ChainActive().Tip()->GetBlockHash());
    }

    std::string assetId = tx.GetHash().ToString();

    tx = CreateMintAssetTx(*m_node.mempool, utxos, coinbaseKey, assetId);

    {
        auto block = std::make_shared<CBlock>(CreateBlock({tx}, coinbaseKey));
        EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(::ChainActive().Height() == nHeight + 2);
        BOOST_ASSERT(block->GetHash() == ::ChainActive().Tip()->GetBlockHash());
    }

    CAssetMetaData asset;
    BOOST_ASSERT(passetsCache->GetAssetMetaData(assetId, asset));
    BOOST_ASSERT(asset.circulatingSupply == 100);

    // Allow TX index to catch up with the block index.
    g_txindex->BlockUntilSyncedToCurrentChain();

    {
        //bad amount, decimalPoint = 2
        CMutableTransaction tx2;

        CKey key;
        key.MakeNewKey(false);
        CScript scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        CAssetTransfer assetTransfer(assetId, 12.1234 * COIN);
        assetTransfer.BuildAssetTransaction(scriptPubKey);
        CTxOut out(0, scriptPubKey);
        tx2.vout.push_back(out);
        //change
        CScript scriptPubKey2 = GetScriptForDestination(coinbaseKey.GetPubKey().GetID());
        CAssetTransfer assetTransfer2(assetId, 87.8766 * COIN);
        assetTransfer2.BuildAssetTransaction(scriptPubKey2);
        CTxOut out2(0, scriptPubKey2);
        tx2.vout.push_back(out2);

        tx2.vin.push_back(CTxIn(COutPoint(tx.GetHash(), 0)));

        FundTransaction(tx2, utxos, GetScriptForDestination(coinbaseKey.GetPubKey().GetID()), 1 * COIN, coinbaseKey);
        BOOST_ASSERT(SignTransaction(*m_node.mempool, tx2, coinbaseKey));

        auto block = std::make_shared<CBlock>(CreateBlock({tx2}, coinbaseKey));
        EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(::ChainActive().Height() == nHeight + 2);
        BOOST_ASSERT(block->GetHash() != ::ChainActive().Tip()->GetBlockHash());
    }

    {
        //input-output mismatch
        CMutableTransaction tx2;

        CKey key;
        key.MakeNewKey(false);
        CScript scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        CAssetTransfer assetTransfer(assetId, 12.12 * COIN);
        assetTransfer.BuildAssetTransaction(scriptPubKey);
        CTxOut out(0, scriptPubKey);
        tx2.vout.push_back(out);
        //change
        CScript scriptPubKey2 = GetScriptForDestination(coinbaseKey.GetPubKey().GetID());
        CAssetTransfer assetTransfer2(assetId, 88.88 * COIN);
        assetTransfer2.BuildAssetTransaction(scriptPubKey2);
        CTxOut out2(0, scriptPubKey2);
        tx2.vout.push_back(out2);

        tx2.vin.push_back(CTxIn(COutPoint(tx.GetHash(), 0)));

        FundTransaction(tx2, utxos, GetScriptForDestination(coinbaseKey.GetPubKey().GetID()), 1 * COIN, coinbaseKey);
        BOOST_ASSERT(SignTransaction(*m_node.mempool, tx2, coinbaseKey));

        auto block = std::make_shared<CBlock>(CreateBlock({tx2}, coinbaseKey));
        EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(::ChainActive().Height() == nHeight + 2);
        BOOST_ASSERT(block->GetHash() != ::ChainActive().Tip()->GetBlockHash());
    }

    {
        //bad native asset amount
        CMutableTransaction tx2;

        CKey key;
        key.MakeNewKey(false);
        CScript scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        CAssetTransfer assetTransfer(assetId, 12.12 * COIN);
        assetTransfer.BuildAssetTransaction(scriptPubKey);
        CTxOut out(1 * COIN, scriptPubKey);
        tx2.vout.push_back(out);
        //change
        CScript scriptPubKey2 = GetScriptForDestination(coinbaseKey.GetPubKey().GetID());
        CAssetTransfer assetTransfer2(assetId, 87.88 * COIN);
        assetTransfer2.BuildAssetTransaction(scriptPubKey2);
        CTxOut out2(0, scriptPubKey2);
        tx2.vout.push_back(out2);

        tx2.vin.push_back(CTxIn(COutPoint(tx.GetHash(), 0)));

        FundTransaction(tx2, utxos, GetScriptForDestination(coinbaseKey.GetPubKey().GetID()), 1 * COIN, coinbaseKey);
        BOOST_ASSERT(SignTransaction(*m_node.mempool, tx2, coinbaseKey));

        auto block = std::make_shared<CBlock>(CreateBlock({tx2}, coinbaseKey));
        EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(::ChainActive().Height() == nHeight + 2);
        BOOST_ASSERT(block->GetHash() != ::ChainActive().Tip()->GetBlockHash());
    }

    //create a unique asset
    tx = CreateNewAssetTx(*m_node.mempool, utxos, coinbaseKey, "Unique Asset", false, true, 0, 0, 10);
    txns = {tx};

    nHeight = ::ChainActive().Height();

    // Mining a block with a asset create transaction
    {
        auto block = std::make_shared<CBlock>(CreateBlock(txns, coinbaseKey));
        EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(::ChainActive().Height() == nHeight + 1);
        BOOST_ASSERT(block->GetHash() == ::ChainActive().Tip()->GetBlockHash());
    }

    assetId = tx.GetHash().ToString();
    tx = CreateMintAssetTx(*m_node.mempool, utxos, coinbaseKey, assetId);

    {
        auto block = std::make_shared<CBlock>(CreateBlock({tx}, coinbaseKey));
        EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(::ChainActive().Height() == nHeight + 2);
        BOOST_ASSERT(block->GetHash() == ::ChainActive().Tip()->GetBlockHash());
    }

    BOOST_ASSERT(passetsCache->GetAssetMetaData(assetId, asset));

    BOOST_ASSERT(asset.circulatingSupply == 10);
    // Allow TX index to catch up with the block index.
    g_txindex->BlockUntilSyncedToCurrentChain();

    {
        //mismatch uniqueid
        CMutableTransaction tx2;

        CKey key;
        key.MakeNewKey(false);
        CScript scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        CAssetTransfer assetTransfer(assetId, 2 * COIN, 0);
        assetTransfer.BuildAssetTransaction(scriptPubKey);
        CTxOut out(0, scriptPubKey);
        tx2.vout.push_back(out);
        CAssetTransfer assetTransfer2(assetId, 8 * COIN, 0);
        assetTransfer.BuildAssetTransaction(scriptPubKey);
        CTxOut out2(0, scriptPubKey);
        tx2.vout.push_back(out2);

        tx2.vin.push_back(CTxIn(COutPoint(tx.GetHash(), 0)));

        FundTransaction(tx2, utxos, GetScriptForDestination(coinbaseKey.GetPubKey().GetID()), 1 * COIN, coinbaseKey);
        BOOST_ASSERT(SignTransaction(*m_node.mempool, tx2, coinbaseKey));

        auto block = std::make_shared<CBlock>(CreateBlock({tx2}, coinbaseKey));
        EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(::ChainActive().Height() == nHeight + 2);
        BOOST_ASSERT(block->GetHash() != ::ChainActive().Tip()->GetBlockHash());
    }

    {
        //amount mismatch
        CMutableTransaction tx2;

        CKey key;
        key.MakeNewKey(false);
        CScript scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        CAssetTransfer assetTransfer(assetId, 1 * COIN, 0); //uniqueId=0
        assetTransfer.BuildAssetTransaction(scriptPubKey);
        CTxOut out(0, scriptPubKey);
        tx2.vout.push_back(out);

        CScript scriptPubKey2 = GetScriptForDestination(key.GetPubKey().GetID());
        CAssetTransfer assetTransfer2(assetId, 1 * COIN, 1); ////uniqueId=1
        assetTransfer.BuildAssetTransaction(scriptPubKey2);
        CTxOut out2(0, scriptPubKey2);
        tx2.vout.push_back(out2);

        tx2.vin.push_back(CTxIn(COutPoint(tx.GetHash(), 0))); //uniqueId=0


        FundTransaction(tx2, utxos, GetScriptForDestination(coinbaseKey.GetPubKey().GetID()), 1 * COIN, coinbaseKey);
        BOOST_ASSERT(SignTransaction(*m_node.mempool, tx2, coinbaseKey));

        auto block = std::make_shared<CBlock>(CreateBlock({tx2}, coinbaseKey));
        EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(::ChainActive().Height() == nHeight + 2);
        BOOST_ASSERT(block->GetHash() != ::ChainActive().Tip()->GetBlockHash());
    }

    {
        //native asset amount != 0
        CMutableTransaction tx2;

        CKey key;
        key.MakeNewKey(false);
        CScript scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        CAssetTransfer assetTransfer(assetId, 10 * COIN, 0);
        assetTransfer.BuildAssetTransaction(scriptPubKey);
        CTxOut out(1, scriptPubKey);
        tx2.vout.push_back(out);

        tx2.vin.push_back(CTxIn(COutPoint(tx.GetHash(), 0)));

        FundTransaction(tx2, utxos, GetScriptForDestination(coinbaseKey.GetPubKey().GetID()), 1 * COIN, coinbaseKey);
        BOOST_ASSERT(SignTransaction(*m_node.mempool, tx2, coinbaseKey));

        auto block = std::make_shared<CBlock>(CreateBlock({tx2}, coinbaseKey));
        EnsureChainman(m_node).ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(::ChainActive().Height() == nHeight + 2);
        BOOST_ASSERT(block->GetHash() != ::ChainActive().Tip()->GetBlockHash());
    }
}

BOOST_AUTO_TEST_SUITE_END()
