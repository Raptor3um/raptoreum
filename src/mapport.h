// Copyright (c) 2011-2020 The Bitcoin Core developers
// Copyright (c)      2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#ifndef MAPPORT_H
#define MAPPORT_H

/** -upnp default */
#ifdef USE_UPNP
static const bool DEFAULT_UPNP = USE_UPNP;
#else
static const bool DEFAULT_UPNP = false;
#endif // USE_UPNP

#ifdef USE_NATPMP
static constexpr bool DEFAULT_NATPMP = USE_NATPMP;
#else
static constexpr bool DEFAULT_NATPMP = false;
#endif // USE_NATPMP

enum MapPortProtoFlag : unsigned int {
  NONE = 0x00,
  UPNP = 0x01,
  NAT_PMP = 0x02,
};

void StartMapPort(bool use_upnp, bool use_natpmp);
void InterruptMapPort();
void StopMapPort();

#endif // MAPPORT_H