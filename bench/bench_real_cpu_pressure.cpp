#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <sys/time.h>

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

extern "C" {
#include <base/time.h>
#include <runtime/timer.h>
}
#include <runtime.h>
#include <sync.h>
#include <thread.h>

#include "nu/commons.hpp"

constexpr uint32_t kNumCPUCores = 46;

bool signalled = false;

void wait_for_signal() {
  while (!rt::access_once(signalled)) {
    timer_sleep(100);
  }
  rt::access_once(signalled) = false;
}

void do_work() {
  std::cout << "waiting for signal..." << std::endl;

  wait_for_signal();

  std::vector<rt::Thread> threads;
  for (uint32_t i = 0; i < kNumCPUCores; i++) {
    threads.push_back(rt::Thread([] {
      while (1)
        ;
    }));
  }
  threads[0].Join();
}

void signal_handler(int signum) { rt::access_once(signalled) = true; }

int main(int argc, char **argv) {
  signal(SIGHUP, signal_handler);

  int ret;

  if (argc < 2) {
    std::cerr << "usage: [cfg_file]" << std::endl;
    return -EINVAL;
  }

  ret = rt::RuntimeInit(std::string(argv[1]), [] { do_work(); });

  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }

  return 0;
}
