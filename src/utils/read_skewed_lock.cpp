extern "C" {
#include <base/time.h>
}

#include "nu/runtime.hpp"
#include "nu/utils/read_skewed_lock.hpp"

namespace nu {

void ReadSkewedLock::reader_wait() {
  rcu_lock_.reader_unlock();

  // Fast path: using thread_yield() to wait for the writer.
  auto start_us = microtime();
  do {
    Caladan::PreemptGuard g;
    get_runtime()->caladan()->thread_yield(g);
  } while (microtime() < start_us + kReaderWaitFastPathMaxUs &&
           unlikely(Caladan::access_once(writer_barrier_)));

  if (unlikely(Caladan::access_once(writer_barrier_))) {
    // Slow path: use Mutex + CondVar.
    reader_spin_.lock();
    while (unlikely(Caladan::access_once(writer_barrier_))) {
      cond_var_.wait(&reader_spin_);
    }
    reader_spin_.unlock();
  }
}

}  // namespace nu
