#include "nu/runtime.hpp"

namespace nu {

inline uint32_t RCULock::reader_lock() {
  Caladan::PreemptGuard g;
  return reader_lock(g);
}

inline uint32_t RCULock::reader_lock(const Caladan::PreemptGuard &g) {
  auto flag = load_acquire(&flag_);
  auto nesting_cnt = get_runtime()->caladan()->thread_hold_rcu(this, flag);
  if (nesting_cnt == 1) {
    auto &cnt_ref = aligned_cnts_[flag][read_cpu()].cnt;
    AlignedCnt::Cnt cnt_copy;
    cnt_copy.raw = cnt_ref.raw;
    cnt_copy.val++;
    cnt_copy.ver++;
    cnt_ref.raw = cnt_copy.raw;
  }
  barrier();
  assert(nesting_cnt >= 1);
  return nesting_cnt;
}

inline void RCULock::reader_unlock() {
  Caladan::PreemptGuard g;
  reader_unlock(g);
}

inline void RCULock::reader_unlock(const Caladan::PreemptGuard &g) {
  barrier();
  bool flag;
  auto nesting_cnt = get_runtime()->caladan()->thread_unhold_rcu(this, &flag);
  assert(nesting_cnt >= 0);
  if (!nesting_cnt) {
    auto &cnt_ref = aligned_cnts_[flag][read_cpu()].cnt;
    AlignedCnt::Cnt cnt_copy;
    cnt_copy.raw = cnt_ref.raw;
    cnt_copy.val--;
    cnt_ref.raw = cnt_copy.raw;
  }
}

}  // namespace nu
