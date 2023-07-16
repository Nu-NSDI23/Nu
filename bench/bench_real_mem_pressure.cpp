#include <runtime.h>
#include <signal.h>
#include <sync.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <thread.h>
#include <timer.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include "nu/commons.hpp"

constexpr uint32_t kMallocGranularityMB = 32;
constexpr uint32_t kNumCores = 3;
constexpr uint32_t kLoggingIntervalUs = 1000;
constexpr uint32_t kFreeMemMBTarget0 = 10000;
constexpr uint32_t kFreeMemMBTarget1 = 900;

struct AllocMemTrace {
  uint64_t time_us;
};

struct AvailMemTrace {
  uint64_t time_us;
  uint64_t ram;
  uint64_t swap;
};

bool done = false;
bool signalled = false;

void alloc_thread_fn(std::atomic<uint32_t> *alloc_times,
                     std::vector<AllocMemTrace> *traces, uint32_t times_target,
                     uint32_t mbs_target) {
  auto size = kMallocGranularityMB * nu::kOneMB;
  while (1) {
    {
      rt::Preempt p;
      rt::PreemptGuard pg(&p);
      auto ptr = malloc(size);
      memset(ptr, 0xFF, size);
    }
    traces->emplace_back(microtime());

    if (++(*alloc_times) >= times_target) {
      struct sysinfo info;
      sysinfo(&info);
      if (info.freeram < mbs_target * nu::kOneMB) {
        break;
      }
    }
  }
}

void logging(std::vector<AvailMemTrace> *avail_mem_traces) {
  auto last_poll_us = microtime();
  while (!rt::access_once(done)) {
    int32_t next_poll_us = last_poll_us + kLoggingIntervalUs;
    auto time_headroom = next_poll_us - microtime();
    if (time_headroom > 0) {
      timer_sleep(time_headroom);
    }
    last_poll_us = microtime();

    struct sysinfo info;
    sysinfo(&info);
    avail_mem_traces->emplace_back(last_poll_us, info.freeram, info.freeswap);
  }
}

void clear_linux_cache() {
  BUG_ON(system("sync; echo 3 > /proc/sys/vm/drop_caches") != 0);
}

std::vector<AllocMemTrace> alloc_until(uint32_t times_target,
                                       uint32_t mbs_target) {
  std::atomic<uint32_t> alloc_times{0};
  std::vector<AllocMemTrace> alloc_mem_traces[kNumCores];
  std::vector<rt::Thread> threads;

  for (uint32_t i = 0; i < kNumCores; i++) {
    threads.emplace_back([=, &alloc_times, &alloc_mem_traces] {
      alloc_thread_fn(&alloc_times, &alloc_mem_traces[i], times_target,
                      mbs_target);
    });
  }

  for (auto &thread : threads) {
    thread.Join();
  }

  std::vector<AllocMemTrace> all_traces;
  for (auto &traces : alloc_mem_traces) {
    all_traces.insert(all_traces.end(), traces.begin(), traces.end());
  }
  std::sort(all_traces.begin(), all_traces.end(),
            [](const AllocMemTrace &x, const AllocMemTrace &y) {
              return x.time_us < y.time_us;
            });

  return all_traces;
}

void wait_for_signal() {
  while (!rt::access_once(signalled)) {
    timer_sleep(100);
  }
  rt::access_once(signalled) = false;
}

void do_work() {
  std::cout << "clearing linux cache..." << std::endl;
  clear_linux_cache();

  std::vector<AvailMemTrace> avail_mem_traces;
  auto logging_thread =
      rt::Thread([&avail_mem_traces] { logging(&avail_mem_traces); });

  std::cout << "working towards target 0..." << std::endl;
  alloc_until(0, kFreeMemMBTarget0);
  std::cout << "waiting for signal..." << std::endl;

  wait_for_signal();

  std::cout << "working towards target 1..." << std::endl;

  auto alloc_mem_traces = alloc_until(0, kFreeMemMBTarget1);

  std::cout << "waiting for signal..." << std::endl;
  wait_for_signal();

  done = true;
  barrier();
  logging_thread.Join();

  std::cout << "writing traces..." << std::endl;
  {
    std::ofstream avail_ofs("avail_mem_traces", std::ofstream::trunc);
    for (auto [time_us, ram, swap] : avail_mem_traces) {
      avail_ofs << time_us << " " << ram << " " << swap << std::endl;
    }

    std::ofstream alloc_ofs("alloc_mem_traces", std::ofstream::trunc);
    for (auto [time_us] : alloc_mem_traces) {
      alloc_ofs << time_us << std::endl;
    }
  }
  std::cout << "done..." << std::endl;
}

void signal_handler(int signum) { rt::access_once(signalled) = true; }

int main(int argc, char **argv) {
  mlockall(MCL_CURRENT | MCL_FUTURE);
  signal(SIGHUP, signal_handler);

  rt::RuntimeInit(std::string(argv[1]), [] { do_work(); });
}
