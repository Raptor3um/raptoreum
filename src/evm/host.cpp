// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/host.h>

#include <evm/account.h>
#include <evm/hashing.h>
#include <evm/precompiles.h>
#include <evm/precompiles_eth.h>
#include <evm/state_cache.h>

#include <evmone/evmone.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace evm {

// ----------------------------------------------------------------------
// Local helpers: byte-level arithmetic on big-endian uint256.
//
// Used by the value-transfer step of nested CALL and CREATE frames.
// Same approach as in apply.cpp's spend path — kept local for now;
// if a third caller needs them, promote to a shared utility header.
// ----------------------------------------------------------------------

namespace {

// True if a >= b, where both are 32-byte big-endian uint256s.
bool U256_BE_ge(const evmc::uint256be& a, const evmc::uint256be& b)
{
    for (size_t i = 0; i < 32; ++i) {
        if (a.bytes[i] != b.bytes[i]) return a.bytes[i] > b.bytes[i];
    }
    return true; // equal
}

// True if v is zero.
bool U256_BE_isZero(const evmc::uint256be& v)
{
    for (size_t i = 0; i < 32; ++i) {
        if (v.bytes[i] != 0) return false;
    }
    return true;
}

// out = a - b (modular). Caller must ensure a >= b.
void U256_BE_sub(evmc::uint256be& a, const evmc::uint256be& b)
{
    int borrow = 0;
    for (int i = 31; i >= 0; --i) {
        const int av = a.bytes[i];
        const int bv = b.bytes[i] + borrow;
        if (av < bv) {
            a.bytes[i] = static_cast<uint8_t>(av + 256 - bv);
            borrow = 1;
        } else {
            a.bytes[i] = static_cast<uint8_t>(av - bv);
            borrow = 0;
        }
    }
}

// a = a + b (modular wrap on overflow, which we treat as a fatal
// arithmetic condition — total RTM supply is far below 2^256, so a
// real overflow indicates a consensus bug, not a legitimate balance).
// Returns false on overflow so the caller can reject the operation.
bool U256_BE_add(evmc::uint256be& a, const evmc::uint256be& b)
{
    int carry = 0;
    for (int i = 31; i >= 0; --i) {
        const int sum = static_cast<int>(a.bytes[i]) +
                        static_cast<int>(b.bytes[i]) + carry;
        a.bytes[i] = static_cast<uint8_t>(sum & 0xFF);
        carry = sum >> 8;
    }
    return carry == 0;
}

// Convert evmc::uint256be -> Bitcoin Core uint256 (byte-for-byte; both
// in big-endian wire layout).
uint256 BeToU256(const evmc::uint256be& b)
{
    uint256 out;
    std::memcpy(out.begin(), b.bytes, 32);
    return out;
}

// And back the other way.
evmc::uint256be U256ToBe(const uint256& u)
{
    evmc::uint256be out{};
    std::memcpy(out.bytes, u.begin(), 32);
    return out;
}

} // anonymous namespace

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
                            const evmc::address& beneficiary) noexcept
{
    // Record the SELFDESTRUCT intent so the caller can erase the
    // account once the surrounding frame returns (we cannot delete
    // mid-execution — evmone may still read the contract's storage
    // before the opcode actually halts the frame).
    //
    // The balance transfer to the beneficiary, on the other hand,
    // happens immediately at the EVM-state level: per Cancun the
    // beneficiary's balance is credited even if the SELFDESTRUCT later
    // gets rolled back by an outer REVERT (which is handled by our
    // savepoint mechanism — the credit is dirty-layer, so reverting
    // the savepoint also discards it).
    const uint160 contractAddr = ToUint160(addr);
    const uint160 beneficiaryAddr = ToUint160(beneficiary);

    evm::CEvmAccount contractAcc;
    if (!state.GetAccount(contractAddr, contractAcc)) {
        // Selfdestruct of a non-existent account is a no-op for
        // balance transfer; still record the intent so the caller can
        // observe it and decide whether to flag the situation.
        auto insertedEmpty = selfdestructed.insert(addr).second;
        return insertedEmpty;
    }

    evmc::uint256be contractBalanceBe = U256ToBe(contractAcc.balance);
    if (!U256_BE_isZero(contractBalanceBe) && contractAddr != beneficiaryAddr)
    {
        evm::CEvmAccount beneficiaryAcc;
        const bool beneficiaryExists =
            state.GetAccount(beneficiaryAddr, beneficiaryAcc);
        if (!beneficiaryExists) {
            beneficiaryAcc = evm::CEvmAccount(
                /*nonce=*/ 0,
                /*balance=*/ uint256(),
                /*codeHash=*/ evm::CEvmAccount::EmptyCodeHash(),
                /*storageRoot=*/ evm::CEvmAccount::EmptyStorageRoot());
        }
        evmc::uint256be beneficiaryBalanceBe = U256ToBe(beneficiaryAcc.balance);
        if (!U256_BE_add(beneficiaryBalanceBe, contractBalanceBe)) {
            // Overflow — refuse the operation rather than corrupt
            // total supply. Returning false here tells evmone the
            // selfdestruct did not register; evmone treats this as
            // a duplicate-flag scenario (no extra gas refund), but
            // the on-chain effect is no balance transfer either way.
            return false;
        }
        beneficiaryAcc.balance = BeToU256(beneficiaryBalanceBe);
        state.SetAccount(beneficiaryAddr, beneficiaryAcc);

        // Zero out the contract's balance immediately. The account
        // record is kept until after execution (so subsequent reads
        // from the same frame see codeHash etc.); the actual erasure
        // happens at the caller level once the frame returns.
        contractAcc.balance = uint256();
        state.SetAccount(contractAddr, contractAcc);
    }

    // EIP-6780 (Cancun): only record the address for deletion if it
    // was created within the current transaction. Otherwise the
    // SELFDESTRUCT opcode acts as a balance transfer only; the
    // account record itself is left intact. We still report the
    // address via Selfdestructs() so callers can observe the event,
    // but the caller-side erasure path must filter by
    // SameTxCreated() to determine which ones actually delete.
    auto inserted = selfdestructed.insert(addr).second;
    return inserted;
}

