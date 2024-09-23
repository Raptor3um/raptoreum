// Copyright (c) 2014-2020 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying

#include <bip39.h>
#include <chainparams.h>
#include <hdchain.h>
#include <key_io.h>
#include <tinyformat.h>
#include <util/system.h>
#include <util/strencodings.h>

#ifdef ENABLE_WALLET
#include <wallet/walletdb.h>
#endif

bool CHDChain::SetNull() {
    LOCK(cs);
    nVersion = CURRENT_VERSION;
    id = uint256();
    fCrypted = false;
    vchSeed.clear();
    vchMnemonic.clear();
    vchMnemonicPassphrase.clear();
    mapAccounts.clear();
    return IsNull();
}

bool CHDChain::IsNull() const {
    LOCK(cs);
    return vchSeed.empty() || id == uint256();
}

void CHDChain::SetCrypted(bool fCryptedIn) {
    LOCK(cs);
    fCrypted = fCryptedIn;
}

bool CHDChain::IsCrypted() const {
    LOCK(cs);
    return fCrypted;
}

void CHDChain::Debug(const std::string &strName) const {
    DBG(
            LOCK(cs);
    std::cout << __func__ << ": ---" << strName << "---" << std::endl;
    if (fCrypted) {
        std::cout << "mnemonic: ***CRYPTED***" << std::endl;
        std::cout << "mnemonicpassphrase: ***CRYPTED***" << std::endl;
        std::cout << "seed: ***CRYPTED***" << std::endl;
    } else {
        std::cout << "mnemonic: " << std::string(vchMnemonic.begin(), vchMnemonic.end()).c_str() << std::endl;
        std::cout << "mnemonicpassphrase: "
                  << std::string(vchMnemonicPassphrase.begin(), vchMnemonicPassphrase.end()).c_str() << std::endl;
        std::cout << "seed: " << HexStr(vchSeed).c_str() << std::endl;

        CExtKey extkey;
        extkey.SetSeed(vchSeed.data(), vchSeed.size());

        std::cout << "extended private masterkey: " << EncodeExtKey(extkey).c_str() << std::endl;

        CExtPubKey extpubkey;
        extpubkey = extkey.Neuter();

        std::cout << "extended public masterkey: " << EncodeExtPubKey(extpubkey).c_str() << std::endl;
    }
    );
}

bool CHDChain::SetMnemonic(const SecureVector &vchMnemonic, const SecureVector &vchMnemonicPassphrase, bool fUpdateID) {
    return SetMnemonic(SecureString(vchMnemonic.begin(), vchMnemonic.end()),
                       SecureString(vchMnemonicPassphrase.begin(), vchMnemonicPassphrase.end()), fUpdateID);
}

bool CHDChain::SetMnemonic(const SecureString &ssMnemonic, const SecureString &ssMnemonicPassphrase, bool fUpdateID) {
    LOCK(cs);
    SecureString ssMnemonicTmp = ssMnemonic;

    if (fUpdateID) {
        // can't (re)set mnemonic if seed was already set
        if (!IsNull())
            return false;

        if (ssMnemonicPassphrase.size() > 256) {
            throw std::runtime_error(
                    std::string(__func__) + ": Mnemonic passphrase is too long, must be at most 256 characters");
        }

        // empty mnemonic i.e. "generate a new one"
        if (ssMnemonic.empty()) {
            ssMnemonicTmp = CMnemonic::Generate(256);
        }
        // NOTE: default mnemonic passphrase is an empty string

        // printf("mnemonic: %s\n", ssMnemonicTmp.c_str());
        if (!CMnemonic::Check(ssMnemonicTmp)) {
            throw std::runtime_error(
                    std::string(__func__) + ": invalid mnemonic: `" + std::string(ssMnemonicTmp.c_str()) + "`");
        }

        CMnemonic::ToSeed(ssMnemonicTmp, ssMnemonicPassphrase, vchSeed);
        id = GetSeedHash();
    }

    vchMnemonic = SecureVector(ssMnemonicTmp.begin(), ssMnemonicTmp.end());
    vchMnemonicPassphrase = SecureVector(ssMnemonicPassphrase.begin(), ssMnemonicPassphrase.end());

    return !IsNull();
}

bool CHDChain::GetMnemonic(SecureVector &vchMnemonicRet, SecureVector &vchMnemonicPassphraseRet) const {
    LOCK(cs);
    // mnemonic was not set, fail
    if (vchMnemonic.empty())
        return false;

    vchMnemonicRet = vchMnemonic;
    vchMnemonicPassphraseRet = vchMnemonicPassphrase;
    return true;
}

