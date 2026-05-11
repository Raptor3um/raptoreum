// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/apply.h>

#include <evm/account.h>
#include <evm/hashing.h>
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

ApplyResult ApplyEvmDeployTx(const CEvmDeployTx& payload,
                             CEvmStateCache& cache,
                             const ExecutionContext& context)
{
    ApplyResult out;

    // ----------------------------------------------------------------
    // 1. Derive sender and the contract address.
    // ----------------------------------------------------------------

    uint160 sender;
    std::memcpy(sender.begin(), payload.senderHash.begin() + 12, 20);

    const uint160 contractAddress =
        ContractAddressFromCreate(sender, payload.nonce);
    out.deployedAddress = contractAddress;

    // ----------------------------------------------------------------
    // 2. CREATE-collision check. Per the Ethereum yellow paper, we
    //    cannot deploy to an address that already has code or a
    //    non-zero nonce. (Pre-existing accounts with only a balance
    //    are legal — the deploy proceeds and the balance is preserved.)
    // ----------------------------------------------------------------

    evm::CEvmAccount existing;
    if (cache.GetAccount(contractAddress, existing)) {
        const bool hasCode =
            existing.codeHash != evm::CEvmAccount::EmptyCodeHash();
        if (hasCode || existing.nonce > 0) {
            out.statusCode = EVMC_FAILURE;
            out.gasUsed = static_cast<int64_t>(payload.gasLimit);
            return out;
        }
    }

    // ----------------------------------------------------------------
    // 3. Dispatch the init bytecode through evmone with EVMC_CREATE.
    //    The init code's RETURN output becomes the runtime code that
    //    is persisted under its keccak256 hash.
    // ----------------------------------------------------------------

    CEvmHost host(cache, context);

    evmc_message msg{};
    msg.kind = EVMC_CREATE;
    msg.flags = 0;
    msg.depth = 0;
    msg.gas = static_cast<int64_t>(payload.gasLimit);
    std::memcpy(msg.recipient.bytes, contractAddress.begin(), 20);
    std::memcpy(msg.sender.bytes, sender.begin(), 20);
    msg.code_address = msg.recipient;
    {
        evmc::uint256be v{};
        // payload uses no value-on-create field for now (Phase 2.3b).
        // The CEvmDeployTx struct does not have a `value` field; the
        // contract is born with zero balance and the sender's balance
        // debit (Phase 2.4) covers gas only.
        msg.value = v;
    }
    msg.input_data = nullptr;
    msg.input_size = 0;

    evmc::VM vm{evmc_create_evmone()};
    evmc::Result r = vm.execute(host, EVMC_CANCUN, msg,
                                payload.code.empty() ? nullptr : payload.code.data(),
                                payload.code.size());

    out.statusCode = r.status_code;
    out.gasUsed = static_cast<int64_t>(payload.gasLimit) - r.gas_left;
    if (r.output_size > 0 && r.output_data != nullptr) {
        out.returnData.assign(r.output_data, r.output_data + r.output_size);
    }
    out.logs = host.Logs();
    out.selfdestructs = host.Selfdestructs();

    // ----------------------------------------------------------------
    // 4. On success: install the runtime code + a fresh account
    //    record. The runtime code is the init code's RETURN output;
    //    its keccak256 becomes the account's codeHash.
    // ----------------------------------------------------------------

    if (r.status_code == EVMC_SUCCESS) {
        const std::vector<uint8_t> runtimeCode = out.returnData;
        const uint256 codeHash = Keccak256(runtimeCode);

        cache.SetCode(codeHash, runtimeCode);

        // The deployed account is born with nonce=1 per EIP-161 (after
        // Spurious Dragon) and with the codeHash pointing at the
        // runtime code we just stored. Balance starts at zero — the
        // caller (Phase 2.4) credits the constructor's CALLVALUE if
        // any when sender-side accounting lands.
        evm::CEvmAccount account(
            /*nonce=*/ 1,
            /*balance=*/ uint256(),
            /*codeHash=*/ codeHash,
            /*storageRoot=*/ evm::CEvmAccount::EmptyStorageRoot());
        cache.SetAccount(contractAddress, account);
    }

    return out;
}

// ----------------------------------------------------------------------
// Phase 2.3c — ApplyEvmSpendTx
// ----------------------------------------------------------------------
//
// Helper arithmetic on 32-byte big-endian uint256s. Bitcoin Core's
// uint256 is treated here as a raw byte blob with byte 0 the most
// significant (matches evmc::bytes32 / our wire format). Bitcoin Core's
// arith_uint256 uses a different layout (little-endian 32-bit words),
// so we avoid that conversion path and do byte-level math directly.
//
// These helpers are intentionally local to this file. If we grow more
// 256-bit arithmetic needs, they get promoted into a shared utility
// header.

