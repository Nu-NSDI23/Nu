extern "C" {
#include <runtime/membarrier.h>
#include <runtime/timer.h>
}

#include "nu/runtime.hpp"
#include "nu/utils/rcu_lock.hpp"
#include "nu/utils/time.hpp"

namespace nu {

RCULock::RCULock() : flag_(false) {
  memset(aligned_cnts_, 0, sizeof(aligned_cnts_));
}

RCULock::~RCULock() {
#ifdef DEBUG
  int32_t sum = 0;
  for (auto &per_flag : aligned_cnts_) {
    for (auto &aligned_cnt : per_flag) {
      sum += aligned_cnt.cnt.val;
    }
  }
  assert(sum == 0);
#endif
}

void RCULock::flip_and_wait(bool poll) {
  auto flag = load_acquire(&flag_);
  store_release(&flag_, !flag);
  mb();

  auto prioritized = false;
  auto start_us = microtime();
retry:
  barrier();
  int32_t sum_val = 0;
  int32_t sum_ver = 0;
  for (const auto &aligned_cnt : aligned_cnts_[flag]) {
    sum_val += aligned_cnt.cnt.val;
    sum_ver += aligned_cnt.cnt.ver;
  }
  if (sum_val) {
    if (poll) {
      if (!prioritized) {
        prioritize_and_wait_rcu_readers(this);
        prioritized = true;
      }
      cpu_relax();
    } else {
      if (likely(microtime() < start_us + kWriterWaitFastPathMaxUs)) {
        // Fast path.
	Caladan::PreemptGuard g;
        get_runtime()->caladan()->thread_yield(g);
      } else {
        // Slow path.
        Time::sleep(kWriterWaitSlowPathSleepUs);
      }
    }
    goto retry;
  }
  barrier();
  auto latest_sum_ver = 0;
  for (const auto &aligned_cnt : aligned_cnts_[flag]) {
    latest_sum_ver += aligned_cnt.cnt.ver;
  }
  if (unlikely(sum_ver != latest_sum_ver)) {
    goto retry;
  }
}

void RCULock::writer_sync(bool poll) {
  membarrier();
  {
    ScopedLock g(&mutex_);
    flip_and_wait(poll);
    flip_and_wait(poll);
  }
  mb();
}

}  // namespace nu
