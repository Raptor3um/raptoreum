// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/precompiles.h>

namespace evm {

// Phase 4.2 implementation in progress.
evmc::Result ExecuteLlmqOraclePrecompile(CEvmHost& /*host*/,
                                         const evmc_message& msg)
{
    return PrecompileFailure(msg.gas);
}

} // namespace evm
