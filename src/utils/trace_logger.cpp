#include <cstring>
#include <iostream>

extern "C" {
#include <base/time.h>
}

#include "nu/utils/trace_logger.hpp"

namespace nu {

TraceLogger::TraceLogger(std::string header_str) : header_str_(header_str) {
  done_ = false;
  disabled_ = true;
  print_thread_ = rt::Thread([&] { print_thread_fn(); });
}

TraceLogger::~TraceLogger() {
  rt::access_once(done_) = true;
  rt::access_once(disabled_) = false;
  barrier();
  cv_.Signal();
  print_thread_.Join();
}

void TraceLogger::print_thread_fn() {
  auto old_us = microtime();
  uint64_t old_sum = 0;
  uint64_t old_bucket_sum[kNumBuckets];
  memset(old_bucket_sum, 0, sizeof(old_bucket_sum));

  while (true) {
    check_disabled();
    if (unlikely(rt::access_once(done_))) {
      break;
    }

    timer_sleep(rt::access_once(print_interval_us_));
    auto cur_us = microtime();
    auto diff_us = cur_us - old_us;

    uint64_t cur_sum = 0;
    uint64_t cur_bucket_sum[kNumBuckets];
    __builtin_memset(cur_bucket_sum, 0, sizeof(cur_bucket_sum));
    for (uint32_t i = 0; i < kNumCores; i++) {
      for (uint32_t j = 0; j < kNumBuckets; j++) {
        cur_bucket_sum[j] += rt::access_once(aligned_cnts_[i][j].cnt);
      }
    }
    for (uint32_t i = 0; i < kNumBuckets; i++) {
      cur_sum += cur_bucket_sum[i];
    }
    auto diff_sum = cur_sum - old_sum;

    {
      rt::Preempt p;
      rt::PreemptGuard g(&p);

      std::cout << header_str_ << std::endl;
      std::cout << "diff_us = " << diff_us << ", diff_sum = " << diff_sum
                << ", mops = " << diff_sum / static_cast<double>(diff_us)
                << std::endl;
      for (uint32_t i = 0; i < kNumBuckets; i++) {
        std::cout << i * kBucketIntervalUs << "-";
        if (i + 1 != kNumBuckets) {
          std::cout << (i + 1) * kBucketIntervalUs;
        } else {
          std::cout << "inf";
        }
        std::cout << ": " << cur_bucket_sum[i] - old_bucket_sum[i] << std::endl;
        old_bucket_sum[i] = cur_bucket_sum[i];
      }
    }
    old_us = cur_us;
    old_sum = cur_sum;
  }
}

inline void TraceLogger::check_disabled() {
  rt::ScopedLock<rt::Mutex> scope(&mutex_);
  while (rt::access_once(disabled_)) {
    cv_.Wait(&mutex_);
  }
}

void TraceLogger::enable_print(uint32_t interval_us) {
  rt::access_once(print_interval_us_) = interval_us;
  rt::ScopedLock<rt::Mutex> scope(&mutex_);
  rt::access_once(disabled_) = false;
  barrier();
  cv_.Signal();
}

void TraceLogger::disable_print() { rt::access_once(disabled_) = true; }

}  // namespace nu
