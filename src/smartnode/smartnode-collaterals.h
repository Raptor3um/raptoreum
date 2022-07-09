/*
 * Copyright (c) 2020-2022 The Raptoreum developers
 * Distributed under the MIT/X11 software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 *
 *      Author: tri
 */

#include <amount.h>
#include <vector>
#include <unordered_map>
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
	int percentage;
};

class SmartnodeCollaterals {
protected:
	vector<Collateral> collaterals;
	vector<RewardPercentage> rewardPercentages;
	unordered_map<CAmount, int> collateralsHeightMap;

public:
	SmartnodeCollaterals(vector<Collateral> collaterals = {}, vector<RewardPercentage> rewardPercentages = {});
	CAmount getCollateral(int height) const;
	bool isValidCollateral(CAmount collateralAmount) const;
	bool isPayableCollateral(int height,CAmount collateralAmount) const;
	int getRewardPercentage(int height) const;
	void printCollateral() const;
	virtual ~SmartnodeCollaterals();
};

#endif /* SRC_SMARTNODE_SMARTNODE_COLLATERALS_H_ */
