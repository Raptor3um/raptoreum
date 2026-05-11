// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAPTOREUM_EVM_HOST_H
#define RAPTOREUM_EVM_HOST_H

#include <evm/account.h>
#include <evm/state_cache.h>
#include <uint256.h>

#include <evmc/evmc.hpp>

#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace evm {

/**
 * Execution context for a single EVM call frame.
 *
 * Populated by the surrounding ConnectBlock integration (Phase 2.4) and
 * passed into CEvmHost so opcodes like CHAINID / COINBASE / NUMBER /
 * TIMESTAMP / BASEFEE / ORIGIN return correct values.
 *
 * Fields use Bitcoin Core types (uint256, uint160) at the API boundary;
 * CEvmHost converts to evmc:: types internally.
 */
struct ExecutionContext
{
    /** Network chain ID per EIP-155. See docs/evm/PROPOSAL-FOR-CORE-TEAM.md
     *  for proposed values (7373 mainnet, 7374 testnet, etc.). */
    int64_t chainId{0};

    /** Height of the block being executed (NUMBER opcode). */
    uint64_t blockHeight{0};

    /** Block timestamp in Unix seconds (TIMESTAMP opcode). */
    int64_t blockTimestamp{0};

    /** Block gas limit (GASLIMIT opcode). */
    uint64_t blockGasLimit{0};

    /** EIP-1559 base fee (BASEFEE opcode). 256-bit. */
    uint256 baseFee;

    /** Miner address (COINBASE opcode). Stored as the low 160 bits of a uint256. */
    uint160 coinbase;

    /** Transaction originator — the EOA that signed the outer tx (ORIGIN
     *  opcode). Stays constant across nested calls per EVM semantics. */
    uint160 txOrigin;

    /** Effective gas price the originator pays (GASPRICE opcode). */
    uint256 txGasPrice;

    /** Hash of the parent block. Used as fallback for BLOCKHASH(N-1).
     *  A full lookback table for BLOCKHASH(N-2..N-256) is plumbed by the
     *  caller via getBlockHash callback (see CEvmHost ctor). */
    uint256 prevBlockHash;
};

/**
 * Concrete evmc::Host backed by a CEvmStateCache.
 *
 * Lifecycle: one instance per EVM transaction execution. Reads and writes
 * pass through to the cache (which itself caches over the persistent
 * CEvmStateDB). Emitted logs and self-destruct intents accumulate on
 * the host and are extracted by the caller after execution.
 *
 * Phase 2.2 scope:
 *   - All read methods implemented: account_exists, get_storage,
 *     get_balance, get_code_size, get_code_hash, copy_code.
 *   - Write methods implemented: set_storage (with EIP-2200/3529-style
 *     status return), transient storage (EIP-1153), emit_log (logs
 *     captured in memory).
 *   - EIP-2929 warm/cold tracking via access_account / access_storage.
 *   - get_tx_context wired to ExecutionContext.
 *   - get_block_hash dispatches to a caller-provided callback (so we
 *     don't depend on chain.cpp here — keeps the EVM layer testable
 *     in isolation).
 *   - selfdestruct: address is recorded for the caller to process after
 *     execution; no automatic balance transfer in this commit.
 *   - call: stubbed to return EVMC_REVERT — nested call/create are
 *     Phase 2.3 work. This is deliberate: it lets us run leaf contracts
 *     (no inter-contract calls) right now and unblock state-touching
 *     tests, while keeping the surface area for this commit small.
 *
 * Phase 2.3d extends call() to a full nested-call dispatcher:
 *   - EVMC_CALL: value transfer + run callee's code in callee's
 *     storage context.
 *   - EVMC_DELEGATECALL / EVMC_CALLCODE: run target code in current
 *     frame's storage context (DELEGATECALL also preserves outer
 *     sender + value).
 *   - EVMC_STATICCALL: read-only nested call (no state mutations).
 *   - EVMC_CREATE / EVMC_CREATE2: derive new contract address,
 *     transfer value, run init code, install runtime on success.
 *
 * Every nested frame opens a CEvmStateCache savepoint up front;
 * EVMC_REVERT or any failure status restores it before returning.
 */
