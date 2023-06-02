// Copyright (c) 2018 The Bitcoin Core developers
// Copyright (c) 2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#include <wallet/coincontrol.h>
#include <wallet/wallet.h>

#include <util/system.h>

void CCoinControl::SetNull(bool fResetCoinType)
{
  destChange = CNoDestination();
  fAllowOtherInputs = false;
  fAllowWatchOnly = false;
  m_avoid_partial_spends = gArgs.GetBoolArg("-avoidpartialspends", DEFAULT_AVOIDPARTIALSPENDS);
  setSelected.clear();
  m_feerate.reset();
  fOverrideFeeRate = false;
  m_confirm_target.reset();
  m_fee_mode = FeeEstimateMode::UNSET;
  fRequireAllInputs = true;
  m_discard_feerate.reset();
  if (fResetCoinType) {
    nCoinType = CoinType::ALL_COINS;
  }
}
