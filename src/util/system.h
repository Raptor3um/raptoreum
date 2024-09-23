// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Server/client environment: argument handling, config file parsing,
 * thread wrappers, startup time
 */
#ifndef BITCOIN_UTIL_SYSTEM_H
#define BITCOIN_UTIL_SYSTEM_H

#if defined(HAVE_CONFIG_H)
#include <config/raptoreum-config.h>
#endif

#include <attributes.h>
#include <compat.h>
#include <compat/assumptions.h>
#include <fs.h>
#include <logging.h>
#include <sync.h>
#include <util/threadnames.h>
#include <tinyformat.h>
#include <util/memory.h>
#include <util/time.h>
#include <amount.h>

#include <exception>
#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include <boost/thread/condition_variable.hpp>

// Debugging macros

// Uncomment the following line to enable debugging messages
// or enable on a per file basis prior to inclusion of util.h
//#define ENABLE_RAPTOREUM_DEBUG
#ifdef ENABLE_RAPTOREUM_DEBUG
#define DBG( x ) x
#else
#define DBG(x)
#endif

//Raptoreum only features

extern bool fSmartnodeMode;
extern bool fDisableGovernance;
extern int nWalletBackups;
extern const std::string gCoinJoinName;

// Application startup time (used for uptime calculation)
int64_t GetStartupTime();

static const int64_t DEFAULT_POW_CACHE_SIZE = 1000000;
static const int DEFAULT_MAX_LOAD_SIZE = 720;
static bool DEFAULT_VALIDATE_POW_CACHE = false;

extern const char *const BITCOIN_CONF_FILENAME;

/** Translate a message to the native user language. */
const extern std::function<std::string(const char *)> G_TRANSLATION_FUN;

/**
 * Translation function:
 * If no translation function is simply return the input.
 */
inline std::string _(const char *psz) {
    return G_TRANSLATION_FUN ? (G_TRANSLATION_FUN)(psz) : psz;
}

void SetupEnvironment();

bool SetupNetworking();

template<typename... Args>
bool error(const char *fmt, const Args &... args) {
    LogPrintf("ERROR: %s\n", SafeStringFormat(fmt, args...));
    return false;
}

void PrintExceptionContinue(const std::exception_ptr pex, const char *pszExceptionOrigin);

bool FileCommit(FILE *file);

bool TruncateFile(FILE *file, unsigned int length);

int RaiseFileDescriptorLimit(int nMinFD);

void AllocateFileRange(FILE *file, unsigned int offset, unsigned int length);

[[nodiscard]] bool RenameOver(fs::path src, fs::path dest);

bool LockDirectory(const fs::path &directory, const std::string lockfile_name, bool probe_only = false);

void UnlockDirectory(const fs::path &dirctory, const std::string &lockfile_name);

bool DirIsWritable(const fs::path &directory);

bool CheckDiskSpace(const fs::path &dir, uint64_t additional_bytes = 0);

/** Release all directory locks. This is used for unit testing only, at runtime
 * the global destructor will take care of the locks.
 */
void ReleaseDirectoryLocks();

bool TryCreateDirectories(const fs::path &p);

fs::path GetDefaultDataDir();

// The blocks directory is always network specific.
const fs::path &GetBlocksDir();

const fs::path &GetDataDir(bool fNetSpecific = true);

fs::path GetBackupsDir();

bool CheckDataDirOption();

void ClearDatadirCache();

fs::path GetConfigFile(const std::string &confPath);

#ifdef WIN32
fs::path GetSpecialFolderPath(int nFolder, bool fCreate = true);
#endif

void runCommand(const std::string &strCommand);

/**
 * Most paths passed as configuration arguments are treated as relative to
 * the datadir if they are not absolute.
 *
 * @param path The path to be conditionally prefixed with datadir.
 * @param net_specific Forwarded to GetDataDir().
 * @return The normalized path.
 */
fs::path AbsPathForConfigVal(const fs::path &path, bool net_specific = true);

inline bool IsSwitchChar(char c) {
#ifdef WIN32
    return c == '-' || c == '/';
#else
    return c == '-';
#endif
}

enum class OptionsCategory {
    OPTIONS,
    CONNECTION,
    INDEXING,
    SMARTNODE,
    STATSD,
    WALLET,
    WALLET_FEE,
    WALLET_HD,
    WALLET_COINJOIN,
    WALLET_DEBUG_TEST,
    ZMQ,
    DEBUG_TEST,
    CHAINPARAMS,
    NODE_RELAY,
    BLOCK_CREATION,
    RPC,
    GUI,
    COMMANDS,
    REGISTER_COMMANDS,

