// Copyright (c) 2018-2019 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evo/providertx.h>

#include <chainparams.h>
#include <coins.h>
#include <consensus/validation.h>
#include <evo/deterministicmns.h>
#include <evo/specialtx.h>
#include <hash.h>
#include <messagesigner.h>
#include <script/standard.h>
#include <validation.h>
#include <spork.h>
#include <assets/assets.h>
#include <assets/assetstype.h>

template<typename ProTx>
static bool CheckService(const ProTx &proTx, CValidationState &state) {
    if (!proTx.addr.IsValid()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-ipaddr");
    }
    if (Params().RequireRoutableExternalIP() && !proTx.addr.IsRoutable()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-ipaddr");
    }

    static int mainnetDefaultPort = CreateChainParams(CBaseChainParams::MAIN)->GetDefaultPort();
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (proTx.addr.GetPort() != mainnetDefaultPort) {
            return state.DoS(10, false, REJECT_INVALID, "bad-protx-ipaddr-port");
        }
    } else if (proTx.addr.GetPort() == mainnetDefaultPort) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-ipaddr-port");
    }

    if (!proTx.addr.IsIPv4()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-ipaddr");
    }

    return true;
}

template<typename ProTx>
static bool CheckHashSig(const ProTx &proTx, const CKeyID &keyID, CValidationState &state) {
    std::string strError;
    if (!CHashSigner::VerifyHash(::SerializeHash(proTx), keyID, proTx.vchSig, strError)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-sig", false, strError);
    }
    return true;
}

template<typename ProTx>
static bool CheckStringSig(const ProTx &proTx, const CKeyID &keyID, CValidationState &state) {
    std::string strError;
    if (!CMessageSigner::VerifyMessage(keyID, proTx.vchSig, proTx.MakeSignString(), strError)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-sig", false, strError);
    }
    return true;
}

template<typename ProTx>
static bool CheckHashSig(const ProTx &proTx, const CBLSPublicKey &pubKey, CValidationState &state) {
    if (!proTx.sig.VerifyInsecure(pubKey, ::SerializeHash(proTx))) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-sig", false);
    }
    return true;
}

template<typename ProTx>
static bool CheckInputsHash(const CTransaction &tx, const ProTx &proTx, CValidationState &state) {
    uint256 inputsHash = CalcTxInputsHash(tx);
    if (inputsHash != proTx.inputsHash) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-inputs-hash");
    }

    return true;
}

