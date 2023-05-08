#pragma once

#include "nu/commons.hpp"
#include "nu/utils/spin_lock.hpp"

namespace nu {

class CPULoad {
 public:
  constexpr static uint32_t kSampleInterval = 32;  // Be power of 2 for speed.
  constexpr static uint32_t kDecayIntervalUs = 100;
  constexpr static float kEMWAWeight = 0.1;

  CPULoad();
  void start_monitor();
  void start_monitor_no_sampling();
  bool is_monitoring() const;
  float get_load() const;
  void zero();
  static void end_monitor();
  static void flush_all();

 private:
  aligned_cycles cycles_[kNumCores];
  struct alignas(kCacheLineBytes) {
    uint64_t invocations;
    uint64_t samples;
  } cnts_[kNumCores];
  uint64_t last_sum_cycles_;
  uint64_t last_sum_invocation_cnts_;
  uint64_t last_sum_sample_cnts_;
  uint64_t last_decay_tsc_;
  uint64_t interval_cycles_;
  float cpu_load_;
  bool first_call_;
  SpinLock spin_;

  void decay(uint64_t now_tsc);
};

}  // namespace nu

#include "nu/impl/cpu_load.ipp"

