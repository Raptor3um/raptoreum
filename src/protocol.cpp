// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <protocol.h>

#include <util/strencodings.h>
#include <util/system.h>

#ifndef WIN32

# include <arpa/inet.h>

#endif

static std::atomic<bool> g_initial_block_download_completed(false);

namespace NetMsgType {
    const char *VERSION = "version";
    const char *VERACK = "verack";
    const char *ADDR = "addr";
    const char *INV = "inv";
    const char *GETDATA = "getdata";
    const char *MERKLEBLOCK = "merkleblock";
    const char *GETBLOCKS = "getblocks";
    const char *GETHEADERS = "getheaders";
    const char *TX = "tx";
    const char *HEADERS = "headers";
    const char *BLOCK = "block";
    const char *GETADDR = "getaddr";
    const char *MEMPOOL = "mempool";
    const char *PING = "ping";
    const char *PONG = "pong";
    const char *NOTFOUND = "notfound";
    const char *FILTERLOAD = "filterload";
    const char *FILTERADD = "filteradd";
    const char *FILTERCLEAR = "filterclear";
    const char *REJECT = "reject";
    const char *SENDHEADERS = "sendheaders";
    const char *SENDCMPCT = "sendcmpct";
    const char *CMPCTBLOCK = "cmpctblock";
    const char *GETBLOCKTXN = "getblocktxn";
    const char *BLOCKTXN = "blocktxn";
// Raptoreum message types
    const char *LEGACYTXLOCKREQUEST = "ix";
    const char *SPORK = "spork";
    const char *GETSPORKS = "getsporks";
    const char *DSACCEPT = "dsa";
    const char *DSVIN = "dsi";
    const char *DSFINALTX = "dsf";
    const char *DSSIGNFINALTX = "dss";
    const char *DSCOMPLETE = "dsc";
    const char *DSSTATUSUPDATE = "dssu";
    const char *DSTX = "dstx";
    const char *DSQUEUE = "dsq";
    const char *SENDDSQUEUE = "senddsq";
    const char *SYNCSTATUSCOUNT = "ssc";
    const char *MNGOVERNANCESYNC = "govsync";
    const char *MNGOVERNANCEOBJECT = "govobj";
    const char *MNGOVERNANCEOBJECTVOTE = "govobjvote";
    const char *GETMNLISTDIFF = "getmnlistd";
    const char *MNLISTDIFF = "mnlistdiff";
    const char *QSENDRECSIGS = "qsendrecsigs";
    const char *QFCOMMITMENT = "qfcommit";
    const char *QCONTRIB = "qcontrib";
    const char *QCOMPLAINT = "qcomplaint";
    const char *QJUSTIFICATION = "qjustify";
    const char *QPCOMMITMENT = "qpcommit";
    const char *QWATCH = "qwatch";
    const char *QSIGSESANN = "qsigsesann";
    const char *QSIGSHARESINV = "qsigsinv";
    const char *QGETSIGSHARES = "qgetsigs";
    const char *QBSIGSHARES = "qbsigs";
    const char *QSIGREC = "qsigrec";
    const char *QSIGSHARE = "qsigshare";
    const char *QGETDATA = "qgetdata";
    const char *QDATA = "qdata";
    const char *CLSIG = "clsig";
    const char *ISLOCK = "islock";
    const char *ISDLOCK = "isdlock";
    const char *MNAUTH = "mnauth";
}; // namespace NetMsgType

/** All known message types. Keep this in the same order as the list of
 * messages above and in protocol.h.
 */
