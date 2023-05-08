// sync.h - support for synchronization primitives

#pragma once

extern "C" {
#include <base/lock.h>
#include <base/stddef.h>
#include <runtime/sync.h>
#include <runtime/thread.h>
}

#include <type_traits>

namespace rt {

// Force the compiler to access a memory location.
template<typename T>
T volatile &access_once(T &t) {
  static_assert(std::is_integral<T>::value || std::is_pointer<T>::value,
                "Integral or pointer required.");
  return static_cast<T volatile &>(t);
}

// Force the compiler to read a memory location.
template<typename T>
T read_once(const T &p) {
  static_assert(std::is_integral<T>::value || std::is_pointer<T>::value,
                "Integral or pointer required.");
  return static_cast<const T volatile &>(p);
}

// Force the compiler to write a memory location.
template<typename T>
void write_once(T &p, const T &val) {
  static_assert(std::is_integral<T>::value || std::is_pointer<T>::value,
                "Integral or pointer required.");
  static_cast<T volatile &>(p) = val;
}

// ThreadWaker is used to wake the current thread after it parks.
class ThreadWaker {
 public:
  ThreadWaker() : th_(nullptr) {}
  ~ThreadWaker() { assert(th_ == nullptr); }

  // disable copy.
  ThreadWaker(const ThreadWaker&) = delete;
  ThreadWaker& operator=(const ThreadWaker&) = delete;

  // allow move.
  ThreadWaker(ThreadWaker &&w) : th_(w.th_) { w.th_ = nullptr; }
  ThreadWaker& operator=(ThreadWaker &&w) {
    th_ = w.th_;
    w.th_ = nullptr;
    return *this;
  }

  // Prepares the running thread for waking after it parks.
  void Arm() { th_ = thread_self(); }

  // Makes the parked thread runnable. Must be called by another thread after
  // the prior thread has called Arm() and has parked (or will park in the
  // immediate future).
  void Wake(bool head = false) {
    if (th_ == nullptr) return;
    thread_t *th = th_;
    th_ = nullptr;
    if (head) {
      thread_ready_head(th);
    } else {
      thread_ready(th);
    }
  }

 private:
  thread_t *th_;
};

// Disables preemption across a critical section.
class Preempt {
 public:
  Preempt() {}
  ~Preempt() {}

  // disable move and copy.
  Preempt(const Preempt&) = delete;
  Preempt& operator=(const Preempt&) = delete;

  // Disables preemption.
  void Lock() { preempt_disable(); }

  // Enables preemption.
  void Unlock() { preempt_enable(); }

  // Atomically enables preemption and parks the running thread.
  void UnlockAndPark() { thread_park_and_preempt_enable(); }

  // Returns true if preemption is currently disabled.
  bool IsHeld() const { return !preempt_enabled(); }

  // Returns true if preemption is needed. Will be handled on Unlock() or on
  // UnlockAndPark().
  bool PreemptNeeded() const {
    assert(IsHeld());
    return preempt_needed();
  }

  // Gets the current CPU index (not the same as the core number).
  unsigned int get_cpu() const {
    assert(IsHeld());
    return read_once(kthread_idx);
  }
};

// Spin lock support.
class Spin {
 public:
  Spin() { spin_lock_init(&lock_); }
  ~Spin() { assert(!spin_lock_held(&lock_)); }

  // Locks the spin lock.
  void Lock() { spin_lock_np(&lock_); }

  // Unlocks the spin lock.
  void Unlock() { spin_unlock_np(&lock_); }

  // Atomically unlocks the spin lock and parks the running thread.
  void UnlockAndPark() { thread_park_and_unlock_np(&lock_); }

  // Locks the spin lock only if it is currently unlocked. Returns true if
  // successful.
  bool TryLock() { return spin_try_lock_np(&lock_); }

  // Returns true if the lock is currently held.
  bool IsHeld() { return spin_lock_held(&lock_); }

  // Returns true if preemption is needed. Will be handled on Unlock() or on
  // UnlockAndPark().
  bool PreemptNeeded() {
    assert(IsHeld());
    return preempt_needed();
  }

  // Gets the current CPU index (not the same as the core number).
  unsigned int get_cpu() {
    assert(IsHeld());
    return read_once(kthread_idx);
  }

 private:
  spinlock_t lock_;

  Spin(const Spin&) = delete;
  Spin& operator=(const Spin&) = delete;
};

// Pthread-like mutex support.
class Mutex {
  friend class CondVar;

 public:
  Mutex() { mutex_init(&mu_); }
  ~Mutex() { assert(!mutex_held(&mu_)); }

  // Locks the mutex.
  void Lock() { mutex_lock(&mu_); }

  // Unlocks the mutex.
  void Unlock() { mutex_unlock(&mu_); }

  // Locks the mutex only if it is currently unlocked. Returns true if
  // successful.
  bool TryLock() { return mutex_try_lock(&mu_); }

  // Returns true if the mutex is currently held.
  bool IsHeld() { return mutex_held(&mu_); }

 private:
  mutex_t mu_;

  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;
};

// std::timed_mutex-like timed mutex support.
class TimedMutex {
  friend class TimedCondVar;

 public:
  TimedMutex() { timed_mutex_init(&mu_); }
  ~TimedMutex() { assert(!timed_mutex_held(&mu_)); }

  // Locks the mutex.
  void Lock() { timed_mutex_lock(&mu_); }

  // Unlocks the mutex.
  void Unlock() { timed_mutex_unlock(&mu_); }

  // Locks the mutex only if it is currently unlocked. Returns true if
  // successful.
  bool TryLock() { return timed_mutex_try_lock(&mu_); }

  // Returns true if the mutex is currently held.
  bool IsHeld() { return timed_mutex_held(&mu_); }

