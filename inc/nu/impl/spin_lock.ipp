#include "nu/utils/caladan.hpp"

namespace nu {

inline SpinLock::SpinLock() { Caladan::spin_lock_init(&spinlock_); }

inline SpinLock::~SpinLock() { assert(!Caladan::spin_lock_held(&spinlock_)); }

inline void SpinLock::lock() { Caladan::spin_lock_np(&spinlock_); }

inline void SpinLock::unlock() { Caladan::spin_unlock_np(&spinlock_); }

inline bool SpinLock::try_lock() {
  return Caladan::spin_try_lock_np(&spinlock_);
}

}  // namespace nu