bool CheckFutureTx(const CTransaction &tx, const CBlockIndex *pindexPrev, CValidationState &state) {

    if (!Params().IsFutureActive(::ChainActive().Tip())) {
        return state.DoS(100, false, REJECT_INVALID, "future-not-enabled");
    }
    if (tx.nType != TRANSACTION_FUTURE) {
        return state.DoS(100, false, REJECT_INVALID, "bad-future-type");
    }

    CFutureTx ftx;
    if (!GetTxPayload(tx, ftx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-future-payload");
    }

    if (ftx.nVersion == 0 || ftx.nVersion > CFutureTx::CURRENT_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-future-version");
    }
    if (!CheckInputsHash(tx, ftx, state)) {
        return false;
    }

    return true;
}

inline bool checkNewUniqueAsset(CNewAssetTx &assetTx, CValidationState &state) {
    if (!assetTx.isUnique)
        return true;

    if (assetTx.amount > 500 * COIN) {
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-amount");
    }

    if (assetTx.decimalPoint > 0) { // alway 0
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-decimalPoint");
    }

    if (assetTx.type > 0) { // manual mint only?
        return state.DoS(100, false, REJECT_INVALID, "bad-unique-assets-distibution-type");
    }

    if (assetTx.updatable) {
        return state.DoS(100, false, REJECT_INVALID, "bad-unique-assets-distibution-type");
    }

    return true;
}

bool CheckNewAssetTx(const CTransaction &tx, const CBlockIndex *pindexPrev, CValidationState &state,
                     CAssetsCache *assetsCache) {
    if (!Params().IsAssetsActive(::ChainActive().Tip())) {
        return state.DoS(100, false, REJECT_INVALID, "assets-not-enabled");
    }

    if (tx.nType != TRANSACTION_NEW_ASSET) {
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-type");
    }

    CNewAssetTx assetTx;
    if (!GetTxPayload(tx, assetTx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-payload");
    }

    if (assetTx.nVersion == 0 || assetTx.nVersion > CNewAssetTx::CURRENT_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-version");
    }

    //validate asset name
    if (!IsAssetNameValid(assetTx.name)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-name");
    }

    //Check if a asset already exist with give name
    std::string assetId = assetTx.name;
    if (assetsCache->GetAssetId(assetTx.name, assetId)) {
        if (assetsCache->CheckIfAssetExists(assetId)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-assets-dup-name");
        }
    }

    //check unique asset
    if (!checkNewUniqueAsset(assetTx, state))
        return false;

    if (assetTx.decimalPoint < 0 || assetTx.decimalPoint > 8) {
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-decimalPoint");
    }

    if (assetTx.ownerAddress.IsNull()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-ownerAddress");
    }

    if (assetTx.targetAddress.IsNull()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-targetAddress");
    }

    if (assetTx.type < 0 && assetTx.type > 3) {
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-distibution-type");
    }

    if (assetTx.collateralAddress.IsNull() && assetTx.type != 0) { //
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-collateralAddress");
    }

    if ((assetTx.referenceHash.length() > 128)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-referenceHash");
    }

    if (!validateAmount(assetTx.amount, assetTx.decimalPoint)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-amount");
    }

    if (!CheckInputsHash(tx, assetTx, state)) {
        return false;
    }

    return true;
}

inline bool checkAssetFeesPayment(const CTransaction &tx, CValidationState &state, const CCoinsViewCache &view,
                                  CAssetMetaData asset) {
    for (auto in: tx.vin) {
        const Coin &coin = view.AccessCoin(in.prevout);
        if (coin.IsSpent())
            return state.DoS(100, false, REJECT_INVALID, "bad-assets-invalid-input");
        CTxDestination dest;
        ExtractDestination(coin.out.scriptPubKey, dest);
        if (EncodeDestination(dest) != EncodeDestination(asset.ownerAddress)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-assets-invalid-input");
        }
    }

    return true;
}

bool CheckUpdateAssetTx(const CTransaction &tx, const CBlockIndex *pindexPrev, CValidationState &state,
                        const CCoinsViewCache &view, CAssetsCache *assetsCache) {
    if (!Params().IsAssetsActive(::ChainActive().Tip())) {
        return state.DoS(100, false, REJECT_INVALID, "assets-not-enabled");
    }

    if (tx.nType != TRANSACTION_UPDATE_ASSET) {
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-type");
    }

    CUpdateAssetTx assetTx;
    if (!GetTxPayload(tx, assetTx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-update-payload");
    }

    if (assetTx.nVersion == 0 || assetTx.nVersion > CUpdateAssetTx::CURRENT_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-version");
    }

    //Check if the provide asset id is valid
    CAssetMetaData asset;
    if (!assetsCache->GetAssetMetaData(assetTx.assetId, asset)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-invalid-id");
    }

    //Check if fees is paid by the owner address
    if (!checkAssetFeesPayment(tx, state, view, asset))
        return false;

    if (assetTx.ownerAddress.IsNull()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-ownerAddress");
    }

    if (assetTx.targetAddress.IsNull()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-targetAddress");
    }

    if (assetTx.type < 0 && assetTx.type > 3) {
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-distibution-type");
    }

    if (assetTx.collateralAddress.IsNull() && assetTx.type != 0) { //
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-collateralAddress");
    }

    if (!CheckInputsHash(tx, assetTx, state)) {
        return false;
    }

    return true;
}

inline bool checkAssetMintAmount(const CTransaction &tx, CValidationState &state, const CAssetMetaData asset) {
    CAmount nAmount = 0;
    std::set <uint16_t> setUniqueId;
    uint16_t minUniqueId = asset.circulatingSupply / COIN;
    for (auto out: tx.vout) {
        if (out.scriptPubKey.IsAssetScript()) {
            CAssetTransfer assetTransfer;
            if (!GetTransferAsset(out.scriptPubKey, assetTransfer)) {
                return state.DoS(100, false, REJECT_INVALID, "bad-mint-assets-transfer");
            }
            if (assetTransfer.assetId != asset.assetId) { //check asset id
                return state.DoS(100, false, REJECT_INVALID, "bad-mint-assets-id");
            }
            if (asset.isUnique) {
                //check validate uniqueId and amount
                if (assetTransfer.uniqueId < minUniqueId || setUniqueId.count(assetTransfer.uniqueId)) {
                    return state.DoS(100, false, REJECT_INVALID, "bad-mint-dup-uniqueid");
                }
                setUniqueId.insert(assetTransfer.uniqueId);
                if (assetTransfer.nAmount != 1 * COIN) {
                    return state.DoS(100, false, REJECT_INVALID, "bad-mint-unique-amount");
                }
            }
            if (!validateAmount(assetTransfer.nAmount, asset.decimalPoint)) {
                return state.DoS(100, false, REJECT_INVALID, "bad-mint-assets-amount");
            }
            nAmount += assetTransfer.nAmount;
        }
    }

    if (asset.amount != nAmount) {
        return state.DoS(100, false, REJECT_INVALID, "bad-mint-assets-amount");
    }

    return true;
}

bool CheckMintAssetTx(const CTransaction &tx, const CBlockIndex *pindexPrev, CValidationState &state,
                      const CCoinsViewCache &view, CAssetsCache *assetsCache) {
    if (!Params().IsAssetsActive(::ChainActive().Tip())) {
        return state.DoS(100, false, REJECT_INVALID, "assets-not-enabled");
    }

    if (tx.nType != TRANSACTION_MINT_ASSET) {
        return state.DoS(100, false, REJECT_INVALID, "bad-mint-assets-type");
    }

    CMintAssetTx assetTx;
    if (!GetTxPayload(tx, assetTx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-mint-assets-payload");
    }

    if (assetTx.nVersion == 0 || assetTx.nVersion > CMintAssetTx::CURRENT_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-mint-assets-version");
    }

    //Check if the provide asset id is valid
    CAssetMetaData asset;
    if (!assetsCache->GetAssetMetaData(assetTx.assetId, asset)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-assets-invalid-asset-id");
    }

    if (asset.type == 0 || asset.isUnique) { // manual mint or unique
        //Check if fees is paid by the owner address
        if (!checkAssetFeesPayment(tx, state, view, asset))
            return false;

        if (!checkAssetMintAmount(tx, state, asset))
            return false;

        if (asset.mintCount >= asset.maxMintCount) {
            return state.DoS(100, false, REJECT_INVALID, "bad-max-mint-count");
        }

    } else {
        return state.DoS(100, false, REJECT_INVALID, "bad-mint-type-not-enabled");
    }


    if (!CheckInputsHash(tx, assetTx, state)) {
        return false;
    }

    return true;
}

bool CheckProRegTx(const CTransaction &tx, const CBlockIndex *pindexPrev, CValidationState &state,
                   const CCoinsViewCache &view, bool check_sigs) {
    if (tx.nType != TRANSACTION_PROVIDER_REGISTER) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-type");
    }

    CProRegTx ptx;
    if (!GetTxPayload(tx, ptx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-payload");
    }

    if (ptx.nVersion == 0 || ptx.nVersion > CProRegTx::CURRENT_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-version");
    }
    if (ptx.nType != 0) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-type");
    }
    if (ptx.nMode != 0) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-mode");
    }

    if (ptx.keyIDOwner.IsNull() || !ptx.pubKeyOperator.IsValid() || ptx.keyIDVoting.IsNull()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-key-null");
    }
    if (!ptx.scriptPayout.IsPayToPublicKeyHash() && !ptx.scriptPayout.IsPayToScriptHash()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee");
    }

    CTxDestination payoutDest;
    if (!ExtractDestination(ptx.scriptPayout, payoutDest)) {
        // should not happen as we checked script types before
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee-dest");
    }
    // don't allow reuse of payout key for other keys (don't allow people to put the payee key onto an online server)
    if (payoutDest == CTxDestination(ptx.keyIDOwner) || payoutDest == CTxDestination(ptx.keyIDVoting)) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee-reuse");
    }

    // It's allowed to set addr to 0, which will put the MN into PoSe-banned state and require a ProUpServTx to be issues later
    // If any of both is set, it must be valid however
    if (ptx.addr != CService() && !CheckService(ptx, state)) {
        // pass the state returned by the function above
        return false;
    }

    if (ptx.nOperatorReward > 10000) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-operator-reward");
    }

    CTxDestination collateralTxDest;
    const CKeyID *keyForPayloadSig = nullptr;
    COutPoint collateralOutpoint;
    Coin coin;
    SmartnodeCollaterals collaterals = Params().GetConsensus().nCollaterals;
    if (!ptx.collateralOutpoint.hash.IsNull()) {
        if (!view.GetCoin(ptx.collateralOutpoint, coin) || coin.IsSpent() ||
            !collaterals.isValidCollateral(coin.out.nValue)) {
            return state.DoS(10, false, REJECT_INVALID, "bad-protx-collateral");
        }

        if (!ExtractDestination(coin.out.scriptPubKey, collateralTxDest)) {
            return state.DoS(10, false, REJECT_INVALID, "bad-protx-collateral-dest");
        }

        // Extract key from collateral. This only works for P2PK and P2PKH collaterals and will fail for P2SH.
        // Issuer of this ProRegTx must prove ownership with this key by signing the ProRegTx
        keyForPayloadSig = boost::get<CKeyID>(&collateralTxDest);
        if (!keyForPayloadSig) {
            return state.DoS(10, false, REJECT_INVALID, "bad-protx-collateral-pkh");
        }

        collateralOutpoint = ptx.collateralOutpoint;
    } else {
        if (ptx.collateralOutpoint.n >= tx.vout.size()) {
            return state.DoS(10, false, REJECT_INVALID, "bad-protx-collateral-index");
        }
        if (!collaterals.isValidCollateral(tx.vout[ptx.collateralOutpoint.n].nValue)) {
            return state.DoS(10, false, REJECT_INVALID, "bad-protx-collateral");
        }

        if (!ExtractDestination(tx.vout[ptx.collateralOutpoint.n].scriptPubKey, collateralTxDest)) {
            return state.DoS(10, false, REJECT_INVALID, "bad-protx-collateral-dest");
        }

        collateralOutpoint = COutPoint(tx.GetHash(), ptx.collateralOutpoint.n);
    }

    // don't allow reuse of collateral key for other keys (don't allow people to put the collateral key onto an online server)
    // this check applies to internal and external collateral, but internal collaterals are not necessarely a P2PKH
    if (collateralTxDest == CTxDestination(ptx.keyIDOwner) || collateralTxDest == CTxDestination(ptx.keyIDVoting)) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-collateral-reuse");
    }

    if (pindexPrev) {
        auto mnList = deterministicMNManager->GetListForBlock(pindexPrev);

        // only allow reusing of addresses when it's for the same collateral (which replaces the old MN)
        if (mnList.HasUniqueProperty(ptx.addr) &&
            mnList.GetUniquePropertyMN(ptx.addr)->collateralOutpoint != collateralOutpoint) {
            return state.DoS(10, false, REJECT_DUPLICATE, "bad-protx-dup-addr");
        }

        // never allow duplicate keys, even if this ProTx would replace an existing MN
        if (mnList.HasUniqueProperty(ptx.keyIDOwner) || mnList.HasUniqueProperty(ptx.pubKeyOperator)) {
            return state.DoS(10, false, REJECT_DUPLICATE, "bad-protx-dup-key");
        }

        if (!deterministicMNManager->IsDIP3Enforced(pindexPrev->nHeight)) {
            if (ptx.keyIDOwner != ptx.keyIDVoting) {
                return state.DoS(10, false, REJECT_INVALID, "bad-protx-key-not-same");
            }
        }
    }

    if (!CheckInputsHash(tx, ptx, state)) {
        return false;
    }

    if (keyForPayloadSig) {
        // collateral is not part of this ProRegTx, so we must verify ownership of the collateral
        if (check_sigs && !CheckStringSig(ptx, *keyForPayloadSig, state)) {
            // pass the state returned by the function above
            return false;
        }
    } else {
        // collateral is part of this ProRegTx, so we know the collateral is owned by the issuer
        if (!ptx.vchSig.empty()) {
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-sig");
        }
    }

    return true;
}

