#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <utility>
#include <vector>

extern "C" {
#include <net/ip.h>
}
#include <runtime.h>

#include "nu/proclet.hpp"
#include "nu/runtime.hpp"

using namespace nu;

class Obj {
 public:
  Obj(uint32_t x) : x_(x) {}
  uint32_t get() { return x_; }

 private:
  uint32_t x_;
};

void do_work() {
  bool passed = true;

  std::vector<Proclet<Obj>> objs;
  // Subtract the main proclet.
  for (uint32_t i = 0; i < kMaxNumProclets - 1; i++) {
    objs.emplace_back(
        make_proclet<Obj>(std::tuple(i), false, kMinProcletHeapSize));
  }
  for (uint32_t i = 0; i < kMaxNumProclets - 1; i++) {
    auto &obj = objs[i];
    if (obj.run(&Obj::get) != i) {
      passed = false;
      break;
    }
  }

  if (passed) {
    std::cout << "Passed" << std::endl;
  } else {
    std::cout << "Failed" << std::endl;
  }
}

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
