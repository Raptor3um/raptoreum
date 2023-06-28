// Copyright (c) 2018-2019 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <base58.h>
#include <chainparams.h>
#include <consensus/validation.h>
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

#include <evo/specialtx.h>
#include <evo/providertx.h>
#include <evo/deterministicmns.h>

#include <boost/test/unit_test.hpp>

using SimpleUTXOMap = std::map <COutPoint, std::pair<int, CAmount>>;

static SimpleUTXOMap BuildSimpleUtxoMap(const std::vector <CTransactionRef> &txs) {
    SimpleUTXOMap utxos;
    for (size_t i = 0; i < txs.size(); i++) {
        auto &tx = txs[i];
        for (size_t j = 0; j < tx->vout.size(); j++) {
            if (tx->vout[j].nValue > 0)
                utxos.emplace(COutPoint(tx->GetHash(), j), std::make_pair((int) i + 1, tx->vout[j].nValue));
        }
    }
    return utxos;
}

static std::vector <COutPoint> SelectUTXOs(SimpleUTXOMap &utoxs, CAmount amount, CAmount &changeRet) {
    changeRet = 0;

    std::vector <COutPoint> selectedUtxos;
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

static void FundTransaction(CMutableTransaction &tx, SimpleUTXOMap &utoxs, const CScript &scriptPayout, CAmount amount,
                            const CKey &coinbaseKey) {
    CAmount change;
    auto inputs = SelectUTXOs(utoxs, amount, change);
    for (size_t i = 0; i < inputs.size(); i++) {
        tx.vin.emplace_back(CTxIn(inputs[i]));
    }
    tx.vout.emplace_back(CTxOut(amount, scriptPayout));
    if (change != 0) {
        tx.vout.emplace_back(CTxOut(change, scriptPayout));
    }
}

static void SignTransaction(const CTxMemPool &mempool, CMutableTransaction &tx, const CKey &coinbaseKey) {
    CBasicKeyStore tempKeystore;
    tempKeystore.AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey());

    for (size_t i = 0; i < tx.vin.size(); i++) {
        uint256 hashBlock;
        CTransactionRef txFrom = GetTransaction(/* block_index */ nullptr, &mempool, tx.vin[i].prevout.hash,
                                                                  Params().GetConsensus(), hashBlock);
        BOOST_ASSERT(txFrom);
        BOOST_ASSERT(SignSignature(tempKeystore, *txFrom, tx, i, SIGHASH_ALL));
    }
}

static CMutableTransaction
CreateProRegTx(const CTxMemPool &mempool, SimpleUTXOMap &utxos, int port, const CScript &scriptPayout,
               const CKey &coinbaseKey, CKey &ownerKeyRet, CBLSSecretKey &operatorKeyRet) {
    ownerKeyRet.MakeNewKey(true);
    operatorKeyRet.MakeNewKey();

    CAmount collateralAmount = Params().GetConsensus().nCollaterals.getCollateral(
            ::ChainActive().Height() < 0 ? 1 : ::ChainActive().Height());

    CProRegTx proTx;
    proTx.collateralOutpoint.n = 0;
    proTx.addr = LookupNumeric("1.1.1.1", port);
    proTx.keyIDOwner = ownerKeyRet.GetPubKey().GetID();
    proTx.pubKeyOperator = operatorKeyRet.GetPublicKey();
    proTx.keyIDVoting = ownerKeyRet.GetPubKey().GetID();
    proTx.scriptPayout = scriptPayout;

    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_PROVIDER_REGISTER;
    FundTransaction(tx, utxos, scriptPayout, collateralAmount, coinbaseKey);
    proTx.inputsHash = CalcTxInputsHash(CTransaction(tx));
    SetTxPayload(tx, proTx);
    SignTransaction(mempool, tx, coinbaseKey);

    return tx;
}

static CMutableTransaction
CreateFutureTransaction(const CTxMemPool &mempool, SimpleUTXOMap &utxos, const CScript &scriptPayout) {
    CMutableTransaction tx;
    tx.nVersion = 4;
    tx.nType = TRANSACTION_FUTURE;
    return tx;
}

