// Copyright (c) 2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <interfaces/wallet.h>

#include <amount.h>
#include <chain.h>
#include <coinjoin/coinjoin-client.h>
#include <consensus/validation.h>
#include <interfaces/handler.h>
#include <net.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <script/ismine.h>
#include <script/standard.h>
#include <support/allocators/secure.h>
#include <sync.h>
#include <timedata.h>
#include <txmempool.h> // for mempool.cs
#include <ui_interface.h>
#include <uint256.h>
#include <validation.h>
#include <wallet/wallet.h>

#include <memory>

namespace interfaces {
namespace {

class PendingWalletTxImpl : public PendingWalletTx
{
public:
    PendingWalletTxImpl(CWallet& wallet) : m_wallet(wallet), m_key(&wallet) {}

    const CTransaction& get() override { return *m_tx; }

    bool commit(WalletValueMap value_map,
        WalletOrderForm order_form,
        std::string from_account,
        std::string& reject_reason) override
    {
        LOCK2(cs_main, mempool.cs);
        LOCK(m_wallet.cs_wallet);
        CValidationState state;
        if (!m_wallet.CommitTransaction(m_tx, std::move(value_map), std::move(order_form), std::move(from_account), m_key, g_connman.get(), state)) {
            reject_reason = state.GetRejectReason();
            return false;
        }
        return true;
    }

