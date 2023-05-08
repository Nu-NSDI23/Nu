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
#include "nu/rem_raw_ptr.hpp"
#include "nu/runtime.hpp"

using namespace nu;

class Obj {};

void do_work() {
  bool passed = true;

  constexpr auto kMagic = 0xDEADBEEF;
  auto proclet_0 = make_proclet<Obj>();
  auto magic = proclet_0.run(+[](Obj &) {
    auto proclet_1 = make_proclet<Obj>();
    return proclet_1.run(+[](Obj &) {
      auto proclet_2 = make_proclet<Obj>();
      return proclet_2.run(+[](Obj &) {
        auto proclet_3 = make_proclet<Obj>();
        return proclet_3.run(+[](Obj &) {
          auto proclet_4 = make_proclet<Obj>();
          return proclet_4.run(+[](Obj &) { return kMagic; });
        });
      });
    });
  });
  passed = (magic == kMagic);

  if (passed) {
    std::cout << "Passed" << std::endl;
  } else {
    std::cout << "Failed" << std::endl;
  }
}

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
