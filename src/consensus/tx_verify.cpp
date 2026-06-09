// Copyright (c) 2017-2023 The Bitcoin Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/tx_verify.h>

#include <consensus/consensus.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <consensus/validation.h>

#include <chainparams.h>
#include <future/fee.h>
#include <spork.h>
//#include <future/utils.h>
#include <evo/specialtx.h>
#include <evo/providertx.h>
#include <timedata.h>
#include <assets/assets.h>
#include <assets/assetstype.h>
#include <evm/apply.h>   // kWeisPerSatoshi
#include <evm/evmtx.h>   // CEvmFundTx

// TODO remove the following dependencies
#include <chain.h>
#include <coins.h>
#include <util/moneystr.h>
#include <version.h>

CChain &ChainActive();

static bool
checkSpecialTxFee(const CTransaction &tx, CAmount &nFeeTotal, CAmount &specialTxFee, bool fFeeVerify = false) {
    if (tx.nVersion >= 3) {
        switch (tx.nType) {
            case TRANSACTION_FUTURE: {
                CFutureTx ftx;
                if (GetTxPayload(tx.vExtraPayload, ftx)) {
                    if (!Params().IsFutureActive(::ChainActive().Tip())) {
                        return false;
                    }
                    bool futureEnabled = sporkManager.IsSporkActive(SPORK_22_SPECIAL_TX_FEE);
                    if (futureEnabled && fFeeVerify && ftx.fee != getFutureFees()) {
                        return false;
                    }
                    specialTxFee = ftx.fee * COIN;
                    nFeeTotal -= specialTxFee;
                }
                break;
            }
            case TRANSACTION_NEW_ASSET: {
                CNewAssetTx asset;
                if (GetTxPayload(tx.vExtraPayload, asset)) {
                    if (!Updates().IsAssetsActive(::ChainActive().Tip())) {
                        return false;
                    }
                    bool assetsEnabled = sporkManager.IsSporkActive(SPORK_22_SPECIAL_TX_FEE);
                    if (assetsEnabled && fFeeVerify && asset.fee != getAssetsFees()) {
                        return false;
                    }
                    specialTxFee = asset.fee * COIN;
                    nFeeTotal -= specialTxFee;
                }
                break;
            }
            case TRANSACTION_UPDATE_ASSET: {
                CUpdateAssetTx asset;
                if (GetTxPayload(tx.vExtraPayload, asset)) {
                    if (!Updates().IsAssetsActive(::ChainActive().Tip())) {
                        return false;
                    }
                    bool assetsEnabled = sporkManager.IsSporkActive(SPORK_22_SPECIAL_TX_FEE);
                    if (assetsEnabled && fFeeVerify && asset.fee != getAssetsFees()) {
                        return false;
                    }
                    specialTxFee = asset.fee * COIN;
                    nFeeTotal -= specialTxFee;
                }
                break;
            }
            case TRANSACTION_MINT_ASSET: {
                CMintAssetTx asset;
                if (GetTxPayload(tx.vExtraPayload, asset)) {
                    if (!Updates().IsAssetsActive(::ChainActive().Tip())) {
                        return false;
                    }
                    bool assetsEnabled = sporkManager.IsSporkActive(SPORK_22_SPECIAL_TX_FEE);
                    if (assetsEnabled && fFeeVerify && asset.fee != getAssetsFees()) {
                        return false;
                    }
                    specialTxFee = asset.fee * COIN;
                    nFeeTotal -= specialTxFee;
                }
                break;
            }
            // EVM transactions (Phase 1.4+ scaffolding).
            //
            // Activation gate: until UPDATE_EVM is registered with a
            // heightActivated in chainparams.cpp, IsEvmActive() returns false
            // and these cases reject. After activation, the cases fall through
            // (return true) — Phase 2 will replace the body with full EIP-1559
            // fee accounting (gasLimit * maxFeePerGas) once the EVM execution
            // pipeline lands.
            //
            // Why a defense-in-depth check here even though CheckSpecialTx also
            // gates on IsEvmActive: tx_verify runs in multiple contexts
            // (mempool, block validation, RPC). Ensuring the activation gate
            // is checked at every entry point means a future refactor that
            // bypasses CheckSpecialTx still cannot accept an EVM tx pre-fork.
            // EVM_FUND (AAL: UTXO -> EVM funding). The funded amount must
            // LEAVE the UTXO money supply WITHOUT being claimable by the
            // coinbase: it reappears as EVM balance (ApplyEvmFundTx). So,
            // unlike the asset/future fees above, it is subtracted from the
            // miner-claimable fee but NOT returned via specialTxFee (which
            // is added to the coinbase value allowance in ConnectBlock).
            // Leaving specialTxFee at 0 ensures the miner cannot claim it;
            // CheckTxInputs' `txfee < 0` guard then enforces that the
            // inputs cover (fund amount + a non-negative miner fee).
            case TRANSACTION_EVM_FUND: {
                if (!Updates().IsEvmActive(::ChainActive().Tip())) {
                    return false;
                }
                evm::CEvmFundTx fund;
                if (GetTxPayload(tx.vExtraPayload, fund)) {
                    if (fund.amount % evm::kWeisPerSatoshi != 0) {
                        return false;  // non-round amount cannot settle
                    }
                    const CAmount fundSat = static_cast<CAmount>(
                        fund.amount / evm::kWeisPerSatoshi);
                    nFeeTotal -= fundSat;  // removed from UTXO supply
                    // specialTxFee deliberately left 0 (not coinbase-claimable).
                }
                break;
            }
            case TRANSACTION_EVM_DEPLOY:
            case TRANSACTION_EVM_CALL:
            case TRANSACTION_EVM_SPEND:
            case TRANSACTION_NEW_EVM_ASSET:    // reserved, Phase 4
            case TRANSACTION_UPDATE_EVM_ASSET: // reserved, Phase 4
            case TRANSACTION_MINT_EVM_ASSET:   // reserved, Phase 4
            case TRANSACTION_WRAP_ASSET:       // reserved, Phase 5+
            case TRANSACTION_UNWRAP_ASSET: {   // reserved, Phase 5+
                if (!Updates().IsEvmActive(::ChainActive().Tip())) {
                    return false;
                }
                // Phase 2 TODO: parse the payload, compute EIP-1559 fee:
                //     specialTxFee = payload.gasLimit * payload.maxFeePerGas
                //   then debit nFeeTotal accordingly. For now the activation
                //   gate above is the only block.
                break;
            }
                break;
        }
    }
    return true;
}