// ----------------------------------------------------------------------
// Nested CALL / DELEGATECALL / CALLCODE / STATICCALL
// ----------------------------------------------------------------------
//
// All four kinds share the same control flow:
//   1. Open a savepoint.
//   2. For non-DELEGATECALL/non-STATICCALL with msg.value > 0: debit
//      sender, credit recipient (account-to-account RTM transfer).
//   3. Locate the code to execute (msg.code_address for DELEGATE/
//      CALLCODE; msg.recipient for CALL/STATICCALL).
//   4. Dispatch through evmone with the appropriate flags.
//   5. On EVMC_SUCCESS: Commit savepoint.
//      On EVMC_REVERT or any failure: Revert savepoint.
//
// evmone tracks msg.depth itself and refuses to recurse past 1024,
// so we don't pre-check depth here.

evmc::Result CEvmHost::call(const evmc_message& msg) noexcept
{
    // Branch CREATE/CREATE2 out to its own helper — it has different
    // pre-conditions (address derivation, code installation) and reads
    // less cleanly when interleaved with the CALL family.
    if (msg.kind == EVMC_CREATE || msg.kind == EVMC_CREATE2) {
        // Defer to the create handler defined below.
        return CallCreate(msg);
    }

    // Phase 4 — RTM-native precompile dispatch. Recognised addresses
    // bypass the bytecode-execution path entirely; the precompile
    // does its own ABI decode + state lookup + ABI encode.
    {
        evmc::Result precompileResult;
        if (ExecutePrecompile(*this, msg, precompileResult)) {
            return precompileResult;
        }
    }

    // Standard Ethereum precompiles (0x01..0x04 currently). evmone
    // delegates all CALLs to the host, so we have to recognise these
    // addresses here. If the precompile is unknown we fall through
    // to normal bytecode execution (which for these addresses runs
    // empty code — wrong per spec, but limits the mistake to the
    // unimplemented subset 0x05..0x0a).
    {
        evmc::Result ethResult;
        if (ExecuteEthereumPrecompile(msg, ethResult)) {
            return ethResult;
        }
    }

    const int snap = state.Snapshot();
    // EIP-2929: warm-access tracking participates in the call-frame
    // snapshot. If the inner frame reverts, addresses/slots accessed
    // inside it must un-warm so that re-accessing them in the
    // surviving outer frame costs cold (2600 / 2100) again. We snapshot
    // the warm sets here and restore them in the revert branch below.
    auto warmAddrsSnap = warmAddresses;
    auto warmSlotsSnap = warmSlots;

    // --- 1. Value transfer (CALL and CALLCODE only — DELEGATECALL
    //        inherits the outer frame's value; STATICCALL disallows
    //        any state change). --------------------------------------
    const bool doTransfer =
        (msg.kind == EVMC_CALL || msg.kind == EVMC_CALLCODE) &&
        !U256_BE_isZero(msg.value);

    if (doTransfer) {
        const uint160 fromAddr = ToUint160(msg.sender);
        const uint160 toAddr = ToUint160(msg.recipient);

        evm::CEvmAccount fromAcc;
        if (!state.GetAccount(fromAddr, fromAcc)) {
            state.Revert(snap);
            evmc::Result r;
            r.status_code = EVMC_INSUFFICIENT_BALANCE;
            r.gas_left = 0;
            return r;
        }
        evmc::uint256be fromBal = U256ToBe(fromAcc.balance);
        if (!U256_BE_ge(fromBal, msg.value)) {
            state.Revert(snap);
            evmc::Result r;
            r.status_code = EVMC_INSUFFICIENT_BALANCE;
            r.gas_left = 0;
            return r;
        }
        U256_BE_sub(fromBal, msg.value);
        fromAcc.balance = BeToU256(fromBal);
        state.SetAccount(fromAddr, fromAcc);

        // Recipient may not exist yet — that's legal for CALL
        // (creates an EOA-style record with the transferred balance).
        evm::CEvmAccount toAcc;
        if (!state.GetAccount(toAddr, toAcc)) {
            toAcc = evm::CEvmAccount(
                /*nonce=*/ 0,
                /*balance=*/ uint256(),
                /*codeHash=*/ evm::CEvmAccount::EmptyCodeHash(),
                /*storageRoot=*/ evm::CEvmAccount::EmptyStorageRoot());
        }
        evmc::uint256be toBal = U256ToBe(toAcc.balance);
        if (!U256_BE_add(toBal, msg.value)) {
            // Catastrophic — total balance would overflow uint256.
            // Refuse to corrupt state.
            state.Revert(snap);
            evmc::Result r;
            r.status_code = EVMC_FAILURE;
            r.gas_left = 0;
            return r;
        }
        toAcc.balance = BeToU256(toBal);
        state.SetAccount(toAddr, toAcc);
    }

    // --- 2. Locate the code to execute. ----------------------------
    // CALL / STATICCALL run the recipient's own code.
    // DELEGATECALL / CALLCODE run the target's code with this frame's
    // (= caller's) recipient identity, hence msg.code_address.
    const evmc::address codeAddr =
        (msg.kind == EVMC_DELEGATECALL || msg.kind == EVMC_CALLCODE)
            ? msg.code_address
            : msg.recipient;

    std::vector<uint8_t> code;
    {
        evm::CEvmAccount codeAcc;
        if (state.GetAccount(ToUint160(codeAddr), codeAcc) &&
            codeAcc.codeHash != evm::CEvmAccount::EmptyCodeHash())
        {
            state.GetCode(codeAcc.codeHash, code);
        }
    }

    // --- 3. Run the inner frame. -----------------------------------
    // If the code is empty this is equivalent to a value-transfer-only
    // call (which already succeeded above). evmone returns success
    // with empty output and no gas consumed beyond the entry.
    evmc::VM vm{evmc_create_evmone()};
    evmc::Result r = vm.execute(*this, EVMC_CANCUN, msg,
                                code.empty() ? nullptr : code.data(),
                                code.size());

    // --- 4. Commit or revert based on the inner status. ------------
    if (r.status_code == EVMC_SUCCESS) {
        state.Commit(snap);
    } else {
        // EVMC_REVERT, EVMC_OUT_OF_GAS, EVMC_INVALID_INSTRUCTION,
        // EVMC_STACK_*, EVMC_FAILURE, etc. — all roll back, including
        // the warm-access sets per EIP-2929.
        state.Revert(snap);
        warmAddresses = std::move(warmAddrsSnap);
        warmSlots = std::move(warmSlotsSnap);
    }

    return r;
}

