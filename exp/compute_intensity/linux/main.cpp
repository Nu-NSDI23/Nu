#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

extern "C" {
#include <base/time.h>
#include <net/ip.h>
#include <runtime/runtime.h>
}
#include <runtime.h>

#include "nu/proclet.hpp"
#include "nu/runtime.hpp"

using namespace nu;

constexpr uint32_t kDelayNs = 10000;
constexpr uint32_t kNumThreads = 600;
constexpr uint32_t kPrintIntervalUS = 1000 * 1000;
constexpr uint32_t kNumItersPerYield = 1000 * 1000 / kDelayNs;

struct alignas(kCacheLineBytes) AlignedCnt {
  uint32_t cnt;
};

void delay_ns(uint64_t ns) {
  auto start_tsc = rdtsc();
  uint64_t cycles = ns * cycles_per_us / 1000.0;
  auto end_tsc = start_tsc + cycles;
  while (rdtsc() < end_tsc)
    ;
}

class Obj {
public:
  void run() { delay_ns(kDelayNs); }
};

class Experiment {
public:
  Experiment() {
    memset(cnts_, 0, sizeof(cnts_));
  }

  void run() {
    Obj obj;

    std::vector<rt::Thread> threads;
    for (uint32_t i = 0; i < kNumThreads; i++) {
      threads.emplace_back([&, tid = i] {
        while (true) {
          for (uint32_t j = 0; j < kNumItersPerYield; j++) {
            obj.run();
            cnts_[tid].cnt++;
          }
	  rt::Yield();
        }
      });
    }

    uint64_t old_sum = 0;
    uint64_t old_us = microtime();
    while (true) {
      timer_sleep(kPrintIntervalUS);
      auto us = microtime();
      uint64_t sum = 0;
      for (uint32_t i = 0; i < kNumThreads; i++) {
        sum += ACCESS_ONCE(cnts_[i].cnt);
      }
      std::cout << us - old_us << " " << sum - old_sum << " "
                << 1.0 * (sum - old_sum) / (us - old_us) << std::endl;
      old_sum = sum;
      old_us = us;
    }
  }

private:
  AlignedCnt cnts_[kNumThreads];
};

void do_work() {
  auto experiment = make_proclet<Experiment>();
  experiment.run(&Experiment::run);
}

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) {
    do_work();
  });
}
