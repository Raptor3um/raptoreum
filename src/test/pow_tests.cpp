// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <pow.h>
#include <random.h>
#include <test/test_raptoreum.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup
)

// Helper to create CBlockIndex for testing:
CBlockIndex MakeIdx(int nHeight, uint32_t nTime, uint32_t nBits, CBlockIndex *pPrev) {
    CBlockIndex ret;
    ret.nHeight = nHeight;
    ret.nTime = nTime;
    ret.nBits = nBits;
    ret.pprev = pPrev;
    return ret;
}

/* Test calculation of next difficulty target with DGW */
BOOST_AUTO_TEST_CASE(get_next_work)
        {
                const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);

                // build the chain of 61 blocks
                // data from: getblockheaders 92c4c355e78a14e1d457b47cba349aabdef2755dfb1a2208ddd1b179d1a45606 61
                CBlockIndex blockIndex123456 = MakeIdx(123456, 1629456114, 0x1d086bf0, nullptr);
                CBlockIndex blockIndex123457 = MakeIdx(123457, 1629456145, 0x1d086414, &blockIndex123456);
                CBlockIndex blockIndex123458 = MakeIdx(123458, 1629456146, 0x1d085246, &blockIndex123457);
                CBlockIndex blockIndex123459 = MakeIdx(123459, 1629456327, 0x1d07fdea, &blockIndex123458);
                CBlockIndex blockIndex123460 = MakeIdx(123460, 1629456563, 0x1d082894, &blockIndex123459);
                CBlockIndex blockIndex123461 = MakeIdx(123461, 1629456668, 0x1d086f52, &blockIndex123460);
                CBlockIndex blockIndex123462 = MakeIdx(123462, 1629456694, 0x1d083168, &blockIndex123461);
                CBlockIndex blockIndex123463 = MakeIdx(123463, 1629456708, 0x1d081952, &blockIndex123462);
                CBlockIndex blockIndex123464 = MakeIdx(123464, 1629456728, 0x1d07fe7c, &blockIndex123463);
                CBlockIndex blockIndex123465 = MakeIdx(123465, 1629456956, 0x1d07e634, &blockIndex123464);
                CBlockIndex blockIndex123466 = MakeIdx(123466, 1629457466, 0x1d080fd7, &blockIndex123465);
                CBlockIndex blockIndex123467 = MakeIdx(123467, 1629457493, 0x1d089514, &blockIndex123466);
                CBlockIndex blockIndex123468 = MakeIdx(123468, 1629457670, 0x1d08960c, &blockIndex123467);
                CBlockIndex blockIndex123469 = MakeIdx(123469, 1629457683, 0x1d08b0bb, &blockIndex123468);
                CBlockIndex blockIndex123470 = MakeIdx(123470, 1629457724, 0x1d08af09, &blockIndex123469);
                CBlockIndex blockIndex123471 = MakeIdx(123471, 1629457864, 0x1d08a0c6, &blockIndex123470);
                CBlockIndex blockIndex123472 = MakeIdx(123472, 1629457903, 0x1d08c8f3, &blockIndex123471);
                CBlockIndex blockIndex123473 = MakeIdx(123473, 1629457917, 0x1d08d816, &blockIndex123472);
                CBlockIndex blockIndex123474 = MakeIdx(123474, 1629457991, 0x1d08cb16, &blockIndex123473);
                CBlockIndex blockIndex123475 = MakeIdx(123475, 1629458017, 0x1d08ac16, &blockIndex123474);
                CBlockIndex blockIndex123476 = MakeIdx(123476, 1629458499, 0x1d08b2bd, &blockIndex123475);
                CBlockIndex blockIndex123477 = MakeIdx(123477, 1629458549, 0x1d091be0, &blockIndex123476);
                CBlockIndex blockIndex123478 = MakeIdx(123478, 1629458731, 0x1d0895ee, &blockIndex123477);
                CBlockIndex blockIndex123479 = MakeIdx(123479, 1629458805, 0x1d08a3d1, &blockIndex123478);
                CBlockIndex blockIndex123480 = MakeIdx(123480, 1629458838, 0x1d08ab5b, &blockIndex123479);
                CBlockIndex blockIndex123481 = MakeIdx(123481, 1629458890, 0x1d084161, &blockIndex123480);
                CBlockIndex blockIndex123482 = MakeIdx(123482, 1629458902, 0x1d0839c3, &blockIndex123481);
                CBlockIndex blockIndex123483 = MakeIdx(123483, 1629459016, 0x1d083a21, &blockIndex123482);
                CBlockIndex blockIndex123484 = MakeIdx(123484, 1629459044, 0x1d0777a6, &blockIndex123483);
                CBlockIndex blockIndex123485 = MakeIdx(123485, 1629459047, 0x1d07126a, &blockIndex123484);
                CBlockIndex blockIndex123486 = MakeIdx(123486, 1629459190, 0x1d070480, &blockIndex123485);
                CBlockIndex blockIndex123487 = MakeIdx(123487, 1629459277, 0x1d072099, &blockIndex123486);
                CBlockIndex blockIndex123488 = MakeIdx(123488, 1629459509, 0x1d07269c, &blockIndex123487);
                CBlockIndex blockIndex123489 = MakeIdx(123489, 1629459634, 0x1d07548b, &blockIndex123488);
                CBlockIndex blockIndex123490 = MakeIdx(123490, 1629459640, 0x1d076964, &blockIndex123489);
                CBlockIndex blockIndex123491 = MakeIdx(123491, 1629459760, 0x1d075174, &blockIndex123490);
                CBlockIndex blockIndex123492 = MakeIdx(123492, 1629459814, 0x1d075618, &blockIndex123491);
                CBlockIndex blockIndex123493 = MakeIdx(123493, 1629459877, 0x1d075fd4, &blockIndex123492);
                CBlockIndex blockIndex123494 = MakeIdx(123494, 1629459927, 0x1d075628, &blockIndex123493);
                CBlockIndex blockIndex123495 = MakeIdx(123495, 1629459970, 0x1d07368d, &blockIndex123494);
                CBlockIndex blockIndex123496 = MakeIdx(123496, 1629460193, 0x1d07292e, &blockIndex123495);
                CBlockIndex blockIndex123497 = MakeIdx(123497, 1629460289, 0x1d074ab4, &blockIndex123496);
                CBlockIndex blockIndex123498 = MakeIdx(123498, 1629460334, 0x1d07338b, &blockIndex123497);
                CBlockIndex blockIndex123499 = MakeIdx(123499, 1629460516, 0x1d073167, &blockIndex123498);
                CBlockIndex blockIndex123500 = MakeIdx(123500, 1629460587, 0x1d06d1cf, &blockIndex123499);
                CBlockIndex blockIndex123501 = MakeIdx(123501, 1629460614, 0x1d06d691, &blockIndex123500);
                CBlockIndex blockIndex123502 = MakeIdx(123502, 1629460961, 0x1d06c567, &blockIndex123501);
                CBlockIndex blockIndex123503 = MakeIdx(123503, 1629460970, 0x1d06e925, &blockIndex123502);
                CBlockIndex blockIndex123504 = MakeIdx(123504, 1629460993, 0x1d06b456, &blockIndex123503);
                CBlockIndex blockIndex123505 = MakeIdx(123505, 1629461225, 0x1d06996d, &blockIndex123504);
                CBlockIndex blockIndex123506 = MakeIdx(123506, 1629461395, 0x1d06c755, &blockIndex123505);
                CBlockIndex blockIndex123507 = MakeIdx(123507, 1629461439, 0x1d06e69f, &blockIndex123506);
                CBlockIndex blockIndex123508 = MakeIdx(123508, 1629461446, 0x1d06cbec, &blockIndex123507);
                CBlockIndex blockIndex123509 = MakeIdx(123509, 1629461774, 0x1d06b62a, &blockIndex123508);
                CBlockIndex blockIndex123510 = MakeIdx(123510, 1629462021, 0x1d06d22a, &blockIndex123509);
                CBlockIndex blockIndex123511 = MakeIdx(123511, 1629462028, 0x1d06e343, &blockIndex123510);
                CBlockIndex blockIndex123512 = MakeIdx(123512, 1629462051, 0x1d068af3, &blockIndex123511);
                CBlockIndex blockIndex123513 = MakeIdx(123513, 1629462097, 0x1d06841f, &blockIndex123512);
                CBlockIndex blockIndex123514 = MakeIdx(123514, 1629462147, 0x1d0686de, &blockIndex123513);
                CBlockIndex blockIndex123515 = MakeIdx(123515, 1629462319, 0x1d0674e2, &blockIndex123514);
                CBlockIndex blockIndex123516 = MakeIdx(123516, 1629462519, 0x1d0697e0, &blockIndex123515);

                // blockHeader is not used for DGW calculations, it depends on the last block in the chain provided.  We provide but ignore:
                CBlockHeader blockHeader;

                // typical timing, mainnet:
                BOOST_CHECK_EQUAL(GetNextWorkRequired(&blockIndex123515, &blockHeader, chainParams->GetConsensus()), 0x1d0697e0); // Block #123516 is 0x1d0697e0

                // test special rules for slow blocks on devnet/testnet
                gArgs.SoftSetBoolArg("-devnet", true);
                const auto chainParamsDev = CreateChainParams(CBaseChainParams::DEVNET);
                int spacing = chainParams->GetConsensus().nPowTargetSpacing;
                int dgwWidth = chainParams->GetConsensus().DGWBlocksAvg;

                // make sure normal rules apply
                BOOST_CHECK_EQUAL(GetNextWorkRequired(&blockIndex123515, &blockHeader, chainParamsDev->GetConsensus()), 0x1d0697e0); // Block #123516 is 0x1d0697e0

                // 10x higher target
                blockIndex123516.nTime = blockIndex123515.nTime + spacing * 10 + 1;
                BOOST_CHECK_EQUAL(GetNextWorkRequired(&blockIndex123516, &blockHeader, chainParamsDev->GetConsensus()), 0x1d07cedd); // Block #123517 would be

                blockIndex123516.nTime = blockIndex123515.nTime + spacing * 20;
                BOOST_CHECK_EQUAL(GetNextWorkRequired(&blockIndex123516, &blockHeader, chainParamsDev->GetConsensus()), 0x1d0913d5); // Block #123517 would be

                // max diff change possible (at least 3x normal time in last N blocks)
                blockIndex123516.nTime = blockIndex123515.nTime + spacing * dgwWidth * 3;
                BOOST_CHECK_EQUAL(GetNextWorkRequired(&blockIndex123516, &blockHeader, chainParamsDev->GetConsensus()), 0x1d16de4e); // Block #123517 would be

                blockIndex123516.nTime = blockIndex123515.nTime + spacing * dgwWidth * 4;
                BOOST_CHECK_EQUAL(GetNextWorkRequired(&blockIndex123516, &blockHeader, chainParamsDev->GetConsensus()), 0x1d16de4e); // Block #123517 would be
        }

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test)
        {
                const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
                std::vector<CBlockIndex> blocks(10000);
                for (int i = 0; i < 10000; i++) {
                    blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
                    blocks[i].nHeight = i;
                    blocks[i].nTime = 1269211443 + i * chainParams->GetConsensus().nPowTargetSpacing;
                    blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
                    blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(
                            0);
                }

                for (int j = 0; j < 1000; j++) {
                    CBlockIndex *p1 = &blocks[InsecureRandRange(10000)];
                    CBlockIndex *p2 = &blocks[InsecureRandRange(10000)];
                    CBlockIndex *p3 = &blocks[InsecureRandRange(10000)];

                    int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, chainParams->GetConsensus());
                    BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
                }
        }

BOOST_AUTO_TEST_SUITE_END()
