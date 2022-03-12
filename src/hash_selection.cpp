/* Copyright (c) 2020 The Raptoreum Core developers
 * Distributed under the MIT software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 *
 *  Created on: May 11, 2018
 *      Author: tri
 */

#include <hash_selection.h>
#include <cryptonote/slow-hash.h>

std::vector<std::vector<int>> GR_GROUP = {
		{0,1,2,3,4},
		{5,6,7,8,9},
		{10,11,12,13,14}
};

//int HashSelection::getHashSelection(int index) {
//	assert(index >= 0);
//	assert(index < size);
//	const int startNibblesHash = 64 - size;
//	int hashSelection = PrevBlockHash.GetNibble(startNibblesHash + index);
//	hashSelection = hashSelection % size;
//	return(hashSelection);
//}

//int HashSelection::getGroupHashSelection(uint256 blockHash) {
//	return blockHash.GetNibble(60);
//}

std::vector<int> HashSelection::getRandomIndexes(std::vector<int> indexes) {
	std::vector<int> groupIndexes;
	unsigned int totalIndexes = indexes.size();
	unsigned int indexCount = 0;
	int i = 63;
	//printf("%d index=", totalIndexes);
	for(; i >=0; i--) {
		unsigned int hashSelection = this->PrevBlockHash.GetNibble(i);
		if(hashSelection >= totalIndexes) {
			hashSelection = hashSelection % totalIndexes;
		}
		int index = indexes[hashSelection];
		if(index >=0)  {
			//printf("%d,", index);
			groupIndexes.push_back(index);
			indexes[hashSelection] = -1;
			indexCount++;
		}
		if(indexCount == indexes.size()) {
			break;
		}
	}
	//printf("indexCount=%d------------------------\n",indexCount);
	if(i < 0 && indexCount < totalIndexes) {
		for(unsigned int j=0; j<indexes.size(); j++) {
			int index = indexes[j];
			if(index >=0) {
				//printf("%d,", index);
				groupIndexes.push_back(index);
			}
		}
	}
	return groupIndexes;
}