static CMutableTransaction CreateProUpServTx(const CTxMemPool &mempool, SimpleUTXOMap &utxos, const uint256 &proTxHash,
                                             const CBLSSecretKey &operatorKey, int port,
                                             const CScript &scriptOperatorPayout, const CKey &coinbaseKey) {
    CProUpServTx proTx;
    proTx.proTxHash = proTxHash;
    proTx.addr = LookupNumeric("1.1.1.1", port);
    proTx.scriptOperatorPayout = scriptOperatorPayout;

    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_PROVIDER_UPDATE_SERVICE;
    FundTransaction(tx, utxos, GetScriptForDestination(coinbaseKey.GetPubKey().GetID()), 1 * COIN, coinbaseKey);
    proTx.inputsHash = CalcTxInputsHash(CTransaction(tx));
    proTx.sig = operatorKey.Sign(::SerializeHash(proTx));
    SetTxPayload(tx, proTx);
    SignTransaction(mempool, tx, coinbaseKey);

    return tx;
}

static CMutableTransaction
CreateProUpRegTx(const CTxMemPool &mempool, SimpleUTXOMap &utxos, const uint256 &proTxHash, const CKey &mnKey,
                 const CBLSPublicKey &pubKeyOperator, const CKeyID &keyIDVoting, const CScript &scriptPayout,
                 const CKey &coinbaseKey) {
    CProUpRegTx proTx;
    proTx.proTxHash = proTxHash;
    proTx.pubKeyOperator = pubKeyOperator;
    proTx.keyIDVoting = keyIDVoting;
    proTx.scriptPayout = scriptPayout;

    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_PROVIDER_UPDATE_REGISTRAR;
    FundTransaction(tx, utxos, GetScriptForDestination(coinbaseKey.GetPubKey().GetID()), 1 * COIN, coinbaseKey);
    proTx.inputsHash = CalcTxInputsHash(CTransaction(tx));
    CHashSigner::SignHash(::SerializeHash(proTx), mnKey, proTx.vchSig);
    SetTxPayload(tx, proTx);
    SignTransaction(mempool, tx, coinbaseKey);

    return tx;
}

static CMutableTransaction CreateProUpRevTx(const CTxMemPool &mempool, SimpleUTXOMap &utxos, const uint256 &proTxHash,
                                            const CBLSSecretKey &operatorKey, const CKey &coinbaseKey) {
    CProUpRevTx proTx;
    proTx.proTxHash = proTxHash;

    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_PROVIDER_UPDATE_REVOKE;
    FundTransaction(tx, utxos, GetScriptForDestination(coinbaseKey.GetPubKey().GetID()), 1 * COIN, coinbaseKey);
    proTx.inputsHash = CalcTxInputsHash(CTransaction(tx));
    proTx.sig = operatorKey.Sign(::SerializeHash(proTx));
    SetTxPayload(tx, proTx);
    SignTransaction(mempool, tx, coinbaseKey);

    return tx;
}

template<typename ProTx>
static CMutableTransaction MalleateProTxPayout(const CMutableTransaction &tx) {
    ProTx proTx;
    GetTxPayload(tx, proTx);

    CKey key;
    key.MakeNewKey(false);
    proTx.scriptPayout = GetScriptForDestination(key.GetPubKey().GetID());

    CMutableTransaction tx2 = tx;
    SetTxPayload(tx2, proTx);

    return tx2;
}

static CScript GenerateRandomAddress() {
    CKey key;
    key.MakeNewKey(false);
    return GetScriptForDestination(key.GetPubKey().GetID());
}