  // Tries to lock the mutex. Blocks until specified duration has elapsed or
  // the lock is acquired, whichever comes first. On successful lock acquisition
  // returns true, otherwise returns false.
  bool TryLockFor(uint64_t duration_us) {
    return timed_mutex_try_lock_for(&mu_, duration_us);
  }

  // Tries to lock the mutex. Blocks until specified deadline has been reached
  // or the lock is acquired, whichever comes first. On successful lock
  // acquisition returns true, otherwise returns false.
  bool TryLockUntil(uint64_t deadline_us) {
    return timed_mutex_try_lock_until(&mu_, deadline_us);
  }

 private:
  timed_mutex_t mu_;

  TimedMutex(const TimedMutex&) = delete;
  TimedMutex& operator=(const TimedMutex&) = delete;
};

// RAII lock support (works with Spin, Preempt, and Mutex).
template <typename L>
class ScopedLock {
 public:
  explicit ScopedLock(L *lock) : lock_(lock) { lock_->Lock(); }
  ~ScopedLock() { lock_->Unlock(); }

  // Park is useful for blocking and waiting on a condition.
  // Only works with Spin and Preempt (not Mutex).
  // Example:
  // rt::ThreadWaker w;
  // rt::SpinLock l;
  // rt::SpinGuard guard(l);
  // while (condition) guard.Park(&w);
  void Park(ThreadWaker *w) {
    assert(lock_->IsHeld());
    w->Arm();
    lock_->UnlockAndPark();
    lock_->Lock();
  }

 private:
  L *const lock_;

  ScopedLock(const ScopedLock&) = delete;
  ScopedLock& operator=(const ScopedLock&) = delete;
};

using SpinGuard = ScopedLock<Spin>;
using MutexGuard = ScopedLock<Mutex>;
using PreemptGuard = ScopedLock<Preempt>;

// RAII lock and park support (works with both Spin and Preempt).
template <typename L>
class ScopedLockAndPark {
 public:
  explicit ScopedLockAndPark(L *lock) : lock_(lock) { lock_->Lock(); }
  ~ScopedLockAndPark() {
    if (lock_) {
      lock_->UnlockAndPark();
    }
  }
  void reset() {
    lock_->Unlock();
    lock_ = nullptr;
  }

 private:
  L *lock_;

  ScopedLockAndPark(const ScopedLockAndPark&) = delete;
  ScopedLockAndPark& operator=(const ScopedLockAndPark&) = delete;
};

using SpinGuardAndPark = ScopedLockAndPark<Spin>;
using PreemptGuardAndPark = ScopedLockAndPark<Preempt>;

// Pthread-like condition variable support.
class CondVar {
 public:
  CondVar() { condvar_init(&cv_); };
  ~CondVar() {}

  // Block until the condition variable is signaled. Recheck the condition
  // after wakeup, as no guarantees are made about preventing spurious wakeups.
  void Wait(Mutex *mu) { condvar_wait(&cv_, &mu->mu_); }

  // Wake up one waiter.
  void Signal() { condvar_signal(&cv_); }

  // Wake up all waiters.
  void SignalAll() { condvar_broadcast(&cv_); }

 private:
  condvar_t cv_;

  CondVar(const CondVar&) = delete;
  CondVar& operator=(const CondVar&) = delete;
};

// A CondVar variant that supports WaitFor() and WaitUntil().
class TimedCondVar {
 public:
  TimedCondVar() { timed_condvar_init(&cv_); };
  ~TimedCondVar() {}

  // Block until the condition variable is signaled. Recheck the condition
  // after wakeup, as no guarantees are made about preventing spurious wakeups.
  void Wait(TimedMutex *mu) { timed_condvar_wait(&cv_, &mu->mu_); }

  // Causes the current thread to block until the condition variable is
  // notified, a specific duration is elapsed, or a spurious wakeup occurs.
  // Returns false if the duration has been elapsed. Otherwise, returns true.
  bool WaitFor(TimedMutex *mu, uint64_t duration_us) {
    return timed_condvar_wait_for(&cv_, &mu->mu_, duration_us);
  }

  // Causes the current thread to block until the condition variable is
  // notified, a specific deadline is reached, or a spurious wakeup occurs.
  // Returns false if the deadline has been reached. Otherwise, returns true.
  bool WaitUntil(TimedMutex *mu, uint64_t deadline_us) {
    return timed_condvar_wait_until(&cv_, &mu->mu_, deadline_us);
  }

  // Wake up one waiter.
  void Signal() { timed_condvar_signal(&cv_); }

  // Wake up all waiters.
  void SignalAll() { timed_condvar_broadcast(&cv_); }

private:
  timed_condvar_t cv_;

  TimedCondVar(const TimedCondVar&) = delete;
  TimedCondVar& operator=(const TimedCondVar&) = delete;
};

// Golang-like waitgroup support.
class WaitGroup {
 public:
  // initializes a waitgroup with zero jobs.
  WaitGroup() { waitgroup_init(&wg_); };

  // Initializes a waitgroup with @count jobs.
  WaitGroup(int count) {
    waitgroup_init(&wg_);
    waitgroup_add(&wg_, count);
  }

  ~WaitGroup() { assert(wg_.cnt == 0); };

  // Changes the number of jobs (can be negative).
  void Add(int count) { waitgroup_add(&wg_, count); }

  // Decrements the number of jobs by one.
  void Done() { Add(-1); }

  // Block until the number of jobs reaches zero.
  void Wait() { waitgroup_wait(&wg_); }

 private:
  waitgroup_t wg_;

  WaitGroup(const WaitGroup&) = delete;
  WaitGroup& operator=(const WaitGroup&) = delete;
};

}  // namespace rt
