#include <iostream>

#include "nu/runtime.hpp"
#include "nu/proclet.hpp"

using namespace nu;

constexpr uint32_t kNumSpinWorkers = 80;
constexpr uint32_t kSrcIp = MAKE_IP_ADDR(18, 18, 1, 2);

class SpinWorker {
 public:
  SpinWorker() {}
  void run() {
    while (1)
      ;
  }
};

void do_work() {
  std::vector<nu::Proclet<SpinWorker>> spin_workers;
  std::vector<nu::Future<void>> futures;
  for (uint32_t i = 0; i < kNumSpinWorkers; i++) {
    spin_workers.emplace_back(
        nu::make_proclet<SpinWorker>(false, std::nullopt, kSrcIp));
  }

  for (uint32_t i = 0; i < kNumSpinWorkers; i++) {
    futures.emplace_back(spin_workers[i].run_async(&SpinWorker::run));
  }
}

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
