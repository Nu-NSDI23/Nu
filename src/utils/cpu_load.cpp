#include <sync.h>

#include "nu/commons.hpp"
#include "nu/utils/cpu_load.hpp"
#include "nu/runtime.hpp"

namespace nu {

void CPULoad::decay(uint64_t now_tsc) {
  uint64_t sum_cycles = 0;
  uint64_t sum_invocation_cnts = 0;
  uint64_t sum_sample_cnts = 0;

  for (uint32_t i = 0; i < kNumCores; i++) {
    sum_cycles += cycles_[i].c;
    sum_invocation_cnts += cnts_[i].invocations;
    sum_sample_cnts += cnts_[i].samples;
  }
  auto diff_sum_cycles = sum_cycles - last_sum_cycles_;
  auto diff_sum_invocation_cnts =
      sum_invocation_cnts - last_sum_invocation_cnts_;
  auto diff_sum_sample_cnts = sum_sample_cnts - last_sum_sample_cnts_;
  auto sample_ratio_inverse =
      diff_sum_sample_cnts
          ? static_cast<float>(diff_sum_invocation_cnts) / diff_sum_sample_cnts
          : 1.0f;
  auto cycles_ratio =
      static_cast<float>(diff_sum_cycles) / (now_tsc - last_decay_tsc_);
  auto latest_cpu_load = sample_ratio_inverse * cycles_ratio;

  last_sum_cycles_ = sum_cycles;
  last_sum_invocation_cnts_ = sum_invocation_cnts;
  last_sum_sample_cnts_ = sum_sample_cnts;
  last_decay_tsc_ = now_tsc;

  if (likely(latest_cpu_load < kNumCores)) {
    // Filter out obviously inaccurate results.
    if (first_call_) {
      first_call_ = false;
      cpu_load_ = latest_cpu_load;
    } else {
      ewma(kEMWAWeight, &cpu_load_, latest_cpu_load);
    }
  }
}

}  // namespace nu
