// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2021 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/raptoreum-config.h>
#endif

#include <chainparams.h>
#include <clientversion.h>
#include <compat.h>
#include <fs.h>
#include <interfaces/chain.h>
#include <rpc/server.h>
#include <init.h>
#include <node/context.h>
#include <noui.h>
#include <shutdown.h>
#include <ui_interface.h>
#include <util/ref.h>
#include <util/system.h>
#include <util/strencodings.h>
#include <util/threadnames.h>
#include <stacktraces.h>

#include <functional>
#include <stdio.h>

const std::function<std::string(const char *)> G_TRANSLATION_FUN = nullptr;

static void WaitForShutdown(NodeContext &node) {
    while (!ShutdownRequested()) {
        UninterruptibleSleep(std::chrono::milliseconds{200});
    }
    Interrupt(node);
}

static bool AppInit(int argc, char *argv[]) {
    NodeContext node;

    bool fRet = false;

    util::ThreadSetInternalName("init");

    //
    // Parameters
    //
    // If Qt is used, parameters/raptoreum.conf are parsed in qt/raptoreum.cpp's main()
    SetupServerArgs();
    std::string error;
    if (!gArgs.ParseParameters(argc, argv, error)) {
        return InitError(strprintf("Error parsing command line arguments: %s\n", error));
    }

    if (gArgs.IsArgSet("-printcrashinfo")) {
        std::cout << GetCrashInfoStrFromSerializedStr(gArgs.GetArg("-printcrashinfo", "")) << std::endl;
        return true;
    }

    // Process help and version before taking care about datadir
    if (HelpRequested(gArgs) || gArgs.IsArgSet("-version")) {
        std::string
        strUsage = PACKAGE_NAME
        " Daemon version " + FormatFullVersion() + "\n";

        if (gArgs.IsArgSet("-version")) {
            strUsage += FormatParagraph(LicenseInfo()) + "\n";
        } else {
            strUsage += "\nUsage: raptoreumd [options]           Start "
            PACKAGE_NAME
            " Daemon\n";
            strUsage += "\n" + gArgs.GetHelpMessage();
        }

        tfm::format(std::cout, "%s", strUsage);
        return true;
    }

    util::Ref context{node};
    try {
        if (!CheckDataDirOption()) {
            return InitError(strprintf("Error: Specified data directory \"%s\" does not exist.\n",
                                       gArgs.GetArg("-datadir", "")));
        }

        if (!gArgs.ReadConfigFiles(error, true)) {
            return InitError(strprintf("Error reading configuration file: %s\n", error));
        }
        // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
        try {
            SelectParams(gArgs.GetChainName());
        } catch (const std::exception &e) {
            return InitError(strprintf("%s\n", e.what()));
        }

        // Error out when loose non-argument tokens are encountered on command line
        for (int i = 1; i < argc; i++) {
            if (!IsSwitchChar(argv[i][0])) {
                return InitError(strprintf(
                        "Error: Command line contains unexpected token '%s', see raptoreumd -h for a list of options.\n",
                        argv[i]));
            }
        }

        // -server defaults to true for raptoreumd but not for the GUI so do this here
        gArgs.SoftSetBoolArg("-server", true);
        // Set this early so that parameter interactions go to console
        InitLogging();
        InitParameterInteraction();
        if (!AppInitBasicSetup()) {
            // InitError will have been called with detailed error, which ends up on console
            return false;
        }
        if (!AppInitParameterInteraction()) {
            // InitError will have been called with detailed error, which ends up on console
            return false;
        }
        if (!AppInitSanityChecks()) {
            // InitError will have been called with detailed error, which ends up on console
            return false;
        }
        if (gArgs.GetBoolArg("-daemon", false)) {
#if HAVE_DECL_DAEMON
#if defined(MAC_OSX)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
            tfm::format(std::cout, PACKAGE_NAME "daemon starting\n");

            // Daemonize
            if (daemon(1, 0)) { // don't chdir (1), do close FDs (0)
                return InitError(strprintf("daemon() failed: %s\n", strerror(errno)));
            }
#if defined(MAC_OSX)
#pragma GCC diagnostic pop
#endif
#else
            return InitError("-daemon is not supported on this operating system\n");
#endif // HAVE_DECL_DAEMON
        }
        // Lock data directory after daemonization
        if (!AppInitLockDataDirectory()) {
            // If locking the data directory failed, exit immediately
            return false;
        }
        fRet = AppInitInterfaces(node) && AppInitMain(context, node);
    } catch (...) {
        PrintExceptionContinue(std::current_exception(), "AppInit()");
    }

    if (fRet) {
        WaitForShutdown(node);
    }
    Interrupt(node);
    Shutdown(node);

    return fRet;
}

int main(int argc, char *argv[]) {
    RegisterPrettyTerminateHander();
    RegisterPrettySignalHandlers();

#ifdef WIN32
    util::WinCmdLineArgs winArgs;
    std::tie(argc, argv) = winArgs.get();
#endif
    SetupEnvironment();

    // Connect raptoreumd signal handlers
    noui_connect();

    return (AppInit(argc, argv) ? EXIT_SUCCESS : EXIT_FAILURE);
}