#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <ranges>
#include <tuple>
#include <type_traits>

#include "nu/pressure_handler.hpp"
#include "nu/proclet.hpp"
#include "nu/runtime.hpp"
#include "nu/utils/time.hpp"

using namespace nu;

constexpr static uint32_t kNumRootObjs = 2048;
constexpr static uint32_t kProcletHeapSize = 2 << 20;
constexpr static uint32_t kArgsSize = 100;
constexpr static uint32_t kNumTiers = 3;
constexpr static uint32_t kFanOuts = 2;
constexpr static uint32_t kMinComputeTimeUs = 10;
constexpr static uint32_t kMaxComputeTimeUs = 200;
constexpr static uint32_t kMigrationIntervalUs = 10 * kOneSecond;
constexpr static uint32_t kNumMigrationSourceNodes = 2;
constexpr static uint32_t kClientConcurrency = 50;

struct Args {
  uint8_t data[kArgsSize];
};

CachelineAligned(uint64_t) invocation_cnts[kClientConcurrency];

class IntGen {
 public:
  IntGen(int left, int right) : rd_(), gen_(rd_()), dis_(left, right) {}
  int next() { return dis_(gen_); }

 private:
  std::random_device rd_;
  std::mt19937 gen_;
  std::uniform_int_distribution<> dis_;
};

template <int Tier>
class Obj {
 public:
  constexpr static auto kIsLeaf = (Tier == kNumTiers - 1);

  Obj() {
    std::ranges::fill(data_, 0);
    if constexpr (!kIsLeaf) {
      for (auto &sub_obj : sub_objs_) {
        sub_obj = nu::make_proclet<Obj<Tier + 1>>();
      }
    }
  }

  Args compute(Args args, int time_us) {
    Time::delay_us(time_us);

    for (auto &byte : args.data) {
      byte = 1;
    }

    if constexpr (!kIsLeaf) {
      std::vector<Future<Args>> futures;
      for (auto &sub_obj : sub_objs_) {
        futures.emplace_back(
            sub_obj.run_async(&Obj<Tier + 1>::compute, args, time_us));
      }
      for (uint32_t i = 0; i < kArgsSize; i++) {
        for (auto &future : futures) {
          args.data[i] += future.get().data[i];
        }
      }
    }

    return args;
  }

 private:
  uint8_t data_[kProcletHeapSize];
  std::conditional_t<!kIsLeaf, Proclet<Obj<Tier + 1>>, nu::ErasedType>
      sub_objs_[kFanOuts];
};

consteval uint32_t num_nodes_per_root() {
  uint64_t p = 1;
  for (uint32_t i = 0; i < kNumTiers; i++) {
    p *= kFanOuts;
  }
  return (p - 1) / (kFanOuts - 1);
}

void do_work() {
  std::vector<Proclet<Obj<0>>> roots;
  for (uint32_t i = 0; i < kNumRootObjs; i++) {
    roots.emplace_back(nu::make_proclet<Obj<0>>());
  }

  for (uint32_t i = 0; i < kClientConcurrency; i++) {
    rt::Spawn([&, tid = i] {
      IntGen idx_gen(0, roots.size() - 1);
      IntGen time_gen(kMinComputeTimeUs, kMaxComputeTimeUs);
      Args args;
      std::ranges::fill(args.data, 0);

      while (true) {
        auto &root = roots[idx_gen.next()];
        auto rets = root.run(&Obj<0>::compute, args, time_gen.next());
        invocation_cnts[tid].d++;

        for (auto n : rets.data) {
          BUG_ON(n !=num_nodes_per_root());
        }
      }
    });
  }

  uint64_t last_time_us = microtime();
  IntGen idx_gen(0, roots.size() - 1);

  while (true) {
    auto now_time_us = microtime();
    if (now_time_us >= last_time_us + kMigrationIntervalUs) {
      last_time_us = now_time_us;
      for (uint32_t i = 0; i < kNumMigrationSourceNodes; i++) {
        auto &root = roots[idx_gen.next()];
        root.run(+[](Obj<0> &_) {
          Caladan::PreemptGuard g;
	  auto *runtime = get_runtime();
          runtime->pressure_handler()->mock_set_pressure();
        });
      }
      std::cout << "*********************" << std::endl;
      for (uint32_t i = 0; i < kClientConcurrency; i++) {
        std::cout << i << " " << invocation_cnts[i].d << std::endl
                  << std::flush;
      }
    }
  }
}

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
