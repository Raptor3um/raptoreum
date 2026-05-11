// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/smoke.h>

#include <evmc/evmc.hpp>
#include <evmc/mocked_host.hpp>
#include <evmone/evmone.h>

namespace evm {

SmokeResult EvmSmokeExecute(const std::vector<uint8_t>& bytecode,
                            const std::vector<uint8_t>& calldata,
                            int64_t gas_limit)
{
    SmokeResult result{};

    // Construct the EVM VM (evmone implementation of the evmc interface).
    // evmc::VM takes ownership of the C handle returned by evmc_create_evmone().
    evmc::VM vm{evmc_create_evmone()};

    // Empty world state: no accounts, no storage, no balance.
    evmc::MockedHost host;

    // Build the call message. EVMC_CALL with empty sender/recipient = 0x00...00.
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.flags = 0;
    msg.depth = 0;
    msg.gas = gas_limit;
    msg.recipient = evmc::address{};
    msg.sender = evmc::address{};
    msg.input_data = calldata.empty() ? nullptr : calldata.data();
    msg.input_size = calldata.size();
    msg.value = evmc::uint256be{};
    msg.code_address = evmc::address{};

    // Execute against fixed Cancun revision per design decision D6.
    evmc::Result evm_result = vm.execute(
        host, EVMC_CANCUN, msg,
        bytecode.empty() ? nullptr : bytecode.data(),
        bytecode.size());

    result.status_code = static_cast<int32_t>(evm_result.status_code);
    result.gas_used = gas_limit - evm_result.gas_left;

    if (evm_result.output_size > 0 && evm_result.output_data != nullptr) {
        result.return_data.assign(
            evm_result.output_data,
            evm_result.output_data + evm_result.output_size);
    }

    return result;
}

} // namespace evm
