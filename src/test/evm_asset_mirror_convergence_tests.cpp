// Copyright (c) 2026 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_raptoreum.h>

#include <assets/assets.h>
#include <evm/apply.h>
#include <evm/asset_ledger.h>
#include <evm/evmtx.h>
#include <evm/host.h>
#include <evm/precompiles.h>
#include <evm/state_cache.h>
#include <evm/state_db.h>
#include <evo/providertx.h>
#include <hash.h>
#include <validation.h>

#include <evmc/evmc.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

/**
 * D4 Smart-Asset mirror (M3) — the T-mirror convergence harness, the
 * acceptance gate for the bidirectional bridge.
 *
 * A randomized sequence of the four operations that move a Smart Asset
 * around the two ledgers is applied through the REAL code paths:
 *
 *   - UTXO transfer  — moves units between holders on the UTXO side only.
 *   - WRAP           — ApplyWrapAssetTx: burns N on the UTXO side, credits
 *                      the EVM ERC-20 ledger.
 *   - EVM transfer   — the asset precompile's transfer(), moving wrapped
 *                      units between holders on the EVM side only.
 *   - UNWRAP         — ApplyUnwrapAssetTx: debits the EVM ledger, mints N
 *                      back on the UTXO side.
 *
 * After EVERY operation the harness asserts the two invariants that define
 * a correct mirror:
 *
 *   (1) cross-side conservation:
 *         sum(UTXO balances) + wrappedSupply == circulatingSupply   (const)
 *   (2) EVM ERC-20 invariant:
 *         sum(balanceOf over all holders) == wrappedSupply
 *
 * and that each holder's precompile balanceOf equals the shared-module
 * ledger read (the precompile and the apply path are the same ledger).
 *
 * A fixed-seed PRNG makes the sequence fully reproducible.
 */

namespace {

evm::ExecutionContext MinimalContext()
{
    evm::ExecutionContext c;
    c.chainId = 7375;
    c.blockHeight = 100;
    c.blockTimestamp = 1700000000;
    c.blockGasLimit = 30'000'000;
    return c;
}

// Deterministic LCG (no wall-clock / no std::random) for reproducibility.
struct Lcg {
    uint64_t s;
    explicit Lcg(uint64_t seed) : s(seed) {}
    uint64_t next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return s >> 17; }
    uint64_t below(uint64_t n) { return next() % n; }
};

evmc::address Holder(int i)
{
    evmc::address a{};
    a.bytes[19] = static_cast<uint8_t>(0x10 + i);  // distinct, non-precompile
    return a;
}

uint160 ToU160(const evmc::address& a)
{
    uint160 o;
    std::memcpy(o.begin(), a.bytes, 20);
    return o;
}

uint256 ToWord160(const evmc::address& a)
{
    uint256 w;
    w.SetNull();
    std::memcpy(w.begin() + 12, a.bytes, 20);
    return w;
}

void SeedAsset(const std::string& assetId, const std::string& name, uint8_t decimals)
{
    CNewAssetTx a;
    a.name = name;
    a.isRoot = true;
    a.updatable = false;
    a.isUnique = false;
    a.maxMintCount = 0;
    a.decimalPoint = decimals;
    a.referenceHash = "";
    a.fee = 0;
    a.type = 0;
    a.issueFrequency = 0;
    a.amount = 1000 * COIN;
    BOOST_REQUIRE(passetsCache->InsertAsset(a, assetId, /*nHeight=*/1));
}

evm::CWrapAssetTx WrapPayload(const std::string& id, const evmc::address& to, uint64_t amt)
{
    evm::CWrapAssetTx p;
    p.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    p.assetId = id;
    p.evmRecipient = ToWord160(to);
    p.amount = amt;
    return p;
}

evm::CUnwrapAssetTx UnwrapPayload(const std::string& id, const evmc::address& from, uint64_t amt)
{
    evm::CUnwrapAssetTx p;
    p.nVersion = evm::EVM_TX_PAYLOAD_VERSION;
    p.assetId = id;
    p.evmSender = ToWord160(from);
    p.amount = amt;
    return p;
}

