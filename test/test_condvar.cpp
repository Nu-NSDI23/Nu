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
#include "nu/utils/cond_var.hpp"
#include "nu/utils/mutex.hpp"

using namespace nu;

constexpr static int kConcurrency = 4000;

namespace nu {
class Test {
 public:
  int get_credits() { return credits_; }

  void consume() {
    mutex_.lock();
    while (rt::access_once(credits_) == 0) {
      condvar_.wait(&mutex_);
    }
    credits_--;
    mutex_.unlock();
  }

  void produce() {
    mutex_.lock();
    credits_++;
    condvar_.signal();
    mutex_.unlock();
  }

  void migrate() {
    rt::Preempt p;
    rt::PreemptGuard g(&p);
    get_runtime()->pressure_handler()->mock_set_pressure();
  }

 private:
  CondVar condvar_;
  Mutex mutex_;
  int credits_ = 0;
};
}  // namespace nu

void do_work() {
  auto proclet = make_proclet<Test>();

  std::vector<Future<void>> futures;
  for (size_t i = 0; i < kConcurrency; i++) {
    futures.emplace_back(proclet.run_async(&Test::consume));
    futures.emplace_back(proclet.run_async(&Test::produce));
    if (i == kConcurrency / 2) {
      futures.emplace_back(proclet.run_async(&Test::migrate));
    }
  }

  for (auto &future : futures) {
    future.get();
  }
  /*
  bool passed = (proclet.run(&Test::get_credits) == 0);

  if (passed) {
    std::cout << "Passed" << std::endl;
  } else {
    std::cout << "Failed" << std::endl;
  }
  */
 int passed = (proclet.run(&Test::get_credits));
 std::cout << "Credits: " << passed;
}

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
