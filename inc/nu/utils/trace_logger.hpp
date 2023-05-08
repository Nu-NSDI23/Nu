#pragma once

#include <sync.h>
#include <thread.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "nu/commons.hpp"

namespace nu {

class TraceLogger {
 public:
  constexpr static uint32_t kNumBuckets = 11;
  constexpr static uint32_t kBucketIntervalUs = 50;
  constexpr static auto kDefaultHeaderStr = "***********TraceLogger***********";

  TraceLogger(std::string header_str = kDefaultHeaderStr);
  ~TraceLogger();
  void enable_print(uint32_t interval_us);
  void disable_print();
  template <typename Fn>
  std::pair<uint64_t, uint64_t> add_trace(Fn &&fn);
  void add_trace(uint64_t duration_tsc);

 private:
  struct alignas(kCacheLineBytes) AlignedCnt {
    uint64_t cnt;
  };

  std::string header_str_;
  AlignedCnt aligned_cnts_[kNumCores][kNumBuckets];
  uint32_t print_interval_us_;
  rt::Thread print_thread_;
  bool disabled_;
  rt::Mutex mutex_;
  rt::CondVar cv_;
  bool done_;

  void check_disabled();
  void print_thread_fn();
};

}  // namespace nu

#include "nu/impl/trace_logger.ipp"