    CTransactionRef m_tx;
    CWallet& m_wallet;
    CReserveKey m_key;
};

//! Construct wallet tx struct.
shared_ptr<WalletTx> MakeWalletTx(CWallet& wallet, const CWalletTx& wtx)
{
    shared_ptr<WalletTx> result(new WalletTx);
    bool fInputDenomFound{false}, fOutputDenomFound{false};
    result->tx = wtx.tx;
    result->txin_is_mine.reserve(wtx.tx->vin.size());
    for (const auto& txin : wtx.tx->vin) {
        result->txin_is_mine.emplace_back(wallet.IsMine(txin));
        if (!fInputDenomFound && result->txin_is_mine.back() && wallet.IsDenominated(txin.prevout)) {
            fInputDenomFound = true;
        }
    }
    result->txout_is_mine.reserve(wtx.tx->vout.size());
    result->txout_address.reserve(wtx.tx->vout.size());
    result->txout_address_is_mine.reserve(wtx.tx->vout.size());
    for (const auto& txout : wtx.tx->vout) {
        result->txout_is_mine.emplace_back(wallet.IsMine(txout));
        result->txout_address.emplace_back();
        result->txout_address_is_mine.emplace_back(ExtractDestination(txout.scriptPubKey, result->txout_address.back()) ?
                                                      IsMine(wallet, result->txout_address.back()) :
                                                      ISMINE_NO);
        if (!fOutputDenomFound && result->txout_address_is_mine.back() && CCoinJoin::IsDenominatedAmount(txout.nValue)) {
            fOutputDenomFound = true;
        }
    }
    result->credit = wtx.GetCredit(ISMINE_ALL);
    result->debit = wtx.GetDebit(ISMINE_ALL);
    result->change = wtx.GetChange();
    result->time = wtx.GetTxTime();
    result->value_map = wtx.mapValue;
    result->is_coinbase = wtx.IsCoinBase();
    // The determination of is_denominate is based on simplified checks here because in this part of the code
    // we only want to know about mixing transactions belonging to this specific wallet.
    result->is_denominate = wtx.tx->vin.size() == wtx.tx->vout.size() && // Number of inputs is same as number of outputs
                           (result->credit - result->debit) == 0 && // Transaction pays no tx fee
                           fInputDenomFound && fOutputDenomFound; // At least 1 input and 1 output are denominated belonging to the provided wallet
    return result;
}

//! Construct wallet tx status struct.
WalletTxStatus MakeWalletTxStatus(const CWalletTx& wtx)
{
    WalletTxStatus result;
    auto mi = ::mapBlockIndex.find(wtx.hashBlock);
    CBlockIndex* block = mi != ::mapBlockIndex.end() ? mi->second : nullptr;
    result.block_height = (block ? block->nHeight : std::numeric_limits<int>::max()),
    result.blocks_to_maturity = wtx.GetBlocksToMaturity();
    result.depth_in_main_chain = wtx.GetDepthInMainChain();
    result.time_received = wtx.nTimeReceived;
    result.lock_time = wtx.tx->nLockTime;
    result.is_final = CheckFinalTx(*wtx.tx);
    result.is_trusted = wtx.IsTrusted();
    result.is_abandoned = wtx.isAbandoned();
    result.is_coinbase = wtx.IsCoinBase();
    result.is_in_main_chain = wtx.IsInMainChain();
    result.is_chainlocked = wtx.IsChainLocked();
    result.is_islocked = wtx.IsLockedByInstantSend();
    return result;
}

//! Construct wallet TxOut struct.
WalletTxOut MakeWalletTxOut(CWallet& wallet, const CWalletTx& wtx, int n, int depth)
{
    WalletTxOut result;
    result.txout = wtx.tx->vout[n];
    result.time = wtx.GetTxTime();
    result.depth_in_main_chain = depth;
    result.is_spent = wallet.IsSpent(wtx.GetHash(), n);
    return result;
}

class CoinJoinImpl : public CoinJoin::Client
{
    std::shared_ptr<CCoinJoinClientManager> m_manager;
public:
    CoinJoinImpl(CWallet& wallet) : m_manager(coinJoinClientManagers.at(wallet.GetName())) {}
    void resetCachedBlocks() override
    {
        m_manager->nCachedNumBlocks = std::numeric_limits<int>::max();
    }
    void resetPool() override
    {
        m_manager->ResetPool();
    }
    void disableAutobackups() override
    {
        m_manager->fCreateAutoBackups = false;
    }
    int getCachedBlocks() override
    {
        return m_manager->nCachedNumBlocks;
    }
    std::string getSessionDenoms() override
    {
        return m_manager->GetSessionDenoms();
    }
    void setCachedBlocks(int nCachedBlocks) override
    {
       m_manager->nCachedNumBlocks = nCachedBlocks;
    }
    bool isMixing() override
    {
        return m_manager->IsMixing();
    }
    bool startMixing() override
    {
        return m_manager->StartMixing();
    }
    void stopMixing() override
    {
        m_manager->StopMixing();
    }
};

class WalletImpl : public Wallet
{
public:
    unordered_lru_cache<uint256, shared_ptr<WalletTx>, std::hash<uint256>, 10000> m_WalletTxCache;
    CoinJoinImpl m_coinjoin;

    WalletImpl(const std::shared_ptr<CWallet>& wallet) : m_shared_wallet(wallet), m_wallet(*wallet.get()), m_coinjoin(*wallet.get()) {}

