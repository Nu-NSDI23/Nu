#include <memory>

#include "nu/runtime.hpp"
#include "nu/utils/scoped_lock.hpp"
#include "nu/utils/time.hpp"

namespace nu {

void Time::timer_callback(unsigned long arg_addr) {
  auto *arg = reinterpret_cast<TimerCallbackArg *>(arg_addr);
  auto *proclet_header = arg->proclet_header;

  auto optional_migration_guard =
      get_runtime()->attach_and_disable_migration(proclet_header);
  if (unlikely(!optional_migration_guard)) {
    return;
  }
  get_runtime()->detach(*optional_migration_guard);

  auto &time = proclet_header->time;
  {
    ScopedLock lock(&time.spin_);
    time.entries_.erase(arg->iter);
  }

  if (arg->high_priority) {
    get_runtime()->caladan()->thread_ready_head(arg->th);
  } else {
    get_runtime()->caladan()->thread_ready(arg->th);
  }
}

uint64_t Time::rdtsc() {
  ProcletHeader *proclet_header;
  {
    Caladan::PreemptGuard g;
    proclet_header = get_runtime()->get_current_proclet_header();
  }
  if (proclet_header) {
    return proclet_header->time.proclet_env_rdtsc();
  } else {
    return get_runtime()->caladan()->rdtsc();
  }
}

uint64_t Time::microtime() {
  ProcletHeader *proclet_header;
  {
    Caladan::PreemptGuard g;
    proclet_header = get_runtime()->get_current_proclet_header();
  }
  if (proclet_header) {
    return proclet_header->time.proclet_env_microtime();
  } else {
    return get_runtime()->caladan()->microtime();
  }
}

void Time::sleep_until(uint64_t deadline_us, bool high_priority) {
  ProcletHeader *proclet_header;
  {
    Caladan::PreemptGuard g;
    proclet_header = get_runtime()->get_current_proclet_header();
  }
  if (proclet_header) {
    proclet_header->time.proclet_env_sleep_until(deadline_us, high_priority);
  } else {
    get_runtime()->caladan()->timer_sleep_until(deadline_us, high_priority);
  }
}

void Time::sleep(uint64_t duration_us, bool high_priority) {
  ProcletHeader *proclet_header;
  {
    Caladan::PreemptGuard g;
    proclet_header = get_runtime()->get_current_proclet_header();
  }
  if (proclet_header) {
    proclet_header->time.proclet_env_sleep(duration_us, high_priority);
  } else {
    get_runtime()->caladan()->timer_sleep(duration_us, high_priority);
  }
}

void Time::proclet_env_sleep_until(uint64_t deadline_us, bool high_priority) {
  auto *e = new timer_entry();
  std::unique_ptr<timer_entry> e_gc(e);
  auto physical_us = to_physical_us(deadline_us);
  auto *arg = new TimerCallbackArg();
  std::unique_ptr<TimerCallbackArg> arg_gc(arg);

  arg->high_priority = high_priority;
  arg->th = Caladan::thread_self();
  {
    Caladan::PreemptGuard g;
    arg->proclet_header = get_runtime()->get_current_proclet_header();
  }
  arg->logical_deadline_us = deadline_us;
  BUG_ON(!arg->proclet_header);
  Caladan::timer_init(e, Time::timer_callback,
                      reinterpret_cast<unsigned long>(arg));

  ScopedLock lock(&spin_);
  {
    RuntimeSlabGuard g;
    entries_.push_back(e);
  }
  arg->iter = --entries_.end();
  get_runtime()->caladan()->timer_start(e, physical_us);
  get_runtime()->caladan()->thread_park_and_unlock_np(std::move(lock));
}

}  // namespace nu
