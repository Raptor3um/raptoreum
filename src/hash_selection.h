/* Copyright (c) 2020 The Raptoreum Core developers
 * Distributed under the MIT software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 *
 * hash_selection.h
 *
 *  Created on: May 11, 2018
 *      Author: tri
 */

#ifndef RAPTOREUM_SELECTION_H_
#define RAPTOREUM_SELECTION_H_

#include "uint256.h"
#include <string>
#include <vector>

#include "crypto/sph_blake.h"
#include "crypto/sph_bmw.h"
#include "crypto/sph_groestl.h"
#include "crypto/sph_jh.h"
#include "crypto/sph_keccak.h"
#include "crypto/sph_skein.h"
#include "crypto/sph_luffa.h"
#include "crypto/sph_cubehash.h"
#include "crypto/sph_shavite.h"
#include "crypto/sph_simd.h"
#include "crypto/sph_echo.h"
#include "crypto/sph_hamsi.h"
#include "crypto/sph_fugue.h"
#include "crypto/sph_shabal.h"
#include "crypto/sph_whirlpool.h"
extern "C" {
#include "crypto/sph_sha2.h"
}

extern std::vector<std::vector<int>> GR_GROUP;

void coreHash(const void *toHash, uint512* hash, int lenToHash, int hashSelection);
void cnHash(uint512* toHash, uint512* hash, int lenToHash, int hashSelection);

class HashSelection {
public:
	HashSelection(const uint256 prevBlockHash, const std::vector<int> algoIndexes, std::vector<int> cnIndexes) {
		this->PrevBlockHash = prevBlockHash;
		this->algoMap = {
				"Blake",      //0
				"Bmw",        //1
				"Groestl",    //2
				"Jh",         //3
				"Keccak",     //4
				"Skein",      //5
				"Luffa",      //6
				"Cubehash",   //7
				"Shavite",    //8
				"Simd",       //9
				"Echo",       //A
				"Jamsi",      //B
				"Fugue",      //C
				"Shabal",     //D
				"Whirlpool",  //E
				"Sha512"     //F
		};
		this->cnVariantMap = {
				"CNDark",        //0
				"CNDarklite",    //1
				"CNFast",        //2
				"CNLite",        //3
				"CNTurtle",      //4
				"CNTurtlelite"   //5

		};
		//printf("before getRandomIndexes cnIndexes\n");
		this->cnIndexes = this->getRandomIndexes(cnIndexes);
		//printf("before getRandomIndexes cnIndexes\n");
		this->algoIndexes = this->getRandomIndexes(algoIndexes);
	}
//	int getHashSelection(int index);
//	static int getGroupHashSelection(uint256 blockHash);
	std::vector<int> getRandomIndexes(std::vector<int> indexes);
	std::string getHashSelectionString();
	std::vector<int> getCnIndexes() {return  this->cnIndexes;}
	std::vector<int> getAlgoIndexes() {return this->algoIndexes;}
protected:
	uint256 PrevBlockHash;
	std::vector<int> algoIndexes;
	std::vector<int> cnIndexes;
	std::vector<std::string> cnVariantMap;
	std::vector<std::string> algoMap;

};

#endif /* RAPTOREUM_SELECTION_H_ */