bool CheckProUpServTx(const CTransaction &tx, const CBlockIndex *pindexPrev, CValidationState &state, bool check_sigs) {
    if (tx.nType != TRANSACTION_PROVIDER_UPDATE_SERVICE) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-type");
    }

    CProUpServTx ptx;
    if (!GetTxPayload(tx, ptx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-payload");
    }

    if (ptx.nVersion == 0 || ptx.nVersion > CProUpServTx::CURRENT_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-version");
    }

    if (!CheckService(ptx, state)) {
        // pass the state returned by the function above
        return false;
    }

    if (pindexPrev) {
        auto mnList = deterministicMNManager->GetListForBlock(pindexPrev);
        auto mn = mnList.GetMN(ptx.proTxHash);
        if (!mn) {
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-hash");
        }

        // don't allow updating to addresses already used by other MNs
        if (mnList.HasUniqueProperty(ptx.addr) && mnList.GetUniquePropertyMN(ptx.addr)->proTxHash != ptx.proTxHash) {
            return state.DoS(10, false, REJECT_DUPLICATE, "bad-protx-dup-addr");
        }

        if (ptx.scriptOperatorPayout != CScript()) {
            if (mn->nOperatorReward == 0) {
                // don't allow setting operator reward payee in case no operatorReward was set
                return state.DoS(10, false, REJECT_INVALID, "bad-protx-operator-payee");
            }
            if (!ptx.scriptOperatorPayout.IsPayToPublicKeyHash() && !ptx.scriptOperatorPayout.IsPayToScriptHash()) {
                return state.DoS(10, false, REJECT_INVALID, "bad-protx-operator-payee");
            }
        }

        // we can only check the signature if pindexPrev != nullptr and the MN is known
        if (!CheckInputsHash(tx, ptx, state)) {
            // pass the state returned by the function above
            return false;
        }
        if (check_sigs && !CheckHashSig(ptx, mn->pdmnState->pubKeyOperator.Get(), state)) {
            // pass the state returned by the function above
            return false;
        }
    }

    return true;
}

