// Copyright (c) 2018-2019 The Dash Core developers
// Copyright (c) 2020-2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bls.h"

#include "hash.h"
#include "random.h"
#include "tinyformat.h"

#ifndef BUILD_BITCOIN_INTERNAL
#include "support/allocators/mt_pooled_secure.h"
#endif

#include <assert.h>
#include <string.h>

bool CBLSId::InternalSetBuf(const void* buf)
{
    memcpy(impl.begin(), buf, sizeof(uint256));
    return true;
}

bool CBLSId::InternalGetBuf(void* buf) const
{
    memcpy(buf, impl.begin(), sizeof(uint256));
    return true;
}

void CBLSId::SetInt(int x)
{
    impl.SetHex(strprintf("%x", x));
    fValid = true;
    UpdateHash();
}

void CBLSId::SetHash(const uint256& hash)
{
    impl = hash;
    fValid = true;
    UpdateHash();
}

CBLSId CBLSId::FromInt(int64_t i)
{
    CBLSId id;
    id.SetInt(i);
    return id;
}

CBLSId CBLSId::FromHash(const uint256& hash)
{
    CBLSId id;
    id.SetHash(hash);
    return id;
}

bool CBLSSecretKey::InternalSetBuf(const void* buf)
{
    try {
        impl = bls::PrivateKey::FromBytes((const uint8_t*)buf);
        return true;
    } catch (...) {
        return false;
    }
}

bool CBLSSecretKey::InternalGetBuf(void* buf) const
{
    impl.Serialize((uint8_t*)buf);
    return true;
}

void CBLSSecretKey::AggregateInsecure(const CBLSSecretKey& o)
{
    assert(IsValid() && o.IsValid());
    impl = bls::PrivateKey::AggregateInsecure({impl, o.impl});
    UpdateHash();
}

CBLSSecretKey CBLSSecretKey::AggregateInsecure(const std::vector<CBLSSecretKey>& sks)
{
    if (sks.empty()) {
        return CBLSSecretKey();
    }

    std::vector<bls::PrivateKey> v;
    v.reserve(sks.size());
    for (auto& sk : sks) {
        v.emplace_back(sk.impl);
    }

    auto agg = bls::PrivateKey::AggregateInsecure(v);
    CBLSSecretKey ret;
    ret.impl = agg;
    ret.fValid = true;
    ret.UpdateHash();
    return ret;
}

#ifndef BUILD_BITCOIN_INTERNAL
void CBLSSecretKey::MakeNewKey()
{
    unsigned char buf[32];
    while (true) {
        GetStrongRandBytes(buf, sizeof(buf));
        try {
            impl = bls::PrivateKey::FromBytes((const uint8_t*)buf);
            break;
        } catch (...) {
        }
    }
    fValid = true;
    UpdateHash();
}
#endif

bool CBLSSecretKey::SecretKeyShare(const std::vector<CBLSSecretKey>& msk, const CBLSId& _id)
{
    fValid = false;
    UpdateHash();

    if (!_id.IsValid()) {
        return false;
    }

    std::vector<bls::PrivateKey> mskVec;
    mskVec.reserve(msk.size());
    for (const CBLSSecretKey& sk : msk) {
        if (!sk.IsValid()) {
            return false;
        }
        mskVec.emplace_back(sk.impl);
    }

    try {
        impl = bls::BLS::PrivateKeyShare(mskVec, (const uint8_t*)_id.impl.begin());
    } catch (...) {
        return false;
    }

    fValid = true;
    UpdateHash();
    return true;
}

CBLSPublicKey CBLSSecretKey::GetPublicKey() const
{
    if (!IsValid()) {
        return CBLSPublicKey();
    }

    CBLSPublicKey pubKey;
    pubKey.impl = impl.GetPublicKey();
    pubKey.fValid = true;
    pubKey.UpdateHash();
    return pubKey;
}

CBLSSignature CBLSSecretKey::Sign(const uint256& hash) const
{
    if (!IsValid()) {
        return CBLSSignature();
    }

    CBLSSignature sigRet;
    sigRet.impl = impl.SignInsecurePrehashed((const uint8_t*)hash.begin());

    sigRet.fValid = true;
    sigRet.UpdateHash();

    return sigRet;
}

bool CBLSPublicKey::InternalSetBuf(const void* buf)
{
    try {
        impl = bls::PublicKey::FromBytes((const uint8_t*)buf);
        return true;
    } catch (...) {
        return false;
    }
}

