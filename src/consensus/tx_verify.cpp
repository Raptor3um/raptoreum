// Copyright (c) 2017-2017 The Bitcoin Core developers
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

// TODO remove the following dependencies
#include <chain.h>
#include <coins.h>
#include <util/moneystr.h>
#include <version.h>

CChain& ChainActive();

static bool checkSpecialTxFee(const CTransaction &tx, CAmount& nFeeTotal, CAmount& specialTxFee, bool fFeeVerify = false) {
	if(tx.nVersion >= 3) {
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
                    if (!Params().IsAssetsActive(::ChainActive().Tip())) {
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
                    if (!Params().IsAssetsActive(::ChainActive().Tip())) {
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
                    if (!Params().IsAssetsActive(::ChainActive().Tip())) {
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
            } break;
            }
        }
    return true;
}

static const char* validateFutureCoin(const Coin& coin, int nSpendHeight)
{
    if (coin.nType == TRANSACTION_FUTURE) {
        CBlockIndex* confirmedBlockIndex = ::ChainActive()[coin.nHeight];
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


bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime)
{
    if (tx.nLockTime == 0)
        return true;
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    for (const auto& txin : tx.vin) {
        if (txin.nSequence != CTxIn::SEQUENCE_FINAL)
            return false;
    }
    return true;
}

std::pair<int, int64_t> CalculateSequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
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
        const CTxIn& txin = tx.vin[txinIndex];

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
            int64_t nCoinTime = block.GetAncestor(std::max(nCoinHeight-1, 0))->GetMedianTimePast();
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
            nMinTime = std::max(nMinTime, nCoinTime + (int64_t)((txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) - 1);
        } else {
            nMinHeight = std::max(nMinHeight, nCoinHeight + (int)(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) - 1);
        }
    }

    return std::make_pair(nMinHeight, nMinTime);
}

bool EvaluateSequenceLocks(const CBlockIndex& block, std::pair<int, int64_t> lockPair)
{
    assert(block.pprev);
    int64_t nBlockTime = block.pprev->GetMedianTimePast();
    if (lockPair.first >= block.nHeight || lockPair.second >= nBlockTime)
        return false;

    return true;
}

bool SequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    return EvaluateSequenceLocks(block, CalculateSequenceLocks(tx, flags, prevHeights, block));
}

unsigned int GetLegacySigOpCount(const CTransaction& tx)
{
    unsigned int nSigOps = 0;
    for (const auto& txin : tx.vin)
    {
        nSigOps += txin.scriptSig.GetSigOpCount(false);
    }
    for (const auto& txout : tx.vout)
    {
        nSigOps += txout.scriptPubKey.GetSigOpCount(false);
    }
    return nSigOps;
}

unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& inputs)
{
    if (tx.IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        const Coin& coin = inputs.AccessCoin(tx.vin[i].prevout);
        assert(!coin.IsSpent());
        const CTxOut &prevout = coin.out;
        if (prevout.scriptPubKey.IsPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(tx.vin[i].scriptSig);
    }
    return nSigOps;
}

unsigned int GetTransactionSigOpCount(const CTransaction& tx, const CCoinsViewCache& inputs, int flags)
{
    unsigned int nSigOps = GetLegacySigOpCount(tx);

    if (tx.IsCoinBase())
        return nSigOps;

    if (flags & SCRIPT_VERIFY_P2SH) {
        nSigOps += GetP2SHSigOpCount(tx, inputs);
    }

    return nSigOps;
}
//check to see where these now

inline bool checkOutput(const CTxOut& out, CValidationState& state, CAmount& nValueIn, std::map<std::pair<std::string, uint16_t>, CAmount>& nAssetVin, bool isV17active)
{
    // Check for negative or overflow values
    nValueIn += out.nValue;
    if (!MoneyRange(out.nValue, isV17active) || !MoneyRange(nValueIn, isV17active)) {
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

        std::pair<std::string, uint16_t> asset(assetTransfer.assetId, assetTransfer.uniqueId);
        if (nAssetVin.count(asset))
            nAssetVin[asset] += assetTransfer.nAmount;
        else
            nAssetVin.insert(std::make_pair(asset, assetTransfer.nAmount));

        //check unique asset amount, should be alway 1 COIN
        if (assetTransfer.isUnique && nAssetVin[asset] != 1 * COIN)
            return state.DoS(100, false, REJECT_INVALID, "bad-asset-inputvalues-outofrange");

        if (!MoneyRange(assetTransfer.nAmount) || !MoneyRange(nAssetVin.at(asset))) {
            return state.DoS(100, false, REJECT_INVALID, "bad-asset-inputvalues-outofrange");
        }
    }

    return true;
}

bool Consensus::CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, CAmount& txfee, CAmount& specialTxFee, bool isV17active, bool fFeeVerify)
{
    // are the actual inputs available?
    if (!inputs.HaveInputs(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputs-missingorspent", false,
                         strprintf("%s: inputs missing/spent", __func__));
    }

    CAmount nValueIn = 0;
    std::map<std::pair<std::string, uint16_t>, CAmount> nAssetVin;
    for (unsigned int i = 0; i < tx.vin.size(); ++i) {
        const COutPoint &prevout = tx.vin[i].prevout;
        const Coin& coin = inputs.AccessCoin(prevout);
        assert(!coin.IsSpent());

        // If prev is coinbase, check that it's matured
        if (coin.IsCoinBase() && nSpendHeight - coin.nHeight < COINBASE_MATURITY) {
            return state.Invalid(false,
                REJECT_INVALID, "bad-txns-premature-spend-of-coinbase",
                strprintf("tried to spend coinbase at depth %d", nSpendHeight - coin.nHeight));
        }

        if (!checkOutput(coin.out, state, nValueIn, nAssetVin, isV17active))
            return false;

        const char* futureValidationError = validateFutureCoin(coin, nSpendHeight);
        if (futureValidationError) {
            return state.DoS(100, false, REJECT_INVALID, futureValidationError);
        }
    }

    CAmount value_out = 0;
    std::map<std::pair<std::string, uint16_t>, CAmount> nAssetVout;
    for (auto out : tx.vout) {
        if (!checkOutput(out, state, value_out, nAssetVout, isV17active))
            return false;
    }

    if (tx.nType != TRANSACTION_MINT_ASSET) {
        for (const auto& outValue : nAssetVout) {
            if (!nAssetVin.count(outValue.first) || nAssetVin.at(outValue.first) != outValue.second)
                return state.DoS(100, false, REJECT_INVALID, "bad-asset-inputs-outputs-mismatch");
        }
        //Check for missing outputs
        for (const auto& outValue : nAssetVin) {
            if (!nAssetVout.count(outValue.first) || nAssetVout.at(outValue.first) != outValue.second)
                return state.DoS(100, false, REJECT_INVALID, "bad-asset-inputs-outputs-mismatch");
        }
    }

    if (nValueIn < value_out) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-in-belowout", false,
            strprintf("value in (%s) < value out (%s)", FormatMoney(nValueIn), FormatMoney(value_out)));
    }

    // Tally transaction fees
    const CAmount txfee_aux = nValueIn - value_out;
    if (!MoneyRange(txfee_aux, isV17active)) {
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
