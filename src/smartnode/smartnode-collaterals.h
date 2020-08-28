/*
 * Copyright (c) 2020 The Raptoreum developers
 * Distributed under the MIT/X11 software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 *
 *      Author: tri
 */

#include "amount.h"
#include <vector>
using namespace std;

#ifndef SRC_SMARTNODE_SMARTNODE_COLLATERALS_H_
#define SRC_SMARTNODE_SMARTNODE_COLLATERALS_H_
class SmartNodeCollaterals;
struct Collateral {
	int height;
	CAmount amount;
};

struct RewardPercentage {
	int height;
	float percentage;
};

class SmartnodeCollaterals {
protected:
	vector<Collateral> collaterals;
	vector<RewardPercentage> rewardPercentages;
public:
	SmartnodeCollaterals(vector<Collateral> collaterals = {}, vector<RewardPercentage> rewardPercentages = {});
	CAmount getCollateral(int height) const;
	float getRewardPercentage(int height) const;
	virtual ~SmartnodeCollaterals();
};

#endif /* SRC_SMARTNODE_SMARTNODE_COLLATERALS_H_ */
