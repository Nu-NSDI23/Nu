#include <algorithm>
#include <iostream>
#include <limits>
#include <random>
#include <ranges>
#include <utility>
#include <cstring>
#include <memory>

#include "nu/cereal.hpp"
#include "nu/runtime.hpp"
#include "nu/sharded_sorter.hpp"

using Key = uint64_t;
constexpr uint64_t kNumElements = 400ULL << 20;
constexpr uint32_t kValSize = 90;
constexpr uint32_t kNumThreads = 34;
constexpr uint32_t kNumNodes = 5;
constexpr uint32_t kNumElementsPerThread =
    kNumElements / kNumNodes / kNumThreads;
constexpr auto kNormalDistributionMean = std::numeric_limits<Key>::max() / 2.0;
constexpr auto kNormalDistributionStdDev = kNumElements / 10;
constexpr auto kUniformDistributionMin = 0;
constexpr auto kUniformDistributionMax = std::numeric_limits<Key>::max();
constexpr bool kUseNormalDistribution = true;
constexpr nu::NodeIP kNodeIPs[] = {
    MAKE_IP_ADDR(18, 18, 1, 2), MAKE_IP_ADDR(18, 18, 1, 3),
    MAKE_IP_ADDR(18, 18, 1, 4), MAKE_IP_ADDR(18, 18, 1, 5),
    MAKE_IP_ADDR(18, 18, 1, 6)};

struct Val {
  char data[kValSize];

  Val() = default;
  Val(char x) { memset(data, x, sizeof(data)); }

  bool operator<(const Val &o) const {
    return std::strncmp(data, o.data, sizeof(data)) < 0;
  }
};

std::unique_ptr<Key[]> keys[kNumThreads];
std::unique_ptr<Val[]> vals[kNumThreads];

struct InputGenerator {
  void work() {
    nu::RuntimeSlabGuard g;

    std::cout << "generate_input()..." << std::endl;

    std::random_device rd{};
    std::mt19937 gen{rd()};
    std::normal_distribution<double> normal_d{kNormalDistributionMean,
                                              kNormalDistributionStdDev};
    std::uniform_int_distribution<uint64_t> uniform_d{kUniformDistributionMin,
                                                      kUniformDistributionMax};

    for (uint32_t i = 0; i < kNumThreads; i++) {
      auto &ks = keys[i];
      ks = std::make_unique_for_overwrite<Key[]>(kNumElementsPerThread);
      auto &vs = vals[i];
      vs = std::make_unique_for_overwrite<Val[]>(kNumElementsPerThread);
      for (uint32_t j = 0; j < kNumElementsPerThread; j++) {
        ks[j] = kUseNormalDistribution ? normal_d(gen) : uniform_d(gen);
        vs[j] = Val(j);
      }
    }
  }
};

struct Emplacer {
  Emplacer(nu::ShardedSorter<Key, Val> sharded_sorter)
      : sharded_sorter_(std::move(sharded_sorter)) {}

  void work() {
    nu::RuntimeSlabGuard g;

    std::vector<nu::Thread> threads;
    for (uint32_t i = 0; i < kNumThreads; i++) {
      threads.emplace_back(
          [&, tid = i,
           ss_ptr = std::make_unique<nu::ShardedSorter<Key, Val>>(
               sharded_sorter_)]() mutable {
            auto &ks = keys[tid];
            auto &vs = vals[tid];
            for (uint32_t j = 0; j < kNumElementsPerThread; j++) {
              ss_ptr->insert(ks[j], vs[j]);
            }
            ks.reset();
            vs.reset();
            ss_ptr.reset();
          });
    }
    for (auto &thread : threads) {
      thread.join();
    }

    std::cout
        << "num proclets = "
        << nu::get_runtime()->proclet_manager()->get_num_present_proclets()
        << std::endl;
  }

  nu::ShardedSorter<Key, Val> sharded_sorter_;
};

void do_work() {
  {
    std::vector<nu::Proclet<InputGenerator>> generators;
    std::vector<nu::Future<void>> futures;
    generators.reserve(kNumNodes);

    for (uint32_t i = 0; i < kNumNodes; i++) {
      auto ip = kNodeIPs[i];
      generators.emplace_back(
          nu::make_proclet<InputGenerator>(true, std::nullopt, ip));
      futures.emplace_back(generators.back().run_async(&InputGenerator::work));
    }
  }

  auto sharded_sorter = nu::make_sharded_sorter<Key, Val>();

  barrier();
  auto t0 = microtime();
  barrier();

  {
    std::vector<nu::Proclet<Emplacer>> emplacers;
    std::vector<nu::Future<void>> futures;
    emplacers.reserve(kNumNodes);

    for (uint32_t i = 0; i < kNumNodes; i++) {
      auto ip = kNodeIPs[i];
      emplacers.emplace_back(nu::make_proclet<Emplacer>(
          std::tuple(sharded_sorter), true, std::nullopt, ip));
      futures.emplace_back(emplacers.back().run_async(&Emplacer::work));
    }
  }

  barrier();
  auto t1 = microtime();
  barrier();

  auto sharded_sorted = sharded_sorter.sort();

  barrier();
  auto t2 = microtime();
  barrier();

  std::cout << "Shuffling takes " << t1 - t0 << " microseconds." << std::endl;
  std::cout << "Sorting takes " << t2 - t1 << " microseconds." << std::endl;
}

int main(int argc, char **argv) {
  return nu::runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
