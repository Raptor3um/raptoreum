// Copyright (c) 2012-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <key.h>

#include <key_io.h>
#include <script/script.h>
#include <uint256.h>
#include <util/system.h>
#include <util/strencodings.h>
#include <test/test_raptoreum.h>

#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

static const std::string strSecret1 = "5J8zMRBt9YU3tyvJC9D3G1TfPXQ81snpu8UHttrC4VUGqgDwWDB";
static const std::string strSecret2 = "5KKMP3aVta8Wt8NcDprQFA1PoiUySCrN1mzFsccg5XeMUnUsMTV";
static const std::string strSecret1C = "KxeG97m2khtPnSEEtBE8yF2gSRB3xsBGR5yTa3r7pQfNua2DhKHk";
static const std::string strSecret2C = "L3qzxJNrWdEAtfYNTcD3bUkjfC6U6FJskRZieQCTUzFsPcGNm92W";
static const std::string addr1 = "RCvA29Ui948z61vhxAuCcJaBZSELTuP1gQ";
static const std::string addr2 = "RTYXreDKjH1k9EyBZEDz48zUrHxh933BTW";
static const std::string addr1C = "RVMg8JuaKgRGUFvoAgBiK5Mp8YAgqJ5cyr";
static const std::string addr2C = "RCcTmMBeN9qGNbQJid1LSfEQu1SCW7FRj5";

static const std::string strAddressBad = "Xta1praZQjyELweyMByXyiREw1ZRsjXzVP";