bool CBLSPublicKey::InternalGetBuf(void* buf) const
{
    impl.Serialize((uint8_t*)buf);
    return true;
}

void CBLSPublicKey::AggregateInsecure(const CBLSPublicKey& o)
{
    assert(IsValid() && o.IsValid());
    impl = bls::PublicKey::AggregateInsecure({impl, o.impl});
    UpdateHash();
}

CBLSPublicKey CBLSPublicKey::AggregateInsecure(const std::vector<CBLSPublicKey>& pks)
{
    if (pks.empty()) {
        return CBLSPublicKey();
    }

    std::vector<bls::PublicKey> v;
    v.reserve(pks.size());
    for (auto& pk : pks) {
        v.emplace_back(pk.impl);
    }

    auto agg = bls::PublicKey::AggregateInsecure(v);
    CBLSPublicKey ret;
    ret.impl = agg;
    ret.fValid = true;
    ret.UpdateHash();
    return ret;
}

bool CBLSPublicKey::PublicKeyShare(const std::vector<CBLSPublicKey>& mpk, const CBLSId& _id)
{
    fValid = false;
    UpdateHash();

    if (!_id.IsValid()) {
        return false;
    }

    std::vector<bls::PublicKey> mpkVec;
    mpkVec.reserve(mpk.size());
    for (const CBLSPublicKey& pk : mpk) {
        if (!pk.IsValid()) {
            return false;
        }
        mpkVec.emplace_back(pk.impl);
    }

    try {
        impl = bls::BLS::PublicKeyShare(mpkVec, (const uint8_t*)_id.impl.begin());
    } catch (...) {
        return false;
    }

    fValid = true;
    UpdateHash();
    return true;
}

bool CBLSPublicKey::DHKeyExchange(const CBLSSecretKey& sk, const CBLSPublicKey& pk)
{
    fValid = false;
    UpdateHash();

    if (!sk.IsValid() || !pk.IsValid()) {
        return false;
    }
    impl = bls::BLS::DHKeyExchange(sk.impl, pk.impl);
    fValid = true;
    UpdateHash();
    return true;
}

bool CBLSSignature::InternalSetBuf(const void* buf)
{
    try {
        impl = bls::InsecureSignature::FromBytes((const uint8_t*)buf);
        return true;
    } catch (...) {
        return false;
    }
}

bool CBLSSignature::InternalGetBuf(void* buf) const
{
    impl.Serialize((uint8_t*)buf);
    return true;
}

void CBLSSignature::AggregateInsecure(const CBLSSignature& o)
{
    assert(IsValid() && o.IsValid());
    impl = bls::InsecureSignature::Aggregate({impl, o.impl});
    UpdateHash();
}

CBLSSignature CBLSSignature::AggregateInsecure(const std::vector<CBLSSignature>& sigs)
{
    if (sigs.empty()) {
        return CBLSSignature();
    }

    std::vector<bls::InsecureSignature> v;
    v.reserve(sigs.size());
    for (auto& pk : sigs) {
        v.emplace_back(pk.impl);
    }

    auto agg = bls::InsecureSignature::Aggregate(v);
    CBLSSignature ret;
    ret.impl = agg;
    ret.fValid = true;
    ret.UpdateHash();
    return ret;
}

CBLSSignature CBLSSignature::AggregateSecure(const std::vector<CBLSSignature>& sigs,
                                             const std::vector<CBLSPublicKey>& pks,
                                             const uint256& hash)
{
    if (sigs.size() != pks.size() || sigs.empty()) {
        return CBLSSignature();
    }

    std::vector<bls::Signature> v;
    v.reserve(sigs.size());

    for (size_t i = 0; i < sigs.size(); i++) {
        bls::AggregationInfo aggInfo = bls::AggregationInfo::FromMsgHash(pks[i].impl, hash.begin());
        v.emplace_back(bls::Signature::FromInsecureSig(sigs[i].impl, aggInfo));
    }

    auto aggSig = bls::Signature::AggregateSigs(v);
    CBLSSignature ret;
    ret.impl = aggSig.GetInsecureSig();
    ret.fValid = true;
    ret.UpdateHash();
    return ret;
}

void CBLSSignature::SubInsecure(const CBLSSignature& o)
{
    assert(IsValid() && o.IsValid());
    impl = impl.DivideBy({o.impl});
    UpdateHash();
}

