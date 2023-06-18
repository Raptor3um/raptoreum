// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/ismine.h>

#include <key.h>
#include <script/script.h>

#include <wallet/wallet.h>

typedef std::vector<unsigned char> valtype;

static bool HaveKeys(const std::vector<valtype>& pubkeys, const CWallet& keystore)
{
    for (const valtype& pubkey : pubkeys) {
        CKeyID keyID = CPubKey(pubkey).GetID();
        if (!keystore.HaveKey(keyID)) return false;
    }
    return true;
}

isminetype IsMine(const CWallet& keystore, const CScript& scriptPubKey, SigVersion sigversion)
{
  bool isInvalid = false;
  return IsMine(keystore, scriptPubKey, isInvalid, sigversion);
}

isminetype IsMine(const CWallet& keystore, const CTxDestination& dest, SigVersion sigversion)
{
  bool isInvalid = false;
  return IsMine(keystore, dest, isInvalid, sigversion);
}

isminetype IsMine(const CWallet &keystore, const CTxDestination& dest, bool& isInvalid, SigVersion sigversion)
{
  CScript script = GetScriptForDestination(dest);
  return IsMine(keystore, script, isInvalid, sigversion);
}

isminetype IsMine(const CWallet &keystore, const CScript& scriptPubKey, bool& isInvalid, SigVersion sigversion)
{
    std::vector<valtype> vSolutions;
    txnouttype whichType = Solver(scriptPubKey, vSolutions);

    CKeyID keyID;
    switch (whichType)
    {
    case TX_NONSTANDARD:
    case TX_NULL_DATA:
        break;
    case TX_PUBKEY:
        keyID = CPubKey(vSolutions[0]).GetID();
        if (sigversion != SigVersion::BASE && vSolutions[0].size() != 33) {
          isInvalid = true;
          return ISMINE_NO;
        }
        if (keystore.HaveKey(keyID))
            return ISMINE_SPENDABLE;
        break;
    case TX_PUBKEYHASH:
        keyID = CKeyID(uint160(vSolutions[0]));
        if (sigversion != SigVersion::BASE) {
          CPubKey pubkey;
          if (keystore.GetPubKey(keyID, pubkey) && !pubkey.IsCompressed()) {
            isInvalid = true;
            return ISMINE_NO;
          }
        }
        if (keystore.HaveKey(keyID))
            return ISMINE_SPENDABLE;
        break;
    case TX_SCRIPTHASH:
    {
        CScriptID scriptID = CScriptID(uint160(vSolutions[0]));
        CScript subscript;
        if (keystore.GetCScript(scriptID, subscript)) {
            isminetype ret = IsMine(keystore, subscript, isInvalid);
            if (ret == ISMINE_SPENDABLE || ret == ISMINE_WATCH_SOLVABLE || (ret == ISMINE_NO && isInvalid))
                return ret;
        }
        break;
    }
    case TX_MULTISIG:
    {
        // Only consider transactions "mine" if we own ALL the
        // keys involved. Multi-signature transactions that are
        // partially owned (somebody else has a key that can spend
        // them) enable spend-out-from-under-you attacks, especially
        // in shared-wallet situations.
        std::vector<valtype> keys(vSolutions.begin()+1, vSolutions.begin()+vSolutions.size()-1);
        if (sigversion != SigVersion::BASE) {
          for (size_t i = 0; i < keys.size(); i++) {
            if (keys[i].size() != 33) {
              isInvalid = true;
              return ISMINE_NO;
            }
          }
        }
        if (HaveKeys(keys, keystore))
            return ISMINE_SPENDABLE;
        break;
    }
    case TX_TRANSFER_ASSET: {
        keyID = CKeyID(uint160(vSolutions[0]));
        if (keystore.HaveKey(keyID))
            return ISMINE_SPENDABLE;
        break;
    }
    }

    if (keystore.HaveWatchOnly(scriptPubKey)) {
        // TODO: This could be optimized some by doing some work after the above solver
        SignatureData sigs;
        return ProduceSignature(keystore, DUMMY_SIGNATURE_CREATOR, scriptPubKey, sigs) ? ISMINE_WATCH_SOLVABLE : ISMINE_WATCH_UNSOLVABLE;
    }
    return ISMINE_NO;
}
