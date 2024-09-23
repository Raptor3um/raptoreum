// Copyright (c) 2012-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <interfaces/chain.h>
#include <key_io.h>
#include <node/context.h>
#include <test/util.h>
#include <test/test_raptoreum.h>
#include <validationinterface.h>
#include <wallet/wallet.h>

#include <optional>

static void WalletBalance(benchmark::Bench &bench, const bool set_dirty, const bool add_watchonly, const bool add_mine,
                          const uint32_t epoch_iters) {
    RegTestingSetup test_setup;
    const auto &ADDRESS_WATCHONLY = ADDRESS_B58T_UNSPENDABLE;

    NodeContext node;
    std::unique_ptr <interfaces::Chain> chain = interfaces::MakeChain(node);
    CWallet wallet{chain.get(), WalletLocation(), CreateMockWalletDatabase()};
    {
        bool first_run;
        if (wallet.LoadWallet(first_run) != DBErrors::LOAD_OK) assert(false);
    }
    auto handler = chain->handleNotifications({&wallet, [](CWallet *) {}});

    const std::optional <std::string> address_mine{
            add_mine ? std::optional < std::string > {getnewaddress(wallet)} : std::nullopt};
    if (add_watchonly) importaddress(wallet, ADDRESS_WATCHONLY);

    for (int i = 0; i < 100; ++i) {
        generatetoaddress(test_setup.m_node, address_mine.value_or(ADDRESS_WATCHONLY));
        generatetoaddress(test_setup.m_node, ADDRESS_WATCHONLY);
    }
    SyncWithValidationInterfaceQueue();

    auto bal = wallet.GetBalance(); // Cache

    bench.minEpochIterations(epoch_iters).run([&] {
        if (set_dirty) wallet.MarkDirty();
        bal = wallet.GetBalance();
        if (add_mine) assert(bal.m_mine_trusted > 0);
        if (add_watchonly) assert(bal.m_watchonly_trusted > 0);
    });
}

static void WalletBalanceDirty(benchmark::Bench &bench) {
    WalletBalance(bench, /* set_dirty */ true, /* add_watchonly */true, /* add_mine */ true, 2500);
}

static void WalletBalanceClean(benchmark::Bench &bench) {
    WalletBalance(bench, /* set_dirty */false, /* add_watchonly */ true, /* add_mine */true, 8000);
}

static void WalletBalanceMine(benchmark::Bench &bench) {
    WalletBalance(bench, /* set_dirty */ false, /* add_watchonly */false, /* add_mine */ true, 16000);
}

static void WalletBalanceWatch(benchmark::Bench &bench) {
    WalletBalance(bench, /* set_dirty */false, /* add_watchonly */ true, /* add_mine */false, 8000);
}

BENCHMARK(WalletBalanceDirty);
BENCHMARK(WalletBalanceClean);
BENCHMARK(WalletBalanceMine);
BENCHMARK(WalletBalanceWatch);