namespace {

// Returns true if `balance` (big-endian uint256) >= `amount` (uint64).
bool Uint256BE_GreaterOrEqualUint64(const uint256& balance, uint64_t amount)
{
    // Any non-zero byte in the high 24 bytes means balance > 2^64 > any
    // uint64 amount, so the comparison is unambiguously true.
    for (int i = 0; i < 24; ++i) {
        if (*(balance.begin() + i) != 0) return true;
    }
    // Otherwise interpret the low 8 bytes as a single uint64 (big-endian).
    uint64_t low = 0;
    for (int i = 24; i < 32; ++i) {
        low = (low << 8) | static_cast<uint64_t>(*(balance.begin() + i));
    }
    return low >= amount;
}

// In-place subtract a uint64 from a big-endian uint256.
// Returns true on success, false on underflow (caller checked
// GreaterOrEqualUint64 first so this should not fail in practice).
bool Uint256BE_SubUint64(uint256& balance, uint64_t amount)
{
    // Read the low 8 bytes as a uint64.
    uint64_t low = 0;
    for (int i = 24; i < 32; ++i) {
        low = (low << 8) | static_cast<uint64_t>(*(balance.begin() + i));
    }

    if (low >= amount) {
        // No borrow needed: simple subtraction in the low 64 bits.
        low -= amount;
        for (int i = 31; i >= 24; --i) {
            *(balance.begin() + i) = static_cast<uint8_t>(low & 0xFF);
            low >>= 8;
        }
        return true;
    }

    // Need to borrow from the high 24 bytes. Find the highest non-zero
    // byte above index 23 and decrement it; fill the bytes in between
    // with 0xFF (the borrow ripple).
    int borrowFrom = -1;
    for (int i = 23; i >= 0; --i) {
        if (*(balance.begin() + i) != 0) {
            borrowFrom = i;
            break;
        }
    }
    if (borrowFrom == -1) {
        return false; // underflow — caller should have checked first
    }
    *(balance.begin() + borrowFrom) -= 1;
    for (int i = borrowFrom + 1; i < 24; ++i) {
        *(balance.begin() + i) = 0xFF;
    }
    // After the borrow, the low 8 bytes effectively gain 2^64.
    // (low + 2^64) - amount, computed modulo 2^64 via wrap.
    const uint64_t new_low = low - amount; // unsigned wrap is well-defined
    uint64_t v = new_low;
    for (int i = 31; i >= 24; --i) {
        *(balance.begin() + i) = static_cast<uint8_t>(v & 0xFF);
        v >>= 8;
    }
    return true;
}

// Convenience: fail-and-return-result for the early-exit error paths
// in ApplyEvmSpendTx.
ApplyResult SpendFailure(int64_t gasLimit)
{
    ApplyResult out;
    out.statusCode = EVMC_FAILURE;
    out.gasUsed = gasLimit;
    return out;
}

} // anonymous namespace

ApplyResult ApplyEvmSpendTx(const CEvmSpendTx& payload,
                            CEvmStateCache& cache,
                            const ExecutionContext& /*context*/)
{
    const int64_t gasLimitSigned = static_cast<int64_t>(payload.gasLimit);

    // 1. Precision check: weis must be an exact multiple of 10^10 so
    //    the satoshi amount round-trips losslessly.
    if (payload.amount == 0 || payload.amount % kWeisPerSatoshi != 0) {
        return SpendFailure(gasLimitSigned);
    }

    // 2. Output script must not be empty.
    if (payload.outputScript.empty()) {
        return SpendFailure(gasLimitSigned);
    }

    // 3. Load the source account from the cache.
    uint160 fromAddr;
    std::memcpy(fromAddr.begin(), payload.fromAddress.begin() + 12, 20);

    evm::CEvmAccount account;
    if (!cache.GetAccount(fromAddr, account)) {
        return SpendFailure(gasLimitSigned);
    }

    // 4. Balance must cover the requested amount.
    if (!Uint256BE_GreaterOrEqualUint64(account.balance, payload.amount)) {
        return SpendFailure(gasLimitSigned);
    }

    // 5. Debit and write back. The subtraction should succeed since
    //    we just checked >=.
    if (!Uint256BE_SubUint64(account.balance, payload.amount)) {
        // Defensive: would mean the GreaterOrEqualUint64 check lied.
        // Treat as failure rather than corrupting state.
        return SpendFailure(gasLimitSigned);
    }
    cache.SetAccount(fromAddr, account);

    // 6. Record the UTXO credit for the caller.
    ApplyResult out;
    out.statusCode = EVMC_SUCCESS;
    // Ethereum's intrinsic gas for a simple value transfer is 21000.
    // Real fee accounting (gasUsed * effectiveGasPrice debit) lives
    // in Phase 2.4 ConnectBlock; this is the lower bound the caller
    // can use today.
    out.gasUsed = 21000;
    ApplyResult::UtxoCredit credit;
    credit.script = payload.outputScript;
    credit.amount = static_cast<CAmount>(payload.amount / kWeisPerSatoshi);
    out.utxoCredits.push_back(std::move(credit));
    return out;
}

} // namespace evm
