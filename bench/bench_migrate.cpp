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

#include "nu/pressure_handler.hpp"
#include "nu/proclet.hpp"
#include "nu/runtime.hpp"

using namespace nu;

constexpr uint32_t kObjSize = 128 << 10;
constexpr uint32_t kNumRuns = 5;

namespace nu {
class Test {
 public:
  void run() {
    {
      rt::Preempt p;
      rt::PreemptGuard g(&p);
      get_runtime()->pressure_handler()->mock_set_pressure();
    }
    delay_ms(1000);
  }

 private:
  uint8_t heap[kObjSize];
};
}  // namespace nu

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) {
    for (uint32_t k = 0; k < kNumRuns; k++) {
      auto proclet = make_proclet<Test>();
      proclet.run(&Test::run);
      delay_ms(100);
    }
  });
}
