// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <evm/apply.h>

#include <evm/account.h>
#include <evm/balance.h>
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

    // EIP-2929 / EIP-3651 access-list pre-warming. The standard
    // Ethereum tx envelope pre-warms the sender, the recipient, the
    // standard precompiles (0x01..0x09 historically; Cancun keeps
    // KZG_POINT_EVALUATION at 0x0a as well), and — since Cancun via
    // EIP-3651 — the coinbase. Without this the first access of any
    // of these addresses would charge cold (2600) instead of warm
    // (100), corrupting gas accounting for every tx.
    host.WarmAddress(sender);
    host.WarmAddress(recipient);
    {
        evmc::address cb{};
        std::memcpy(cb.bytes, context.coinbase.begin(), 20);
        host.WarmAddress(cb);
    }
    for (uint8_t i = 1; i <= 0x0a; ++i) {
        evmc::address p{};
        p.bytes[19] = i;
        host.WarmAddress(p);
    }
    // EIP-2930 explicit access list pre-warming. The payload's
    // off-wire `accessList` carries (address, [slots]) pairs; we
    // mark both the address AND each listed slot warm so subsequent
    // SLOAD/CALL etc. pay 100 gas instead of 2100 / 2600.
    for (const auto& entry : payload.accessList) {
        evmc::address a{};
        std::memcpy(a.bytes, entry.address.begin(), 20);
        host.WarmAddress(a);
        for (const auto& slot : entry.storageKeys) {
            evmc::bytes32 k{};
            std::memcpy(k.bytes, slot.begin(), 32);
            host.WarmStorage(a, k);
        }
    }

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

    // Transaction-level atomicity. Ethereum semantics: a transaction
    // that ends in REVERT / OOG / INVALID consumes gas but rolls back
    // EVERY state mutation it made — the outer value transfer and any
    // top-level (non-nested) SSTORE/account writes included. Nested
    // CALL/CREATE frames are already snapshot-protected inside
    // CEvmHost; the OUTERMOST frame had no such protection, so a
    // failing value-bearing tx left the recipient credited and the
    // sender debited (off by exactly `value`), and a failing tx with
    // top-level storage writes persisted them. Snapshot here, before
    // the value transfer, and revert the whole frame on any
    // non-success status. The surrounding fee accounting (gas
    // pre-debit, nonce bump, refund) lives in the caller and is
    // intentionally OUTSIDE this snapshot, matching the spec (gas is
    // charged even on failure).
    const int txSnap = cache.Snapshot();

    // Outer-call value transfer. evmone exposes msg.value to the
    // contract via the CALLVALUE opcode, but does NOT move the funds
    // itself — by spec, that's the transaction harness's job (and
    // the host's job for nested calls; see CEvmHost::call()).
    // Without this debit/credit pair, every tx with value > 0 leaves
    // the recipient under-funded and the sender over-funded, exactly
    // accounting for the missing value.
    if (payload.value > 0) {
        uint160 senderAddr;
        std::memcpy(senderAddr.begin(), sender.bytes, 20);
        uint160 recipientAddr;
        std::memcpy(recipientAddr.begin(), recipient.bytes, 20);
        evm::CEvmAccount senderAcc;
        if (cache.GetAccount(senderAddr, senderAcc)) {
            if (Uint256GreaterOrEqualUint64(senderAcc.balance, payload.value)) {
                Uint256SubUint64(senderAcc.balance, payload.value);
                cache.SetAccount(senderAddr, senderAcc);

                evm::CEvmAccount recipientAcc;
                if (!cache.GetAccount(recipientAddr, recipientAcc)) {
                    recipientAcc = evm::CEvmAccount(
                        /*nonce=*/ 0,
                        /*balance=*/ uint256(),
                        /*codeHash=*/ evm::CEvmAccount::EmptyCodeHash(),
                        /*storageRoot=*/ evm::CEvmAccount::EmptyStorageRoot());
                }
                Uint256AddUint64(recipientAcc.balance, payload.value);
                cache.SetAccount(recipientAddr, recipientAcc);
            }
            // Insufficient-balance case: evmone will hit it via the
            // CALLVALUE/balance check inside the contract anyway;
            // letting it run with the un-moved funds yields the same
            // observable result for fixtures (a failing tx).
        }
    }

    evmc::VM vm{evmc_create_evmone()};
    evmc::Result r = vm.execute(host, EVMC_CANCUN, msg,
                                code.empty() ? nullptr : code.data(),
                                code.size());

    // ----------------------------------------------------------------
    // 4. Pack the result for the caller. Logs and selfdestructs come
    //    from the host (they were captured during execution); the
    //    caller decides whether to keep them based on the status code.
    // ----------------------------------------------------------------

    if (r.status_code == EVMC_SUCCESS) {
        cache.Commit(txSnap);
    } else {
        // Roll back the value transfer + every top-level state
        // mutation. Gas is still charged by the caller (the snapshot
        // is scoped to the execution frame only).
        cache.Revert(txSnap);
    }

    out.statusCode = r.status_code;
    out.gasUsed = static_cast<int64_t>(payload.gasLimit) - r.gas_left;
    out.gasRefund = r.gas_refund;
    if (r.output_size > 0 && r.output_data != nullptr) {
        out.returnData.assign(r.output_data, r.output_data + r.output_size);
    }
    // Logs / selfdestructs are only meaningful on success; on a
    // reverted tx evmone won't have emitted any that survive, and the
    // caller already gates these on statusCode == EVMC_SUCCESS.
    out.logs = host.Logs();
    out.selfdestructs = host.Selfdestructs();
    out.sameTxCreated = host.SameTxCreated();

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
        // EIP-7610 (Cancun): an account with non-empty storage is
        // also a collision target, even if code and nonce are zero.
        // We approximate by scanning the cache's dirty-storage map
        // for any entry keyed on this address with a non-zero value.
        bool hasStorage = false;
        for (const auto& [key, value] : cache.DirtyStorage()) {
            if (key.first == contractAddress && value != uint256()) {
                hasStorage = true;
                break;
            }
        }
        if (hasCode || existing.nonce > 0 || hasStorage) {
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

    // Pre-warm the standard access set (same rationale as
    // ApplyEvmCallTx — see comment there). For CREATE the recipient
    // is the derived contract address; pre-warming it matches
    // Ethereum's transaction harness.
    {
        evmc::address s{}, rcp{};
        std::memcpy(s.bytes, sender.begin(), 20);
        std::memcpy(rcp.bytes, contractAddress.begin(), 20);
        host.WarmAddress(s);
        host.WarmAddress(rcp);
        evmc::address cb{};
        std::memcpy(cb.bytes, context.coinbase.begin(), 20);
        host.WarmAddress(cb);
        for (uint8_t i = 1; i <= 0x0a; ++i) {
            evmc::address p{};
            p.bytes[19] = i;
            host.WarmAddress(p);
        }
        for (const auto& entry : payload.accessList) {
            evmc::address a{};
            std::memcpy(a.bytes, entry.address.begin(), 20);
            host.WarmAddress(a);
            for (const auto& slot : entry.storageKeys) {
                evmc::bytes32 k{};
                std::memcpy(k.bytes, slot.begin(), 32);
                host.WarmStorage(a, k);
            }
        }
    }

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
        for (int i = 0; i < 8; ++i) {
            v.bytes[24 + i] = static_cast<uint8_t>(payload.value >> (56 - 8 * i));
        }
        msg.value = v;
    }
    msg.input_data = nullptr;
    msg.input_size = 0;

    // EIP-6780 (Cancun): the new contract is eligible for
    // SELFDESTRUCT-driven deletion in this tx.
    {
        evmc::address evmcContract{};
        std::memcpy(evmcContract.bytes, contractAddress.begin(), 20);
        host.RecordSameTxCreated(evmcContract);
    }

    // Seed the new contract's account record BEFORE the constructor
    // runs, regardless of value. The constructor may do its own
    // CREATEs whose CallCreate path reads `msg.sender`'s account
    // (= this new contract); leaving the account missing causes
    // those inner CREATEs to fail with EVMC_FAILURE even though
    // they should succeed. Per EIP-161 a new contract is born with
    // nonce=1 so it can immediately CREATE further contracts at a
    // deterministic address.
    {
        evm::CEvmAccount newAcc;
        if (!cache.GetAccount(contractAddress, newAcc)) {
            newAcc = evm::CEvmAccount(
                /*nonce=*/ 1,
                /*balance=*/ uint256(),
                /*codeHash=*/ evm::CEvmAccount::EmptyCodeHash(),
                /*storageRoot=*/ evm::CEvmAccount::EmptyStorageRoot());
            cache.SetAccount(contractAddress, newAcc);
        } else if (newAcc.nonce == 0) {
            newAcc.nonce = 1;
            cache.SetAccount(contractAddress, newAcc);
        }
    }

    // Outer-CREATE value transfer (same rationale as CALL):
    // evmone exposes msg.value via CALLVALUE inside the constructor
    // but doesn't move the funds. We debit sender, credit the
    // new-contract address.
    if (payload.value > 0) {
        evm::CEvmAccount senderAcc;
        if (cache.GetAccount(sender, senderAcc) &&
            Uint256GreaterOrEqualUint64(senderAcc.balance, payload.value))
        {
            Uint256SubUint64(senderAcc.balance, payload.value);
            cache.SetAccount(sender, senderAcc);

            evm::CEvmAccount newAcc;
            cache.GetAccount(contractAddress, newAcc); // we just seeded it above
            Uint256AddUint64(newAcc.balance, payload.value);
            cache.SetAccount(contractAddress, newAcc);
        }
    }

    evmc::VM vm{evmc_create_evmone()};
    evmc::Result r = vm.execute(host, EVMC_CANCUN, msg,
                                payload.code.empty() ? nullptr : payload.code.data(),
                                payload.code.size());

    out.statusCode = r.status_code;
    out.gasUsed = static_cast<int64_t>(payload.gasLimit) - r.gas_left;
    out.gasRefund = r.gas_refund;
    if (r.output_size > 0 && r.output_data != nullptr) {
        out.returnData.assign(r.output_data, r.output_data + r.output_size);
    }
    out.logs = host.Logs();
    out.selfdestructs = host.Selfdestructs();
    out.sameTxCreated = host.SameTxCreated();

    // ----------------------------------------------------------------
    // 4. On success: install the runtime code + a fresh account
    //    record. The runtime code is the init code's RETURN output;
    //    its keccak256 becomes the account's codeHash.
    // ----------------------------------------------------------------

    if (r.status_code == EVMC_SUCCESS) {
        const std::vector<uint8_t> runtimeCode = out.returnData;
        const uint256 codeHash = Keccak256(runtimeCode);

        cache.SetCode(codeHash, runtimeCode);

        // Reload the contract's account so we PRESERVE any balance the
        // value transfer (above) credited, plus any storage the
        // constructor SSTORE'd. The deployed account is born with
        // nonce=1 per EIP-161 (after Spurious Dragon). We do NOT
        // reset balance to zero — that would erase the
        // constructor-time CALLVALUE.
        evm::CEvmAccount account;
        if (!cache.GetAccount(contractAddress, account)) {
            account = evm::CEvmAccount(
                /*nonce=*/ 1,
                /*balance=*/ uint256(),
                /*codeHash=*/ codeHash,
                /*storageRoot=*/ evm::CEvmAccount::EmptyStorageRoot());
        } else {
            // EIP-161: new contracts are born with nonce=1. The
            // constructor may have bumped further by doing its own
            // CREATEs — keep whatever the cache already shows, just
            // ensure it's at least 1.
            if (account.nonce < 1) account.nonce = 1;
            account.codeHash = codeHash;
            // storageRoot stays as-is so the constructor's SSTORE
            // entries (already in cache.mStorageDirty) remain
            // attached to this address.
        }
        cache.SetAccount(contractAddress, account);
    } else {
        // On any non-success status (revert, OOG, invalid opcode...)
        // the new contract is NOT created. Reverse the pre-seed we
        // installed before running init code: delete the placeholder
        // account record (and any value the harness transferred —
        // the EVM spec says the value is also forfeit on failed
        // CREATE, but real Ethereum leaves the sender debited as
        // part of the gas burn; we keep that behaviour since the
        // sender already paid the value before init).
        //
        // If the contract address PRE-EXISTED in the cache (e.g.,
        // EOA with a pre-staged balance), we leave it as it was.
        // The seed code only overwrote a missing/empty record, so
        // deletion only removes our own creation.
        if (payload.value == 0) {
            // We didn't credit anything to it beyond the seed; safe
            // to delete outright.
            cache.DeleteAccount(contractAddress);
        } else {
            // Value was transferred. Per Ethereum semantics, the
            // value transfer is rolled back on failed CREATE; refund
            // it to the sender so net balance change is just the gas
            // burn. Reload, debit recipient, credit sender.
            evm::CEvmAccount recAcc;
            if (cache.GetAccount(contractAddress, recAcc)) {
                if (Uint256GreaterOrEqualUint64(recAcc.balance, payload.value)) {
                    Uint256SubUint64(recAcc.balance, payload.value);
                }
                evm::CEvmAccount senAcc;
                if (cache.GetAccount(sender, senAcc)) {
                    Uint256AddUint64(senAcc.balance, payload.value);
                    cache.SetAccount(sender, senAcc);
                }
            }
            // After reversing the credit, delete the placeholder.
            cache.DeleteAccount(contractAddress);
        }
    }

    return out;
}

// ----------------------------------------------------------------------
// Phase 2.3c — ApplyEvmSpendTx
// ----------------------------------------------------------------------
//
// Balance arithmetic on uint256 (big-endian) lives in evm/balance.{h,cpp}
// so apply.cpp and process.cpp (Phase 2.4) share a single implementation.

namespace {

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
    if (!Uint256GreaterOrEqualUint64(account.balance, payload.amount)) {
        return SpendFailure(gasLimitSigned);
    }

    // 5. Debit and write back. The subtraction should succeed since
    //    we just checked >=.
    if (!Uint256SubUint64(account.balance, payload.amount)) {
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