// EVM-side transfer via the asset precompile over the shared cache.
bool PrecompileTransfer(evm::CEvmHost& host, const std::string& id,
                        const evmc::address& from, const evmc::address& to, uint64_t amt)
{
    const evmc::address addr = evm::AssetErc20Address(id);
    std::vector<uint8_t> input = {0xa9, 0x05, 0x9c, 0xbb};  // transfer(address,uint256)
    std::vector<uint8_t> a(32, 0);
    std::memcpy(a.data() + 12, to.bytes, 20);
    std::vector<uint8_t> v(32, 0);
    for (int i = 0; i < 8; ++i) v[31 - i] = static_cast<uint8_t>((amt >> (8 * i)) & 0xFF);
    input.insert(input.end(), a.begin(), a.end());
    input.insert(input.end(), v.begin(), v.end());

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.gas = 100000;
    msg.recipient = addr;
    msg.code_address = addr;
    msg.sender = from;
    msg.input_data = input.data();
    msg.input_size = input.size();
    evmc::Result out{};
    BOOST_REQUIRE(evm::ExecutePrecompile(host, msg, out));
    return out.status_code == EVMC_SUCCESS;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(evm_asset_mirror_convergence_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(t_mirror_random_convergence)
{
    const std::string assetId = "MIRRORCONV000001";
    SeedAsset(assetId, "CONVERGE", 8);

    evm::CEvmStateDB db(1 << 20, /*fMemory=*/true);
    evm::CEvmStateCache cache(db);
    const evm::ExecutionContext ctx = MinimalContext();
    evm::CEvmHost host(cache, ctx);

    constexpr int kHolders = 4;
    // circulatingSupply is fixed; start it all on the UTXO side, holder 0.
    const int64_t kCirculating = 1'000'000;
    std::array<int64_t, kHolders> utxo{};
    utxo[0] = kCirculating;

    auto utxoTotal = [&]() {
        int64_t t = 0;
        for (int i = 0; i < kHolders; ++i) t += utxo[i];
        return t;
    };
    auto evmSum = [&]() {
        int64_t t = 0;
        for (int i = 0; i < kHolders; ++i)
            t += static_cast<int64_t>(
                evm::AssetLedgerBalanceOf(cache, assetId, ToU160(Holder(i))));
        return t;
    };
    auto wrappedSupply = [&]() {
        return static_cast<int64_t>(evm::AssetLedgerWrappedSupply(cache, assetId));
    };
    auto checkInvariants = [&]() {
        // (1) cross-side conservation.
        BOOST_REQUIRE_EQUAL(utxoTotal() + wrappedSupply(), kCirculating);
        // (2) EVM ERC-20 invariant: sum(balanceOf) == wrappedSupply.
        BOOST_REQUIRE_EQUAL(evmSum(), wrappedSupply());
        // (3) precompile balanceOf == shared-module ledger read, per holder.
        for (int i = 0; i < kHolders; ++i) {
            const evmc::address h = Holder(i);
            evm::CEvmHost rh(cache, ctx);
            const evmc::address addr = evm::AssetErc20Address(assetId);
            std::vector<uint8_t> in = {0x70, 0xa0, 0x82, 0x31};  // balanceOf
            std::vector<uint8_t> arg(32, 0);
            std::memcpy(arg.data() + 12, h.bytes, 20);
            in.insert(in.end(), arg.begin(), arg.end());
            evmc_message m{};
            m.kind = EVMC_CALL; m.gas = 100000; m.recipient = addr;
            m.code_address = addr; m.input_data = in.data(); m.input_size = in.size();
            evmc::Result r{};
            BOOST_REQUIRE(evm::ExecutePrecompile(rh, m, r));
            BOOST_REQUIRE(r.status_code == EVMC_SUCCESS);
            std::vector<uint8_t> o(r.output_data, r.output_data + r.output_size);
            uint64_t pbal = 0;
            BOOST_REQUIRE(evm::AbiReadUint64(o, 0, pbal));
            BOOST_REQUIRE_EQUAL(static_cast<int64_t>(pbal),
                                static_cast<int64_t>(
                                    evm::AssetLedgerBalanceOf(cache, assetId, ToU160(h))));
        }
    };

    checkInvariants();

    Lcg rng(0xC0FFEE123456789ULL);
    int wraps = 0, unwraps = 0, evmXfers = 0, utxoXfers = 0;
    for (int step = 0; step < 2000; ++step) {
        const int op = static_cast<int>(rng.below(4));
        const int aIdx = static_cast<int>(rng.below(kHolders));
        int bIdx = static_cast<int>(rng.below(kHolders));
        if (bIdx == aIdx) bIdx = (bIdx + 1) % kHolders;

        switch (op) {
        case 0: {  // UTXO transfer a -> b
            if (utxo[aIdx] > 0) {
                const int64_t amt = 1 + static_cast<int64_t>(rng.below(utxo[aIdx]));
                utxo[aIdx] -= amt;
                utxo[bIdx] += amt;
                ++utxoXfers;
            }
            break;
        }
        case 1: {  // WRAP a (UTXO -> EVM ledger, same holder a)
            if (utxo[aIdx] > 0) {
                const uint64_t amt = 1 + rng.below(static_cast<uint64_t>(utxo[aIdx]));
                BOOST_REQUIRE(
                    evm::ApplyWrapAssetTx(WrapPayload(assetId, Holder(aIdx), amt), cache, ctx)
                        .statusCode == EVMC_SUCCESS);
                utxo[aIdx] -= static_cast<int64_t>(amt);
                ++wraps;
            }
            break;
        }
        case 2: {  // EVM transfer a -> b (within the EVM ledger)
            const uint64_t bal =
                evm::AssetLedgerBalanceOf(cache, assetId, ToU160(Holder(aIdx)));
            if (bal > 0) {
                const uint64_t amt = 1 + rng.below(bal);
                BOOST_REQUIRE(PrecompileTransfer(host, assetId, Holder(aIdx),
                                                 Holder(bIdx), amt));
                ++evmXfers;
            }
            break;
        }
        case 3: {  // UNWRAP a (EVM ledger -> UTXO, same holder a)
            const uint64_t bal =
                evm::AssetLedgerBalanceOf(cache, assetId, ToU160(Holder(aIdx)));
            if (bal > 0) {
                const uint64_t amt = 1 + rng.below(bal);
                BOOST_REQUIRE(
                    evm::ApplyUnwrapAssetTx(UnwrapPayload(assetId, Holder(aIdx), amt), cache, ctx)
                        .statusCode == EVMC_SUCCESS);
                utxo[aIdx] += static_cast<int64_t>(amt);
                ++unwraps;
            }
            break;
        }
        }

        checkInvariants();
    }

    // The fixed seed must actually exercise every op kind (guards against a
    // future PRNG change silently skipping a branch).
    BOOST_CHECK(wraps > 0);
    BOOST_CHECK(unwraps > 0);
    BOOST_CHECK(evmXfers > 0);
    BOOST_CHECK(utxoXfers > 0);

    // End state still conserves and the EVM ledger never went negative.
    BOOST_CHECK_EQUAL(utxoTotal() + wrappedSupply(), kCirculating);
    BOOST_CHECK(wrappedSupply() >= 0);
}

BOOST_AUTO_TEST_SUITE_END()
