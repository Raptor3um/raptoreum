// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <evm/account.h>
#include <evm/apply.h>
#include <evm/evmtx.h>
#include <evm/hashing.h>
#include <evm/host.h>
#include <evm/state_cache.h>
#include <evm/state_db.h>

#include <fs.h>
#include <uint256.h>
#include <univalue.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

/**
 * T1 Capa B: drive the official ethereum/tests blockchain-test fixtures
 * through our own EVM pipeline (CEvmStateCache + ApplyEvmCallTx /
 * ApplyEvmDeployTx) and compare the resulting state to the fixture's
 * expected post-state.
 *
 * Capa A (test/evm-official/run_official_state_tests.sh) validated that
 * libevmone agrees with the reference. This suite is what actually
 * validates *our* integration: gas accounting, storage encoding, log
 * capture, CREATE/CREATE2 address derivation, EIP-1559 wrapping, etc.
 *
 * Activation:
 *   The suite is opt-in via the EVM_OFFICIAL_TESTS_PATH environment
 *   variable. Without it the suite logs "skipped" and exits cleanly,
 *   so the default `make check` build doesn't depend on the ~400 MB
 *   test data set being present.
 *
 *   Typical invocation inside the rtm-builder container:
 *       EVM_OFFICIAL_TESTS_PATH=/root/.cache/raptoreum-evm-official/tests-data/BlockchainTests/GeneralStateTests \
 *           ./src/test/test_raptoreum --run_test=evm_official_blockchaintest_tests
 *
 * Scope (v1):
 *   - Fork filter: only "Cancun" fixtures (matches design decision D6).
 *   - Tx kinds: CALL (to != null) and CREATE (to == null) — mapped to
 *     ApplyEvmCallTx and ApplyEvmDeployTx respectively.
 *   - Single-block, single-tx fixtures only. Multi-block / multi-tx
 *     get counted as "skipped" rather than failing.
 *   - Post-state comparison: per-address nonce, balance, code, storage
 *     slots. We do NOT compare the canonical state root yet — that's a
 *     follow-up tied to building a Merkle Patricia Trie of the state.
 *   - Gas accounting is verified only loosely (gasUsed > 0 on success,
 *     0 on the failure paths). Strict gas matching against the
 *     fixture's expected gasUsed lands when Phase 2.4 fee accounting
 *     is in place.
 *
 * Reporting:
 *   The suite walks the directory tree, runs every Cancun fixture it
 *   finds, and prints a summary table at the end:
 *
 *       Cancun fixtures: 1234 pass, 56 skip (reason), 7 fail
 *
 *   The Boost test case fails iff any fixture produced an unexpected
 *   diff. Skipped fixtures (multi-tx, unsupported tx type, etc.) do
 *   not fail the suite — they're acknowledged absences of coverage.
 */

