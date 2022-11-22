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
    CKey Key;
    Key.MakeNewKey(false);
    CKeyID ownerKey = Key.GetPubKey().GetID();
    CNewAssetTx newasset;

    newasset.Name = name;
    newasset.updatable = updatable;
    newasset.isUnique = is_unique;
    newasset.decimalPoint = decimalPoint;
    newasset.referenceHash = "";
    newasset.type = type;
    newasset.fee = 100; //spork is off any value is valid
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


static CScript GenerateRandomAddress()
{
    CKey key;
    key.MakeNewKey(false);
    return GetScriptForDestination(key.GetPubKey().GetID());
}

BOOST_AUTO_TEST_SUITE(assets_creation_tests)

BOOST_FIXTURE_TEST_CASE(assets_creation, TestChainDIP3BeforeActivationSetup)
{
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

BOOST_AUTO_TEST_SUITE_END()