bool CBLSSignature::VerifyInsecure(const CBLSPublicKey& pubKey, const uint256& hash) const
{
    if (!IsValid() || !pubKey.IsValid()) {
        return false;
    }

    try {
        return impl.Verify({(const uint8_t*)hash.begin()}, {pubKey.impl});
    } catch (...) {
        return false;
    }
}

bool CBLSSignature::VerifyInsecureAggregated(const std::vector<CBLSPublicKey>& pubKeys, const std::vector<uint256>& hashes) const
{
    if (!IsValid()) {
        return false;
    }
    assert(!pubKeys.empty() && !hashes.empty() && pubKeys.size() == hashes.size());

    std::vector<bls::PublicKey> pubKeyVec;
    std::vector<const uint8_t*> hashes2;
    hashes2.reserve(hashes.size());
    pubKeyVec.reserve(pubKeys.size());
    for (size_t i = 0; i < pubKeys.size(); i++) {
        auto& p = pubKeys[i];
        if (!p.IsValid()) {
            return false;
        }
        pubKeyVec.push_back(p.impl);
        hashes2.push_back((uint8_t*)hashes[i].begin());
    }

    try {
        return impl.Verify(hashes2, pubKeyVec);
    } catch (...) {
        return false;
    }
}

bool CBLSSignature::VerifySecureAggregated(const std::vector<CBLSPublicKey>& pks, const uint256& hash) const
{
    if (pks.empty()) {
        return false;
    }

    std::vector<bls::AggregationInfo> v;
    v.reserve(pks.size());
    for (auto& pk : pks) {
        auto aggInfo = bls::AggregationInfo::FromMsgHash(pk.impl, hash.begin());
        v.emplace_back(aggInfo);
    }

    bls::AggregationInfo aggInfo = bls::AggregationInfo::MergeInfos(v);
    bls::Signature aggSig = bls::Signature::FromInsecureSig(impl, aggInfo);
    return aggSig.Verify();
}

bool CBLSSignature::Recover(const std::vector<CBLSSignature>& sigs, const std::vector<CBLSId>& ids)
{
    fValid = false;
    UpdateHash();

    if (sigs.empty() || ids.empty() || sigs.size() != ids.size()) {
        return false;
    }

    std::vector<bls::InsecureSignature> sigsVec;
    std::vector<const uint8_t*> idsVec;
    sigsVec.reserve(sigs.size());
    idsVec.reserve(sigs.size());

    for (size_t i = 0; i < sigs.size(); i++) {
        if (!sigs[i].IsValid() || !ids[i].IsValid()) {
            return false;
        }
        sigsVec.emplace_back(sigs[i].impl);
        idsVec.emplace_back(ids[i].impl.begin());
    }

    try {
        impl = bls::BLS::RecoverSig(sigsVec, idsVec);
    } catch (...) {
        return false;
    }

    fValid = true;
    UpdateHash();
    return true;
}

#ifndef BUILD_BITCOIN_INTERNAL

static std::once_flag init_flag;
static mt_pooled_secure_allocator<uint8_t>* secure_allocator_instance;
static void create_secure_allocator()
{
    // make sure LockedPoolManager is initialized first (ensures destruction order)
    LockedPoolManager::Instance();

    // static variable in function scope ensures it's initialized when first accessed
    // and destroyed before LockedPoolManager
    static mt_pooled_secure_allocator<uint8_t> a(sizeof(bn_t) + sizeof(size_t));
    secure_allocator_instance = &a;
}

static mt_pooled_secure_allocator<uint8_t>& get_secure_allocator()
{
    std::call_once(init_flag, create_secure_allocator);
    return *secure_allocator_instance;
}

static void* secure_allocate(size_t n)
{
    uint8_t* ptr = get_secure_allocator().allocate(n + sizeof(size_t));
    *(size_t*)ptr = n;
    return ptr + sizeof(size_t);
}

static void secure_free(void* p)
{
    if (!p) {
        return;
    }

    uint8_t* ptr = (uint8_t*)p - sizeof(size_t);
    size_t n = *(size_t*)ptr;
    return get_secure_allocator().deallocate(ptr, n);
}
#endif

bool BLSInit()
{
#ifndef BUILD_BITCOIN_INTERNAL
    bls::BLS::SetSecureAllocator(secure_allocate, secure_free);
#endif
    return true;
}
