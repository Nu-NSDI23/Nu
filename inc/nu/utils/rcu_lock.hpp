#pragma once

#include <cstdint>

#include "nu/commons.hpp"
#include "nu/utils/caladan.hpp"
#include "nu/utils/mutex.hpp"
#include "nu/utils/scoped_lock.hpp"

namespace nu {

class RCULock {
 public:
  constexpr static uint32_t kWriterWaitFastPathMaxUs = 20;
  constexpr static uint32_t kWriterWaitSlowPathSleepUs = 10;

  RCULock();
  ~RCULock();
  uint32_t reader_lock();
  void reader_unlock();
  uint32_t reader_lock(const Caladan::PreemptGuard &g);
  void reader_unlock(const Caladan::PreemptGuard &g);
  void writer_sync(bool poll = false);

 private:
  struct alignas(kCacheLineBytes) AlignedCnt {
    union Cnt {
      struct {
        int32_t val;
        int32_t ver;
      };
      uint64_t raw;
    } cnt;
  };

  bool flag_;
  Mutex mutex_;
  AlignedCnt aligned_cnts_[2][kNumCores];

  void flip_and_wait(bool poll);
};
}  // namespace nu

#include "nu/impl/rcu_lock.ipp"
