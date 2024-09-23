// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LOGGING_H
#define BITCOIN_LOGGING_H

#include <fs.h>
#include <tinyformat.h>
#include <threadsafety.h>
#include <util/string.h>

#include <atomic>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <vector>

static const bool DEFAULT_LOGTIMEMICROS = false;
static const bool DEFAULT_LOGIPS = false;
static const bool DEFAULT_LOGTIMESTAMPS = true;
static const bool DEFAULT_LOGTHREADNAMES = false;
static const bool DEFAULT_LOGSOURCELOCATIONS = false;
extern const char *const DEFAULT_DEBUGLOGFILE;

extern bool fLogThreadNames;
extern bool fLogIPs;

struct LogCategory {
    std::string category;
    bool active;
};

namespace BCLog {
    enum LogFlags : uint64_t {
        NONE = 0,
        NET = (1 << 0),
        TOR = (1 << 1),
        MEMPOOL = (1 << 2),
        HTTP = (1 << 3),
        BENCHMARK = (1 << 4),
        ZMQ = (1 << 5),
        WALLETDB = (1 << 6),
        RPC = (1 << 7),
        ESTIMATEFEE = (1 << 8),
        ADDRMAN = (1 << 9),
        SELECTCOINS = (1 << 10),
        REINDEX = (1 << 11),
        CMPCTBLOCK = (1 << 12),
        RANDOM = (1 << 13),
        PRUNE = (1 << 14),
        PROXY = (1 << 15),
        MEMPOOLREJ = (1 << 16),
        LIBEVENT = (1 << 17),
        COINDB = (1 << 18),
        QT = (1 << 19),
        LEVELDB = (1 << 20),
        LOCK = (1 << 21),

        //Start Raptoreum
        CHAINLOCKS = ((uint64_t) 1 << 32),
        GOBJECT = ((uint64_t) 1 << 33),
        INSTANTSEND = ((uint64_t) 1 << 34),
        LLMQ = ((uint64_t) 1 << 36),
        LLMQ_DKG = ((uint64_t) 1 << 37),
        LLMQ_SIGS = ((uint64_t) 1 << 38),
        MNPAYMENTS = ((uint64_t) 1 << 39),
        MNSYNC = ((uint64_t) 1 << 40),
        COINJOIN = ((uint64_t) 1 << 41),
        SPORK = ((uint64_t) 1 << 42),
        NETCONN = ((uint64_t) 1 << 43),
        QUORUMS = ((uint64_t) 1 << 44),
        UPDATES = ((uint64_t) 1 << 45),

        RTM = CHAINLOCKS | GOBJECT | INSTANTSEND | LLMQ | LLMQ_DKG | LLMQ_SIGS
              | MNPAYMENTS | MNSYNC | COINJOIN | SPORK | NETCONN | QUORUMS | UPDATES,

        NET_NETCONN = NET | NETCONN, // use this to have something logged in NET and NETCONN as well
        //End Raptoreum

        ALL = ~(uint64_t) 0,
    };

    class Logger {
    private:
        mutable StdMutex m_cs; // Can not use Mutex from sync.h because in debug mode it would cause deadlock when a potential deadlock was detected

        FILE *m_fileout
        GUARDED_BY(m_cs) = nullptr;
        std::list <std::string> m_msgs_before_open
        GUARDED_BY(m_cs);
        bool m_buffering
        GUARDED_BY(m_cs) = true; //!< Buffer messages before logging can be started.

        /**
         * m_started_new_line is a state variable that will suppress
         * printing of the timestamp when multiple calls are made
         * that do not end in a newline.
         */
        std::atomic_bool m_started_new_line{true};

        /** Log categories bitfield. */
        std::atomic <uint64_t> m_categories{0};

        std::string LogTimestampStr(const std::string &str);

        std::string LogThreadNameStr(const std::string &str);

    public:
        bool m_print_to_console = false;
        bool m_print_to_file = false;

        bool m_log_timestamps = DEFAULT_LOGTIMESTAMPS;
        bool m_log_time_micros = DEFAULT_LOGTIMEMICROS;
        bool m_log_threadnames = DEFAULT_LOGTHREADNAMES;
        bool m_log_sourcelocations = DEFAULT_LOGSOURCELOCATIONS;

        fs::path m_file_path;
        std::atomic<bool> m_reopen_file{false};

        /** Send a string to ne log output */
        void LogPrintStr(const std::string &str, const std::string &logging_function, const std::string &source_file,
                         const int source_line);

        /** Returns wheter logs will be written to any output */
        bool Enabled() const {
            StdLockGuard scoped_lock(m_cs);
            return m_buffering || m_print_to_console || m_print_to_file;
        }

        /** Start logging (and flush all buffered messages) */
        bool StartLogging();

        /** Only for testing */
        void DisconnectTestLogger();

        void ShrinkDebugFile();

        uint64_t GetCategoryMask() const { return m_categories.load(); }

        void EnableCategory(LogFlags flag);

        bool EnableCategory(const std::string &str);

        void DisableCategory(LogFlags flag);

        bool DisableCategory(const std::string &str);

        bool WillLogCategory(LogFlags category) const;

        /** Returns a vector of the log categories in Alphabetical order. */
        std::vector <LogCategory> LogCategoriesList() const;

        /** Returns a string with the log categories in Alphabetical order. */
        std::string LogCategoriesString() const {
            return Join(LogCategoriesList(), ", ", [&](const LogCategory &i) { return i.category; });
        };

        bool DefaultShrinkDebugFile() const;
    };
}

BCLog::Logger &LogInstance();

/** Return true if log accepts specified category */
static inline bool LogAcceptCategory(BCLog::LogFlags category) {
    return LogInstance().WillLogCategory(category);
}

/** Return true of str parses as a log category and set the flag */
bool GetLogCategory(BCLog::LogFlags &flag, const std::string &str);

/** Formats a string without throwing exceptions. Instead, it'll return an error string instead of formatted string. */
template<typename... Args>
std::string SafeStringFormat(const std::string &fmt, const Args &... args) {
    try {
        return tinyformat::format(fmt, args...);
    } catch (std::runtime_error &fmterr) {
        std::string message = tinyformat::format("\n****TINYFORMAT ERROR****\n    err=\"%s\"\n    fmt=\"%s\"\n",
                                                 fmterr.what(), fmt);
        tfm::format(std::cerr, "%s", message);
        return message;
    }
}

template<typename... Args>
static inline void
LogPrintf_(const std::string &logging_function, const std::string &source_file, const int source_line, const char *fmt,
           const Args &... args) {
    if (LogInstance().Enabled()) {
        std::string log_msg;
        try {
            log_msg = tfm::format(fmt, args...);
        } catch (tinyformat::format_error &fmterr) {
            /* Original format string will have newline so don't add one here */
            log_msg = "Error \"" + std::string(fmterr.what()) + "\" while formatting log message: " + fmt;
        }
        LogInstance().LogPrintStr(log_msg, logging_function, source_file, source_line);
    }
}

#define LogPrintf(...) LogPrintf_(__func__, __FILE__, __LINE__, __VA_ARGS__)

// Use a macro instead of a function for conditional logging to prevent
// evaluating arguments when logging for the category is not enabled.
#define LogPrint(category, ...)              \
    do {                                     \
        if (LogAcceptCategory((category))) { \
            LogPrintf(__VA_ARGS__);          \
        }                                    \
    } while (0)

#endif // BITCOIN_LOGGING_H