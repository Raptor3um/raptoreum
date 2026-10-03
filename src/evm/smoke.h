// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_SMOKE_H
#define RAPTOREUM_EVM_SMOKE_H

#include <cstdint>
#include <string>
#include <vector>

/**
 * Phase 0 smoke entry point for the EVM integration.
 *
 * This file exists ONLY to validate that evmone compiles and links into
 * raptoreumd, and that we can call into it from C++ with the evmc interface.
 *
 * It has NO consensus impact and is NOT used by any consensus code path.
 * Phase 1 introduces the real integration via new transaction types and
 * opcodes (see docs/evm/PLAN.md).
 *
 * EVM revision is fixed to Cancun per design decision D6.
 */

namespace evm {

/** Result of a Phase 0 smoke EVM execution against an empty world state. */
struct SmokeResult {
    /** evmc_status_code value cast to int32_t (0 = EVMC_SUCCESS). */
    int32_t status_code{0};
    /** Gas consumed by the execution (gas_limit - gas_left). */
    int64_t gas_used{0};
    /** Bytes returned by the contract via RETURN, if any. */
    std::vector<uint8_t> return_data;
};

/**
 * Execute the given bytecode against an empty world state with the given
 * calldata and gas limit. Returns status, gas consumed, and any RETURN
 * data produced.
 *
 * Implementation uses evmone via the evmc interface with an empty
 * MockedHost — no storage, no balance, no nested calls, no real chain
 * state. This is a functional smoke test of the evmone library only.
 *
 * Thread-safety: each call constructs its own VM and host, so concurrent
 * calls from different threads are safe.
 */
SmokeResult EvmSmokeExecute(const std::vector<uint8_t>& bytecode,
                            const std::vector<uint8_t>& calldata,
                            int64_t gas_limit = 1000000);

/** Identity of the linked EVM engine, for the build-time version pin. */
struct EngineInfo {
    std::string name;            // evmc_vm::name, e.g. "evmone"
    std::string version;         // evmc_vm::version, e.g. "0.12.0"
    bool abi_compatible{false};  // evmc::VM::is_abi_compatible()
};

/**
 * Report the linked EVM engine's name/version/ABI compatibility.
 *
 * Used by the evmone version-pin unit test (test gap T4): a silent
 * evmone bump can change execution or gas semantics and cause node
 * state-root divergence. The pin test asserts the engine matches the
 * version pinned in depends/packages/evmone.mk; bumping evmone then
 * forces a conscious update of that constant and a re-validation of the
 * official-tests (Capa B) baseline.
 */
EngineInfo EvmEngineInfo();

} // namespace evm

#endif // RAPTOREUM_EVM_SMOKE_H
