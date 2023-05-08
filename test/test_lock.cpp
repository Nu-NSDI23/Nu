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
#include "nu/utils/mutex.hpp"
#include "nu/utils/spin_lock.hpp"

using namespace nu;

constexpr static int kConcurrency = 100;

namespace nu {
class Test {
 public:
  void do_work() { delay_us(100 * 1000); }

  void mutex() {
    mutex_.lock();
    do_work();
    cnt_++;
    mutex_.unlock();
  }

  void migrate() {
    rt::Preempt p;
    rt::PreemptGuard g(&p);
    get_runtime()->pressure_handler()->mock_set_pressure();
  }

  int get_cnt() { return cnt_; }

 private:
  Mutex mutex_;
  int cnt_ = 0;
};
}  // namespace nu

void do_work() {
  bool passed = true;

  auto proclet = make_proclet<Test>();

  std::vector<Future<void>> futures;
  for (size_t i = 0; i < kConcurrency; i++) {
    futures.emplace_back(proclet.run_async(&Test::mutex));
  }
  futures.emplace_back(proclet.run_async(&Test::migrate));

  for (auto &future : futures) {
    future.get();
  }

  passed &= (proclet.run(&Test::get_cnt) == kConcurrency);

  if (passed) {
    std::cout << "Passed" << std::endl;
  } else {
    std::cout << "Failed" << std::endl;
  }
}

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