static const char *validateFutureCoin(const Coin &coin, int nSpendHeight) {
    if (coin.nType == TRANSACTION_FUTURE) {
        CBlockIndex *confirmedBlockIndex = ::ChainActive()[coin.nHeight];
        if (confirmedBlockIndex) {
            int64_t adjustCurrentTime = GetAdjustedTime();
            uint32_t confirmedTime = confirmedBlockIndex->GetBlockTime();
            CFutureTx futureTx;
            if (GetTxPayload(coin.vExtraPayload, futureTx)) {
                bool isBlockMature = futureTx.maturity >= 0 && nSpendHeight - coin.nHeight >= futureTx.maturity;
                bool isTimeMature = futureTx.lockTime >= 0 && adjustCurrentTime - confirmedTime >= futureTx.lockTime;
                bool canSpend = isBlockMature || isTimeMature;
                if (!canSpend) {
                    return "bad-txns-premature-spend-of-future";
                }
                return nullptr;
            }
            return "bad-txns-unable-to-parse-future";
        }
        // should not get here
        return "bad-txns-unable-to-block-index-for-future";
    }
    return nullptr;
}


bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime) {
    if (tx.nLockTime == 0)
        return true;
    if ((int64_t) tx.nLockTime < ((int64_t) tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t) nBlockHeight : nBlockTime))
        return true;
    for (const auto &txin: tx.vin) {
        if (txin.nSequence != CTxIn::SEQUENCE_FINAL)
            return false;
    }
    return true;
}

std::pair<int, int64_t>
CalculateSequenceLocks(const CTransaction &tx, int flags, std::vector<int> *prevHeights, const CBlockIndex &block) {
    assert(prevHeights->size() == tx.vin.size());

    // Will be set to the equivalent height- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid height/time, so
    // use -1 to have the effect of any height or time being valid.
    int nMinHeight = -1;
    int64_t nMinTime = -1;

    // tx.nVersion is signed integer so requires cast to unsigned otherwise
    // we would be doing a signed comparison and half the range of nVersion
    // wouldn't support BIP 68.
    bool fEnforceBIP68 = static_cast<uint32_t>(tx.nVersion) >= 2
                         && flags & LOCKTIME_VERIFY_SEQUENCE;

    // Do not enforce sequence numbers as a relative lock time
    // unless we have been instructed to
    if (!fEnforceBIP68) {
        return std::make_pair(nMinHeight, nMinTime);
    }

    for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
        const CTxIn &txin = tx.vin[txinIndex];

        // Sequence numbers with the most significant bit set are not
        // treated as relative lock-times, nor are they given any
        // consensus-enforced meaning at this point.
        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) {
            // The height of this input is not relevant for sequence locks
            (*prevHeights)[txinIndex] = 0;
            continue;
        }

        int nCoinHeight = (*prevHeights)[txinIndex];

        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG) {
            int64_t nCoinTime = block.GetAncestor(std::max(nCoinHeight - 1, 0))->GetMedianTimePast();
            // NOTE: Subtract 1 to maintain nLockTime semantics
            // BIP 68 relative lock times have the semantics of calculating
            // the first block or time at which the transaction would be
            // valid. When calculating the effective block time or height
            // for the entire transaction, we switch to using the
            // semantics of nLockTime which is the last invalid block
            // time or height.  Thus we subtract 1 from the calculated
            // time or height.

            // Time-based relative lock-times are measured from the
            // smallest allowed timestamp of the block containing the
            // txout being spent, which is the median time past of the
            // block prior.
            nMinTime = std::max(nMinTime, nCoinTime + (int64_t)(
                    (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) - 1);
        } else {
            nMinHeight = std::max(nMinHeight, nCoinHeight + (int) (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) - 1);
        }
    }

    return std::make_pair(nMinHeight, nMinTime);
}

