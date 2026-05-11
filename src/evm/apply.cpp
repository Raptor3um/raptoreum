// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/apply.h>

#include <evm/account.h>
#include <evm/state_cache.h>

#include <evmc/evmc.hpp>
#include <evmone/evmone.h>

#include <cstring>

namespace evm {

namespace {

// uint256 (Bitcoin Core layout: m_data[0] is highest-order byte to
// match the on-wire big-endian form we use elsewhere in this module)
// to evmc::address: take the low 20 bytes (m_data[12..31]).
evmc::address Uint256LowToEvmcAddress(const uint256& u)
{
    evmc::address out{};
    std::memcpy(out.bytes, u.begin() + 12, 20);
    return out;
}

// uint256 to evmc::uint256be: byte-for-byte copy. m_data layout
// matches the big-endian wire form, so no reordering needed.
evmc::uint256be Uint256ToEvmcU256(const uint256& u)
{
    evmc::uint256be out{};
    std::memcpy(out.bytes, u.begin(), 32);
    return out;
}

} // anonymous namespace

ApplyResult ApplyEvmCallTx(const CEvmCallTx& payload,
                           CEvmStateCache& cache,
                           const ExecutionContext& context)
{
    ApplyResult out;

    // ----------------------------------------------------------------
    // 1. Derive sender and recipient (EVM 20-byte) addresses from the
    //    payload's 32-byte fields. The payload uses uint256 for both
    //    senderHash and toAddress so the serialized form is regular;
    //    EVM addresses live in the low 20 bytes.
    // ----------------------------------------------------------------

    const evmc::address sender = Uint256LowToEvmcAddress(payload.senderHash);
    const evmc::address recipient = Uint256LowToEvmcAddress(payload.toAddress);

    // ----------------------------------------------------------------
    // 2. Load recipient's deployed code (if any) for evmone to execute.
    //    A call to an account with no code is legal — evmone just
    //    transfers value (if any) and returns success with empty data.
    // ----------------------------------------------------------------

    std::vector<uint8_t> code;
    {
        evm::CEvmAccount recipientAccount;
        const uint160 recipient160(std::vector<unsigned char>(
            recipient.bytes, recipient.bytes + 20));
        if (cache.GetAccount(recipient160, recipientAccount) &&
            recipientAccount.codeHash != evm::CEvmAccount::EmptyCodeHash())
        {
            cache.GetCode(recipientAccount.codeHash, code);
        }
    }

    // ----------------------------------------------------------------
    // 3. Build the host and the evmc_message, then execute via evmone.
    // ----------------------------------------------------------------

    CEvmHost host(cache, context);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.flags = 0;
    msg.depth = 0;
    msg.gas = static_cast<int64_t>(payload.gasLimit);
    msg.recipient = recipient;
    msg.sender = sender;
    msg.code_address = recipient;
    // payload.value is uint64_t (RTM weis). Promote into the high bits
    // of an evmc::uint256be — last 8 bytes are the value, big-endian.
    {
        evmc::uint256be v{};
        for (int i = 0; i < 8; ++i) {
            v.bytes[24 + i] = static_cast<uint8_t>(payload.value >> (56 - 8 * i));
        }
        msg.value = v;
    }
    msg.input_data = payload.data.empty() ? nullptr : payload.data.data();
    msg.input_size = payload.data.size();

    evmc::VM vm{evmc_create_evmone()};
    evmc::Result r = vm.execute(host, EVMC_CANCUN, msg,
                                code.empty() ? nullptr : code.data(),
                                code.size());

    // ----------------------------------------------------------------
    // 4. Pack the result for the caller. Logs and selfdestructs come
    //    from the host (they were captured during execution); the
    //    caller decides whether to keep them based on the status code.
    // ----------------------------------------------------------------

    out.statusCode = r.status_code;
    out.gasUsed = static_cast<int64_t>(payload.gasLimit) - r.gas_left;
    if (r.output_size > 0 && r.output_data != nullptr) {
        out.returnData.assign(r.output_data, r.output_data + r.output_size);
    }
    out.logs = host.Logs();
    out.selfdestructs = host.Selfdestructs();

    return out;
}

} // namespace evm