bool CheckProUpRegTx(const CTransaction &tx, const CBlockIndex *pindexPrev, CValidationState &state,
                     const CCoinsViewCache &view, bool check_sigs) {
    if (tx.nType != TRANSACTION_PROVIDER_UPDATE_REGISTRAR) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-type");
    }

    CProUpRegTx ptx;
    if (!GetTxPayload(tx, ptx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-payload");
    }

    if (ptx.nVersion == 0 || ptx.nVersion > CProUpRegTx::CURRENT_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-version");
    }
    if (ptx.nMode != 0) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-mode");
    }

    if (!ptx.pubKeyOperator.IsValid() || ptx.keyIDVoting.IsNull()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-key-null");
    }
    if (!ptx.scriptPayout.IsPayToPublicKeyHash() && !ptx.scriptPayout.IsPayToScriptHash()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee");
    }

    CTxDestination payoutDest;
    if (!ExtractDestination(ptx.scriptPayout, payoutDest)) {
        // should not happen as we checked script types before
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee-dest");
    }

    if (pindexPrev) {
        auto mnList = deterministicMNManager->GetListForBlock(pindexPrev);
        auto dmn = mnList.GetMN(ptx.proTxHash);
        if (!dmn) {
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-hash");
        }

        // don't allow reuse of payee key for other keys (don't allow people to put the payee key onto an online server)
        if (payoutDest == CTxDestination(dmn->pdmnState->keyIDOwner) || payoutDest == CTxDestination(ptx.keyIDVoting)) {
            return state.DoS(10, false, REJECT_INVALID, "bad-protx-payee-reuse");
        }

        Coin coin;
        if (!view.GetCoin(dmn->collateralOutpoint, coin) || coin.IsSpent()) {
            // this should never happen (there would be no dmn otherwise)
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-collateral");
        }

        // don't allow reuse of collateral key for other keys (don't allow people to put the collateral key onto an online server)
        CTxDestination collateralTxDest;
        if (!ExtractDestination(coin.out.scriptPubKey, collateralTxDest)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-collateral-dest");
        }
        if (collateralTxDest == CTxDestination(dmn->pdmnState->keyIDOwner) ||
            collateralTxDest == CTxDestination(ptx.keyIDVoting)) {
            return state.DoS(10, false, REJECT_INVALID, "bad-protx-collateral-reuse");
        }

        if (mnList.HasUniqueProperty(ptx.pubKeyOperator)) {
            auto otherDmn = mnList.GetUniquePropertyMN(ptx.pubKeyOperator);
            if (ptx.proTxHash != otherDmn->proTxHash) {
                return state.DoS(10, false, REJECT_DUPLICATE, "bad-protx-dup-key");
            }
        }

        if (!deterministicMNManager->IsDIP3Enforced(pindexPrev->nHeight)) {
            if (dmn->pdmnState->keyIDOwner != ptx.keyIDVoting) {
                return state.DoS(10, false, REJECT_INVALID, "bad-protx-key-not-same");
            }
        }

        if (!CheckInputsHash(tx, ptx, state)) {
            // pass the state returned by the function above
            return false;
        }
        if (check_sigs && !CheckHashSig(ptx, dmn->pdmnState->keyIDOwner, state)) {
            // pass the state returned by the function above
            return false;
        }
    }

    return true;
}