bool EvaluateSequenceLocks(const CBlockIndex &block, std::pair<int, int64_t> lockPair) {
    assert(block.pprev);
    int64_t nBlockTime = block.pprev->GetMedianTimePast();
    if (lockPair.first >= block.nHeight || lockPair.second >= nBlockTime)
        return false;

    return true;
}

bool SequenceLocks(const CTransaction &tx, int flags, std::vector<int> *prevHeights, const CBlockIndex &block) {
    return EvaluateSequenceLocks(block, CalculateSequenceLocks(tx, flags, prevHeights, block));
}

unsigned int GetLegacySigOpCount(const CTransaction &tx) {
    unsigned int nSigOps = 0;
    for (const auto &txin: tx.vin) {
        nSigOps += txin.scriptSig.GetSigOpCount(false);
    }
    for (const auto &txout: tx.vout) {
        nSigOps += txout.scriptPubKey.GetSigOpCount(false);
    }
    return nSigOps;
}

unsigned int GetP2SHSigOpCount(const CTransaction &tx, const CCoinsViewCache &inputs) {
    if (tx.IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const Coin &coin = inputs.AccessCoin(tx.vin[i].prevout);
        assert(!coin.IsSpent());
        const CTxOut &prevout = coin.out;
        if (prevout.scriptPubKey.IsPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(tx.vin[i].scriptSig);
    }
    return nSigOps;
}

unsigned int GetTransactionSigOpCount(const CTransaction &tx, const CCoinsViewCache &inputs, int flags) {
    unsigned int nSigOps = GetLegacySigOpCount(tx);

    if (tx.IsCoinBase())
        return nSigOps;

    if (flags & SCRIPT_VERIFY_P2SH) {
        nSigOps += GetP2SHSigOpCount(tx, inputs);
    }

    return nSigOps;
}

inline bool checkOutput(const CTxOut &out, CValidationState &state, CAmount &nValueIn,
                        std::map <std::string, CAmount> &nAssetVin,
                        std::map <std::string, std::vector<std::pair<uint64_t, uint64_t>>> &nMapids) {
    // Check for negative or overflow values
    nValueIn += out.nValue;
    if (!MoneyRange(out.nValue) || !MoneyRange(nValueIn)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputvalues-outofrange");
    }

    if (out.scriptPubKey.IsAssetScript()) {
        if (out.nValue != 0)
            return state.DoS(100, false, REJECT_INVALID, "bad-asset-value-outofrange");

        CAssetTransfer assetTransfer;
        if (!GetTransferAsset(out.scriptPubKey, assetTransfer)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-asset-transfer");
        }

        if (!validateAmount(assetTransfer.assetId, assetTransfer.nAmount)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-assets-transfer-amount");
        }

        if (nAssetVin.count(assetTransfer.assetId))
            nAssetVin[assetTransfer.assetId] += assetTransfer.nAmount;
        else
            nAssetVin.insert(std::make_pair(assetTransfer.assetId, assetTransfer.nAmount));

        if (assetTransfer.isUnique) {
            uint64_t idRange = assetTransfer.uniqueId + assetTransfer.nAmount / COIN;

            if (!nMapids.count(assetTransfer.assetId))
                nMapids.insert({assetTransfer.assetId, {}});

            nMapids[assetTransfer.assetId].emplace_back(std::make_pair(assetTransfer.uniqueId, idRange));
        }

        if (!MoneyRange(assetTransfer.nAmount) || !MoneyRange(nAssetVin.at(assetTransfer.assetId))) {
            return state.DoS(100, false, REJECT_INVALID, "bad-asset-inputvalues-outofrange");
        }
    }

    return true;
}

inline bool checkAssetsOutputs(CValidationState &state, std::map <std::string, CAmount> nAssetVin,
                                    std::map <std::string, CAmount> nAssetVout,
                                    std::map <std::string, std::vector<std::pair<uint64_t, uint64_t>>> nVinIds, 
                                    std::map <std::string, std::vector<std::pair<uint64_t, uint64_t>>> nVoutIds) {
    //check vin/vout amouts
    for (const auto &outValue: nAssetVout) {
        if (!nAssetVin.count(outValue.first) || nAssetVin.at(outValue.first) != outValue.second)
            return state.DoS(100, false, REJECT_INVALID, "bad-asset-inputs-outputs-mismatch");
    }
    //Check for missing outputs
    for (const auto &inValue: nAssetVin) {
        if (!nAssetVout.count(inValue.first) || nAssetVout.at(inValue.first) != inValue.second)
            return state.DoS(100, false, REJECT_INVALID, "bad-asset-inputs-outputs-mismatch");
    }

    for (auto &vin : nVinIds){
        vin.second = combineUniqueIdPairs(vin.second);
    }
    
    for (auto &vout : nVoutIds){
        vout.second = combineUniqueIdPairs(vout.second);
    }

    //check uniqueids
    for (auto vin : nVinIds){
        std::unordered_map<uint64_t, uint64_t> mapIds;
        mapIds.clear();
        for (auto pair : nVoutIds[vin.first]){
            mapIds[pair.first] = pair.second;
        }
        for (auto vinIds : vin.second){
            if (!(mapIds.find(vinIds.first) != mapIds.end() && mapIds[vinIds.first] == vinIds.second)){
                return state.DoS(100, false, REJECT_INVALID, "bad-asset-id-inputs-outputs-mismatch");
            }
        }
    }
    return true;
}

bool Consensus::CheckTxInputs(const CTransaction &tx, CValidationState &state, const CCoinsViewCache &inputs,
                              int nSpendHeight, CAmount &txfee, CAmount &specialTxFee, bool fFeeVerify) {
    // are the actual inputs available?
    if (!inputs.HaveInputs(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-missingorspent", false,
                         strprintf("%s: inputs missing/spent", __func__));
    }

    CAmount nValueIn = 0;
    std::map <std::string, CAmount> nAssetVin;
    std::map <std::string, std::vector<std::pair<uint64_t, uint64_t>>> mapVinIds;

    for (unsigned int i = 0; i < tx.vin.size(); ++i) {
        const COutPoint &prevout = tx.vin[i].prevout;
        const Coin &coin = inputs.AccessCoin(prevout);
        assert(!coin.IsSpent());

        // If prev is coinbase, check that it's matured
        if (coin.IsCoinBase() && nSpendHeight - coin.nHeight < COINBASE_MATURITY) {
            return state.Invalid(false,
                                 REJECT_INVALID, "bad-txns-premature-spend-of-coinbase",
                                 strprintf("tried to spend coinbase at depth %d", nSpendHeight - coin.nHeight));
        }

        if (!checkOutput(coin.out, state, nValueIn, nAssetVin, mapVinIds))
            return false;

        const char *futureValidationError = validateFutureCoin(coin, nSpendHeight);
        if (futureValidationError) {
            return state.DoS(100, false, REJECT_INVALID, futureValidationError);
        }
    }
    
    CAmount value_out = 0;
    std::map <std::string, CAmount> nAssetVout;
    std::map <std::string, std::vector<std::pair<uint64_t, uint64_t>>> mapVoutIds;

    for (auto out: tx.vout) {
        if (!checkOutput(out, state, value_out, nAssetVout, mapVoutIds))
            return false;
    }

    // MINT creates units (outputs without inputs); WRAP burns units to the
    // EVM ledger (inputs exceed outputs by the wrapped amount); UNWRAP mints
    // units back from the EVM ledger (outputs exceed inputs). All three are
    // exempt from the strict per-tx input==output asset conservation here.
    // The exempted txs each re-impose a CONSTRAINED conservation in their
    // own CheckSpecialTx handler (CheckWrapAssetTx / CheckUnwrapAssetTx /
    // CheckMintAssetTx) so the exemption cannot be abused to create or
    // destroy arbitrary assets.
    if (tx.nType != TRANSACTION_MINT_ASSET &&
        tx.nType != TRANSACTION_WRAP_ASSET &&
        tx.nType != TRANSACTION_UNWRAP_ASSET) {
        if (!checkAssetsOutputs(state, nAssetVin, nAssetVout, mapVinIds, mapVoutIds))
            return false;
    }

    if (nValueIn < value_out) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-in-belowout", false,
                         strprintf("value in (%s) < value out (%s)", FormatMoney(nValueIn), FormatMoney(value_out)));
    }

    // Tally transaction fees
    const CAmount txfee_aux = nValueIn - value_out;
    if (!MoneyRange(txfee_aux)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-outofrange");
    }
    txfee = txfee_aux;

    if (!checkSpecialTxFee(tx, txfee, specialTxFee, fFeeVerify)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-wrong-future-fee-or-not-enable");
    }

    if (txfee < 0) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-too-low", false,
                         strprintf("fee (%s), special tx fee (%s)", FormatMoney(txfee), FormatMoney(specialTxFee)));
    }
    return true;
}