BOOST_FIXTURE_TEST_SUITE(key_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(key_test1)
{
    CKey key1  = DecodeSecret(strSecret1);
    BOOST_CHECK(key1.IsValid() && !key1.IsCompressed());
    CKey key2  = DecodeSecret(strSecret2);
    BOOST_CHECK(key2.IsValid() && !key2.IsCompressed());
    CKey key1C = DecodeSecret(strSecret1C);
    BOOST_CHECK(key1C.IsValid() && key1C.IsCompressed());
    CKey key2C = DecodeSecret(strSecret2C);
    BOOST_CHECK(key2C.IsValid() && key2C.IsCompressed());
    CKey bad_key = DecodeSecret(strAddressBad);
    BOOST_CHECK(!bad_key.IsValid());

    CPubKey pubkey1  = key1. GetPubKey();
    CPubKey pubkey2  = key2. GetPubKey();
    CPubKey pubkey1C = key1C.GetPubKey();
    CPubKey pubkey2C = key2C.GetPubKey();

    BOOST_CHECK(key1.VerifyPubKey(pubkey1));
    BOOST_CHECK(!key1.VerifyPubKey(pubkey1C));
    BOOST_CHECK(!key1.VerifyPubKey(pubkey2));
    BOOST_CHECK(!key1.VerifyPubKey(pubkey2C));

    BOOST_CHECK(!key1C.VerifyPubKey(pubkey1));
    BOOST_CHECK(key1C.VerifyPubKey(pubkey1C));
    BOOST_CHECK(!key1C.VerifyPubKey(pubkey2));
    BOOST_CHECK(!key1C.VerifyPubKey(pubkey2C));

    BOOST_CHECK(!key2.VerifyPubKey(pubkey1));
    BOOST_CHECK(!key2.VerifyPubKey(pubkey1C));
    BOOST_CHECK(key2.VerifyPubKey(pubkey2));
    BOOST_CHECK(!key2.VerifyPubKey(pubkey2C));

    BOOST_CHECK(!key2C.VerifyPubKey(pubkey1));
    BOOST_CHECK(!key2C.VerifyPubKey(pubkey1C));
    BOOST_CHECK(!key2C.VerifyPubKey(pubkey2));
    BOOST_CHECK(key2C.VerifyPubKey(pubkey2C));

    BOOST_CHECK(DecodeDestination(addr1)  == CTxDestination(pubkey1.GetID()));
    BOOST_CHECK(DecodeDestination(addr2)  == CTxDestination(pubkey2.GetID()));
    BOOST_CHECK(DecodeDestination(addr1C) == CTxDestination(pubkey1C.GetID()));
    BOOST_CHECK(DecodeDestination(addr2C) == CTxDestination(pubkey2C.GetID()));

    for (int n=0; n<16; n++)
    {
        std::string strMsg = strprintf("Very secret message %i: 11", n);
        uint256 hashMsg = Hash(strMsg.begin(), strMsg.end());

        // normal signatures

        std::vector<unsigned char> sign1, sign2, sign1C, sign2C;

        BOOST_CHECK(key1.Sign (hashMsg, sign1));
        BOOST_CHECK(key2.Sign (hashMsg, sign2));
        BOOST_CHECK(key1C.Sign(hashMsg, sign1C));
        BOOST_CHECK(key2C.Sign(hashMsg, sign2C));

        BOOST_CHECK( pubkey1.Verify(hashMsg, sign1));
        BOOST_CHECK(!pubkey1.Verify(hashMsg, sign2));
        BOOST_CHECK( pubkey1.Verify(hashMsg, sign1C));
        BOOST_CHECK(!pubkey1.Verify(hashMsg, sign2C));

        BOOST_CHECK(!pubkey2.Verify(hashMsg, sign1));
        BOOST_CHECK( pubkey2.Verify(hashMsg, sign2));
        BOOST_CHECK(!pubkey2.Verify(hashMsg, sign1C));
        BOOST_CHECK( pubkey2.Verify(hashMsg, sign2C));

        BOOST_CHECK( pubkey1C.Verify(hashMsg, sign1));
        BOOST_CHECK(!pubkey1C.Verify(hashMsg, sign2));
        BOOST_CHECK( pubkey1C.Verify(hashMsg, sign1C));
        BOOST_CHECK(!pubkey1C.Verify(hashMsg, sign2C));

        BOOST_CHECK(!pubkey2C.Verify(hashMsg, sign1));
        BOOST_CHECK( pubkey2C.Verify(hashMsg, sign2));
        BOOST_CHECK(!pubkey2C.Verify(hashMsg, sign1C));
        BOOST_CHECK( pubkey2C.Verify(hashMsg, sign2C));

        // compact signatures (with key recovery)

        std::vector<unsigned char> csign1, csign2, csign1C, csign2C;

        BOOST_CHECK(key1.SignCompact (hashMsg, csign1));
        BOOST_CHECK(key2.SignCompact (hashMsg, csign2));
        BOOST_CHECK(key1C.SignCompact(hashMsg, csign1C));
        BOOST_CHECK(key2C.SignCompact(hashMsg, csign2C));

        CPubKey rkey1, rkey2, rkey1C, rkey2C;

        BOOST_CHECK(rkey1.RecoverCompact (hashMsg, csign1));
        BOOST_CHECK(rkey2.RecoverCompact (hashMsg, csign2));
        BOOST_CHECK(rkey1C.RecoverCompact(hashMsg, csign1C));
        BOOST_CHECK(rkey2C.RecoverCompact(hashMsg, csign2C));

        BOOST_CHECK(rkey1  == pubkey1);
        BOOST_CHECK(rkey2  == pubkey2);
        BOOST_CHECK(rkey1C == pubkey1C);
        BOOST_CHECK(rkey2C == pubkey2C);
    }

    // test deterministic signing
    
    std::vector<unsigned char> detsig, detsigc;
    std::string strMsg = "Very deterministic message";
    uint256 hashMsg = Hash(strMsg.begin(), strMsg.end());
    BOOST_CHECK(key1.Sign(hashMsg, detsig));
    BOOST_CHECK(key1C.Sign(hashMsg, detsigc));
    BOOST_CHECK(detsig == detsigc);
    BOOST_CHECK(detsig == ParseHex("30450221009842b16af3d7ece7bbd28551faea69386f77969539b26052880658d2f13a58e102202c7a8c17c70301cd56fbc2b5473266f500c758069abac398246d808889e9cff7"));
    BOOST_CHECK(key2.Sign(hashMsg, detsig));
    BOOST_CHECK(key2C.Sign(hashMsg, detsigc));
    BOOST_CHECK(detsig == detsigc);
    BOOST_CHECK(detsig == ParseHex("30440220227dadff585108210635ed0341c494bef756b1050d30cd0460f5c0949d2f46f3022073b3b5bd1a76d8673ca94a8a897bad379cbfc1076365635307ec7de1c7828d11"));
    BOOST_CHECK(key1.SignCompact(hashMsg, detsig));
    BOOST_CHECK(key1C.SignCompact(hashMsg, detsigc));
    BOOST_CHECK(detsig == ParseHex("1b9842b16af3d7ece7bbd28551faea69386f77969539b26052880658d2f13a58e12c7a8c17c70301cd56fbc2b5473266f500c758069abac398246d808889e9cff7"));
    BOOST_CHECK(detsigc == ParseHex("1f9842b16af3d7ece7bbd28551faea69386f77969539b26052880658d2f13a58e12c7a8c17c70301cd56fbc2b5473266f500c758069abac398246d808889e9cff7"));
    BOOST_CHECK(key2.SignCompact(hashMsg, detsig));
    BOOST_CHECK(key2C.SignCompact(hashMsg, detsigc));
    BOOST_CHECK(detsig == ParseHex("1c227dadff585108210635ed0341c494bef756b1050d30cd0460f5c0949d2f46f373b3b5bd1a76d8673ca94a8a897bad379cbfc1076365635307ec7de1c7828d11"));
    BOOST_CHECK(detsigc == ParseHex("20227dadff585108210635ed0341c494bef756b1050d30cd0460f5c0949d2f46f373b3b5bd1a76d8673ca94a8a897bad379cbfc1076365635307ec7de1c7828d11"));
}

BOOST_AUTO_TEST_SUITE_END()
