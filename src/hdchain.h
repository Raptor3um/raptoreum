// Copyright (c) 2014-2019 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
#ifndef RAPTOREUM_HDCHAIN_H
#define RAPTOREUM_HDCHAIN_H

#include <key.h>
#include <sync.h>

class CKeyMetadata;

/* hd account data model */
class CHDAccount {
public:
    uint32_t nExternalChainCounter;
    uint32_t nInternalChainCounter;

    CHDAccount() : nExternalChainCounter(0), nInternalChainCounter(0) {}

    SERIALIZE_METHODS(CHDAccount, obj
    )
    {
        READWRITE(obj.nExternalChainCounter, obj.nInternalChainCounter);
    }
};

/* simple HD chain data model */
class CHDChain {
private:
    mutable RecursiveMutex cs;

    static const int CURRENT_VERSION = 1;
    int nVersion
    GUARDED_BY(cs) {CURRENT_VERSION};

    uint256 id
    GUARDED_BY(cs);

    bool fCrypted
    GUARDED_BY(cs) {false};

    SecureVector vchSeed
    GUARDED_BY(cs);
    SecureVector vchMnemonic
    GUARDED_BY(cs);
    SecureVector vchMnemonicPassphrase
    GUARDED_BY(cs);

    std::map <uint32_t, CHDAccount> GUARDED_BY(cs)

    mapAccounts;

public:

    CHDChain() = default;

    CHDChain(const CHDChain &other) :
            nVersion(other.nVersion),
            id(other.id),
            fCrypted(other.fCrypted),
            vchSeed(other.vchSeed),
            vchMnemonic(other.vchMnemonic),
            vchMnemonicPassphrase(other.vchMnemonicPassphrase),
            mapAccounts(other.mapAccounts) {}

    SERIALIZE_METHODS(CHDChain, obj
    )
    {
        LOCK(obj.cs);
        READWRITE(obj.nVersion, obj.id, obj.fCrypted, obj.vchSeed,
                  obj.vchMnemonic, obj.vchMnemonicPassphrase, obj.mapAccounts);
    }

    void swap(CHDChain &first, CHDChain &second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.nVersion, second.nVersion);
        swap(first.id, second.id);
        swap(first.fCrypted, second.fCrypted);
        swap(first.vchSeed, second.vchSeed);
        swap(first.vchMnemonic, second.vchMnemonic);
        swap(first.vchMnemonicPassphrase, second.vchMnemonicPassphrase);
        swap(first.mapAccounts, second.mapAccounts);
    }

    CHDChain &operator=(CHDChain from) {
        swap(*this, from);
        return *this;
    }

    bool SetNull();

    bool IsNull() const;

    void SetCrypted(bool fCryptedIn);

    bool IsCrypted() const;

    void Debug(const std::string &strName) const;

    bool SetMnemonic(const SecureVector &vchMnemonic, const SecureVector &vchMnemonicPassphrase, bool fUpdateID);

    bool SetMnemonic(const SecureString &ssMnemonic, const SecureString &ssMnemonicPassphrase, bool fUpdateID);

    bool GetMnemonic(SecureVector &vchMnemonicRet, SecureVector &vchMnemonicPassphraseRet) const;

    bool GetMnemonic(SecureString &ssMnemonicRet, SecureString &ssMnemonicPassphraseRet) const;

    bool SetSeed(const SecureVector &vchSeedIn, bool fUpdateID);

    SecureVector GetSeed() const;

    uint256 GetID() const {
        LOCK(cs);
        return id;
    }

    uint256 GetSeedHash();

    void DeriveChildExtKey(uint32_t nAccountIndex, bool fInternal, uint32_t nChildIndex, CExtKey &extKeyRet,
                           CKeyMetadata &metadata);

    void AddAccount();

    bool GetAccount(uint32_t nAccountIndex, CHDAccount &hdAccountRet);

    bool SetAccount(uint32_t nAccountIndex, const CHDAccount &hdAccount);

    size_t CountAccounts();
};

/* hd pubkey data model */
class CHDPubKey {
private:
    static const int CURRENT_VERSION = 1;
    int nVersion{CHDPubKey::CURRENT_VERSION};

public:
    CExtPubKey extPubKey;
    uint256 hdchainID;
    uint32_t nAccountIndex{0};
    uint32_t nChangeIndex{0};

    CHDPubKey() = default;

    SERIALIZE_METHODS(CHDPubKey, obj
    )
    {
        READWRITE(obj.nVersion, obj.extPubKey, obj.hdchainID, obj.nAccountIndex, obj.nChangeIndex);
    }

    std::string GetKeyPath() const;
};

#endif // RAPTOREUM_HDCHAIN_H
