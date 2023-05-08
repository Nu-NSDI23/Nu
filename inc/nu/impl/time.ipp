#include "nu/runtime.hpp"
#include "nu/utils/caladan.hpp"

namespace nu {

inline Time::Time() : offset_tsc_(0) {}

inline void Time::delay_us(uint64_t us) {
  delay_cycles(us * cycles_per_us);
}

inline void Time::delay_ns(uint64_t ns) {
  delay_cycles(ns * cycles_per_us / 1000);
}

inline void Time::delay_cycles(uint64_t cycles) {
  unsigned long start = rdtsc();

  while (rdtsc() - start < cycles) {
    cpu_relax();
  }
}

inline uint64_t Time::to_logical_tsc(uint64_t physical_tsc) {
  return physical_tsc + Caladan::access_once(offset_tsc_);
}

inline uint64_t Time::to_logical_us(uint64_t physical_us) {
  return physical_us + Caladan::access_once(offset_tsc_) / cycles_per_us;
}

inline uint64_t Time::to_physical_tsc(uint64_t logical_tsc) {
  return logical_tsc - Caladan::access_once(offset_tsc_);
}

inline uint64_t Time::to_physical_us(uint64_t logical_us) {
  return logical_us - Caladan::access_once(offset_tsc_) / cycles_per_us;
}

inline uint64_t Time::proclet_env_microtime() {
  return to_logical_us(::microtime());
}

inline uint64_t Time::proclet_env_rdtsc() { return to_logical_tsc(::rdtsc()); }

inline void Time::proclet_env_sleep(uint64_t duration_us, bool high_priority) {
  proclet_env_sleep_until(proclet_env_microtime() + duration_us, high_priority);
}

}  // namespace nu