namespace evm_official {

namespace {

// ---------------------------------------------------------------------------
// Hex / numeric helpers
// ---------------------------------------------------------------------------

// Strip a leading "0x" / "0X" if present, returning a view-equivalent string.
std::string StripHexPrefix(const std::string& s)
{
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return s.substr(2);
    return s;
}

// Decode a "0x..." or bare-hex string into raw bytes. Odd-length input is
// treated as if zero-padded on the left (matches the JSON convention of
// trimming leading zeroes from numeric quantities).
std::vector<uint8_t> ParseHexBytes(const std::string& sIn)
{
    std::string s = StripHexPrefix(sIn);
    if (s.size() & 1u) s.insert(s.begin(), '0');
    std::vector<uint8_t> out;
    out.reserve(s.size() / 2);
    for (size_t i = 0; i < s.size(); i += 2) {
        const auto byte = strtoul(s.substr(i, 2).c_str(), nullptr, 16);
        out.push_back(static_cast<uint8_t>(byte));
    }
    return out;
}

// Pack the raw bytes of a parsed hex string into the high-order positions of
// a uint256. ParseU256("0x42") == uint256 with byte[31] = 0x42.
uint256 ParseU256(const std::string& sIn)
{
    auto bytes = ParseHexBytes(sIn);
    if (bytes.size() > 32) bytes.erase(bytes.begin(), bytes.begin() + (bytes.size() - 32));
    uint256 out;
    const size_t offset = 32 - bytes.size();
    for (size_t i = 0; i < bytes.size(); ++i)
        *(out.begin() + offset + i) = bytes[i];
    return out;
}

// "0xabcd...ef" (20 bytes) -> uint160 with the bytes preserved in EVM
// big-endian convention (byte[0] is the high-order byte).
uint160 ParseAddress(const std::string& sIn)
{
    auto bytes = ParseHexBytes(sIn);
    uint160 out;
    if (bytes.size() > 20) bytes.erase(bytes.begin(), bytes.begin() + (bytes.size() - 20));
    const size_t offset = 20 - bytes.size();
    for (size_t i = 0; i < bytes.size(); ++i)
        *(out.begin() + offset + i) = bytes[i];
    return out;
}

// Parse "0x..." as an unsigned integer up to 64 bits. Used for nonces,
// gas limits, etc. Throws on overflow so the suite fails loudly rather
// than silently truncating.
uint64_t ParseU64(const std::string& sIn)
{
    auto bytes = ParseHexBytes(sIn);
    if (bytes.size() > 8) {
        // Allow leading zero padding but reject genuine > 2^64-1.
        for (size_t i = 0; i + 8 < bytes.size(); ++i)
            if (bytes[i] != 0)
                throw std::runtime_error("u64 overflow parsing: " + sIn);
        bytes.erase(bytes.begin(), bytes.begin() + (bytes.size() - 8));
    }
    uint64_t v = 0;
    for (uint8_t b : bytes) v = (v << 8) | b;
    return v;
}

// Lift a uint160 into the high 160 bits of a uint256 (matches the
// CEvmCallTx::toAddress / senderHash convention: address bytes live at
// positions 12..31 of the uint256, low-order zero in positions 0..11).
uint256 AddressToU256(const uint160& a)
{
    uint256 out;
    std::memcpy(out.begin() + 12, a.begin(), 20);
    return out;
}

uint160 U256ToAddress(const uint256& u)
{
    uint160 out;
    std::memcpy(out.begin(), u.begin() + 12, 20);
    return out;
}

// ---------------------------------------------------------------------------
// Fixture result tracking
// ---------------------------------------------------------------------------

enum class Outcome
{
    PASS,
    FAIL,
    SKIP,
};

struct FixtureResult
{
    std::string fixtureName;
    std::string filePath;
    Outcome outcome{Outcome::SKIP};
    std::string reason;   // human-readable: pass message, skip reason, or fail detail
};

struct SummaryStats
{
    size_t pass{0};
    size_t fail{0};
    size_t skip{0};
    std::map<std::string, size_t> skipsByReason;
    // Bucket failures by the leading diagnostic word so we can see the
    // dominant failure category at a glance (balance/storage/nonce/...).
    std::map<std::string, size_t> failsByCategory;
    std::vector<FixtureResult> failures;