std::string HashSelection::getHashSelectionString() {
	std::string selectedAlgoes;
	int i = 0;
	for(; i < 5; i++) {
		//int hashSelection = getHashSelection(i);
		int selectedAlgoIndex = this->algoIndexes[i];
		std::string selectedAlgo = this->algoMap[selectedAlgoIndex];
		selectedAlgoes.append(selectedAlgo);
	}
	std::string selectedCN_1 = this->cnVariantMap[this->cnIndexes[0]];
	selectedAlgoes.append(selectedCN_1);
	for(; i < 10; i++) {
		//int hashSelection = getHashSelection(i);
		int selectedAlgoIndex = this->algoIndexes[i];
		std::string selectedAlgo = this->algoMap[selectedAlgoIndex];
		selectedAlgoes.append(selectedAlgo);
	}
	std::string selectedCN_2 =  this->cnVariantMap[this->cnIndexes[1]];
	selectedAlgoes.append(selectedCN_2);
	for(; i < 15; i++) {
		//int hashSelection = getHashSelection(i);
		int selectedAlgoIndex = this->algoIndexes[i];
		std::string selectedAlgo = this->algoMap[selectedAlgoIndex];
		selectedAlgoes.append(selectedAlgo);
	}
	std::string selectedCN_3 =  this->cnVariantMap[this->cnIndexes[2]];
	selectedAlgoes.append(selectedCN_3);
	return selectedAlgoes;
}
void coreHash(const void *toHash, uint512* hash, int lenToHash, int hashSelection) {
	sph_blake512_context     ctx_blake;      //0
	sph_bmw512_context       ctx_bmw;        //1
	sph_groestl512_context   ctx_groestl;    //2
	sph_jh512_context        ctx_jh;         //3
	sph_keccak512_context    ctx_keccak;     //4
	sph_skein512_context     ctx_skein;      //5
	sph_luffa512_context     ctx_luffa;      //6
	sph_cubehash512_context  ctx_cubehash;   //7
	sph_shavite512_context   ctx_shavite;    //8
	sph_simd512_context      ctx_simd;       //9
	sph_echo512_context      ctx_echo;       //A
	sph_hamsi512_context     ctx_hamsi;      //B
	sph_fugue512_context     ctx_fugue;      //C
	sph_shabal512_context    ctx_shabal;     //D
	sph_whirlpool_context    ctx_whirlpool;  //E
	sph_sha512_context       ctx_sha512;     //F
	switch(hashSelection) {
		case 0:
			sph_blake512_init(&ctx_blake);
			sph_blake512 (&ctx_blake, toHash, lenToHash);
			sph_blake512_close(&ctx_blake, static_cast<void*>(hash));
			break;
		case 1:
			sph_bmw512_init(&ctx_bmw);
			sph_bmw512 (&ctx_bmw, toHash, lenToHash);
			sph_bmw512_close(&ctx_bmw, static_cast<void*>(hash));
			break;
		case 2:
			sph_groestl512_init(&ctx_groestl);
			sph_groestl512 (&ctx_groestl, toHash, lenToHash);
			sph_groestl512_close(&ctx_groestl, static_cast<void*>(hash));
			break;
		case 3:
			sph_jh512_init(&ctx_jh);
			sph_jh512 (&ctx_jh, toHash, lenToHash);
			sph_jh512_close(&ctx_jh, static_cast<void*>(hash));
			break;
		case 4:
			sph_keccak512_init(&ctx_keccak);
			sph_keccak512 (&ctx_keccak, toHash, lenToHash);
			sph_keccak512_close(&ctx_keccak, static_cast<void*>(hash));
			break;
		case 5:
			sph_skein512_init(&ctx_skein);
			sph_skein512 (&ctx_skein, toHash, lenToHash);
			sph_skein512_close(&ctx_skein, static_cast<void*>(hash));
			break;
		case 6:
			sph_luffa512_init(&ctx_luffa);
			sph_luffa512 (&ctx_luffa, toHash, lenToHash);
			sph_luffa512_close(&ctx_luffa, static_cast<void*>(hash));
			break;
		case 7:
			sph_cubehash512_init(&ctx_cubehash);
			sph_cubehash512 (&ctx_cubehash, toHash, lenToHash);
			sph_cubehash512_close(&ctx_cubehash, static_cast<void*>(hash));
			break;
		case 8:
			sph_shavite512_init(&ctx_shavite);
			sph_shavite512(&ctx_shavite, toHash, lenToHash);
			sph_shavite512_close(&ctx_shavite, static_cast<void*>(hash));
			break;
		case 9:
			sph_simd512_init(&ctx_simd);
			sph_simd512 (&ctx_simd, toHash, lenToHash);
			sph_simd512_close(&ctx_simd, static_cast<void*>(hash));
			break;
		case 10:
			sph_echo512_init(&ctx_echo);
			sph_echo512 (&ctx_echo, toHash, lenToHash);
			sph_echo512_close(&ctx_echo, static_cast<void*>(hash));
			break;
	   case 11:
			sph_hamsi512_init(&ctx_hamsi);
			sph_hamsi512 (&ctx_hamsi, toHash, lenToHash);
			sph_hamsi512_close(&ctx_hamsi, static_cast<void*>(hash));
			break;
	   case 12:
			sph_fugue512_init(&ctx_fugue);
			sph_fugue512 (&ctx_fugue, toHash, lenToHash);
			sph_fugue512_close(&ctx_fugue, static_cast<void*>(hash));
			break;
	   case 13:
			sph_shabal512_init(&ctx_shabal);
			sph_shabal512 (&ctx_shabal, toHash, lenToHash);
			sph_shabal512_close(&ctx_shabal, static_cast<void*>(hash));
			break;
	   case 14:
			sph_whirlpool_init(&ctx_whirlpool);
			sph_whirlpool(&ctx_whirlpool, toHash, lenToHash);
			sph_whirlpool_close(&ctx_whirlpool, static_cast<void*>(hash));
			break;
	   case 15:
			sph_sha512_init(&ctx_sha512);
			sph_sha512 (&ctx_sha512, toHash, lenToHash);
			sph_sha512_close(&ctx_sha512, static_cast<void*>(hash));
			break;
	}
}

void cnHash(uint512* toHash, uint512* hash, int lenToHash, int hashSelection) {

    const char* input  = reinterpret_cast<char*>(toHash->begin());
    char*       output = reinterpret_cast<char*>(hash->begin());

    switch (hashSelection)
    {
        case 0 : crypto::cryptonight_dark_hash      (input, output, lenToHash, 1); break;
        case 1 : crypto::cryptonight_darklite_hash  (input, output, lenToHash, 1); break;
        case 2 : crypto::cryptonight_cnfast_hash    (input, output, lenToHash, 1); break;
        case 3 : crypto::cryptonight_cnlite_hash    (input, output, lenToHash, 1); break;
        case 4 : crypto::cryptonight_turtle_hash    (input, output, lenToHash, 1); break;
        case 5 : crypto::cryptonight_turtlelite_hash(input, output, lenToHash, 1); break;
    }
}