bool CHDChain::GetMnemonic(SecureString &ssMnemonicRet, SecureString &ssMnemonicPassphraseRet) const {
    LOCK(cs);
    // mnemonic was not set, fail
    if (vchMnemonic.empty())
        return false;

    ssMnemonicRet = SecureString(vchMnemonic.begin(), vchMnemonic.end());
    ssMnemonicPassphraseRet = SecureString(vchMnemonicPassphrase.begin(), vchMnemonicPassphrase.end());

    return true;
}

bool CHDChain::SetSeed(const SecureVector &vchSeedIn, bool fUpdateID) {
    LOCK(cs);
    vchSeed = vchSeedIn;

    if (fUpdateID) {
        id = GetSeedHash();
    }

    return !IsNull();
}

SecureVector CHDChain::GetSeed() const {
    LOCK(cs);
    return vchSeed;
}

uint256 CHDChain::GetSeedHash() {
    LOCK(cs);
    return Hash(vchSeed.begin(), vchSeed.end());
}

void CHDChain::DeriveChildExtKey(uint32_t nAccountIndex, bool fInternal, uint32_t nChildIndex, CExtKey &extKeyRet,
                                 CKeyMetadata &metadata) {
    LOCK(cs);
    // Use BIP44 keypath scheme i.e. m / purpose' / coin_type' / account' / change / address_index
    CExtKey masterKey;              //hd master key
    CExtKey purposeKey;             //key at m/purpose'
    CExtKey cointypeKey;            //key at m/purpose'/coin_type'
    CExtKey accountKey;             //key at m/purpose'/coin_type'/account'
    CExtKey changeKey;              //key at m/purpose'/coin_type'/account'/change
    CExtKey childKey;               //key at m/purpose'/coin_type'/account'/change/address_index

    masterKey.SetSeed(vchSeed.data(), vchSeed.size());

    // Use hardened derivation for purpose, coin_type and account
    // (keys >= 0x80000000 are hardened after bip32)

    // derive m/purpose'
    masterKey.Derive(purposeKey, 44 | 0x80000000);
    // derive m/purpose'/coin_type'
    purposeKey.Derive(cointypeKey, Params().ExtCoinType() | 0x80000000);
    // derive m/purpose'/coin_type'/account'
    cointypeKey.Derive(accountKey, nAccountIndex | 0x80000000);
    // derive m/purpose'/coin_type'/account'/change
    accountKey.Derive(changeKey, fInternal ? 1 : 0);
    // derive m/purpose'/coin_type'/account'/change/address_index
    changeKey.Derive(extKeyRet, nChildIndex);

#ifdef ENABLE_WALLET
    metadata.key_origin.path.push_back(44 | 0x80000000);
    metadata.key_origin.path.push_back(Params().ExtCoinType() | 0x80000000);
    metadata.key_origin.path.push_back(nAccountIndex | 0x80000000);
    metadata.key_origin.path.push_back(fInternal ? 1 : 0);
    metadata.key_origin.path.push_back(nChildIndex);

    CKeyID master_id = masterKey.key.GetPubKey().GetID();
    std::copy(master_id.begin(), master_id.begin() + 4, metadata.key_origin.fingerprint);
    metadata.has_key_origin = true;
#endif
}

void CHDChain::AddAccount() {
    LOCK(cs);
    mapAccounts.insert(std::pair<uint32_t, CHDAccount>(mapAccounts.size(), CHDAccount()));
}

bool CHDChain::GetAccount(uint32_t nAccountIndex, CHDAccount &hdAccountRet) {
    LOCK(cs);
    if (nAccountIndex > mapAccounts.size() - 1)
        return false;
    hdAccountRet = mapAccounts[nAccountIndex];
    return true;
}

bool CHDChain::SetAccount(uint32_t nAccountIndex, const CHDAccount &hdAccount) {
    LOCK(cs);
    // can only replace existing accounts
    if (nAccountIndex > mapAccounts.size() - 1)
        return false;
    mapAccounts[nAccountIndex] = hdAccount;
    return true;
}

size_t CHDChain::CountAccounts() {
    LOCK(cs);
    return mapAccounts.size();
}

std::string CHDPubKey::GetKeyPath() const {
    return strprintf("m/44'/%d'/%d'/%d/%d", Params().ExtCoinType(), nAccountIndex, nChangeIndex, extPubKey.nChild);
}
