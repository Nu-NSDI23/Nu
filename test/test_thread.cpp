#include <atomic>
#include <iostream>

#include "nu/pressure_handler.hpp"
#include "nu/proclet.hpp"
#include "nu/runtime.hpp"
#include "nu/utils/thread.hpp"
#include "nu/utils/time.hpp"

constexpr uint32_t kNumInvocations = 1000;
constexpr uint32_t kNumThreadsPerInvocation = 4;

namespace nu {
class Test {
 public:
  void inc() {
    std::vector<nu::Thread> threads;
    for (uint32_t i = 0; i < kNumThreadsPerInvocation; i++) {
      threads.emplace_back([&] {
        Time::sleep(2 * 1000 * 1000);
        s_++;
      });
      Time::sleep(1000 * 1000);
    }
    for (auto &thread : threads) {
      thread.join();
    }
  }

  int read() { return s_; }

  void migrate() {
    rt::Preempt p;
    rt::PreemptGuard g(&p);
    get_runtime()->pressure_handler()->mock_set_pressure();
  }

 private:
  std::atomic<int> s_;
};
}  // namespace nu

bool run_in_obj_env() {
  auto proclet = nu::make_proclet<nu::Test>();
  std::vector<nu::Future<void>> futures;
  for (uint32_t i = 0; i < kNumInvocations; i++) {
    futures.emplace_back(proclet.run_async(&nu::Test::inc));
  }
  proclet.run(&nu::Test::migrate);
  for (auto &future : futures) {
    future.get();
  }
  return proclet.run(&nu::Test::read) ==
         kNumInvocations * kNumThreadsPerInvocation;
}

bool run_in_runtime_env() {
  std::atomic<int> s{0};
  std::vector<nu::Thread> invocations;

  for (uint32_t i = 0; i < kNumInvocations; i++) {
    invocations.emplace_back([&] {
      std::vector<nu::Thread> threads;
      for (uint32_t i = 0; i < kNumThreadsPerInvocation; i++) {
        threads.emplace_back([&] { s++; });
      }
      for (auto &thread : threads) {
        thread.join();
      }
    });
  }
  for (auto &invocation : invocations) {
    invocation.join();
  }
  return s == kNumInvocations * kNumThreadsPerInvocation;
}

bool run_all_tests() { return run_in_runtime_env() && run_in_obj_env(); }

int main(int argc, char **argv) {
  return nu::runtime_main_init(argc, argv, [](int, char **) {
    if (run_all_tests()) {
      std::cout << "Passed" << std::endl;
    } else {
      std::cout << "Failed" << std::endl;
    }
  });
}
