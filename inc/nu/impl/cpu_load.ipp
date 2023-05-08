#include <cstring>

#include "nu/runtime.hpp"
#include "nu/utils/scoped_lock.hpp"

namespace nu {

inline CPULoad::CPULoad() {
  memset(cycles_, 0, sizeof(cycles_));
  memset(cnts_, 0, sizeof(cnts_));
  last_sum_cycles_ = 0;
  last_sum_invocation_cnts_ = 0;
  last_sum_sample_cnts_ = 0;
  last_decay_tsc_ = rdtsc();
  interval_cycles_ = kDecayIntervalUs * cycles_per_us;
  cpu_load_ = 0;
  first_call_ = true;
}

inline void CPULoad::start_monitor() {
  auto core_id = read_cpu();

  if (unlikely(cnts_[core_id].invocations++ % kSampleInterval == 0 ||
               is_monitoring())) {
    cnts_[core_id].samples++;
    get_runtime()->caladan()->thread_start_monitor_cycles();
  }
}

inline void CPULoad::start_monitor_no_sampling() {
  auto core_id = read_cpu();
  cnts_[core_id].invocations++;
  cnts_[core_id].samples++;
  get_runtime()->caladan()->thread_start_monitor_cycles();
}

inline void CPULoad::end_monitor() {
  get_runtime()->caladan()->thread_end_monitor_cycles();
}

inline void CPULoad::flush_all() {
  get_runtime()->caladan()->thread_flush_all_monitor_cycles();
}

inline float CPULoad::get_load() const {
  auto now_tsc = rdtsc();
  if (unlikely(first_call_ || now_tsc >= last_decay_tsc_ + interval_cycles_)) {
    auto *mut_this = const_cast<CPULoad *>(this);
    ScopedLock g(&mut_this->spin_);

    if (likely(first_call_ || now_tsc >= last_decay_tsc_ + interval_cycles_)) {
      mut_this->decay(now_tsc);
    }
  }

  return cpu_load_;
}

inline bool CPULoad::is_monitoring() const {
  return get_runtime()->caladan()->thread_monitored();
}

inline void CPULoad::zero() { cpu_load_ = 0; }

}  // namespace nu