const static std::string allNetMessageTypes[] = {
        NetMsgType::VERSION,
        NetMsgType::VERACK,
        NetMsgType::ADDR,
        NetMsgType::INV,
        NetMsgType::GETDATA,
        NetMsgType::MERKLEBLOCK,
        NetMsgType::GETBLOCKS,
        NetMsgType::GETHEADERS,
        NetMsgType::TX,
        NetMsgType::HEADERS,
        NetMsgType::BLOCK,
        NetMsgType::GETADDR,
        NetMsgType::MEMPOOL,
        NetMsgType::PING,
        NetMsgType::PONG,
        NetMsgType::NOTFOUND,
        NetMsgType::FILTERLOAD,
        NetMsgType::FILTERADD,
        NetMsgType::FILTERCLEAR,
        NetMsgType::REJECT,
        NetMsgType::SENDHEADERS,
        NetMsgType::SENDCMPCT,
        NetMsgType::CMPCTBLOCK,
        NetMsgType::GETBLOCKTXN,
        NetMsgType::BLOCKTXN,
        // Raptoreum message types
        // NOTE: do NOT include non-implmented here, we want them to be "Unknown command" in ProcessMessage()
        NetMsgType::LEGACYTXLOCKREQUEST,
        NetMsgType::SPORK,
        NetMsgType::GETSPORKS,
        NetMsgType::SENDDSQUEUE,
        NetMsgType::DSACCEPT,
        NetMsgType::DSVIN,
        NetMsgType::DSFINALTX,
        NetMsgType::DSSIGNFINALTX,
        NetMsgType::DSCOMPLETE,
        NetMsgType::DSSTATUSUPDATE,
        NetMsgType::DSTX,
        NetMsgType::DSQUEUE,
        NetMsgType::SYNCSTATUSCOUNT,
        NetMsgType::MNGOVERNANCESYNC,
        NetMsgType::MNGOVERNANCEOBJECT,
        NetMsgType::MNGOVERNANCEOBJECTVOTE,
        NetMsgType::GETMNLISTDIFF,
        NetMsgType::MNLISTDIFF,
        NetMsgType::QSENDRECSIGS,
        NetMsgType::QFCOMMITMENT,
        NetMsgType::QCONTRIB,
        NetMsgType::QCOMPLAINT,
        NetMsgType::QJUSTIFICATION,
        NetMsgType::QPCOMMITMENT,
        NetMsgType::QWATCH,
        NetMsgType::QSIGSESANN,
        NetMsgType::QSIGSHARESINV,
        NetMsgType::QGETSIGSHARES,
        NetMsgType::QBSIGSHARES,
        NetMsgType::QSIGREC,
        NetMsgType::QSIGSHARE,
        NetMsgType::QGETDATA,
        NetMsgType::QDATA,
        NetMsgType::CLSIG,
        NetMsgType::ISLOCK,
        NetMsgType::ISDLOCK,
        NetMsgType::MNAUTH,
};
const static std::vector <std::string> allNetMessageTypesVec(allNetMessageTypes,
                                                             allNetMessageTypes + ARRAYLEN(allNetMessageTypes));

CMessageHeader::CMessageHeader(const MessageStartChars &pchMessageStartIn) {
    memcpy(pchMessageStart, pchMessageStartIn, MESSAGE_START_SIZE);
    memset(pchCommand, 0, sizeof(pchCommand));
    nMessageSize = -1;
    memset(pchChecksum, 0, CHECKSUM_SIZE);
}

CMessageHeader::CMessageHeader(const MessageStartChars &pchMessageStartIn, const char *pszCommand,
                               unsigned int nMessageSizeIn) {
    memcpy(pchMessageStart, pchMessageStartIn, MESSAGE_START_SIZE);

    // Copy the command name, zero-padding to COMMAND_SIZE bytes
    size_t i = 0;
    for (; i < COMMAND_SIZE && pszCommand[i] != 0; ++i) pchCommand[i] = pszCommand[i];
    assert(pszCommand[i] == 0); // Assert that the command name passed in is not longer than COMMAND_SIZE
    for (; i < COMMAND_SIZE; ++i) pchCommand[i] = 0;

    nMessageSize = nMessageSizeIn;
    memset(pchChecksum, 0, CHECKSUM_SIZE);
}

std::string CMessageHeader::GetCommand() const {
    return std::string(pchCommand, pchCommand + strnlen(pchCommand, COMMAND_SIZE));
}

bool CMessageHeader::IsValid(const MessageStartChars &pchMessageStartIn) const {
    // Check start string
    if (memcmp(pchMessageStart, pchMessageStartIn, MESSAGE_START_SIZE) != 0)
        return false;

    // Check the command string for errors
    for (const char *p1 = pchCommand; p1 < pchCommand + COMMAND_SIZE; p1++) {
        if (*p1 == 0) {
            // Must be all zeros after the first zero
            for (; p1 < pchCommand + COMMAND_SIZE; p1++)
                if (*p1 != 0)
                    return false;
        } else if (*p1 < ' ' || *p1 > 0x7E)
            return false;
    }

    // Message size
    if (nMessageSize > MAX_SIZE) {
        LogPrintf("CMessageHeader::IsValid(): (%s, %u bytes) nMessageSize > MAX_SIZE\n", GetCommand(), nMessageSize);
        return false;
    }

    return true;
}


ServiceFlags GetDesirableServiceFlags(ServiceFlags services) {
    if ((services & NODE_NETWORK_LIMITED) && g_initial_block_download_completed) {
        return ServiceFlags(NODE_NETWORK_LIMITED);
    }
    return ServiceFlags(NODE_NETWORK);
}

