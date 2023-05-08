#include <iostream>
#include <vector>
#include <memory>

#include "nu/proclet.hpp"
#include "nu/runtime.hpp"

using namespace nu;
using namespace std;

constexpr static uint32_t kNumThreads = 184;
constexpr static uint32_t kObjSize = 100;
constexpr static uint32_t kBufSize = 102400;
constexpr static uint32_t kNumInvocationsPerThread = 1000;

struct Obj {
  uint8_t data[kObjSize];
};

using Buf = std::vector<Obj>;

class Worker {
 public:
  void foo(Buf buf) {}
};

void do_work() {
  std::vector<Proclet<Worker>> workers;

  for (uint32_t i = 0; i < kNumThreads; i++) {
    workers.emplace_back(make_proclet<Worker>());
  }

  std::vector<rt::Thread> ths;
  for (uint32_t i = 0; i < kNumThreads; i++) {
    ths.emplace_back([worker = std::move(workers[i])]() mutable {
      Buf buf;
      buf.resize(kBufSize / kObjSize);
      for (uint32_t j = 0; j < kNumInvocationsPerThread; j++) {
        worker.run(&Worker::foo, buf);
      }
    });
  }

  auto t0 = microtime();
  for (auto &th : ths) {
    th.Join();
  }
  auto t1 = microtime();
  auto us = t1 - t0;
  auto size =
      static_cast<uint64_t>(kBufSize) * kNumInvocationsPerThread * kNumThreads;
  auto mbs = size / us;

  std::cout << t1 - t0 << " us, " << mbs << " MB/s" << std::endl;
}

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