class CEvmHost final : public evmc::Host
{
public:
    /** Callback type for BLOCKHASH lookups. The host calls this when a
     *  contract executes BLOCKHASH(N). Caller supplies the chain access.
     *  Return all-zero bytes32 when the height is out of the EVM's
     *  256-block lookback window or before genesis. */
    using BlockHashFn = std::function<evmc::bytes32(int64_t blockNumber)>;

    CEvmHost(CEvmStateCache& cacheIn,
             ExecutionContext contextIn,
             BlockHashFn blockHashFn = nullptr);

    // --- HostInterface (all 14 overrides) -----------------------------

    bool account_exists(const evmc::address& addr) const noexcept override;

    evmc::bytes32 get_storage(const evmc::address& addr,
                              const evmc::bytes32& key) const noexcept override;

    evmc_storage_status set_storage(const evmc::address& addr,
                                    const evmc::bytes32& key,
                                    const evmc::bytes32& value) noexcept override;

    evmc::uint256be get_balance(const evmc::address& addr) const noexcept override;

    size_t get_code_size(const evmc::address& addr) const noexcept override;

    evmc::bytes32 get_code_hash(const evmc::address& addr) const noexcept override;

    size_t copy_code(const evmc::address& addr,
                     size_t code_offset,
                     uint8_t* buffer_data,
                     size_t buffer_size) const noexcept override;

    bool selfdestruct(const evmc::address& addr,
                      const evmc::address& beneficiary) noexcept override;

    evmc::Result call(const evmc_message& msg) noexcept override;

    evmc_tx_context get_tx_context() const noexcept override;

    evmc::bytes32 get_block_hash(int64_t block_number) const noexcept override;

    void emit_log(const evmc::address& addr,
                  const uint8_t* data,
                  size_t data_size,
                  const evmc::bytes32 topics[],
                  size_t topics_count) noexcept override;

    evmc_access_status access_account(const evmc::address& addr) noexcept override;

    evmc_access_status access_storage(const evmc::address& addr,
                                      const evmc::bytes32& key) noexcept override;

    evmc::bytes32 get_transient_storage(const evmc::address& addr,
                                        const evmc::bytes32& key) const noexcept override;

    void set_transient_storage(const evmc::address& addr,
                               const evmc::bytes32& key,
                               const evmc::bytes32& value) noexcept override;

    // --- Inspection helpers (for the caller, post-execution) ---------

    /** Single log emitted by the executing contract. */
    struct Log
    {
        evmc::address address;
        std::vector<evmc::bytes32> topics;
        std::vector<uint8_t> data;
    };

    /** Logs accumulated during execution. The caller drains these to
     *  build the receipt for the surrounding transaction. */
    const std::vector<Log>& Logs() const { return logs; }

    /** Addresses that issued SELFDESTRUCT in this execution. The caller
     *  processes the actual balance transfer + account deletion AFTER
     *  the EVM frame returns. Phase 2.3 will track beneficiaries too. */
    const std::set<evmc::address>& Selfdestructs() const { return selfdestructed; }

private:
    CEvmStateCache& state;
    ExecutionContext context;
    BlockHashFn blockHashFn;

    // EIP-1153 transient storage. Lives only within this execution
    // (cleared when the host is destroyed; the caller constructs a fresh
    // host per outer transaction, satisfying the EIP-1153 cross-tx
    // isolation requirement).
    std::map<std::pair<evmc::address, evmc::bytes32>, evmc::bytes32> transient;

    // EIP-2929 access lists. Tracked per host instance.
    mutable std::set<evmc::address> warmAddresses;
    mutable std::set<std::pair<evmc::address, evmc::bytes32>> warmSlots;

    // SELFDESTRUCT bookkeeping.
    std::set<evmc::address> selfdestructed;

    // Captured logs (LOG0..LOG4 opcodes).
    std::vector<Log> logs;

    // --- helpers -----------------------------------------------------
    static uint160 ToUint160(const evmc::address& a);
    static evmc::address FromUint160(const uint160& a);
    static uint256 ToUint256(const evmc::bytes32& b);
    static evmc::bytes32 FromUint256(const uint256& u);

    // Nested CREATE / CREATE2 dispatcher. Split out of call() because
    // CREATE has a different control flow (address derivation, nonce
    // bump, runtime code install) that doesn't compose cleanly with
    // the CALL-family path.
    evmc::Result CallCreate(const evmc_message& msg) noexcept;
};

} // namespace evm

#endif // RAPTOREUM_EVM_HOST_H
