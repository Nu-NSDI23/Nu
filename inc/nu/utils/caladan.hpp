#pragma once

#include <vector>

#include <sync.h>
#include <thread.h>
#include <timer.h>

#include "nu/utils/scoped_lock.hpp"

namespace nu {

class SlabAllocator;
class RCULock;
struct ProcletHeader;

class Caladan {
 public:
  struct PreemptGuard {
    PreemptGuard();
    ~PreemptGuard();
    uint32_t read_cpu() const;
    template <typename F>
    void enable_for(F &&f);
  };

  void thread_park();
  template <typename T>
  void thread_park_and_unlock_np(ScopedLock<T> &&lock);
  void thread_park_and_unlock_np(spinlock_t *spin, list_head *waiters);
  void thread_exit();
  void thread_yield();
  void thread_yield(const PreemptGuard &g);
  template <typename F>
  void thread_spawn(F &&f);
  void thread_ready(thread_t *th);
  void thread_ready_head(thread_t *th);
  bool preempt_enabled();
  template <typename F>
  void context_switch_to(F &&f);
  void *thread_get_nu_state(thread_t *th, size_t *nu_state_size);
  thread_t *restore_thread(void *nu_state);
  thread_t *thread_create_with_buf(thread_fn_t fn, void **buf, size_t len);
  thread_t *thread_nu_create_with_args(void *proclet_stack,
                                       uint32_t proclet_stack_size,
                                       thread_fn_t fn, void *args,
                                       bool copy_rcu_ctxs);
  thread_id_t get_thread_id(thread_t *th);
  thread_id_t get_current_thread_id();
  ProcletHeader *thread_unset_owner_proclet(thread_t *th, bool update_monitor);
  ProcletHeader *thread_set_owner_proclet(thread_t *th,
                                          ProcletHeader *owner_proclet,
                                          bool update_monitor);
  ProcletHeader *thread_get_owner_proclet(thread_t *th = thread_self());
  SlabAllocator *thread_get_proclet_slab();
  SlabAllocator *thread_set_proclet_slab(SlabAllocator *proclet_slab);
  void *thread_get_runtime_stack_base();
  uint64_t thread_get_rsp(thread_t *th);
  uint32_t thread_get_creator_ip();
  bool thread_has_been_migrated();
  int32_t thread_hold_rcu(RCULock *rcu, bool flag);
  int32_t thread_unhold_rcu(RCULock *rcu, bool *flag);
  bool thread_is_rcu_held(thread_t *th, RCULock *rcu);
  void thread_start_monitor_cycles();
  void thread_end_monitor_cycles();
  bool thread_monitored();
  void thread_flush_all_monitor_cycles();
  void unblock_and_relax();
  thread_t *pop_one_waiter(list_head *waiters);
  std::vector<thread_t *> pop_all_waiters(list_head *waiters);
  void wakeup_one_waiter(list_head *waiters);
  uint64_t rdtsc();
  uint64_t microtime();
  void timer_sleep_until(uint64_t deadline_us, bool high_priority = false);
  void timer_sleep(uint64_t deadline_us, bool high_priority = false);
  void timer_start(timer_entry *e, uint64_t deadline_us);
  static uint32_t get_ip();

  static thread_t *thread_self();
  template <typename T>
  static T volatile &access_once(T &t);
  static void spin_lock_init(spinlock_t *spin);
  static void spin_lock_np(spinlock_t *spin);
  static void spin_lock(spinlock_t *spin);
  static bool spin_try_lock_np(spinlock_t *spin);
  static void spin_unlock_np(spinlock_t *spin);
  static void spin_unlock(spinlock_t *spin);
  static bool spin_lock_held(spinlock_t *spin);
  static void mutex_init(mutex_t *mutex);
  static bool mutex_held(mutex_t *mutex);
  static bool mutex_try_lock(mutex_t *mutex);
  static void condvar_init(condvar_t *condvar);
  static void timer_init(timer_entry *e, timer_fn_t fn, unsigned long arg);

 private:
  Caladan() = default;
  friend class Runtime;
};

}  // namespace nu

#include "nu/impl/caladan.ipp"
