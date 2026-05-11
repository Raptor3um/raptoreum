// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/host.h>

#include <evm/account.h>
#include <evm/state_cache.h>

#include <cstring>
#include <vector>

namespace evm {

// ----------------------------------------------------------------------
// Type conversion helpers (evmc:: <-> Bitcoin Core uint types)
// ----------------------------------------------------------------------

uint160 CEvmHost::ToUint160(const evmc::address& a)
{
    uint160 out;
    std::memcpy(out.begin(), a.bytes, 20);
    return out;
}

evmc::address CEvmHost::FromUint160(const uint160& a)
{
    evmc::address out{};
    std::memcpy(out.bytes, a.begin(), 20);
    return out;
}

uint256 CEvmHost::ToUint256(const evmc::bytes32& b)
{
    uint256 out;
    std::memcpy(out.begin(), b.bytes, 32);
    return out;
}

evmc::bytes32 CEvmHost::FromUint256(const uint256& u)
{
    evmc::bytes32 out{};
    std::memcpy(out.bytes, u.begin(), 32);
    return out;
}

// ----------------------------------------------------------------------
// Construction
// ----------------------------------------------------------------------

CEvmHost::CEvmHost(CEvmStateCache& cacheIn,
                   ExecutionContext contextIn,
                   BlockHashFn blockHashFnIn)
    : state(cacheIn),
      context(std::move(contextIn)),
      blockHashFn(std::move(blockHashFnIn))
{
}

// ----------------------------------------------------------------------
// Reads
// ----------------------------------------------------------------------

bool CEvmHost::account_exists(const evmc::address& addr) const noexcept
{
    return state.HasAccount(ToUint160(addr));
}

evmc::bytes32 CEvmHost::get_storage(const evmc::address& addr,
                                    const evmc::bytes32& key) const noexcept
{
    uint256 value;
    if (!state.GetStorage(ToUint160(addr), ToUint256(key), value)) {
        // EVM convention: missing slot reads as zero.
        return evmc::bytes32{};
    }
    return FromUint256(value);
}

evmc::uint256be CEvmHost::get_balance(const evmc::address& addr) const noexcept
{
    evm::CEvmAccount account;
    if (!state.GetAccount(ToUint160(addr), account)) {
        return evmc::uint256be{};
    }
    evmc::uint256be out{};
    std::memcpy(out.bytes, account.balance.begin(), 32);
    return out;
}

size_t CEvmHost::get_code_size(const evmc::address& addr) const noexcept
{
    evm::CEvmAccount account;
    if (!state.GetAccount(ToUint160(addr), account)) {
        return 0;
    }
    if (account.codeHash == evm::CEvmAccount::EmptyCodeHash()) {
        return 0;
    }
    std::vector<uint8_t> code;
    if (!state.GetCode(account.codeHash, code)) {
        return 0;
    }
    return code.size();
}

evmc::bytes32 CEvmHost::get_code_hash(const evmc::address& addr) const noexcept
{
    evm::CEvmAccount account;
    if (!state.GetAccount(ToUint160(addr), account)) {
        // Missing accounts return all-zero per the EVM yellow paper
        // (NOT EmptyCodeHash — that's only for present-but-empty
        // accounts. The distinction matters for EXTCODEHASH callers
        // doing `== keccak256("")` checks.)
        return evmc::bytes32{};
    }
    return FromUint256(account.codeHash);
}

size_t CEvmHost::copy_code(const evmc::address& addr,
                           size_t code_offset,
                           uint8_t* buffer_data,
                           size_t buffer_size) const noexcept
{
    evm::CEvmAccount account;
    if (!state.GetAccount(ToUint160(addr), account)) {
        return 0;
    }
    if (account.codeHash == evm::CEvmAccount::EmptyCodeHash()) {
        return 0;
    }
    std::vector<uint8_t> code;
    if (!state.GetCode(account.codeHash, code)) {
        return 0;
    }
    if (code_offset >= code.size()) {
        return 0;
    }
    const size_t available = code.size() - code_offset;
    const size_t to_copy = std::min(available, buffer_size);
    std::memcpy(buffer_data, code.data() + code_offset, to_copy);
    return to_copy;
}

// ----------------------------------------------------------------------
// Writes
// ----------------------------------------------------------------------

evmc_storage_status CEvmHost::set_storage(const evmc::address& addr,
                                          const evmc::bytes32& key,
                                          const evmc::bytes32& value) noexcept
{
    const uint160 address = ToUint160(addr);
    const uint256 slot = ToUint256(key);
    const uint256 newValue = ToUint256(value);

    uint256 currentValue;
    const bool hadCurrent = state.GetStorage(address, slot, currentValue);
    if (!hadCurrent) {
        currentValue.SetNull();
    }

    state.SetStorage(address, slot, newValue);

    // Status return per EIP-2200/3529. The "_RESTORED" variants require
    // tracking the value at the start of the surrounding transaction,
    // which is wired in Phase 2.3 (transaction-level snapshotting).
    // For Phase 2.2 we report the simpler 4-state distinction; evmone's
    // gas accounting handles this correctly but does not get refunds
    // for the "restored to original" cases yet.
    const bool oldZero = currentValue.IsNull();
    const bool newZero = newValue.IsNull();

    if (currentValue == newValue) {
        return EVMC_STORAGE_ASSIGNED;
    }
    if (oldZero) {
        return EVMC_STORAGE_ADDED;
    }
    if (newZero) {
        return EVMC_STORAGE_DELETED;
    }
    return EVMC_STORAGE_MODIFIED;
}

bool CEvmHost::selfdestruct(const evmc::address& addr,
                            const evmc::address& /*beneficiary*/) noexcept
{
    // Record the intent. The caller will, after execution completes,
    // transfer the contract's balance to the beneficiary and erase the
    // account record from state. Beneficiary-side accounting is added
    // in Phase 2.3 alongside the call() implementation.
    auto inserted = selfdestructed.insert(addr).second;
    return inserted;
}

evmc::Result CEvmHost::call(const evmc_message& /*msg*/) noexcept
{
    // Phase 2.2 deliberately does NOT support nested CALL / CREATE /
    // CREATE2 / DELEGATECALL / STATICCALL / CALLCODE. Contracts that
    // attempt such operations get an empty-output revert. Phase 2.3
    // implements full nested-call semantics with snapshot/revert.
    evmc::Result r;
    r.status_code = EVMC_REVERT;
    r.gas_left = 0;
    return r;
}

evmc::bytes32 CEvmHost::get_block_hash(int64_t block_number) const noexcept
{
    if (blockHashFn) {
        return blockHashFn(block_number);
    }
    // No callback installed: return zero per the EVM yellow paper for
    // out-of-window queries. This is also the right default for unit
    // tests that don't care about BLOCKHASH.
    return evmc::bytes32{};
}

// ----------------------------------------------------------------------
// Transaction context (CHAINID/NUMBER/TIMESTAMP/COINBASE/etc.)
// ----------------------------------------------------------------------

evmc_tx_context CEvmHost::get_tx_context() const noexcept
{
    evmc_tx_context tx{};

    std::memcpy(tx.tx_gas_price.bytes, context.txGasPrice.begin(), 32);

    evmc::address origin = FromUint160(context.txOrigin);
    std::memcpy(tx.tx_origin.bytes, origin.bytes, 20);

    evmc::address coinbase = FromUint160(context.coinbase);
    std::memcpy(tx.block_coinbase.bytes, coinbase.bytes, 20);

    tx.block_number    = static_cast<int64_t>(context.blockHeight);
    tx.block_timestamp = context.blockTimestamp;
    tx.block_gas_limit = static_cast<int64_t>(context.blockGasLimit);

    std::memcpy(tx.block_prev_randao.bytes, context.prevBlockHash.begin(), 32);

    tx.chain_id = evmc::uint256be{static_cast<uint64_t>(context.chainId)};

    std::memcpy(tx.block_base_fee.bytes, context.baseFee.begin(), 32);

    // blob_base_fee + blob_hashes are EIP-4844 fields. We exclude blob
    // transactions in the Cancun target per design decision D6, so leave
    // these zero and empty.
    tx.blob_base_fee = evmc::uint256be{};
    tx.blob_hashes = nullptr;
    tx.blob_hashes_count = 0;

    // initcodes for EOF (post-Cancun); leave empty.
    tx.initcodes = nullptr;
    tx.initcodes_count = 0;

    return tx;
}

void CEvmHost::emit_log(const evmc::address& addr,
                        const uint8_t* data,
                        size_t data_size,
                        const evmc::bytes32 topics[],
                        size_t topics_count) noexcept
{
    Log log;
    log.address = addr;
    log.topics.assign(topics, topics + topics_count);
    log.data.assign(data, data + data_size);
    logs.push_back(std::move(log));
}

// ----------------------------------------------------------------------
// EIP-2929 access lists (warm/cold tracking)
// ----------------------------------------------------------------------

evmc_access_status CEvmHost::access_account(const evmc::address& addr) noexcept
{
    auto inserted = warmAddresses.insert(addr).second;
    return inserted ? EVMC_ACCESS_COLD : EVMC_ACCESS_WARM;
}

evmc_access_status CEvmHost::access_storage(const evmc::address& addr,
                                             const evmc::bytes32& key) noexcept
{
    auto inserted = warmSlots.insert(std::make_pair(addr, key)).second;
    return inserted ? EVMC_ACCESS_COLD : EVMC_ACCESS_WARM;
}

// ----------------------------------------------------------------------
// EIP-1153 transient storage (TLOAD / TSTORE)
// ----------------------------------------------------------------------

evmc::bytes32 CEvmHost::get_transient_storage(const evmc::address& addr,
                                               const evmc::bytes32& key) const noexcept
{
    auto it = transient.find(std::make_pair(addr, key));
    if (it == transient.end()) {
        return evmc::bytes32{};
    }
    return it->second;
}

void CEvmHost::set_transient_storage(const evmc::address& addr,
                                      const evmc::bytes32& key,
                                      const evmc::bytes32& value) noexcept
{
    transient[std::make_pair(addr, key)] = value;
}

} // namespace evm
