/*
 * Copyright (c) 2020 The Raptoreum developers
 * Distributed under the MIT/X11 software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 *      Author: tri
 */

#include "smartnode-collaterals.h"
#include <limits.h>

SmartnodeCollaterals::SmartnodeCollaterals(vector<Collateral> collaterals, vector<RewardPercentage> rewardPercentages) {
	this->collaterals = collaterals;
	this->rewardPercentages = rewardPercentages;

}

CAmount SmartnodeCollaterals::getCollateral(int height) const {
	for (auto& it : this->collaterals) {
		if(it.height == INT_MAX || height <= it.height) {
			return it.amount;
		}
	}
	return 0;
}

float SmartnodeCollaterals::getRewardPercentage(int height) const {
	for (auto& it : this->rewardPercentages) {
		if(it.height == INT_MAX || height <= it.height) {
			return it.percentage;
		}
	}
	return 0;
}

SmartnodeCollaterals::~SmartnodeCollaterals() {
	this->collaterals.clear();
}

bool SmartnodeCollaterals::isValidCollateral(int height) const {
	return true;
}

bool SmartnodeCollaterals::isPayableCollateral(int height) const {
	return true;
}
