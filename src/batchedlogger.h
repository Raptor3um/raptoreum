// Copyright (c) 2018-2020 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BATCHEDLOGGER_H
#define BITCOIN_BATCHEDLOGGER_H

#include "logging.h"

class CBatchedLogger {
private:
    bool accept;
    std::string header;
    std::string msg;
public:
    CBatchedLogger(BCLog::LogFlags _category, const std::string &_header);

    virtual ~CBatchedLogger();

    template<typename... Args>
    void Batch(const std::string &fmt, const Args &... args) {
        if (!accept) {
            return;
        }
        msg += "    " + strprintf(fmt, args...) + "\n";
    }

    void Flush();
};

#endif // BITCOIN_BATCHEDLOGGER_H
