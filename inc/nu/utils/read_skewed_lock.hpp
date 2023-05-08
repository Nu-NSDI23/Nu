#pragma once

#include <functional>

#include "nu/utils/cond_var.hpp"
#include "nu/utils/mutex.hpp"
#include "nu/utils/spin_lock.hpp"
#include "nu/utils/rcu_lock.hpp"

namespace nu {

class ReadSkewedLock {
 public:
  constexpr static uint32_t kReaderWaitFastPathMaxUs = 20;

  ReadSkewedLock();
  void reader_lock();
  void reader_unlock();
  bool reader_try_lock();
  void writer_lock();
  bool writer_lock_if(std::function<bool()> f);
  void writer_unlock();

 private:
  bool writer_barrier_;
  Mutex writer_mutex_;
  SpinLock reader_spin_;
  RCULock rcu_lock_;
  CondVar cond_var_;

  void reader_wait();
};

}  // namespace nu

#include "nu/impl/read_skewed_lock.ipp"
