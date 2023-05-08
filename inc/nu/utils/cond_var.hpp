#pragma once

#include <sync.h>

namespace nu {

class Mutex;
class SpinLock;
struct ProcletHeader;

class CondVar {
 public:
  CondVar();
  CondVar(const CondVar &) = delete;
  CondVar &operator=(const CondVar &) = delete;
  ~CondVar();
  void wait(Mutex *mutex);
  void wait_and_unlock(Mutex *mutex);
  void wait(SpinLock *spin);
  void wait_and_unlock(SpinLock *spin);
  void signal();
  void signal_all();

 private:
  condvar_t cv_;
  friend class Migrator;

  list_head *get_waiters();
  void __wait_and_unlock(auto *l);
};

}  // namespace nu

#include "nu/impl/cond_var.ipp"