    HIDDEN // Always the last option to avoid printing these in the help.
};

struct SectionInfo {
    std::string m_name;
    std::string m_file;
    int m_line;
};

class ArgsManager {
public:
    enum Flags {
        NONE = 0x00,
        // Boolean options can accept negation syntaz -noOPTION or -noOPTION=1
        ALLOW_BOOL = 0x01,
        ALLOW_INT = 0x02,
        ALLOW_STRING = 0x04,
        ALLOW_ANY = ALLOW_BOOL | ALLOW_INT | ALLOW_STRING,
        DEBUG_ONLY = 0x100,
        /* Some options would cause cross-contamination if values for
         * mainnet were used while running regtest/testnet (or vice-versa).
         * Setting them as NETWORK_ONLY ensures that sharing a config file
         * between mainnet and regtest/testnet will not cause problems
         * due to these parameters by accident
         */
        NETWORK_ONLY = 0x200,
    };

protected:
    friend class ArgsManagerHelper;

    struct Arg {
        std::string m_help_param;
        std::string m_help_text;
        unsigned int m_flags;
    };

    mutable RecursiveMutex cs_args;
    std::map <std::string, std::vector<std::string>> m_override_args
    GUARDED_BY(cs_args);
    std::map <std::string, std::vector<std::string>> m_command_line_args
    GUARDED_BY(cs_args);
    std::map <std::string, std::vector<std::string>> m_config_args
    GUARDED_BY(cs_args);
    std::string m_network
    GUARDED_BY(cs_args);
    std::set <std::string> m_network_only_args
    GUARDED_BY(cs_args);
    std::map <OptionsCategory, std::map<std::string, Arg>> m_available_args
    GUARDED_BY(cs_args);
    std::list <SectionInfo> m_config_sections
    GUARDED_BY(cs_args);

    [[nodiscard]] bool ReadConfigStream(std::istream &stream, const std::string &filepath, std::string &error,
                                        bool ignore_invalid_keys = false);

public:
    ArgsManager();

    /**
     * Select the network in use
     */
    void SelectConfigNetwork(const std::string &network);

    bool ParseParameters(int argc, const char *const argv[], std::string &error);

    [[nodiscard]] bool ReadConfigFiles(std::string &error, bool ignore_invalid_keys = false);

    /**
     * Log warnings for options in m_section_only_args when
     * they are specified in the default section but not overriden
     * on the command line or in a network-specific section
     * in the config file.
     */
    const std::set <std::string> GetUnsuitableSectionOnlyArgs() const;

    /**
     * Log warnings for unrecognized section names in the config file.
     */
    const std::list <SectionInfo> GetUnrecognizedSections() const;

    /**
     * Return the map of all the args passed via command line.
     */
    const std::map <std::string, std::vector<std::string>> GetCommandLineArgs() const;

    /**
     * Return a vector of strings of the given argument
     *
     * @param strArg Argument to get (e.g. "-foo")
     * @return command-line arguments
     */
    std::vector <std::string> GetArgs(const std::string &strArg) const;

    /**
     * Return true if the given argument has been manually set
     *
     * @param strArg Argument to get (e.g. "-foo")
     * @return true if the argument has been set
     */
    bool IsArgSet(const std::string &strArg) const;

    /**
     * Return true if the argument was originally passed as a negated option,
     * i.e. -nofoo.
     *
     * @param strArg Argument to get (e.g. "-foo")
     * @return true if the argument was passed negated
     */
    bool IsArgNegated(const std::string &strArg) const;

    /**
     * Return string argument or default value
     *
     * @param strArg Argument to get (e.g. "-foo")
     * @param strDefault (e.g. "1")
     * @return command-line argument or default value
     */
    std::string GetArg(const std::string &strArg, const std::string &strDefault) const;

    /**
     * Return integer argument or default value
     *
     * @param strArg Argument to get (e.g. "-foo")
     * @param nDefault (e.g. 1)
     * @return command-line argument (0 if invalid number) or default value
     */
    int64_t GetArg(const std::string &strArg, int64_t nDefault) const;

    /**
     * Return boolean argument or default value
     *
     * @param strArg Argument to get (e.g. "-foo")
     * @param fDefault (true or false)
     * @return command-line argument or default value
     */
    bool GetBoolArg(const std::string &strArg, bool fDefault) const;