    void markDirty() override
    {
        m_wallet.MarkDirty();
    }
    bool encryptWallet(const SecureString& wallet_passphrase) override
    {
        return m_wallet.EncryptWallet(wallet_passphrase);
    }
    bool isCrypted() override { return m_wallet.IsCrypted(); }
    bool lock(bool fAllowMixing) override { return m_wallet.Lock(fAllowMixing); }
    bool unlock(const SecureString& wallet_passphrase, bool fAllowMixing) override { return m_wallet.Unlock(wallet_passphrase, fAllowMixing); }
    bool isLocked(bool fForMixing) override { return m_wallet.IsLocked(fForMixing); }
    bool changeWalletPassphrase(const SecureString& old_wallet_passphrase,
        const SecureString& new_wallet_passphrase) override
    {
        return m_wallet.ChangeWalletPassphrase(old_wallet_passphrase, new_wallet_passphrase);
    }
    void abortRescan() override { m_wallet.AbortRescan(); }
    bool backupWallet(const std::string& filename) override { return m_wallet.BackupWallet(filename); }
    bool autoBackupWallet(const fs::path& wallet_path, std::string& strBackupWarningRet, std::string& strBackupErrorRet) override
    {
        return m_wallet.AutoBackupWallet(wallet_path, strBackupWarningRet, strBackupErrorRet);
    }
    int64_t getKeysLeftSinceAutoBackup() override { return m_wallet.nKeysLeftSinceAutoBackup; }
    std::string getWalletName() override { return m_wallet.GetName(); }
    bool getKeyFromPool(bool internal, CPubKey& pub_key) override
    {
        return m_wallet.GetKeyFromPool(pub_key, internal);
    }
    bool getPubKey(const CKeyID& address, CPubKey& pub_key) override { return m_wallet.GetPubKey(address, pub_key); }
    bool getPrivKey(const CKeyID& address, CKey& key) override { return m_wallet.GetKey(address, key); }
    bool isSpendable(const CScript& script) override { return IsMine(m_wallet, script) & ISMINE_SPENDABLE; }
    bool isSpendable(const CTxDestination& dest) override { return IsMine(m_wallet, dest) & ISMINE_SPENDABLE; }
    bool haveWatchOnly() override { return m_wallet.HaveWatchOnly(); };
    bool setAddressBook(const CTxDestination& dest, const std::string& name, const std::string& purpose) override
    {
        return m_wallet.SetAddressBook(dest, name, purpose);
    }
    bool delAddressBook(const CTxDestination& dest) override
    {
        return m_wallet.DelAddressBook(dest);
    }
    bool getAddress(const CTxDestination& dest, std::string* name, isminetype* is_mine) override
    {
        LOCK(m_wallet.cs_wallet);
        auto it = m_wallet.mapAddressBook.find(dest);
        if (it == m_wallet.mapAddressBook.end()) {
            return false;
        }
        if (name) {
            *name = it->second.name;
        }
        if (is_mine) {
            *is_mine = IsMine(m_wallet, dest);
        }
        return true;
    }
    std::vector<WalletAddress> getAddresses() override
    {
        LOCK(m_wallet.cs_wallet);
        std::vector<WalletAddress> result;
        for (const auto& item : m_wallet.mapAddressBook) {
            result.emplace_back(item.first, IsMine(m_wallet, item.first), item.second.name, item.second.purpose);
        }
        return result;
    }
    bool addDestData(const CTxDestination& dest, const std::string& key, const std::string& value) override
    {
        LOCK(m_wallet.cs_wallet);
        return m_wallet.AddDestData(dest, key, value);
    }
    bool eraseDestData(const CTxDestination& dest, const std::string& key) override
    {
        LOCK(m_wallet.cs_wallet);
        return m_wallet.EraseDestData(dest, key);
    }
    std::vector<std::string> getDestValues(const std::string& prefix) override
    {
        return m_wallet.GetDestValues(prefix);
    }
    void lockCoin(const COutPoint& output) override
    {
        LOCK2(cs_main, m_wallet.cs_wallet);
        return m_wallet.LockCoin(output);
    }
    void unlockCoin(const COutPoint& output) override
    {
        LOCK2(cs_main, m_wallet.cs_wallet);
        return m_wallet.UnlockCoin(output);
    }
    bool isLockedCoin(const COutPoint& output) override
    {
        LOCK2(cs_main, m_wallet.cs_wallet);
        return m_wallet.IsLockedCoin(output.hash, output.n);
    }
    void listLockedCoins(std::vector<COutPoint>& outputs) override
    {
        LOCK2(cs_main, m_wallet.cs_wallet);
        return m_wallet.ListLockedCoins(outputs);
    }
    void listProTxCoins(std::vector<COutPoint>& outputs) override
    {
        LOCK2(cs_main, m_wallet.cs_wallet);
        return m_wallet.ListProTxCoins(outputs);
    }
    std::unique_ptr<PendingWalletTx> createTransaction(const std::vector<CRecipient>& recipients,
        const CCoinControl& coin_control,
        bool sign,
        int& change_pos,
        CAmount& fee,
        std::string& fail_reason,
        int nExtraPayloadSize = 0,
        FuturePartialPayload* fpp = nullptr) override
    {
        LOCK2(cs_main, mempool.cs);
        LOCK(m_wallet.cs_wallet);
        auto pending = MakeUnique<PendingWalletTxImpl>(m_wallet);
        if (!m_wallet.CreateTransaction(recipients, pending->m_tx, pending->m_key, fee, change_pos,
                fail_reason, coin_control, sign, nExtraPayloadSize, fpp)) {
            return {};
        }
        return std::move(pending);
    }
    bool transactionCanBeAbandoned(const uint256& txid) override { return m_wallet.TransactionCanBeAbandoned(txid); }
    bool abandonTransaction(const uint256& txid) override
    {
        LOCK2(cs_main, m_wallet.cs_wallet);
        return m_wallet.AbandonTransaction(txid);
    }
    CTransactionRef getTx(const uint256& txid) override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        auto mi = m_wallet.mapWallet.find(txid);
        if (mi != m_wallet.mapWallet.end()) {
            return mi->second.tx;
        }
        return {};
    }
    shared_ptr<WalletTx> getWalletTx(const uint256& txid) override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        auto mi = m_wallet.mapWallet.find(txid);
        if (mi != m_wallet.mapWallet.end()) {
            shared_ptr<WalletTx> tx;
            if (!m_WalletTxCache.get(mi->first, tx)) {
               tx = MakeWalletTx(m_wallet, mi->second);
               m_WalletTxCache.insert(mi->first, tx);
            }
            return tx;
        }
        return {};
    }
    std::vector<shared_ptr<WalletTx>> getWalletTxs() override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        std::vector<shared_ptr<WalletTx>> result;
        result.reserve(m_wallet.mapWallet.size());
        for (const auto& entry : m_wallet.mapWallet) {
            shared_ptr<WalletTx> tx;
            if (!m_WalletTxCache.get(entry.first, tx)) {
               tx = MakeWalletTx(m_wallet, entry.second);
               m_WalletTxCache.insert(entry.first, tx);
            }
            result.emplace_back(tx);
        }
        return result;
    }
    bool tryGetTxStatus(const uint256& txid,
        interfaces::WalletTxStatus& tx_status,
        int64_t& adjusted_time) override
    {
        TRY_LOCK(::cs_main, locked_chain);
        if (!locked_chain) {
            return false;
        }
        TRY_LOCK(m_wallet.cs_wallet, locked_wallet);
        if (!locked_wallet) {
            return false;
        }
        auto mi = m_wallet.mapWallet.find(txid);
        if (mi == m_wallet.mapWallet.end()) {
            return false;
        }
        adjusted_time = GetAdjustedTime();
        tx_status = MakeWalletTxStatus(mi->second);
        return true;
    }
    shared_ptr<WalletTx> getWalletTxDetails(const uint256& txid,
        WalletTxStatus& tx_status,
        WalletOrderForm& order_form,
        bool& in_mempool,
        int& num_blocks,
        int64_t& adjusted_time) override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        auto mi = m_wallet.mapWallet.find(txid);
        if (mi != m_wallet.mapWallet.end()) {
            num_blocks = ::chainActive.Height();
            adjusted_time = GetAdjustedTime();
            in_mempool = mi->second.InMempool();
            order_form = mi->second.vOrderForm;
            tx_status = MakeWalletTxStatus(mi->second);
            return MakeWalletTx(m_wallet, mi->second);
        }
        return {};
    }
    int getRealOutpointCoinJoinRounds(const COutPoint& outpoint) override { return m_wallet.GetRealOutpointCoinJoinRounds(outpoint); }
    bool isFullyMixed(const COutPoint& outpoint) override { return m_wallet.IsFullyMixed(outpoint); }
    WalletBalances getBalances() override
    {
        WalletBalances result;
        result.balance = m_wallet.GetBalance();
        result.unconfirmed_balance = m_wallet.GetUnconfirmedBalance();
        result.immature_balance = m_wallet.GetImmatureBalance();
        result.anonymized_balance = m_wallet.GetAnonymizedBalance();
        result.have_watch_only = m_wallet.HaveWatchOnly();
        if (result.have_watch_only) {
            result.watch_only_balance = m_wallet.GetBalance(ISMINE_WATCH_ONLY);
            result.unconfirmed_watch_only_balance = m_wallet.GetUnconfirmedWatchOnlyBalance();
            result.immature_watch_only_balance = m_wallet.GetImmatureWatchOnlyBalance();
        }
        return result;
    }

    std::map<CTxDestination, CAmount> GetAddressBalances() override
    {
        std::map<CTxDestination, CAmount> balances;

        {
            LOCK2(::cs_main, m_wallet.cs_wallet);
            for (const auto& walletEntry : m_wallet.mapWallet)
            {
                const CWalletTx *pcoin = &walletEntry.second;

                if (!pcoin->IsTrusted())
                    continue;

                if (pcoin->IsCoinBase() && pcoin->GetBlocksToMaturity() > 0)
                    continue;

                int nDepth = pcoin->GetDepthInMainChain();
                if ((nDepth < (pcoin->IsFromMe(ISMINE_ALL) ? 0 : 1)) && !pcoin->IsLockedByInstantSend())
                    continue;

                for (unsigned int i = 0; i < pcoin->tx->vout.size(); i++)
                {
                    CTxDestination addr;
                    if (!m_wallet.IsMine(pcoin->tx->vout[i]))
                        continue;
                    if(!ExtractDestination(pcoin->tx->vout[i].scriptPubKey, addr))
                        continue;

                    CAmount n = m_wallet.IsSpent(walletEntry.first, i) ? 0 : pcoin->tx->vout[i].nValue;

                    if (!balances.count(addr))
                        balances[addr] = 0;
                    balances[addr] += n;
                }
            }
        }

        return balances;
    }

    bool tryGetBalances(WalletBalances& balances) override
    {
        TRY_LOCK(cs_main, locked_chain);
        if (!locked_chain) return false;
        TRY_LOCK(m_wallet.cs_wallet, locked_wallet);
        if (!locked_wallet) {
            return false;
        }
        balances = getBalances();
        return true;
    }
    CAmount getBalance() override
    {
        return m_wallet.GetBalance();
    }
    CAmount getAnonymizableBalance(bool fSkipDenominated, bool fSkipUnconfirmed) override
    {
        return m_wallet.GetAnonymizableBalance(fSkipDenominated, fSkipUnconfirmed);
    }
    CAmount getAnonymizedBalance() override
    {
        return m_wallet.GetAnonymizedBalance();
    }
    CAmount getDenominatedBalance(bool unconfirmed) override
    {
        return m_wallet.GetDenominatedBalance(unconfirmed);
    }
    CAmount getNormalizedAnonymizedBalance() override
    {
        return m_wallet.GetNormalizedAnonymizedBalance();
    }
    CAmount getAverageAnonymizedRounds() override
    {
        return m_wallet.GetAverageAnonymizedRounds();
    }
    CAmount getAvailableBalance(const CCoinControl& coin_control) override
    {
        if (coin_control.IsUsingCoinJoin()) {
            return m_wallet.GetAnonymizedBalance(&coin_control);
        } else {
            return m_wallet.GetAvailableBalance(&coin_control);
        }
    }
    isminetype txinIsMine(const CTxIn& txin) override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        return m_wallet.IsMine(txin);
    }
    isminetype txoutIsMine(const CTxOut& txout) override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        return m_wallet.IsMine(txout);
    }
    CAmount getDebit(const CTxIn& txin, isminefilter filter) override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        return m_wallet.GetDebit(txin, filter);
    }
    CAmount getCredit(const CTxOut& txout, isminefilter filter) override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        return m_wallet.GetCredit(txout, filter);
    }
    CoinsList listCoins() override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        CoinsList result;
        for (const auto& entry : m_wallet.ListCoins()) {
            auto& group = result[entry.first];
            for (const auto& coin : entry.second) {
                group.emplace_back(
                    COutPoint(coin.tx->GetHash(), coin.i), MakeWalletTxOut(m_wallet, *coin.tx, coin.i, coin.nDepth));
            }
        }
        return result;
    }
    std::vector<WalletTxOut> getCoins(const std::vector<COutPoint>& outputs) override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        std::vector<WalletTxOut> result;
        result.reserve(outputs.size());
        for (const auto& output : outputs) {
            result.emplace_back();
            auto it = m_wallet.mapWallet.find(output.hash);
            if (it != m_wallet.mapWallet.end()) {
                int depth = it->second.GetDepthInMainChain();
                if (depth >= 0) {
                    result.back() = MakeWalletTxOut(m_wallet, it->second, output.n, depth);
                }
            }
        }
        return result;
    }
    bool hdEnabled() override { return m_wallet.IsHDEnabled(); }
    CoinJoin::Client& coinJoin() override { return m_coinjoin; }
    std::unique_ptr<Handler> handleUnload(UnloadFn fn) override
    {
        return MakeHandler(m_wallet.NotifyUnload.connect(fn));
    }
    std::unique_ptr<Handler> handleShowProgress(ShowProgressFn fn) override
    {
        return MakeHandler(m_wallet.ShowProgress.connect(fn));
    }
    std::unique_ptr<Handler> handleStatusChanged(StatusChangedFn fn) override
    {
        return MakeHandler(m_wallet.NotifyStatusChanged.connect([fn](CCryptoKeyStore*) { fn(); }));
    }
    std::unique_ptr<Handler> handleAddressBookChanged(AddressBookChangedFn fn) override
    {
        return MakeHandler(m_wallet.NotifyAddressBookChanged.connect(
            [fn](CWallet*, const CTxDestination& address, const std::string& label, bool is_mine,
                const std::string& purpose, ChangeType status) { fn(address, label, is_mine, purpose, status); }));
    }
    std::unique_ptr<Handler> handleTransactionChanged(TransactionChangedFn fn) override
    {
        return MakeHandler(m_wallet.NotifyTransactionChanged.connect(
            [fn](CWallet*, const uint256& txid, ChangeType status) { fn(txid, status); }));
    }
    std::unique_ptr<Handler> handleInstantLockReceived(InstantLockReceivedFn fn) override
    {
        return MakeHandler(m_wallet.NotifyISLockReceived.connect(
            [fn, this]() { fn(); }));
    }
    std::unique_ptr<Handler> handleChainLockReceived(ChainLockReceivedFn fn) override
    {
        return MakeHandler(m_wallet.NotifyChainLockReceived.connect(
            [fn, this](int chainLockHeight) { fn(chainLockHeight); }));
    }
    std::unique_ptr<Handler> handleWatchOnlyChanged(WatchOnlyChangedFn fn) override
    {
        return MakeHandler(m_wallet.NotifyWatchonlyChanged.connect(fn));
    }
    std::unique_ptr<Handler> handleBlockNotifyTip(BlockNotifyTipFn fn) override
    {
        return MakeHandler(::uiInterface.BlockNotifyTip.connect([fn](bool initial_download, const CBlockIndex* block) {
            fn(initial_download, block->nHeight);}));
    }

    std::shared_ptr<CWallet> m_shared_wallet;
    CWallet& m_wallet;
};

} // namespace

std::unique_ptr<Wallet> MakeWallet(const std::shared_ptr<CWallet>& wallet) { return MakeUnique<WalletImpl>(wallet); }

} // namespace interfaces
