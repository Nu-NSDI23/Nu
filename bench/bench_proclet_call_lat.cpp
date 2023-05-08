#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
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

constexpr static uint32_t kNumRuns = 100000;

class Obj {
 public:
  int foo() { return 0x88; }

 private:
};

void do_work() {
  auto proclet = make_proclet<Obj>();

  std::vector<uint64_t> tscs;
  for (uint32_t i = 0; i < kNumRuns; i++) {
    auto start_tsc = rdtsc();
    auto ret = proclet.run(&Obj::foo);
    auto end_tsc = rdtsc();
    tscs.push_back(end_tsc - start_tsc);
    BUG_ON(ret != 0x88);
  }
  print_percentile(&tscs);
}

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