    /**
     * Set an argument if it doesn't already have a value
     *
     * @param strArg Argument to set (e.g. "-foo")
     * @param strValue Value (e.g. "1")
     * @return true if argument gets set, false if it already had a value
     */
    bool SoftSetArg(const std::string &strArg, const std::string &strValue);

    /**
     * Set a boolean argument if it doesn't already have a value
     *
     * @param strArg Argument to set (e.g. "-foo")
     * @param fValue Value (e.g. false)
     * @return true if argument gets set, false if it already had a value
     */
    bool SoftSetBoolArg(const std::string &strArg, bool fValue);

    // Forces an arg setting. Called by SoftSetArg() if the arg hasn't already
    // been set. Also called directly in testing.
    void ForceSetArg(const std::string &strArg, const std::string &strValue);

    void ForceRemoveArg(const std::string &strArg);

    /**
     * Looks for -regtest, -testnet and returns the appropriate BIP70 chain name.
     * @return CBaseChainParams::MAIN by default; raises runtime error if an invalid combination is given.
     */
    std::string GetChainName() const;

    /**
     * Looks for -devnet and returns either "devnet-<name>" or simply "devnet" if no name was specified.
     * This function should never be called for non-devnets.
     * @return either "devnet-<name>" or "devnet"; raises runtime error if no -devent was specified.
     */
    std::string GetDevNetName() const;

    /**
     * Add argument
     */
    void AddArg(const std::string &name, const std::string &help, unsigned int flags, const OptionsCategory &cat);

    /**
     * Add many hidden arguments
     */
    void AddHiddenArgs(const std::vector <std::string> &args);

    /**
     * Clear available arguments
     */
    void ClearArgs() {
        LOCK(cs_args);
        m_available_args.clear();
        m_network_only_args.clear();
    }

    std::string GetHelpMessage() const;

    unsigned int FlagsOfKnownArg(const std::string &key) const;
};

extern ArgsManager gArgs;

bool HelpRequested(const ArgsManager &args);

/**
 * Format a string to be used as group of options in help messages
 *
 * @param message Group name (e.g. "RPC server options:")
 * @return the formatted string
 */
std::string HelpMessageGroup(const std::string &message);

/**
 * Format a string to be used as option description in help messages
 *
 * @param option Option message (e.g. "-rpcuser=<user>")
 * @param message Option description (e.g. "Username for JSON-RPC connections")
 * @return the formatted string
 */
std::string HelpMessageOpt(const std::string &option, const std::string &message);

/**
 * Return the number of cores available on the current system.
 * @note This does count virtual cores, such as those provided by HyperThreading.
 */
int GetNumCores();

namespace ctpl {
    class thread_pool;
}

void RenameThreadPool(ctpl::thread_pool &tp, const char *baseName);

template<typename Callable>
void TraceThread(const std::string name, Callable func) {
    util::ThreadRename(name.c_str());
    try {
        LogPrintf("%s thread start\n", name);
        func();
        LogPrintf("%s thread exit\n", name);
    }
    catch (const boost::thread_interrupted &) {
        LogPrintf("%s thread interrupt\n", name);
        throw;
    }
    catch (...) {
        PrintExceptionContinue(std::current_exception(), name.c_str());
        throw;
    }
}

std::string CopyrightHolders(const std::string &strPrefix, unsigned int nStartYear, unsigned int nEndYear);

/**
 * On platforms that support it, tell the kernel the calling thread is
 * CPU-intensive and non-interactive. See SCHED_BATCH in sched(7) for details.
 *
 * @return The return value of sched_setschedule(), or 1 on systems without
 * sched_setchedule().
 */
int ScheduleBatchPriority();

void SetThreadPriority(int nPriority);

namespace util {

//! Simplification of std isertion
    template<typename Tdst, typename Tsrc>
    inline void insert(Tdst &dst, const Tsrc &src) {
        dst.insert(dst.begin(), src.begin(), src.end());
    }

    template<typename TsetT, typename Tsrc>
    inline void insert(std::set <TsetT> &dst, const Tsrc &src) {
        dst.insert(src.begin(), src.end());
    }

#ifdef WIN32
    class WinCmdLineArgs
    {
    public:
      WinCmdLineArgs();
      ~WinCmdLineArgs();
      std::pair<int, char**> get();

    private:
      int argc;
      char** argv;
      std::vector<std::string> args;
    };
#endif

} // namespace util

#endif // BITCOIN_UTIL_SYSTEM_H
