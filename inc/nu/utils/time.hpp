#pragma once

#include <cstdint>
#include <list>

extern "C" {
#include <base/time.h>
#include <runtime/timer.h>
}

#include "nu/utils/spin_lock.hpp"

namespace nu {

class Time {
 public:
  Time();
  static uint64_t rdtsc();
  static uint64_t microtime();
  static void delay_us(uint64_t us);
  static void delay_ns(uint64_t ns);
  static void delay_cycles(uint64_t cycles);
  static void sleep_until(uint64_t deadline_us, bool high_priority = false);
  static void sleep(uint64_t duration_us, bool high_priority = false);

 private:
  int64_t offset_tsc_;
  std::list<timer_entry *> entries_;
  SpinLock spin_;
  friend class Migrator;

  static void timer_callback(unsigned long arg_addr);
  uint64_t proclet_env_microtime();
  uint64_t proclet_env_rdtsc();
  void proclet_env_sleep(uint64_t duration_us, bool high_priority);
  void proclet_env_sleep_until(uint64_t deadline_us, bool high_priority);
  uint64_t to_logical_tsc(uint64_t physical_tsc);
  uint64_t to_logical_us(uint64_t physical_us);
  uint64_t to_physical_tsc(uint64_t logical_tsc);
  uint64_t to_physical_us(uint64_t logical_us);
};

struct TimerCallbackArg {
  bool high_priority;
  thread_t *th;
  ProcletHeader *proclet_header;
  uint64_t logical_deadline_us;
  std::list<timer_entry *>::iterator iter;
};

}  // namespace nu

#include "nu/impl/time.ipp"
