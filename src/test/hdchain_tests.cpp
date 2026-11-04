// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <hdchain.h>

#include <boost/test/unit_test.hpp>

// CHDChain::swap() exchanges every data member -- including the seed and mnemonic, real key
// material -- between two chains, but did so without holding either chain's own `cs` lock. Every
// other accessor (GetSeed(), GetID(), IsCrypted(), ...) takes that lock, so a concurrent reader
// could observe a torn, partially-swapped chain while swap() is running.
//
// That race window is real but not reachable from a deterministic single-threaded test: proving
// mutual exclusion needs actual concurrent execution (ThreadSanitizer or a real multi-threaded
// stress harness), neither of which this test file sets up, so no test here claims to demonstrate
// the race itself. What it does cover, and what this class had zero coverage of before this fix:
// swap() must still exchange every member correctly with the lock held, so the fix is a pure
// addition of mutual exclusion, not a behavioural change to what swap() produces.

BOOST_AUTO_TEST_SUITE(hdchain_tests)

BOOST_AUTO_TEST_CASE(swap_exchanges_every_member)
{
    CHDChain a;
    const SecureVector seedA{1, 2, 3, 4, 5};
    a.SetSeed(seedA, /*fUpdateID=*/true);
    a.SetCrypted(false);
    const SecureVector mnemonicA{10, 11, 12}, passphraseA{20};
    a.SetMnemonic(mnemonicA, passphraseA, /*fUpdateID=*/false);
    a.AddAccount();

    CHDChain b;
    const SecureVector seedB{6, 7, 8, 9, 10, 11};
    b.SetSeed(seedB, /*fUpdateID=*/true);
    b.SetCrypted(true);
    const SecureVector mnemonicB{30, 31, 32, 33}, passphraseB{40, 41};
    b.SetMnemonic(mnemonicB, passphraseB, /*fUpdateID=*/false);
    b.AddAccount();
    b.AddAccount();

    const uint256 idA = a.GetID();
    const uint256 idB = b.GetID();
    BOOST_REQUIRE(idA != idB);
    const size_t accountsA = a.CountAccounts();
    const size_t accountsB = b.CountAccounts();
    BOOST_REQUIRE(accountsA != accountsB);

    a.swap(a, b);

    BOOST_CHECK(a.GetSeed() == seedB);
    BOOST_CHECK(b.GetSeed() == seedA);
    BOOST_CHECK(a.IsCrypted());
    BOOST_CHECK(!b.IsCrypted());
    BOOST_CHECK(a.GetID() == idB);
    BOOST_CHECK(b.GetID() == idA);
    BOOST_CHECK_EQUAL(a.CountAccounts(), accountsB);
    BOOST_CHECK_EQUAL(b.CountAccounts(), accountsA);

    SecureVector mnemonicOut, passphraseOut;
    BOOST_REQUIRE(a.GetMnemonic(mnemonicOut, passphraseOut));
    BOOST_CHECK(mnemonicOut == mnemonicB);
    BOOST_CHECK(passphraseOut == passphraseB);
    BOOST_REQUIRE(b.GetMnemonic(mnemonicOut, passphraseOut));
    BOOST_CHECK(mnemonicOut == mnemonicA);
    BOOST_CHECK(passphraseOut == passphraseA);
}

BOOST_AUTO_TEST_SUITE_END()
