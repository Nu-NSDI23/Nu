#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

extern "C" {
#include <net/ip.h>
#include <runtime/runtime.h>
}
#include <runtime.h>

#include "nu/proclet.hpp"
#include "nu/runtime.hpp"
#include "nu/utils/bench.hpp"

using namespace nu;
using namespace std;

constexpr static uint32_t kNumThreads = 2000;

struct AlignedCnt {
  uint32_t cnt;
  uint8_t pads[kCacheLineBytes - sizeof(cnt)];
};

AlignedCnt cnts[kNumThreads];

class Obj {
 public:
  int foo() { return 0x88; }

 private:
};

void do_work() {
  std::vector<int> ids[kNumThreads];
  Proclet<Obj> proclets[8192];
  for (uint32_t i = 0; i < 8192; i++) {
    proclets[i] = make_proclet<Obj>();
  }

  std::vector<rt::Thread> threads;
  for (uint32_t i = 0; i < kNumThreads; i++) {
    threads.emplace_back([&, tid = i] {
      std::random_device rd;
      std::mt19937 mt(rd());
      std::uniform_int_distribution<int> dist(0, 8191);
      for (uint32_t j = 0; j < 8192 * 16; j++) {
        ids[tid].push_back(dist(mt));
      }
    });
  }
  for (auto &thread : threads) {
    thread.Join();
  }

  for (uint32_t i = 0; i < kNumThreads; i++) {
    rt::Thread([&, tid = i] {
      while (true) {
        for (auto id : ids[tid]) {
          auto ret = proclets[id].run(&Obj::foo);
          ACCESS_ONCE(ret);
          cnts[tid].cnt++;
        }
      }
    }).Detach();
  }

  uint64_t old_sum = 0;
  uint64_t old_us = microtime();
  while (true) {
    timer_sleep(1000 * 1000);
    auto us = microtime();
    uint64_t sum = 0;
    for (uint32_t i = 0; i < kNumThreads; i++) {
      sum += rt::access_once(cnts[i].cnt);
    }
    std::cout << us - old_us << " " << sum - old_sum << std::endl;
    old_sum = sum;
    old_us = us;
  }
}

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