static CDeterministicMNCPtr FindPayoutDmn(const CBlock &block) {
    auto dmnList = deterministicMNManager->GetListAtChainTip();

    for (const auto &txout: block.vtx[0]->vout) {
        CDeterministicMNCPtr found;
        dmnList.ForEachMN(true, [&](const CDeterministicMNCPtr &dmn) {
            if (found == nullptr && txout.scriptPubKey == dmn->pdmnState->scriptPayout) {
                found = dmn;
            }
        });
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

static bool CheckTransactionSignature(const CTxMemPool &mempool, const CMutableTransaction &tx) {
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const auto &txin = tx.vin[i];
        uint256 hashBlock;
        CTransactionRef txFrom = GetTransaction(/* block_index */ nullptr, &mempool, txin.prevout.hash,
                                                                  Params().GetConsensus(), hashBlock);
        BOOST_ASSERT(txFrom);

        CAmount amount = txFrom->vout[txin.prevout.n].nValue;
        if (!VerifyScript(txin.scriptSig, txFrom->vout[txin.prevout.n].scriptPubKey, STANDARD_SCRIPT_VERIFY_FLAGS,
                          MutableTransactionSignatureChecker(&tx, i, amount))) {
            return false;
        }
    }
    return true;
}

BOOST_AUTO_TEST_SUITE(evo_dip3_activation_tests)

BOOST_FIXTURE_TEST_CASE(dip3_activation, TestChainDIP3BeforeActivationSetup
)
{
auto utxos = BuildSimpleUtxoMap(m_coinbase_txns);
CKey ownerKey;
CBLSSecretKey operatorKey;
CTxDestination payoutDest = DecodeDestination("yRq1Ky1AfFmf597rnotj7QRxsDUKePVWNF");
auto tx = CreateProRegTx(*m_node.mempool, utxos, 1, GetScriptForDestination(payoutDest), coinbaseKey, ownerKey,
                         operatorKey);
std::vector <CMutableTransaction> txns = {tx};

int nHeight = ::ChainActive().Height();

// Mining a block with a DIP3 transaction
auto block = std::make_shared<CBlock>(CreateBlock(txns, coinbaseKey));

BOOST_ASSERT (EnsureChainman(m_node)

.

ProcessNewBlock(Params(), block,

true, nullptr));
deterministicMNManager->

UpdatedBlockTip (::ChainActive()

.

Tip()

);

BOOST_CHECK_EQUAL (::ChainActive()

.

Height(), nHeight

+ 1);
BOOST_CHECK_EQUAL(block
->

GetHash(), ::ChainActive()

.Tip()->GetBlockHash());
BOOST_ASSERT(deterministicMNManager
->

GetListAtChainTip()

.
HasMN(tx
.

GetHash()

));
}

BOOST_FIXTURE_TEST_CASE(dip3_protx, TestChainDIP3Setup
)
{
CKey sporkKey;
sporkKey.MakeNewKey(false);
sporkManager.
SetSporkAddress(EncodeDestination(sporkKey.GetPubKey().GetID())
);
sporkManager.
SetPrivKey(EncodeSecret(sporkKey)
);

auto utxos = BuildSimpleUtxoMap(m_coinbase_txns);

int nHeight = ::ChainActive().Height();
int port = 1;

std::vector <uint256> dmnHashes;
std::map <uint256, CKey> ownerKeys;
std::map <uint256, CBLSSecretKey> operatorKeys;

// register one MN per block
for (
size_t i = 0;
i < 12; i++) {
CKey ownerKey;
CBLSSecretKey operatorKey;
auto tx = CreateProRegTx(*m_node.mempool, utxos, port++, GenerateRandomAddress(), coinbaseKey, ownerKey, operatorKey);
dmnHashes.
emplace_back(tx
.

GetHash()

);
ownerKeys.
emplace(tx
.

GetHash(), ownerKey

);
operatorKeys.
emplace(tx
.

GetHash(), operatorKey

);

// also verify that payloads are not malleable after they have been signed
// the form of ProRegTx we use here is one with a collateral included, so there is no signature inside the
// payload itself. This means, we need to rely on script verification, which takes the hash of the extra payload
// into account
auto tx2 = MalleateProTxPayout<CProRegTx>(tx);
CValidationState dummyState;
// Technically, the payload is still valid...
{
LOCK(cs_main);
BOOST_ASSERT(CheckProRegTx(CTransaction(tx), ::ChainActive().Tip(), dummyState, ::ChainstateActive().CoinsTip(), true)
);
BOOST_ASSERT(CheckProRegTx(CTransaction(tx2), ::ChainActive().Tip(), dummyState, ::ChainstateActive().CoinsTip(), true)
);
}
// But the signature should not verify anymore
BOOST_ASSERT(CheckTransactionSignature(*m_node.mempool, tx)
);
BOOST_ASSERT(!
CheckTransactionSignature(*m_node
.mempool, tx2));

CreateAndProcessBlock({
tx}, coinbaseKey);
deterministicMNManager->

UpdatedBlockTip (::ChainActive()

.

Tip()

);

BOOST_CHECK_EQUAL (::ChainActive()

.

Height(), nHeight

+ 1);
BOOST_ASSERT(deterministicMNManager
->

GetListAtChainTip()

.
HasMN(tx
.

GetHash()

));

nHeight++;
}

//int DIP0003EnforcementHeightBackup = Params().GetConsensus().DIP0003EnforcementHeight;
int DIP0003EnforcementHeightBackup = 1;
//const_cast<Consensus::Params&>(Params().GetConsensus()).DIP0003EnforcementHeight = ::ChainActive().Height() + 1;
CreateAndProcessBlock({
}, coinbaseKey);
deterministicMNManager->

UpdatedBlockTip (::ChainActive()

.

Tip()

);
nHeight++;

// check MN reward payments
for (
size_t i = 0;
i < 24; i++) {
auto dmnExpectedPayee = deterministicMNManager->GetListAtChainTip().GetMNPayee();

CBlock block = CreateAndProcessBlock({}, coinbaseKey);
deterministicMNManager->

UpdatedBlockTip (::ChainActive()

.

Tip()

);
BOOST_ASSERT(!block.vtx.

empty()

);

auto dmnPayout = FindPayoutDmn(block);
BOOST_ASSERT(dmnPayout
!= nullptr);
BOOST_CHECK_EQUAL(dmnPayout
->proTxHash.

ToString(), dmnExpectedPayee

->proTxHash.

ToString()

);

nHeight++;
}

// register multiple MNs per block
for (
size_t i = 0;
i < 3; i++) {
std::vector <CMutableTransaction> txns;
for (
size_t j = 0;
j < 3; j++) {
CKey ownerKey;
CBLSSecretKey operatorKey;
auto tx = CreateProRegTx(*m_node.mempool, utxos, port++, GenerateRandomAddress(), coinbaseKey, ownerKey, operatorKey);
dmnHashes.
emplace_back(tx
.

GetHash()

);
ownerKeys.
emplace(tx
.

GetHash(), ownerKey

);
operatorKeys.
emplace(tx
.

GetHash(), operatorKey

);
txns.
emplace_back(tx);
}
CreateAndProcessBlock(txns, coinbaseKey
);
deterministicMNManager->

UpdatedBlockTip (::ChainActive()

.

Tip()

);

BOOST_CHECK_EQUAL (::ChainActive()

.

Height(), nHeight

+ 1);

for (
size_t j = 0;
j < 3; j++) {
BOOST_ASSERT(deterministicMNManager
->

GetListAtChainTip()

.
HasMN(txns[j]
.

GetHash()

));
}

nHeight++;
}

// test ProUpServTx
auto tx = CreateProUpServTx(*m_node.mempool, utxos, dmnHashes[0], operatorKeys[dmnHashes[0]], 1000, CScript(),
                            coinbaseKey);
CreateAndProcessBlock({
tx}, coinbaseKey);
deterministicMNManager->

UpdatedBlockTip (::ChainActive()

.

Tip()

);

BOOST_CHECK_EQUAL (::ChainActive()

.

Height(), nHeight

+ 1);
nHeight++;

auto dmn = deterministicMNManager->GetListAtChainTip().GetMN(dmnHashes[0]);
BOOST_ASSERT(dmn
!=
nullptr &&dmn
->pdmnState->addr.

GetPort()

== 1000);

// test ProUpRevTx
tx = CreateProUpRevTx(*m_node.mempool, utxos, dmnHashes[0], operatorKeys[dmnHashes[0]], coinbaseKey);
CreateAndProcessBlock({
tx}, coinbaseKey);
deterministicMNManager->

UpdatedBlockTip (::ChainActive()

.

Tip()

);

BOOST_CHECK_EQUAL (::ChainActive()

.

Height(), nHeight

+ 1);
nHeight++;

dmn = deterministicMNManager->GetListAtChainTip().GetMN(dmnHashes[0]);
BOOST_ASSERT(dmn
!=
nullptr &&dmn
->pdmnState->

GetBannedHeight()

== nHeight);

// test that the revoked MN does not get paid anymore
for (
size_t i = 0;
i < 24; i++) {
auto dmnExpectedPayee = deterministicMNManager->GetListAtChainTip().GetMNPayee();
BOOST_ASSERT(dmnExpectedPayee
->proTxHash != dmnHashes[0]);

CBlock block = CreateAndProcessBlock({}, coinbaseKey);
deterministicMNManager->

UpdatedBlockTip (::ChainActive()

.

Tip()

);
BOOST_ASSERT(!block.vtx.

empty()

);

auto dmnPayout = FindPayoutDmn(block);
BOOST_ASSERT(dmnPayout
!= nullptr);
BOOST_CHECK_EQUAL(dmnPayout
->proTxHash.

ToString(), dmnExpectedPayee

->proTxHash.

ToString()

);

nHeight++;
}

// test reviving the MN
CBLSSecretKey newOperatorKey;
newOperatorKey.

MakeNewKey();

dmn = deterministicMNManager->GetListAtChainTip().GetMN(dmnHashes[0]);
tx = CreateProUpRegTx(*m_node.mempool, utxos, dmnHashes[0], ownerKeys[dmnHashes[0]], newOperatorKey.GetPublicKey(),
                      ownerKeys[dmnHashes[0]].GetPubKey().GetID(), dmn->pdmnState->scriptPayout, coinbaseKey);
// check malleability protection again, but this time by also relying on the signature inside the ProUpRegTx
auto tx2 = MalleateProTxPayout<CProUpRegTx>(tx);
CValidationState dummyState;
{
LOCK(cs_main);
BOOST_ASSERT(CheckProUpRegTx(CTransaction(tx), ::ChainActive().Tip(), dummyState, ::ChainstateActive().CoinsTip(), true)
);
BOOST_ASSERT(!
CheckProUpRegTx(CTransaction(tx2), ::ChainActive()
.

Tip(), dummyState, ::ChainstateActive()

.

CoinsTip(),

true));
}
BOOST_ASSERT(CheckTransactionSignature(*m_node.mempool, tx)
);
BOOST_ASSERT(!
CheckTransactionSignature(*m_node
.mempool, tx2));
// now process the block
CreateAndProcessBlock({
tx}, coinbaseKey);
deterministicMNManager->

UpdatedBlockTip (::ChainActive()

.

Tip()

);

BOOST_CHECK_EQUAL (::ChainActive()

.

Height(), nHeight

+ 1);
nHeight++;

tx = CreateProUpServTx(*m_node.mempool, utxos, dmnHashes[0], newOperatorKey, 100, CScript(), coinbaseKey);
CreateAndProcessBlock({
tx}, coinbaseKey);
deterministicMNManager->

UpdatedBlockTip (::ChainActive()

.

Tip()

);

BOOST_CHECK_EQUAL (::ChainActive()

.

Height(), nHeight

+ 1);
nHeight++;

dmn = deterministicMNManager->GetListAtChainTip().GetMN(dmnHashes[0]);
BOOST_ASSERT(dmn
!=
nullptr &&dmn
->pdmnState->addr.

GetPort()

== 100);
BOOST_ASSERT(dmn
!= nullptr && !dmn->pdmnState->

IsBanned()

);

// test that the revived MN gets payments again
bool foundRevived = false;
for (
size_t i = 0;
i < 24; i++) {
auto dmnExpectedPayee = deterministicMNManager->GetListAtChainTip().GetMNPayee();
if (dmnExpectedPayee->proTxHash == dmnHashes[0]) {
foundRevived = true;
}

CBlock block = CreateAndProcessBlock({}, coinbaseKey);
deterministicMNManager->

UpdatedBlockTip (::ChainActive()

.

Tip()

);
BOOST_ASSERT(!block.vtx.

empty()

);

auto dmnPayout = FindPayoutDmn(block);
BOOST_ASSERT(dmnPayout
!= nullptr);
BOOST_CHECK_EQUAL(dmnPayout
->proTxHash.

ToString(), dmnExpectedPayee

->proTxHash.

ToString()

);

nHeight++;
}
BOOST_ASSERT(foundRevived);

//const_cast<Consensus::Params&>(Params().GetConsensus()).DIP0003EnforcementHeight = DIP0003EnforcementHeightBackup;
}

BOOST_FIXTURE_TEST_CASE(dip3_test_mempool_reorg, TestChainDIP3Setup
)
{
int nHeight = ::ChainActive().Height();
CAmount collateralAmount = Params().GetConsensus().nCollaterals.getCollateral(nHeight < 0 ? 1 : nHeight);
auto utxos = BuildSimpleUtxoMap(m_coinbase_txns);

CKey ownerKey;
CKey payoutKey;
CKey collateralKey;
CBLSSecretKey operatorKey;

ownerKey.MakeNewKey(true);
payoutKey.MakeNewKey(true);
collateralKey.MakeNewKey(true);
operatorKey.

MakeNewKey();

auto scriptPayout = GetScriptForDestination(payoutKey.GetPubKey().GetID());
auto scriptCollateral = GetScriptForDestination(collateralKey.GetPubKey().GetID());

// Create a MN with an external collateral
CMutableTransaction tx_collateral;
FundTransaction(tx_collateral, utxos, scriptCollateral, collateralAmount, coinbaseKey
);
SignTransaction(*m_node
.mempool, tx_collateral, coinbaseKey);

auto block = std::make_shared<CBlock>(CreateBlock({tx_collateral}, coinbaseKey));
BOOST_ASSERT(EnsureChainman(m_node)
.

ProcessNewBlock(Params(), block,

true, nullptr));
deterministicMNManager->

UpdatedBlockTip (::ChainActive()

.

Tip()

);

BOOST_CHECK_EQUAL (::ChainActive()

.

Height(), nHeight

+ 1);
BOOST_CHECK_EQUAL(block
->

GetHash(), ::ChainActive()

.Tip()->GetBlockHash());

CProRegTx payload;
payload.
addr = LookupNumeric("1.1.1.1", 1);
payload.
keyIDOwner = ownerKey.GetPubKey().GetID();
payload.
pubKeyOperator = operatorKey.GetPublicKey();
payload.
keyIDVoting = ownerKey.GetPubKey().GetID();
payload.
scriptPayout = scriptPayout;

for (
size_t i = 0;
i<tx_collateral.vout.

size();

++i) {
if (tx_collateral.vout[i].nValue == collateralAmount) {
payload.
collateralOutpoint = COutPoint(tx_collateral.GetHash(), i);
break;
}
}

CMutableTransaction tx_reg;
tx_reg.
nVersion = 3;
tx_reg.
nType = TRANSACTION_PROVIDER_REGISTER;
FundTransaction(tx_reg, utxos, scriptPayout, collateralAmount, coinbaseKey
);
payload.
inputsHash = CalcTxInputsHash(CTransaction(tx_reg));
CMessageSigner::SignMessage(payload
.

MakeSignString(), payload

.vchSig, collateralKey);
SetTxPayload(tx_reg, payload
);
SignTransaction(*m_node
.mempool, tx_reg, coinbaseKey);

CTxMemPool testPool;
TestMemPoolEntryHelper entry;
LOCK(testPool
.cs);

// Create ProUpServ and test block reorg which double-spend ProRegTx
auto tx_up_serv = CreateProUpServTx(*m_node.mempool, utxos, tx_reg.GetHash(), operatorKey, 2, CScript(), coinbaseKey);
testPool.
addUnchecked(entry
.
FromTx(tx_up_serv)
);
// A disconnected block would insert ProRegTx back into mempool
testPool.
addUnchecked(entry
.
FromTx(tx_reg)
);
BOOST_CHECK_EQUAL(testPool
.

size(),

2U);

// Create a tx that will double-spend ProRegTx
CMutableTransaction tx_reg_ds;
tx_reg_ds.
vin = tx_reg.vin;
tx_reg_ds.vout.emplace_back(0,

CScript()

<< OP_RETURN);
SignTransaction(*m_node
.mempool, tx_reg_ds, coinbaseKey);

// Check mempool as if a new block with tx_reg_ds was connected instead of the old one with tx_reg
std::vector <CTransactionRef> block_reorg;
block_reorg.
emplace_back(std::make_shared<CTransaction>(tx_reg_ds)
);
testPool.
removeForBlock(block_reorg, nHeight
+ 2);
BOOST_CHECK_EQUAL(testPool
.

size(),

0U);
}

BOOST_FIXTURE_TEST_CASE(dip3_test_mempool_dual_proregtx, TestChainDIP3Setup
)
{
int nHeight = ::ChainActive().Height();
CAmount collateralAmount = Params().GetConsensus().nCollaterals.getCollateral(nHeight < 0 ? 1 : nHeight);
auto utxos = BuildSimpleUtxoMap(m_coinbase_txns);

// Create a MN
CKey ownerKey1;
CBLSSecretKey operatorKey1;
auto tx_reg1 = CreateProRegTx(*m_node.mempool, utxos, 1, GenerateRandomAddress(), coinbaseKey, ownerKey1, operatorKey1);

// Create a MN with an external collateral that references tx_reg1
CKey ownerKey;
CKey payoutKey;
CKey collateralKey;
CBLSSecretKey operatorKey;

ownerKey.MakeNewKey(true);
payoutKey.MakeNewKey(true);
collateralKey.MakeNewKey(true);
operatorKey.

MakeNewKey();

auto scriptPayout = GetScriptForDestination(payoutKey.GetPubKey().GetID());
auto scriptCollateral = GetScriptForDestination(collateralKey.GetPubKey().GetID());

CProRegTx payload;
payload.
addr = LookupNumeric("1.1.1.1", 2);
payload.
keyIDOwner = ownerKey.GetPubKey().GetID();
payload.
pubKeyOperator = operatorKey.GetPublicKey();
payload.
keyIDVoting = ownerKey.GetPubKey().GetID();
payload.
scriptPayout = scriptPayout;

for (
size_t i = 0;
i<tx_reg1.vout.

size();

++i) {
if (tx_reg1.vout[i].nValue == collateralAmount) {
payload.
collateralOutpoint = COutPoint(tx_reg1.GetHash(), i);
break;
}
}

CMutableTransaction tx_reg2;
tx_reg2.
nVersion = 3;
tx_reg2.
nType = TRANSACTION_PROVIDER_REGISTER;
FundTransaction(tx_reg2, utxos, scriptPayout, collateralAmount, coinbaseKey
);
payload.
inputsHash = CalcTxInputsHash(CTransaction(tx_reg2));
CMessageSigner::SignMessage(payload
.

MakeSignString(), payload

.vchSig, collateralKey);
SetTxPayload(tx_reg2, payload
);
SignTransaction(*m_node
.mempool, tx_reg2, coinbaseKey);

CTxMemPool testPool;
TestMemPoolEntryHelper entry;
LOCK(testPool
.cs);

testPool.
addUnchecked(entry
.
FromTx(tx_reg1)
);
BOOST_CHECK_EQUAL(testPool
.

size(),

1U);
BOOST_CHECK(testPool
.
existsProviderTxConflict(CTransaction(tx_reg2)
));
}

BOOST_FIXTURE_TEST_CASE(dip3_verify_db, TestChainDIP3Setup
)
{
int nHeight = ::ChainActive().Height();
CAmount collateralAmount = Params().GetConsensus().nCollaterals.getCollateral(nHeight < 0 ? 1 : nHeight);
auto utxos = BuildSimpleUtxoMap(m_coinbase_txns);

CKey ownerKey;
CKey payoutKey;
CKey collateralKey;
CBLSSecretKey operatorKey;

ownerKey.MakeNewKey(true);
payoutKey.MakeNewKey(true);
collateralKey.MakeNewKey(true);
operatorKey.

MakeNewKey();

auto scriptPayout = GetScriptForDestination(payoutKey.GetPubKey().GetID());
auto scriptCollateral = GetScriptForDestination(collateralKey.GetPubKey().GetID());

// Create a MN with an external collateral
CMutableTransaction tx_collateral;
FundTransaction(tx_collateral, utxos, scriptCollateral, collateralAmount, coinbaseKey
);
SignTransaction(*m_node
.mempool, tx_collateral, coinbaseKey);

auto block = std::make_shared<CBlock>(CreateBlock({tx_collateral}, coinbaseKey));
BOOST_ASSERT(EnsureChainman(m_node)
.

ProcessNewBlock(Params(), block,

true, nullptr));
deterministicMNManager->

UpdatedBlockTip (::ChainActive()

.

Tip()

);

BOOST_CHECK_EQUAL (::ChainActive()

.

Height(), nHeight

+ 1);
BOOST_CHECK_EQUAL(block
->

GetHash(), ::ChainActive()

.Tip()->GetBlockHash());

CProRegTx payload;
payload.
addr = LookupNumeric("1.1.1.1", 1);
payload.
keyIDOwner = ownerKey.GetPubKey().GetID();
payload.
pubKeyOperator = operatorKey.GetPublicKey();
payload.
keyIDVoting = ownerKey.GetPubKey().GetID();
payload.
scriptPayout = scriptPayout;

for (
size_t i = 0;
i<tx_collateral.vout.

size();

++i) {
if (tx_collateral.vout[i].nValue == collateralAmount) {
payload.
collateralOutpoint = COutPoint(tx_collateral.GetHash(), i);
break;
}
}

CMutableTransaction tx_reg;
tx_reg.
nVersion = 3;
tx_reg.
nType = TRANSACTION_PROVIDER_REGISTER;
FundTransaction(tx_reg, utxos, scriptPayout, collateralAmount, coinbaseKey
);
payload.
inputsHash = CalcTxInputsHash(CTransaction(tx_reg));
CMessageSigner::SignMessage(payload
.

MakeSignString(), payload

.vchSig, collateralKey);
SetTxPayload(tx_reg, payload
);
SignTransaction(*m_node
.mempool, tx_reg, coinbaseKey);

auto tx_reg_hash = tx_reg.GetHash();

block = std::make_shared<CBlock>(CreateBlock({tx_reg}, coinbaseKey));
BOOST_ASSERT(EnsureChainman(m_node)
.

ProcessNewBlock(Params(), block,

true, nullptr));
deterministicMNManager->

UpdatedBlockTip (::ChainActive()

.

Tip()

);

BOOST_CHECK_EQUAL (::ChainActive()

.

Height(), nHeight

+ 2);
BOOST_CHECK_EQUAL(block
->

GetHash(), ::ChainActive()

.Tip()->GetBlockHash());
BOOST_ASSERT(deterministicMNManager
->

GetListAtChainTip()

.
HasMN(tx_reg_hash)
);

// Now spend the collateral while updating the same MN
SimpleUTXOMap collateral_utxos;
collateral_utxos.
emplace(payload
.collateralOutpoint, std::make_pair(1, collateralAmount));
auto proUpRevTx = CreateProUpRevTx(*m_node.mempool, collateral_utxos, tx_reg_hash, operatorKey, collateralKey);

block = std::make_shared<CBlock>(CreateBlock({proUpRevTx}, coinbaseKey));
BOOST_ASSERT(EnsureChainman(m_node)
.

ProcessNewBlock(Params(), block,

true, nullptr));
deterministicMNManager->

UpdatedBlockTip (::ChainActive()

.

Tip()

);

BOOST_CHECK_EQUAL (::ChainActive()

.

Height(), nHeight

+ 3);
BOOST_CHECK_EQUAL(block
->

GetHash(), ::ChainActive()

.Tip()->GetBlockHash());
BOOST_ASSERT(!deterministicMNManager->

GetListAtChainTip()

.
HasMN(tx_reg_hash)
);

// Verify db consistency
LOCK(cs_main);

BOOST_ASSERT (CVerifyDB()

.

VerifyDB(Params(), &::ChainstateActive()

.

CoinsTip(),

4, 2));
}

BOOST_AUTO_TEST_SUITE_END()