bool CheckProUpRevTx(const CTransaction &tx, const CBlockIndex *pindexPrev, CValidationState &state, bool check_sigs) {
    if (tx.nType != TRANSACTION_PROVIDER_UPDATE_REVOKE) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-type");
    }

    CProUpRevTx ptx;
    if (!GetTxPayload(tx, ptx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-payload");
    }

    if (ptx.nVersion == 0 || ptx.nVersion > CProUpRevTx::CURRENT_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-version");
    }

    // ptx.nReason < CProUpRevTx::REASON_NOT_SPECIFIED is always `false` since
    // ptx.nReason is unsigned and CProUpRevTx::REASON_NOT_SPECIFIED == 0
    if (ptx.nReason > CProUpRevTx::REASON_LAST) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-reason");
    }

    if (pindexPrev) {
        auto mnList = deterministicMNManager->GetListForBlock(pindexPrev);
        auto dmn = mnList.GetMN(ptx.proTxHash);
        if (!dmn)
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-hash");

        if (!CheckInputsHash(tx, ptx, state)) {
            // pass the state returned by the function above
            return false;
        }
        if (check_sigs && !CheckHashSig(ptx, dmn->pdmnState->pubKeyOperator.Get(), state)) {
            // pass the state returned by the function above
            return false;
        }
    }

    return true;
}