    void absorb(const FixtureResult& r)
    {
        switch (r.outcome) {
            case Outcome::PASS: ++pass; break;
            case Outcome::FAIL: {
                ++fail;
                failures.push_back(r);
                // Strip the leading "post-state mismatch ..." prefix
                // and key the bucket by the underlying diff kind.
                std::string cat = r.reason;
                auto colon = cat.find(": ");
                if (colon != std::string::npos) cat = cat.substr(colon + 2);
                colon = cat.find(' ');
                if (colon != std::string::npos) cat = cat.substr(0, colon);
                ++failsByCategory[cat];
                break;
            }
            case Outcome::SKIP:
                ++skip;
                ++skipsByReason[r.reason];
                break;
        }
    }
};

// ---------------------------------------------------------------------------
// Fixture parsing
// ---------------------------------------------------------------------------

// Populate the cache with the addresses listed in the fixture's "pre"
// object. Returns false on any parse/setup error.
bool SetupPreState(const UniValue& pre, evm::CEvmStateCache& cache, std::string& err)
{
    if (!pre.isObject()) {
        err = "pre is not an object";
        return false;
    }
    std::map<std::string, UniValue> kv;
    pre.getObjMap(kv);

    for (const auto& [addrStr, accountObj] : kv) {
        uint160 addr;
        try { addr = ParseAddress(addrStr); }
        catch (...) { err = "bad address in pre: " + addrStr; return false; }

        // Required fields per the test format.
        const auto& nonceField   = accountObj["nonce"];
        const auto& balanceField = accountObj["balance"];
        const auto& codeField    = accountObj["code"];
        const auto& storageField = accountObj["storage"];

        evm::CEvmAccount account;
        try {
            if (!nonceField.isNull()) account.nonce = ParseU64(nonceField.getValStr());
            if (!balanceField.isNull()) account.balance = ParseU256(balanceField.getValStr());
        } catch (const std::exception& e) {
            err = std::string("bad numeric field in pre[") + addrStr + "]: " + e.what();
            return false;
        }

        // Code: empty string / "0x" -> EOA; else deploy the bytes and
        // record the keccak256 as codeHash.
        std::vector<uint8_t> code;
        if (codeField.isStr()) code = ParseHexBytes(codeField.getValStr());
        if (code.empty()) {
            account.codeHash = evm::CEvmAccount::EmptyCodeHash();
        } else {
            account.codeHash = evm::Keccak256(code);
            cache.SetCode(account.codeHash, code);
        }
        account.storageRoot = evm::CEvmAccount::EmptyStorageRoot();

        cache.SetAccount(addr, account);

        // Storage: { "<slot>" : "<value>", ... }
        if (storageField.isObject()) {
            std::map<std::string, UniValue> storage;
            storageField.getObjMap(storage);
            for (const auto& [slotStr, valStr] : storage) {
                uint256 slot, val;
                try {
                    slot = ParseU256(slotStr);
                    val = ParseU256(valStr.getValStr());
                } catch (...) { err = "bad storage entry"; return false; }
                cache.SetStorage(addr, slot, val);
            }
        }
    }

    return true;
}


// Compare the cache's post-execution state to the fixture's expected
// "postState" object. Returns true if everything matches; on mismatch
// returns false with the first diff in `diff`.
bool ComparePostState(const UniValue& expectedPost,
                      evm::CEvmStateCache& cache,
                      std::string& diff)
{
    if (!expectedPost.isObject()) {
        diff = "postState not an object";
        return false;
    }
    std::map<std::string, UniValue> kv;
    expectedPost.getObjMap(kv);

    for (const auto& [addrStr, expected] : kv) {
        uint160 addr;
        try { addr = ParseAddress(addrStr); }
        catch (...) { diff = "postState contains bad address " + addrStr; return false; }

        evm::CEvmAccount actual;
        const bool exists = cache.GetAccount(addr, actual);

        const auto& expBalance = expected["balance"];
        const auto& expNonce   = expected["nonce"];
        const auto& expCode    = expected["code"];
        const auto& expStorage = expected["storage"];

        // Account existence and basic fields.
        const uint256 wantBalance = expBalance.isStr() ? ParseU256(expBalance.getValStr())
                                                       : uint256();
        const uint64_t wantNonce  = expNonce.isStr()   ? ParseU64(expNonce.getValStr())
                                                       : 0;

        if (!exists && (wantBalance != uint256() || wantNonce != 0)) {
            diff = "address " + addrStr + " expected to exist with balance/nonce, missing";
            return false;
        }
        if (exists) {
            if (actual.balance != wantBalance) {
                std::ostringstream os;
                os << "balance mismatch at " << addrStr
                   << ": expected " << wantBalance.GetHex()
                   << ", got " << actual.balance.GetHex();
                diff = os.str();
                return false;
            }
            if (actual.nonce != wantNonce) {
                std::ostringstream os;
                os << "nonce mismatch at " << addrStr
                   << ": expected " << wantNonce
                   << ", got " << actual.nonce;
                diff = os.str();
                return false;
            }
        }

        // Code: compare bytes (if any) by recomputing keccak.
        if (expCode.isStr()) {
            auto wantCode = ParseHexBytes(expCode.getValStr());
            uint256 wantCodeHash = wantCode.empty()
                                       ? evm::CEvmAccount::EmptyCodeHash()
                                       : evm::Keccak256(wantCode);
            if (exists && actual.codeHash != wantCodeHash) {
                std::ostringstream os;
                os << "code mismatch at " << addrStr
                   << ": expected codeHash " << wantCodeHash.GetHex()
                   << ", got " << actual.codeHash.GetHex();
                diff = os.str();
                return false;
            }
        }

        // Storage: every expected slot must match the cache.
        if (expStorage.isObject()) {
            std::map<std::string, UniValue> sm;
            expStorage.getObjMap(sm);
            for (const auto& [slotStr, valEntry] : sm) {
                uint256 slot = ParseU256(slotStr);
                uint256 wantVal = ParseU256(valEntry.getValStr());
                uint256 gotVal;
                const bool present = cache.GetStorage(addr, slot, gotVal);
                if (!present) gotVal = uint256();
                if (gotVal != wantVal) {
                    std::ostringstream os;
                    os << "storage[" << addrStr << "][" << slot.GetHex()
                       << "] mismatch: expected " << wantVal.GetHex()
                       << ", got " << gotVal.GetHex();
                    diff = os.str();
                    return false;
                }
            }
        }
    }

    return true;
}

// EIP-4788 beacon-roots predeploy: the Cancun spec auto-invokes a system
// call at the top of every block that writes `parentBeaconBlockRoot` into
// the beacon-roots contract at 0x000F...Beac02. The official test
// fixtures assume this has already happened by the time the user's
// transaction runs, so any fixture's "postState" reflects it.
//
// Our production pipeline does not yet drive this system call (it
// belongs to a future Phase 2.5 block-preamble step). For Capa B we
// simulate it here so the comparator doesn't trip over storage diffs
// at the beacon-roots address.
const uint160 kBeaconRootsAddress = []() {
    return ParseAddress("0x000F3df6D732807Ef1319fB7B8bB8522d0Beac02");
}();
const uint160 kSystemAddress = []() {
    return ParseAddress("0xfffffffffffffffffffffffffffffffffffffffe");
}();

void DriveBeaconRootsSystemCall(const UniValue& blockHeader,
                                evm::CEvmStateCache& cache,
                                const evm::ExecutionContext& outerCtx)
{
    const auto& root = blockHeader["parentBeaconBlockRoot"];
    if (!root.isStr()) return; // not a Cancun-format header
    // The system call is a noop if the contract has no code (e.g.,
    // pre-Cancun fixtures that happen to include the field).
    evm::CEvmAccount acc;
    if (!cache.GetAccount(kBeaconRootsAddress, acc)) return;
    if (acc.codeHash == evm::CEvmAccount::EmptyCodeHash()) return;

    evm::CEvmCallTx sysTx;
    sysTx.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    sysTx.toAddress = AddressToU256(kBeaconRootsAddress);
    sysTx.value = 0;
    sysTx.data = ParseHexBytes(root.getValStr());
    // Spec: 30M gas limit on the system call.
    sysTx.gasLimit = 30'000'000;
    sysTx.maxFeePerGas = 0;
    sysTx.maxPriorityFeePerGas = 0;
    sysTx.senderHash = AddressToU256(kSystemAddress);
    sysTx.nonce = 0;

    evm::ExecutionContext sysCtx = outerCtx;
    sysCtx.txOrigin = kSystemAddress;
    (void)evm::ApplyEvmCallTx(sysTx, cache, sysCtx);
    // Result intentionally ignored: per spec the system call's failure
    // is silently absorbed — it must never block block processing.
}

// ---------------------------------------------------------------------------
// Per-fixture execution
// ---------------------------------------------------------------------------

FixtureResult RunOneFixture(const std::string& filePath,
                            const std::string& name,
                            const UniValue& fixture)
{
    FixtureResult r;
    r.fixtureName = name;
    r.filePath = filePath;

    // Filter by fork.
    const auto& network = fixture["network"];
    if (!network.isStr() || network.getValStr() != "Cancun") {
        r.outcome = Outcome::SKIP;
        r.reason = "fork != Cancun";
        return r;
    }

    // Some fixtures only ship a "postStateHash" (the MPT root) without
    // an expanded "postState" — typically because the full state is too
    // big to inline. Capa B v1 does not compute the Merkle-Patricia-
    // Trie root, so we can't verify those; skip them.
    if (!fixture["postState"].isObject()) {
        r.outcome = Outcome::SKIP;
        r.reason = "fixture has postStateHash only (no expanded postState)";
        return r;
    }

    const auto& blocks = fixture["blocks"];
    if (!blocks.isArray() || blocks.empty()) {
        r.outcome = Outcome::SKIP;
        r.reason = "no blocks in fixture";
        return r;
    }

    // Set up the cache from "pre" once for the whole fixture; the
    // cache persists across all blocks and transactions.
    evm::CEvmStateDB db(1 << 20, /*fMemory=*/ true);
    evm::CEvmStateCache cache(db);
    std::string err;
    if (!SetupPreState(fixture["pre"], cache, err)) {
        r.outcome = Outcome::FAIL;
        r.reason = "pre-state setup failed: " + err;
        return r;
    }

    // Our uint256 is big-endian — byte[0] is MSB, byte[31] is LSB —
    // because CEvmHost::get_balance memcpys it straight into an
    // evmc::uint256be (see host.cpp). So arithmetic propagates carry
    // from byte[31] downwards.
    auto u256AddU64 = [](const uint256& v, uint64_t delta) -> uint256 {
        uint256 out = v;
        uint64_t carry = delta;
        for (int i = 31; i >= 0 && carry; --i) {
            uint16_t sum = static_cast<uint8_t>(*(out.begin() + i)) + (carry & 0xff);
            *(out.begin() + i) = static_cast<uint8_t>(sum);
            carry = (carry >> 8) + (sum >> 8);
        }
        return out;
    };
    auto u256SubU64 = [](const uint256& v, uint64_t delta) -> uint256 {
        uint256 out = v;
        int64_t borrow = static_cast<int64_t>(delta);
        for (int i = 31; i >= 0 && borrow; --i) {
            int32_t diff = static_cast<uint8_t>(*(out.begin() + i)) - (borrow & 0xff);
            if (diff < 0) {
                *(out.begin() + i) = static_cast<uint8_t>(diff + 0x100);
                borrow = (borrow >> 8) + 1;
            } else {
                *(out.begin() + i) = static_cast<uint8_t>(diff);
                borrow >>= 8;
            }
        }
        return out;
    };

    // Loop over blocks. The cache persists across them so multi-block
    // fixtures get applied serially.
    for (size_t blockIdx = 0; blockIdx < blocks.size(); ++blockIdx) {
        const auto& block = blocks[blockIdx];
        const auto& header = block["blockHeader"];
        if (!header.isObject()) {
            r.outcome = Outcome::SKIP;
            r.reason = "block has no blockHeader (likely an InvalidBlocks fixture)";
            return r;
        }

        // Build the per-block ExecutionContext.
        evm::ExecutionContext blockCtx;
        blockCtx.chainId = 1;
        if (header["number"].isStr())    blockCtx.blockHeight    = ParseU64(header["number"].getValStr());
        if (header["timestamp"].isStr()) blockCtx.blockTimestamp = static_cast<int64_t>(ParseU64(header["timestamp"].getValStr()));
        if (header["gasLimit"].isStr())  blockCtx.blockGasLimit  = ParseU64(header["gasLimit"].getValStr());
        if (header["baseFeePerGas"].isStr())
            blockCtx.baseFee = ParseU256(header["baseFeePerGas"].getValStr());
        if (header["coinbase"].isStr())  blockCtx.coinbase = ParseAddress(header["coinbase"].getValStr());
        // Post-Merge, `mixHash` carries the Beacon Chain's prevRandao
        // value, which the EVM exposes via the PREVRANDAO opcode (the
        // renamed DIFFICULTY). Our ExecutionContext reuses prevBlockHash
        // for this slot (host.cpp memcpys it into tx.block_prev_randao);
        // populate it from the JSON's mixHash field for correct behavior
        // of fixtures that branch on PREVRANDAO.
        if (header["mixHash"].isStr())
            blockCtx.prevBlockHash = ParseU256(header["mixHash"].getValStr());

        // EIP-4788 beacon-roots system pre-call: once per block, before
        // any user transactions. Reuse the block context.
        DriveBeaconRootsSystemCall(header, cache, blockCtx);

        const auto& txs = block["transactions"];
        if (!txs.isArray()) {
            r.outcome = Outcome::SKIP;
            r.reason = "block transactions field missing/non-array";
            return r;
        }

        for (size_t txIdx = 0; txIdx < txs.size(); ++txIdx) {
            const auto& txObj = txs[txIdx];

            // CREATE (to == "") vs CALL.
            const auto& toField = txObj["to"];
            const bool isCreate = !toField.isStr() || toField.getValStr().empty()
                                  || ParseHexBytes(toField.getValStr()).empty();
    bool isEip1559 = false;
    if (txObj["type"].isStr()) {
        try { isEip1559 = ParseU64(txObj["type"].getValStr()) == 2; }
        catch (...) {}
    } else if (txObj["maxFeePerGas"].isStr()) {
        isEip1559 = true;
    }

    uint64_t txValue = 0;
    std::vector<uint8_t> txData;
    uint64_t parsedGasLimit = 0;
    uint64_t maxFeePerGas = 0, maxPriorityFeePerGas = 0;
    uint64_t txNonce = 0;
    uint256 senderHash;
    uint256 toAddressU256;
    std::vector<evm::AccessListEntry> accessList;

    try {
        txValue = txObj["value"].isStr() ? ParseU64(txObj["value"].getValStr()) : 0;
        txData = txObj["data"].isStr() ? ParseHexBytes(txObj["data"].getValStr())
                                       : std::vector<uint8_t>{};
        parsedGasLimit = txObj["gasLimit"].isStr() ? ParseU64(txObj["gasLimit"].getValStr()) : 0;
        if (txObj["maxFeePerGas"].isStr())
            maxFeePerGas = ParseU64(txObj["maxFeePerGas"].getValStr());
        else if (txObj["gasPrice"].isStr())
            maxFeePerGas = ParseU64(txObj["gasPrice"].getValStr());
        if (txObj["maxPriorityFeePerGas"].isStr())
            maxPriorityFeePerGas = ParseU64(txObj["maxPriorityFeePerGas"].getValStr());
        txNonce = txObj["nonce"].isStr() ? ParseU64(txObj["nonce"].getValStr()) : 0;

        // Sender: fixtures supply an explicit "sender" field (the
        // recovered EOA). We don't do (v,r,s) recovery here; skip if
        // absent rather than fabricating.
        const auto& sender = txObj["sender"];
        if (!sender.isStr()) {
            r.outcome = Outcome::SKIP;
            r.reason = "no explicit sender in tx (signature recovery not implemented in v1)";
            return r;
        }
        senderHash = AddressToU256(ParseAddress(sender.getValStr()));
        if (!isCreate) {
            toAddressU256 = AddressToU256(ParseAddress(toField.getValStr()));
        }

        // EIP-2930 access list (type-1 and type-2 envelopes). Parse
        // into the off-wire payload field so ApplyEvmCallTx /
        // ApplyEvmDeployTx can pre-warm at execution time.
        const auto& al = txObj["accessList"];
        if (al.isArray()) {
            for (size_t i = 0; i < al.size(); ++i) {
                const auto& e = al[i];
                if (!e.isObject()) continue;
                evm::AccessListEntry entry;
                if (e["address"].isStr())
                    entry.address = ParseAddress(e["address"].getValStr());
                const auto& keys = e["storageKeys"];
                if (keys.isArray()) {
                    for (size_t k = 0; k < keys.size(); ++k) {
                        if (keys[k].isStr())
                            entry.storageKeys.push_back(ParseU256(keys[k].getValStr()));
                    }
                }
                accessList.push_back(std::move(entry));
            }
        }
    } catch (const std::exception& e) {
        r.outcome = Outcome::FAIL;
        r.reason = std::string("tx parsing failed: ") + e.what();
        return r;
    }

    // Use a per-tx context built from the per-block context. We must
    // also set `txGasPrice` correctly because the EVM exposes it via
    // the GASPRICE opcode — contracts may branch on it, so leaving it
    // zero would change control flow in real fixtures. The exact
    // value Ethereum surfaces is the *effective* gas price (post
    // EIP-1559 reconciliation), which is computed below. Stash it
    // here as a placeholder; we recompute and overwrite once
    // effectiveGasPrice is known.
    evm::ExecutionContext ctx = blockCtx;
    ctx.txOrigin = U256ToAddress(senderHash);
    ctx.txGasPrice = uint256();

    // Capa B simulation of Phase 2.4 fee accounting.
    // Our production `ApplyEvmTx` deliberately does not debit gas, pay
    // the coinbase, or increment the sender nonce — those live in the
    // surrounding ConnectBlock layer (Phase 2.4). The official fixtures
    // assume an Ethereum-style "transaction harness" that applies them,
    // so we replicate that harness here:
    //   1. Pre-debit the sender by gasLimit * effectiveGasPrice and
    //      bump nonce.
    //   2. Run the EVM call.
    //   3. Refund unused gas to the sender at effectiveGasPrice.
    //   4. Credit the coinbase with the priority-fee portion of the
    //      consumed gas (gasUsed * (effectiveGasPrice - baseFee)).
    //   5. Burn the baseFee portion (no-op in our model — it just
    //      stays debited from the sender and is not credited anywhere).

    const uint160 senderAddr = U256ToAddress(senderHash);
    const uint64_t baseFeeU64 = [&]() -> uint64_t {
        // baseFee is small enough in fixtures to fit in u64. Read the
        // low 8 bytes of the big-endian uint256 (byte[24..31]).
        const auto& b = ctx.baseFee;
        uint64_t v = 0;
        for (int i = 24; i < 32; ++i) v = (v << 8) | static_cast<uint8_t>(*(b.begin() + i));
        return v;
    }();
    // Effective gas price:
    //   - Legacy (type 0/1): just gasPrice (which we stashed in
    //     maxFeePerGas). The whole price acts as the fee cap AND the
    //     tip; baseFee is burned from it, anything above goes to the
    //     coinbase.
    //   - EIP-1559 (type 2): min(maxFeePerGas, baseFee +
    //     maxPriorityFeePerGas).
    uint64_t effectiveGasPrice;
    if (isEip1559) {
        const uint64_t basePlusTip = baseFeeU64 + maxPriorityFeePerGas;
        effectiveGasPrice = (maxFeePerGas < basePlusTip)
                                ? maxFeePerGas
                                : basePlusTip;
    } else {
        effectiveGasPrice = maxFeePerGas;
    }
    const uint64_t priorityPerGas =
        effectiveGasPrice > baseFeeU64 ? effectiveGasPrice - baseFeeU64 : 0;

    // Surface effectiveGasPrice to the EVM via GASPRICE opcode.
    // Our uint256 is big-endian byte[0]=MSB; the low 8 bytes (24..31)
    // carry the value.
    {
        uint256 gpU256;
        for (int i = 0; i < 8; ++i) {
            *(gpU256.begin() + 31 - i) =
                static_cast<uint8_t>((effectiveGasPrice >> (8 * i)) & 0xFF);
        }
        ctx.txGasPrice = gpU256;
    }

    // EIP-2 intrinsic gas:
    //   - CALL: 21000 base + 4 per zero byte of calldata + 16 per non-zero.
    //   - CREATE: 53000 base + same per-byte cost on init code + EIP-3860
    //     init-code metering of 2 gas per 32-byte word (Cancun).
    //   - EIP-2930 access list adds 2400 per address + 1900 per slot.
    uint64_t intrinsicGas = isCreate ? 53000 : 21000;
    for (uint8_t b : txData) intrinsicGas += (b == 0) ? 4 : 16;
    if (isCreate) {
        const uint64_t words = (txData.size() + 31) / 32;
        intrinsicGas += 2 * words;
    }
    for (const auto& entry : accessList) {
        intrinsicGas += 2400;
        intrinsicGas += 1900 * entry.storageKeys.size();
    }

    const uint64_t txGasLimit = parsedGasLimit;
    if (txGasLimit < intrinsicGas) {
        r.outcome = Outcome::SKIP;
        r.reason = "tx gasLimit below intrinsic (out-of-gas-before-execution)";
        return r;
    }
    const uint64_t evmGas = txGasLimit - intrinsicGas;

    evm::CEvmAccount senderAcc;
    if (cache.GetAccount(senderAddr, senderAcc)) {
        senderAcc.balance = u256SubU64(senderAcc.balance, txGasLimit * effectiveGasPrice);
        senderAcc.nonce += 1;
        cache.SetAccount(senderAddr, senderAcc);
    }

    // Build the payload (CALL or CREATE) and execute via the right
    // pipeline entry point.
    evm::ApplyResult exec;
    try {
        if (isCreate) {
            evm::CEvmDeployTx deployTx;
            deployTx.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
            deployTx.code = txData;
            deployTx.gasLimit = evmGas;
            deployTx.maxFeePerGas = maxFeePerGas;
            deployTx.maxPriorityFeePerGas = maxPriorityFeePerGas;
            deployTx.senderHash = senderHash;
            // ApplyEvmDeployTx derives the contract address from
            // (sender, nonce). The fixture's expected nonce on the
            // sender is the same one we passed to the deploy
            // (pre-bump): the deploy uses payload.nonce to derive the
            // address, while the harness already bumped senderAcc.nonce
            // above. So we pass the *pre-bump* nonce here.
            deployTx.nonce = txNonce;
            deployTx.accessList = accessList;
            exec = evm::ApplyEvmDeployTx(deployTx, cache, ctx);
        } else {
            evm::CEvmCallTx callTx;
            callTx.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
            callTx.toAddress = toAddressU256;
            callTx.value = txValue;
            callTx.data = txData;
            callTx.gasLimit = evmGas;
            callTx.maxFeePerGas = maxFeePerGas;
            callTx.maxPriorityFeePerGas = maxPriorityFeePerGas;
            callTx.senderHash = senderHash;
            callTx.nonce = txNonce;
            callTx.accessList = accessList;
            exec = evm::ApplyEvmCallTx(callTx, cache, ctx);
        }
    } catch (const std::exception& e) {
        r.outcome = Outcome::FAIL;
        r.reason = std::string("apply threw: ") + e.what();
        return r;
    }

    // Apply EIP-3529 (Cancun) refund cap: refund = min(gasUsed / 5,
    // accumulated_gas_refund). The "gasUsed" used for the cap is the
    // pre-refund total (intrinsic + raw exec gas). evmone reports
    // r.gas_refund accumulated from SSTORE clears (and historically
    // SELFDESTRUCT); we apply it here, since ApplyEvmCallTx's docstring
    // delegates fee accounting to the surrounding caller.
    const uint64_t preRefundUsed =
        static_cast<uint64_t>(exec.gasUsed) + intrinsicGas;
    const uint64_t refundCap = preRefundUsed / 5;
    const uint64_t rawRefund = exec.gasRefund > 0
                                   ? static_cast<uint64_t>(exec.gasRefund)
                                   : 0;
    const uint64_t appliedRefund = rawRefund < refundCap ? rawRefund : refundCap;
    const uint64_t totalGasUsed = preRefundUsed - appliedRefund;

    // Settle gas: refund the unused gas to the sender, credit the
    // priority portion of the used gas to the coinbase. Use the
    // ORIGINAL pre-intrinsic gasLimit since that's what we debited.
    if (cache.GetAccount(senderAddr, senderAcc)) {
        const uint64_t unused = txGasLimit - totalGasUsed;
        senderAcc.balance = u256AddU64(senderAcc.balance, unused * effectiveGasPrice);
        cache.SetAccount(senderAddr, senderAcc);
    }
    if (priorityPerGas > 0) {
        evm::CEvmAccount coinbaseAcc;
        if (!cache.GetAccount(ctx.coinbase, coinbaseAcc)) {
            coinbaseAcc = evm::CEvmAccount{};
            coinbaseAcc.codeHash = evm::CEvmAccount::EmptyCodeHash();
            coinbaseAcc.storageRoot = evm::CEvmAccount::EmptyStorageRoot();
        }
        coinbaseAcc.balance =
            u256AddU64(coinbaseAcc.balance, totalGasUsed * priorityPerGas);
        cache.SetAccount(ctx.coinbase, coinbaseAcc);
    }
        }  // end per-tx loop
    }  // end per-block loop

    // After every block / every tx has been applied, compare the
    // final cache state against the fixture's expected `postState`.
    std::string diff;
    if (!ComparePostState(fixture["postState"], cache, diff)) {
        std::ostringstream os;
        os << "post-state mismatch: " << diff;
        r.outcome = Outcome::FAIL;
        r.reason = os.str();
        return r;
    }

    r.outcome = Outcome::PASS;
    return r;
}

// ---------------------------------------------------------------------------
// Directory walk
// ---------------------------------------------------------------------------

void WalkAndRun(const fs::path& root, SummaryStats& stats, size_t maxFiles = 0)
{
    if (!fs::is_directory(root)) {
        BOOST_TEST_MESSAGE("EVM official tests path is not a directory: " << root.string());
        return;
    }
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
        const fs::path& p = it->path();
        if (fs::is_regular_file(p) && p.extension() == ".json")
            files.push_back(p);
    }
    std::sort(files.begin(), files.end());
    if (maxFiles > 0 && files.size() > maxFiles)
        files.resize(maxFiles);

