// Copyright (c) 2018 The Dash Core developers
// Copyright (c) 2020-2023 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <random.h>
#include <bls/bls_worker.h>

struct Member {
    CBLSId id;

    BLSVerificationVectorPtr vvec;
    BLSSecretKeyVector skShares;
};

class DKG {
private:
    std::vector <CBLSId> ids;

    std::vector <Member> members;
    std::vector <BLSVerificationVectorPtr> receivedVvecs;

    BLSSecretKeyVector receivedSkShares;
    BLSVerificationVectorPtr quorumVvec;
    CBLSWorker blsWorker;

    void ReceiveVvecs() {
        receivedVvecs.clear();
        for (size_t i = 0; i < members.size(); i++) {
            receivedVvecs.emplace_back(members[i].vvec);
        }
        quorumVvec = blsWorker.BuildQuorumVerificationVector(receivedVvecs);
    }

    void ReceiveShares(size_t whoAmI) {
        receivedSkShares.clear();
        for (size_t i = 0; i < members.size(); i++) {
            receivedSkShares.emplace_back(members[i].skShares[whoAmI]);
        }
    }

    void VerifyContributionShares(size_t whoAmI, const std::set <size_t> &invalidIndexes, bool aggregated) {
        auto result = blsWorker.VerifyContributionShares(members[whoAmI].id, receivedVvecs, receivedSkShares,
                                                         aggregated);
        for (size_t i = 0; i < receivedVvecs.size(); i++) {
            if (invalidIndexes.count(i)) {
                assert(!result[i]);
            } else {
                assert(result[i]);
            }
        }
    }

public:
    explicit DKG(int quorumSize) {
        members.reserve(quorumSize);
        ids.reserve(quorumSize);

        for (int i = 0; i < quorumSize; i++) {
            uint256 id;
            WriteLE64(id.begin(), i + 1);
            members.push_back({CBLSId(id), {}, {}});
            ids.emplace_back(id);
        }

        blsWorker.Start();
        for (int i = 0; i < quorumSize; i++) {
            blsWorker.GenerateContributions(quorumSize / 2 + 1, ids, members[i].vvec, members[i].skShares);
        }
    }

    ~DKG() {
        blsWorker.Stop();
    }

    void Bench_BuildQuorumVerificationVectors(benchmark::Bench &bench, uint32_t epoch_iters) {
        ReceiveVvecs();

        bench.minEpochIterations(epoch_iters).run([&] {
            quorumVvec = blsWorker.BuildQuorumVerificationVector(receivedVvecs, 0, 0, false);
        });
    }

    void
    Bench_VerifyContributionShares(benchmark::Bench &bench, int invalidCount, bool aggregated, uint32_t epoch_iters) {
        ReceiveVvecs();
        size_t memberIdx = 0;
        bench.minEpochIterations(epoch_iters).run([&] {
            ReceiveShares(memberIdx);

            std::set <size_t> invalidIndexes;
            for (int i = 0; i < invalidCount; i++) {
                int shareIdx = GetRandInt(receivedSkShares.size());
                receivedSkShares[shareIdx].MakeNewKey();
                invalidIndexes.emplace(shareIdx);
            }

            VerifyContributionShares(memberIdx, invalidIndexes, aggregated);

            memberIdx = (memberIdx + 1) % members.size();
        });
    }
};

static void BLSDKG_GenerateContributions(benchmark::Bench &bench, uint32_t epoch_iters, int quorumSize) {
    CBLSWorker blsWorker;
    blsWorker.Start();
    std::vector <CBLSId> ids;
    std::vector <Member> members;
    for (int i = 0; i < quorumSize; i++) {
        uint256 id;
        WriteLE64(id.begin(), i + 1);
        members.push_back({CBLSId(id), {}, {}});
        ids.emplace_back(id);
    }
    bench.minEpochIterations(epoch_iters).run([&blsWorker, &quorumSize, &ids, &members] {
        for (int i = 0; i < quorumSize; i++) {
            blsWorker.GenerateContributions(quorumSize / 2 + 1, ids, members[i].vvec, members[i].skShares);
        }
    });
    blsWorker.Stop();
}

#define BENCH_GenerateContributions(name, quorumSize, epoch_iters)                          \
    static void BLSDKG_GenerateContributions_##name##_##quorumSize(benchmark::Bench& bench) \
    {                                                                                       \
        BLSDKG_InitDKG(bench, epoch_iters, quorumSize);                                     \
    }                                                                                       \
    BENCHMARK(BLSDKG_GenerateContributions_##name##_##quorumSize)

static void BLSDKG_InitDKG(benchmark::Bench &bench, uint32_t epoch_iters, int quorumSize) {
    bench.minEpochIterations(epoch_iters).run([&] {
        DKG d(quorumSize);
    });
}

#define BENCH_InitDKG(name, quorumSize, epoch_iters)                          \
    static void BLSDKG_InitDKG_##name##_##quorumSize(benchmark::Bench& bench) \
    {                                                                         \
        BLSDKG_InitDKG(bench, epoch_iters, quorumSize);                       \
    }                                                                         \
    BENCHMARK(BLSDKG_InitDKG_##name##_##quorumSize)


#define BENCH_BuildQuorumVerificationVectors(name, quorumSize, epoch_iters) \
    static void BLSDKG_BuildQuorumVerificationVectors_##name##_##quorumSize(benchmark::Bench& bench) \
    { \
        std::unique_ptr<DKG> ptr = std::make_unique<DKG>(quorumSize); \
        ptr->Bench_BuildQuorumVerificationVectors(bench, epoch_iters); \
        ptr.reset(); \
    } \
    BENCHMARK(BLSDKG_BuildQuorumVerificationVectors_##name##_##quorumSize)

#define BENCH_VerifyContributionShares(name, quorumSize, invalidCount, aggregated, epoch_iters) \
    static void BLSDKG_VerifyContributionShares_##name##_##quorumSize(benchmark::Bench& bench) \
    { \
        std::unique_ptr<DKG> ptr = std::make_unique<DKG>(quorumSize); \
        ptr->Bench_VerifyContributionShares(bench, invalidCount, aggregated, epoch_iters); \
        ptr.reset(); \
    } \
    BENCHMARK(BLSDKG_VerifyContributionShares_##name##_##quorumSize)

BENCH_BuildQuorumVerificationVectors(simple, 10, 1000)

BENCH_BuildQuorumVerificationVectors(simple, 100, 10)

BENCH_BuildQuorumVerificationVectors(simple, 400, 1)

BENCH_VerifyContributionShares(simple, 10, 5, false, 10)

BENCH_VerifyContributionShares(simple, 100, 5, false, 10)

BENCH_VerifyContributionShares(simple, 400, 5, false, 1)

BENCH_VerifyContributionShares(aggregated, 10, 5, true, 100)

BENCH_VerifyContributionShares(aggregated, 100, 5, true, 10)

BENCH_VerifyContributionShares(aggregated, 400, 5, true, 1)