std::string CProRegTx::MakeSignString() const {
    std::string s;

    // We only include the important stuff in the string form...

    CTxDestination destPayout;
    std::string strPayout;
    if (ExtractDestination(scriptPayout, destPayout)) {
        strPayout = EncodeDestination(destPayout);
    } else {
        strPayout = HexStr(scriptPayout);
    }

    s += strPayout + "|";
    s += strprintf("%d", nOperatorReward) + "|";
    s += EncodeDestination(keyIDOwner) + "|";
    s += EncodeDestination(keyIDVoting) + "|";

    // ... and also the full hash of the payload as a protection agains malleability and replays
    s += ::SerializeHash(*this).ToString();

    return s;
}

std::string CProRegTx::ToString() const {
    CTxDestination dest;
    std::string payee = "unknown";
    if (ExtractDestination(scriptPayout, dest)) {
        payee = EncodeDestination(dest);
    }

    return strprintf(
            "CProRegTx(nVersion=%d, collateralOutpoint=%s, addr=%s, nOperatorReward=%f, ownerAddress=%s, pubKeyOperator=%s, votingAddress=%s, scriptPayout=%s)",
            nVersion, collateralOutpoint.ToStringShort(), addr.ToString(), (double) nOperatorReward / 100,
            EncodeDestination(keyIDOwner), pubKeyOperator.ToString(), EncodeDestination(keyIDVoting), payee);
}

std::string CProUpServTx::ToString() const {
    CTxDestination dest;
    std::string payee = "unknown";
    if (ExtractDestination(scriptOperatorPayout, dest)) {
        payee = EncodeDestination(dest);
    }

    return strprintf("CProUpServTx(nVersion=%d, proTxHash=%s, addr=%s, operatorPayoutAddress=%s)",
                     nVersion, proTxHash.ToString(), addr.ToString(), payee);
}

std::string CProUpRegTx::ToString() const {
    CTxDestination dest;
    std::string payee = "unknown";
    if (ExtractDestination(scriptPayout, dest)) {
        payee = EncodeDestination(dest);
    }

    return strprintf("CProUpRegTx(nVersion=%d, proTxHash=%s, pubKeyOperator=%s, votingAddress=%s, payoutAddress=%s)",
                     nVersion, proTxHash.ToString(), pubKeyOperator.ToString(), EncodeDestination(keyIDVoting), payee);
}

std::string CProUpRevTx::ToString() const {
    return strprintf("CProUpRevTx(nVersion=%d, proTxHash=%s, nReason=%d)",
                     nVersion, proTxHash.ToString(), nReason);
}
