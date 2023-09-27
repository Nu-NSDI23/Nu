#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

extern "C" {
#include <base/time.h>
#include <net/ip.h>
}
#include <runtime.h>

#include "nu/pressure_handler.hpp"
#include "nu/proclet.hpp"
#include "nu/runtime.hpp"

using namespace nu;

constexpr static int kMagic = 0xDEADBEEF;
constexpr static int kMaxLoop = 10;

namespace nu {
class Test {
 public:
  int run() {
    // Should be printed at the initial server node.
    std::cout << "I am here 0" << std::endl;

    if (int i = 1; i < kMaxLoop; i++) {
	    // Set resource pressure using the mock interface
	    {
	      rt::Preempt p;
	      rt::PreemptGuard g(&p);
	      get_runtime()->pressure_handler()->mock_set_pressure();
	    }
	    // Ensure that the migration happens before the function returns.
	    delay_us(1000 * 1000);
	    // Should be printed at the new server node.
	    std::cout << "I am here " << i << std::endl;
    } 

    return kMagic;
  }
};
}  // namespace nu

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) {
    auto proclet = make_proclet<Test>();
    bool passed = (proclet.run(&Test::run) == kMagic);

    if (passed) {
      std::cout << "Passed" << std::endl;
    } else {
      std::cout << "Failed" << std::endl;
    }
  });
}
