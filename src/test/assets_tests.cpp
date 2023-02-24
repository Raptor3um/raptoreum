// Copyright (c) 2018-2019 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <script/interpreter.h>
#include <script/standard.h>
#include <script/sign.h>
#include <validation.h>
#include <base58.h>
#include <netbase.h>
#include <messagesigner.h>
#include <policy/policy.h>
#include <keystore.h>
#include <spork.h>
#include <txmempool.h>

#include <evo/specialtx.h>
#include <evo/providertx.h>
#include <assets/assets.h>
#include <core_io.h>

#include <boost/test/unit_test.hpp>

typedef std::map<COutPoint, std::pair<int, CAmount>> SimpleUTXOMap;

static SimpleUTXOMap BuildSimpleUtxoMap(const std::vector<CTransaction>& txs)
{
    SimpleUTXOMap utxos;
    for (size_t i = 0; i < txs.size(); i++) {
        auto& tx = txs[i];
        for (size_t j = 0; j < tx.vout.size(); j++) {
            if(tx.vout[j].nValue > 0)
            utxos.emplace(COutPoint(tx.GetHash(), j), std::make_pair((int)i + 1, tx.vout[j].nValue));
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
            if (chainActive.Height() - it->second.first < 101) {
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

static void SignTransaction(CMutableTransaction& tx, const CKey& coinbaseKey)
{
    CBasicKeyStore tempKeystore;
    tempKeystore.AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey());

    for (size_t i = 0; i < tx.vin.size(); i++) {
        CTransactionRef txFrom;
        uint256 hashBlock;
        BOOST_ASSERT(GetTransaction(tx.vin[i].prevout.hash, txFrom, Params().GetConsensus(), hashBlock));
        BOOST_ASSERT(SignSignature(tempKeystore, *txFrom, tx, i, SIGHASH_ALL));
    }
}

static CMutableTransaction CreateNewAssetTx(SimpleUTXOMap& utxos, const CKey& coinbaseKey, std::string name, bool updatable, bool is_unique, uint8_t type, uint8_t decimalPoint, CAmount amount)
{
    CKeyID ownerKey = coinbaseKey.GetPubKey().GetID();
    CNewAssetTx newasset;

    newasset.Name = name;
    newasset.updatable = updatable;
    newasset.isUnique = is_unique;
    newasset.decimalPoint = decimalPoint;
    newasset.referenceHash = "";
    newasset.type = type;
    newasset.fee = getAssetsFees();
    newasset.targetAddress = ownerKey;
    newasset.ownerAddress = ownerKey;
    newasset.Amount = amount * COIN;
    //newasset.collateralAddress = CKey();

    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_NEW_ASSET;
    FundTransaction(tx, utxos, GetScriptForDestination(coinbaseKey.GetPubKey().GetID()), newasset.fee * COIN + 1 * COIN, coinbaseKey);
    newasset.inputsHash = CalcTxInputsHash(tx);
    SetTxPayload(tx, newasset);
    SignTransaction(tx, coinbaseKey);

    return tx;
}

static CMutableTransaction CreateUpdateAssetTx(SimpleUTXOMap& utxos, const CKey& coinbaseKey, const CKey& newowner, std::string assetId, bool updatable, uint8_t type, CAmount amount)
{
    CKeyID ownerKey = newowner.GetPubKey().GetID();
    CUpdateAssetTx upasset;

    CAssetMetaData asset;
    GetAssetMetaData(assetId, asset);
    
    upasset.AssetId = assetId;
    upasset.updatable = updatable;
    upasset.referenceHash = "";
    upasset.fee = getAssetsFees();
    upasset.type = type;
    upasset.targetAddress = asset.targetAddress;
    upasset.issueFrequency = asset.issueFrequency;
    upasset.Amount = amount * COIN;
    upasset.ownerAddress = ownerKey;
    upasset.collateralAddress = asset.collateralAddress;
    
    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_UPDATE_ASSET;
    FundTransaction(tx, utxos, GetScriptForDestination(coinbaseKey.GetPubKey().GetID()), upasset.fee * COIN + 1 * COIN, coinbaseKey);
    upasset.inputsHash = CalcTxInputsHash(tx);
    SetTxPayload(tx, upasset);
    SignTransaction(tx, coinbaseKey);

    return tx;
}

static CMutableTransaction CreateMintAssetTx(SimpleUTXOMap& utxos, const CKey& coinbaseKey, std::string assetId)
{
    CKeyID ownerKey = coinbaseKey.GetPubKey().GetID();
    CMintAssetTx mint;

    CAssetMetaData asset;
    GetAssetMetaData(assetId, asset);
    
    mint.AssetId = assetId;
    mint.fee = getAssetsFees();
    
    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_MINT_ASSET;

    CScript scriptPubKey = GetScriptForDestination(asset.targetAddress);
    CAssetTransfer assetTransfer(assetId, asset.Amount);
    assetTransfer.BuildAssetTransaction(scriptPubKey);
    CTxOut out(0 , scriptPubKey);
    tx.vout.push_back(out);

    FundTransaction(tx, utxos, GetScriptForDestination(coinbaseKey.GetPubKey().GetID()), mint.fee * COIN + 1 * COIN, coinbaseKey);
    mint.inputsHash = CalcTxInputsHash(tx);
    SetTxPayload(tx, mint);
    SignTransaction(tx, coinbaseKey);

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
    sporkManager.UpdateSpork(SPORK_22_SPECIAL_TX_FEE, 2560, *g_connman);

    auto utxos = BuildSimpleUtxoMap(coinbaseTxns);

    auto tx = CreateNewAssetTx(utxos, coinbaseKey,"Test Asset", true, false, 0, 8, 1000);
    std::vector<CMutableTransaction> txns = {tx};

    int nHeight = chainActive.Height();

    // Mining a block with a asset create transaction
    auto block = std::make_shared<CBlock>(CreateBlock(txns, coinbaseKey));
    ProcessNewBlock(Params(), block, true, nullptr);

    BOOST_ASSERT(chainActive.Height() == nHeight + 1);
    BOOST_ASSERT(block->GetHash() == chainActive.Tip()->GetBlockHash());
    
    //invalid asset name
    tx = CreateNewAssetTx(utxos, coinbaseKey,"*Test_Asset*", true, false, 0, 8, 1000);
    txns = {tx};
    block = std::make_shared<CBlock>(CreateBlock(txns, coinbaseKey));
    //block should be rejected
    ProcessNewBlock(Params(), block, true, nullptr);
    BOOST_ASSERT(chainActive.Height() == nHeight + 1);

    //invalid distribution type
    tx = CreateNewAssetTx(utxos, coinbaseKey,"Test Asset", true, false, 5, 8, 1000);
    txns = {tx};
    block = std::make_shared<CBlock>(CreateBlock(txns, coinbaseKey));
    //block should be rejected
    ProcessNewBlock(Params(), block, true, nullptr);
    BOOST_ASSERT(chainActive.Height() == nHeight + 1);

   //invalid decimalPoint
    tx = CreateNewAssetTx(utxos, coinbaseKey,"Test Asset", true, false, 0, 9, 1000);
    txns = {tx};
    block = std::make_shared<CBlock>(CreateBlock(txns, coinbaseKey));
    //block should be rejected
    ProcessNewBlock(Params(), block, true, nullptr);
    BOOST_ASSERT(chainActive.Height() == nHeight + 1);
}

BOOST_FIXTURE_TEST_CASE(assets_update, TestChainDIP3BeforeActivationSetup)
{
    CKey sporkKey;
    sporkKey.MakeNewKey(false);
    sporkManager.SetSporkAddress(EncodeDestination(sporkKey.GetPubKey().GetID()));
    sporkManager.SetPrivKey(EncodeSecret(sporkKey));
    sporkManager.UpdateSpork(SPORK_22_SPECIAL_TX_FEE, 2560, *g_connman);

    auto utxos = BuildSimpleUtxoMap(coinbaseTxns);

    auto tx = CreateNewAssetTx(utxos, coinbaseKey,"Test Asset", true, false, 0, 8, 1000);
    std::vector<CMutableTransaction> txns = {tx};

    int nHeight = chainActive.Height();

    // Mining a block with a asset create transaction
    {
        auto block = std::make_shared<CBlock>(CreateBlock(txns, coinbaseKey));
        ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(chainActive.Height() == nHeight + 1);
        BOOST_ASSERT(block->GetHash() == chainActive.Tip()->GetBlockHash());
    }

    CAssetMetaData asset;
    BOOST_ASSERT(GetAssetMetaData(tx.GetHash().ToString(), asset));

    //change asset owner
    CKey key;
    key.MakeNewKey(false);
    std::string assetid = tx.GetHash().ToString();
    tx = CreateUpdateAssetTx(utxos, coinbaseKey, key, assetid, true, 0, 1000);
    {
        auto block = std::make_shared<CBlock>(CreateBlock({tx}, coinbaseKey));
        ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(chainActive.Height() == nHeight + 2);
        BOOST_ASSERT(block->GetHash() == chainActive.Tip()->GetBlockHash());
    }

    BOOST_ASSERT(GetAssetMetaData(assetid, asset));
    BOOST_ASSERT(asset.ownerAddress == key.GetPubKey().GetID());

    //any atemp to update with the coinbaseKey should fail
    tx = CreateUpdateAssetTx(utxos, coinbaseKey, coinbaseKey, assetid, true, 0, 10000);
    {
        auto block = std::make_shared<CBlock>(CreateBlock({tx}, coinbaseKey));
        ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(chainActive.Height() == nHeight + 2);
        BOOST_ASSERT(block->GetHash() != chainActive.Tip()->GetBlockHash());
    }
}

BOOST_FIXTURE_TEST_CASE(assets_mint, TestChainDIP3BeforeActivationSetup)
{
    CKey sporkKey;
    sporkKey.MakeNewKey(false);
    sporkManager.SetSporkAddress(EncodeDestination(sporkKey.GetPubKey().GetID()));
    sporkManager.SetPrivKey(EncodeSecret(sporkKey));
    sporkManager.UpdateSpork(SPORK_22_SPECIAL_TX_FEE, 2560, *g_connman);

    auto utxos = BuildSimpleUtxoMap(coinbaseTxns);

    auto tx = CreateNewAssetTx(utxos, coinbaseKey,"Test Asset", true, false, 0, 8, 1000);
    std::vector<CMutableTransaction> txns = {tx};

    int nHeight = chainActive.Height();

    // Mining a block with a asset create transaction
    {
        auto block = std::make_shared<CBlock>(CreateBlock(txns, coinbaseKey));
        ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(chainActive.Height() == nHeight + 1);
        BOOST_ASSERT(block->GetHash() == chainActive.Tip()->GetBlockHash());
    }
    
    std::string assetid = tx.GetHash().ToString();
    tx = CreateMintAssetTx(utxos, coinbaseKey, assetid);

    {
        auto block = std::make_shared<CBlock>(CreateBlock({tx}, coinbaseKey));
        ProcessNewBlock(Params(), block, true, nullptr);

        BOOST_ASSERT(chainActive.Height() == nHeight + 2);
        BOOST_ASSERT(block->GetHash() == chainActive.Tip()->GetBlockHash());
    }

    CAssetMetaData asset;
    BOOST_ASSERT(GetAssetMetaData(assetid, asset));
    
    BOOST_ASSERT(asset.circulatingSuply == 1000 * COIN);

}
BOOST_AUTO_TEST_SUITE_END()
