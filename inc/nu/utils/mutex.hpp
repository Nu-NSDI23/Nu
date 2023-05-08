#pragma once

#include <sync.h>

namespace nu {

class Mutex {
 public:
  Mutex();
  Mutex(const Mutex &) = delete;
  Mutex &operator=(const Mutex &) = delete;
  ~Mutex();
  void lock();
  void unlock();
  bool try_lock();
  uint32_t get_num_waiters() const;

 private:
  mutex_t m_;
  uint32_t num_waiters_;
  friend class CondVar;
  friend class Migrator;

  list_head *get_waiters();
  void __lock();
  void __unlock();
};

}  // namespace nu

#include "nu/impl/mutex.ipp"
