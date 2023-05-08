#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

extern "C" {
#include <net/ip.h>
}
#include <runtime.h>

#include "nu/pressure_handler.hpp"
#include "nu/proclet.hpp"
#include "nu/runtime.hpp"
#include "nu/utils/time.hpp"

using namespace nu;

namespace nu {

class Test {
 public:
  uint64_t microtime() { return Time::microtime(); }
  void delay_us(uint64_t us) { Time::delay_us(us); }
  void sleep_until(uint64_t deadline_us) {
    return Time::sleep_until(deadline_us);
  }
  void sleep(uint64_t duration_us) { Time::sleep(duration_us); }
  void migrate() {
    rt::Preempt p;
    rt::PreemptGuard g(&p);
    get_runtime()->pressure_handler()->mock_set_pressure();
  }
};
}  // namespace nu

bool around_one_second(uint64_t us) {
  std::cout << us << std::endl;
  return abs(static_cast<int64_t>(us) - 1000 * 1000) < 5000;
}

void do_work() {
  bool passed = true;
  uint64_t us[5];

  auto proclet = make_proclet<Test>();
  us[0] = proclet.run(&Test::microtime);
  proclet.run(&Test::delay_us, 1000 * 1000);
  us[1] = proclet.run(&Test::microtime);
  proclet.run(&Test::sleep, 1000 * 1000);
  us[2] = proclet.run(&Test::microtime);
  proclet.run(+[](Test &t) {
    auto us = t.microtime();
    t.sleep_until(us + 1000 * 1000);
  });
  us[3] = proclet.run(&Test::microtime);
  auto future = proclet.run_async(&Test::sleep, 1000 * 1000);
  proclet.run(&Test::migrate);
  future.get();
  us[4] = proclet.run(&Test::microtime);

  for (uint64_t i = 0; i < 4; i++) {
    passed &= around_one_second(us[i + 1] - us[i]);
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