void SetServiceFlagsIBDCache(bool state) {
    g_initial_block_download_completed = state;
}


CAddress::CAddress() : CService() {
    Init();
}

CAddress::CAddress(CService ipIn, ServiceFlags nServicesIn) : CService(ipIn) {
    Init();
    nServices = nServicesIn;
}

void CAddress::Init() {
    nServices = NODE_NONE;
    nTime = 100000000;
}

CInv::CInv() {
    type = 0;
    hash.SetNull();
}

CInv::CInv(int typeIn, const uint256 &hashIn) : type(typeIn), hash(hashIn) {}

bool operator<(const CInv &a, const CInv &b) {
    return (a.type < b.type || (a.type == b.type && a.hash < b.hash));
}

bool CInv::IsKnownType() const {
    return GetCommandInternal() != nullptr;
}

const char *CInv::GetCommandInternal() const {
    switch (type) {
        case MSG_TX:
            return NetMsgType::TX;
        case MSG_BLOCK:
            return NetMsgType::BLOCK;
        case MSG_FILTERED_BLOCK:
            return NetMsgType::MERKLEBLOCK;
        case MSG_LEGACY_TXLOCK_REQUEST:
            return NetMsgType::LEGACYTXLOCKREQUEST;
        case MSG_CMPCT_BLOCK:
            return NetMsgType::CMPCTBLOCK;
        case MSG_SPORK:
            return NetMsgType::SPORK;
        case MSG_DSTX:
            return NetMsgType::DSTX;
        case MSG_GOVERNANCE_OBJECT:
            return NetMsgType::MNGOVERNANCEOBJECT;
        case MSG_GOVERNANCE_OBJECT_VOTE:
            return NetMsgType::MNGOVERNANCEOBJECTVOTE;
        case MSG_QUORUM_FINAL_COMMITMENT:
            return NetMsgType::QFCOMMITMENT;
        case MSG_QUORUM_CONTRIB:
            return NetMsgType::QCONTRIB;
        case MSG_QUORUM_COMPLAINT:
            return NetMsgType::QCOMPLAINT;
        case MSG_QUORUM_JUSTIFICATION:
            return NetMsgType::QJUSTIFICATION;
        case MSG_QUORUM_PREMATURE_COMMITMENT:
            return NetMsgType::QPCOMMITMENT;
        case MSG_QUORUM_RECOVERED_SIG:
            return NetMsgType::QSIGREC;
        case MSG_CLSIG:
            return NetMsgType::CLSIG;
        case MSG_ISLOCK:
            return NetMsgType::ISLOCK;
        case MSG_ISDLOCK:
            return NetMsgType::ISDLOCK;
        default:
            return nullptr;
    }
}

std::string CInv::GetCommand() const {
    auto cmd = GetCommandInternal();
    if (cmd == nullptr) {
        throw std::out_of_range(strprintf("CInv::GetCommand(): type=%d unknown type", type));
    }
    return cmd;
}

std::string CInv::ToString() const {
    auto cmd = GetCommandInternal();
    if (!cmd) {
        return strprintf("0x%08x %s", type, hash.ToString());
    } else {
        return strprintf("%s %s", cmd, hash.ToString());
    }
}

const std::vector <std::string> &getAllNetMessageTypes() {
    return allNetMessageTypesVec;
}

/**
 * Convert a service flag (NODE_*) to a human readable srting.
 * It supports unknown service flags which will be returned as "UNKNOWN[...]".
 * @param[in] bit the service flag is calculated as (1 << bit)
 */
static std::string serviceFlagToStr(size_t bit) {
    const uint64_t service_flag = 1ULL << bit;
    switch ((ServiceFlags) service_flag) {
        case NODE_NONE:
            abort(); // impossible situation
        case NODE_NETWORK:
            return "NETWORK";
        case NODE_GETUTXO:
            return "GETUTXO";
        case NODE_BLOOM:
            return "BLOOM";
        case NODE_XTHIN:
            return "XTHIN";
        case NODE_NETWORK_LIMITED:
            return "NETWORK_LIMITED";
            // Not using default, so we get wqrned when a case is missing
    }

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "UNKNOWN[";
    stream << "2^" << bit;
    stream << "]";
    return stream.str();
}

std::vector <std::string> serviceFlagsToStr(uint64_t flags) {
    std::vector <std::string> str_flags;

    for (size_t i = 0; i < sizeof(flags) * 8; ++i) {
        if (flags & (1ULL << i)) {
            str_flags.emplace_back(serviceFlagToStr(i));
        }
    }

    return str_flags;
}
