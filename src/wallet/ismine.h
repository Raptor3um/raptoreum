// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_ISMINE_H
#define BITCOIN_WALLET_ISMINE_H

#include <script/standard.h>

#include <stdint.h>

class CWallet;

class CScript;

/** IsMine() return codes */
enum isminetype : unsigned int {
    ISMINE_NO = 0,
    //! Indicates that we don't know how to create a scriptSig that would solve this if we were given the appropriate private keys
    ISMINE_WATCH_UNSOLVABLE = 1,
    //! Indicates that we know how to create a scriptSig that would solve this if we were given the appropriate private keys
    ISMINE_WATCH_SOLVABLE = 2,
    ISMINE_WATCH_ONLY = ISMINE_WATCH_SOLVABLE | ISMINE_WATCH_UNSOLVABLE,
    ISMINE_SPENDABLE = 4,
    ISMINE_ALL = ISMINE_WATCH_ONLY | ISMINE_SPENDABLE
};
/** used for bitflags of isminetype */
typedef uint8_t isminefilter;

/* isInvalid becomes true when the script is found invalid by consensus  or policy. This will terminate the recursion
 * and return ISMINE_NO immediately as an invalid script should never be considered as "mine". This is needed as
 * different SIGVERSION may have different network rules.
 */
isminetype IsMine(const CWallet &wallet, const CScript &scriptPubKey, bool &isInvalid, SigVersion = SigVersion::BASE);

isminetype IsMine(const CWallet &wallet, const CScript &scriptPubKey, SigVersion = SigVersion::BASE);

isminetype IsMine(const CWallet &wallet, const CTxDestination &dest, bool &isInvalid, SigVersion = SigVersion::BASE);

isminetype IsMine(const CWallet &wallet, const CTxDestination &dest, SigVersion = SigVersion::BASE);

#endif // BITCOIN_WALLET_ISMINE_H
