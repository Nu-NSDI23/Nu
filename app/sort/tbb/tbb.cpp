#include <algorithm>
#include <chrono>
#include <cstring>
#include <execution>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <ranges>
#include <thread>
#include <type_traits>
#include <utility>

using namespace std;

using Key = uint64_t;
constexpr uint64_t kNumElements = 400ULL << 20;
constexpr uint32_t kValSize = 90;
constexpr uint32_t kNumThreads = 34;
constexpr uint32_t kNumElementsPerThread = kNumElements / kNumThreads;
constexpr auto kNormalDistributionMean = std::numeric_limits<Key>::max() / 2.0;
constexpr auto kNormalDistributionStdDev = kNumElements / 10;
constexpr auto kUniformDistributionMin = 0;
constexpr auto kUniformDistributionMax = std::numeric_limits<Key>::max();
constexpr bool kUseNormalDistribution = true;

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

void generate_input() {
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

void run_std_sorter() {
  std::cout << "run_std_sorter()..." << std::endl;

  auto kvs =
      std::make_unique_for_overwrite<std::pair<Key, Val>[]>(kNumElements);

  auto t0 = chrono::steady_clock::now();

  std::vector<std::thread> threads;
  for (uint32_t i = 0; i < kNumThreads; i++) {
    threads.emplace_back([&, tid = i] {
      auto &ks = keys[tid];
      auto &vs = vals[tid];
      auto idx = tid * kNumElementsPerThread;
      for (uint32_t j = 0; j < kNumElementsPerThread; j++) {
        kvs[idx++] = std::make_pair(ks[j], vs[j]);
      }
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  auto t1 = chrono::steady_clock::now();

  std::sort(std::execution::par_unseq, kvs.get(), kvs.get() + kNumElements);

  auto t2 = chrono::steady_clock::now();

  auto duration0 = chrono::duration_cast<chrono::microseconds>(t1 - t0).count();
  auto duration1 = chrono::duration_cast<chrono::microseconds>(t2 - t1).count();
  std::cout << duration0 << " " << duration1 << " " << duration0 + duration1
            << std::endl;
}

int main(int argc, char **argv) {
  generate_input();
  run_std_sorter();
}
