// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_TEST_WALLET_TEST_FIXTURE_H
#define BITCOIN_WALLET_TEST_WALLET_TEST_FIXTURE_H

#include <test/test_raptoreum.h>

#include <net.h>
#include <interfaces/chain.h>
#include <interfaces/wallet.h>
#include <node/context.h>
#include <wallet/wallet.h>

#include <memory>

/** Testing setup and teardown for wallet.
 */
struct WalletTestingSetup : public TestingSetup {
    explicit WalletTestingSetup(const std::string &chainName = CBaseChainParams::MAIN);

    NodeContext m_node;
    std::unique_ptr <interfaces::Chain> m_chain = interfaces::MakeChain(m_node);
    std::unique_ptr <interfaces::WalletClient> m_wallet_client = interfaces::MakeWalletClient(*m_chain, {});
    CWallet m_wallet;
    std::unique_ptr <interfaces::Handler> m_chain_notification_handler;
};

#endif // BITCOIN_WALLET_TEST_WALLET_TEST_FIXTURE_H