// ----------------------------------------------------------------------
// Nested CREATE / CREATE2
// ----------------------------------------------------------------------
//
// Responsibilities (per the evmc::Host contract):
//   1. Read sender's account (caller-side state). Reject if missing.
//   2. Derive the new contract address:
//        - CREATE:  keccak(rlp([sender, nonce]))[12:]
//        - CREATE2: keccak(0xff || sender || salt ||
//                          keccak(init_code))[12:]
//   3. Increment sender's nonce immediately (per EIP-161, the nonce
//      bump happens whether or not the constructor succeeds).
//   4. Collision check: refuse if the derived address has code or a
//      non-zero nonce.
//   5. Transfer msg.value from sender to the (about to exist) new
//      contract account.
//   6. Run msg.input_data as init code through evmone with the new
//      address as msg.recipient.
//   7. On EVMC_SUCCESS: store the RETURN bytes as the runtime code,
//      install a fresh account record at the new address, commit
//      savepoint. Populate r.create_address.
//   8. On any failure: revert savepoint (nonce bump survives in
//      Ethereum semantics, but our savepoint includes it — matching
//      what other EVM hosts do at this layer is fine; the caller
//      already debited gas to make this irreversible at the fee
//      level, even though state mutation rolls back).
evmc::Result CEvmHost::CallCreate(const evmc_message& msg) noexcept
{
    const int snap = state.Snapshot();
    // Mirror the warm-access snapshot from `call()` — EIP-2929 requires
    // the warm sets to roll back with state on any failure.
    auto warmAddrsSnap = warmAddresses;
    auto warmSlotsSnap = warmSlots;

    const uint160 senderAddr = ToUint160(msg.sender);
    evm::CEvmAccount senderAcc;
    if (!state.GetAccount(senderAddr, senderAcc)) {
        state.Revert(snap);
        evmc::Result r;
        r.status_code = EVMC_FAILURE;
        r.gas_left = 0;
        return r;
    }

    // Derive the new address. For CREATE the nonce we use is the
    // sender's CURRENT nonce (before increment).
    uint160 newAddr;
    if (msg.kind == EVMC_CREATE) {
        newAddr = ContractAddressFromCreate(senderAddr, senderAcc.nonce);
    } else {
        // CREATE2: hash the init code.
        std::vector<uint8_t> initCode(msg.input_data,
                                      msg.input_data + msg.input_size);
        const uint256 initCodeHash = Keccak256(initCode);
        const uint256 salt = ToUint256(msg.create2_salt);
        newAddr = ContractAddressFromCreate2(senderAddr, salt, initCodeHash);
    }

    // EIP-2681: a CREATE/CREATE2 by a sender whose nonce is already
    // at 2^64-1 must fail without bumping (overflowing the nonce
    // would silently restart the address space). The official tests
    // exercise this against contracts pre-staged with nonce =
    // 0xffffffffffffffff.
    if (senderAcc.nonce >= static_cast<uint64_t>(-1)) {
        state.Revert(snap);
        warmAddresses = std::move(warmAddrsSnap);
        warmSlots = std::move(warmSlotsSnap);
        evmc::Result r;
        r.status_code = EVMC_FAILURE;
        r.gas_left = 0;
        return r;
    }
    // Bump sender's nonce now. EIP-161 mandates the increment even
    // on failure (in real Ethereum); a revert here would leave us
    // out of sync with that, but our outer caller is responsible for
    // the cross-tx nonce bookkeeping anyway (Phase 2.4). Within a
    // single nested CREATE frame, the bump must be visible to a
    // subsequent CREATE by the same sender at the same depth, hence
    // we apply it before running the constructor.
    senderAcc.nonce += 1;

    // Collision check on the derived address.
    evm::CEvmAccount existing;
    if (state.GetAccount(newAddr, existing)) {
        const bool hasCode =
            existing.codeHash != evm::CEvmAccount::EmptyCodeHash();
        // EIP-7610 (Cancun): an account with non-empty storage is
        // also a collision target.
        bool hasStorage = false;
        for (const auto& [key, value] : state.DirtyStorage()) {
            if (key.first == newAddr && value != uint256()) {
                hasStorage = true;
                break;
            }
        }
        if (hasCode || existing.nonce > 0 || hasStorage) {
            state.Revert(snap);
            warmAddresses = std::move(warmAddrsSnap);
            warmSlots = std::move(warmSlotsSnap);
            evmc::Result r;
            r.status_code = EVMC_FAILURE;
            r.gas_left = 0;
            return r;
        }
    }

    // Value transfer sender -> new contract account.
    if (!U256_BE_isZero(msg.value)) {
        evmc::uint256be senderBal = U256ToBe(senderAcc.balance);
        if (!U256_BE_ge(senderBal, msg.value)) {
            state.Revert(snap);
            evmc::Result r;
            r.status_code = EVMC_INSUFFICIENT_BALANCE;
            r.gas_left = 0;
            return r;
        }
        U256_BE_sub(senderBal, msg.value);
        senderAcc.balance = BeToU256(senderBal);
    }
    state.SetAccount(senderAddr, senderAcc);

    // Seed the new account with the value (and a nonce of 1 per
    // EIP-161 — this gets overwritten below on success but exists
    // here so the constructor can observe a non-zero existence).
    evm::CEvmAccount newAcc(
        /*nonce=*/ 1,
        /*balance=*/ BeToU256(msg.value),
        /*codeHash=*/ evm::CEvmAccount::EmptyCodeHash(),
        /*storageRoot=*/ evm::CEvmAccount::EmptyStorageRoot());
    state.SetAccount(newAddr, newAcc);

    // Build the init-code execution message. evmone runs the input
    // as the code; the RETURN bytes become the runtime code on
    // success.
    evmc_message initMsg = msg;
    initMsg.kind = EVMC_CREATE;
    std::memcpy(initMsg.recipient.bytes, newAddr.begin(), 20);
    initMsg.code_address = initMsg.recipient;
    initMsg.input_data = nullptr;
    initMsg.input_size = 0;

    evmc::VM vm{evmc_create_evmone()};
    evmc::Result r = vm.execute(*this, EVMC_CANCUN, initMsg,
                                msg.input_size > 0 ? msg.input_data : nullptr,
                                msg.input_size);

    if (r.status_code == EVMC_SUCCESS) {
        // Install the runtime code.
        std::vector<uint8_t> runtime;
        if (r.output_size > 0 && r.output_data != nullptr) {
            runtime.assign(r.output_data, r.output_data + r.output_size);
        }
        const uint256 codeHash = Keccak256(runtime);
        state.SetCode(codeHash, runtime);

        evm::CEvmAccount finalAcc;
        // Reload — the constructor may have written balance/storage,
        // and (per EIP-161) bumped the new contract's nonce if it did
        // its own CREATEs. Reloading preserves those changes. We seed
        // nonce=1 at line 568 before init runs, so any post-init nonce
        // we read here is >= 1; do NOT overwrite — clobbering would
        // lose the bumps from inner CREATEs.
        if (!state.GetAccount(newAddr, finalAcc)) {
            finalAcc = newAcc;
        }
        finalAcc.codeHash = codeHash;
        state.SetAccount(newAddr, finalAcc);

        state.Commit(snap);
        std::memcpy(r.create_address.bytes, newAddr.begin(), 20);

        // EIP-6780: the new contract address is eligible for
        // SELFDESTRUCT-driven deletion within this transaction.
        evmc::address evmcAddr{};
        std::memcpy(evmcAddr.bytes, newAddr.begin(), 20);
        sameTxCreated.insert(evmcAddr);
    } else {
        state.Revert(snap);
        warmAddresses = std::move(warmAddrsSnap);
        warmSlots = std::move(warmSlotsSnap);
    }

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