    BOOST_TEST_MESSAGE("Walking " << files.size() << " fixture files under "
                                  << root.string());

    for (const auto& p : files) {
        std::ifstream f(p.string());
        std::stringstream ss;
        ss << f.rdbuf();
        UniValue doc;
        if (!doc.read(ss.str())) {
            FixtureResult r;
            r.filePath = p.string();
            r.outcome = Outcome::FAIL;
            r.reason = "JSON parse failed";
            stats.absorb(r);
            continue;
        }
        if (!doc.isObject()) {
            FixtureResult r;
            r.filePath = p.string();
            r.outcome = Outcome::FAIL;
            r.reason = "top-level not an object";
            stats.absorb(r);
            continue;
        }
        std::map<std::string, UniValue> caseMap;
        doc.getObjMap(caseMap);
        for (const auto& [name, body] : caseMap) {
            stats.absorb(RunOneFixture(p.string(), name, body));
        }
    }
}

} // anonymous namespace

} // namespace evm_official

// ---------------------------------------------------------------------------
// Boost suite
// ---------------------------------------------------------------------------

BOOST_FIXTURE_TEST_SUITE(evm_official_blockchaintest_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(run_general_state_tests_cancun)
{
    using namespace evm_official;

    const char* envPath = std::getenv("EVM_OFFICIAL_TESTS_PATH");
    if (!envPath || !*envPath) {
        BOOST_TEST_MESSAGE(
            "EVM_OFFICIAL_TESTS_PATH not set — skipping Capa B run. "
            "Run test/evm-official/run_official_state_tests.sh first to fetch "
            "the fixtures, then set EVM_OFFICIAL_TESTS_PATH to the "
            "BlockchainTests/GeneralStateTests directory.");
        return;
    }

    SummaryStats stats;
    const char* limitStr = std::getenv("EVM_OFFICIAL_TESTS_LIMIT");
    const size_t limit = limitStr ? static_cast<size_t>(std::atoi(limitStr)) : 0;
    WalkAndRun(fs::path(envPath), stats, limit);

    BOOST_TEST_MESSAGE("");
    BOOST_TEST_MESSAGE("=== Capa B (official ethereum/tests via our pipeline) ===");
    BOOST_TEST_MESSAGE("Cancun fixtures: " << stats.pass << " pass, "
                                            << stats.skip << " skip, "
                                            << stats.fail << " fail");
    BOOST_TEST_MESSAGE("Skip reasons:");
    for (const auto& [reason, count] : stats.skipsByReason) {
        BOOST_TEST_MESSAGE("  " << count << " × " << reason);
    }
    BOOST_TEST_MESSAGE("Failure categories:");
    for (const auto& [cat, count] : stats.failsByCategory) {
        BOOST_TEST_MESSAGE("  " << count << " × " << cat);
    }
    if (!stats.failures.empty()) {
        BOOST_TEST_MESSAGE("First failures (up to 20):");
        const size_t shown = std::min<size_t>(20, stats.failures.size());
        for (size_t i = 0; i < shown; ++i) {
            const auto& f = stats.failures[i];
            BOOST_TEST_MESSAGE("  - " << f.fixtureName << ": " << f.reason);
        }
    }
    BOOST_TEST_MESSAGE("=========================================================");
    BOOST_TEST_MESSAGE("");

    BOOST_CHECK_EQUAL(stats.fail, 0U);
}

BOOST_AUTO_TEST_SUITE_END()
