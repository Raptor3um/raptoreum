// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_INIT_H
#define BITCOIN_INIT_H

#include <memory>
#include <string>
#include <util/system.h>

struct NodeContext;
namespace interfaces {
    struct BlockAndHeaderTipInfo;
} // namespace interfaces
namespace boost {
    class thread_group;
} // namespace boost
namespace util {
    class Ref;
}

/** Interrupt threads */
void Interrupt(NodeContext &node);

void Shutdown(NodeContext &node);

//!Initialize the logging infrastructure
void InitLogging();

//!Parameter interaction: change current parameters depending on various rules
void InitParameterInteraction();

/** Initialize Raptoreum Core: Basic context setup.
 *  @note This can be done before daemonization. Do not call Shutdown() if this function fails.
 *  @pre Parameters should be parsed and config file should be read.
 */
bool AppInitBasicSetup();

/**
 * Initialization: parameter interaction.
 * @note This can be done before daemonization. Do not call Shutdown() if this function fails.
 * @pre Parameters should be parsed and config file should be read, AppInitBasicSetup should have been called.
 */
bool AppInitParameterInteraction();

/**
 * Initialization sanity checks: ecc init, sanity checks, dir lock.
 * @note This can be done before daemonization. Do not call Shutdown() if this function fails.
 * @pre Parameters should be parsed and config file should be read, AppInitParameterInteraction should have been called.
 */
bool AppInitSanityChecks();

/**
 * Lock Raptoreum Core data directory.
 * @note This should only be done after daemonization. Do not call Shutdown() if this function fails.
 * @pre Parameters should be parsed and config file should be read, AppInitSanityChecks should have been called.
 */
bool AppInitLockDataDirectory();

/**
 * Initialize node and wallet interface pointers. Has no prerequisites or side effects besides allocating memory.
 */
bool AppInitInterfaces(NodeContext &node);

/**
 * Raptoreum Core main initialization.
 * @note This should only be done after daemonization. Call Shutdown() if this function fails.
 * @pre Parameters should be parsed and config file should be read, AppInitLockDataDirectory should have been called.
 */
bool AppInitMain(const util::Ref &context, NodeContext &node, interfaces::BlockAndHeaderTipInfo *tip_info = nullptr);

void PrepareShutdown(NodeContext &node);

/**
 * Setup the arguments for gArgs
 */
void SetupServerArgs();

/** Returns licensing information (for -version) */
std::string LicenseInfo();

#endif // BITCOIN_INIT_H
