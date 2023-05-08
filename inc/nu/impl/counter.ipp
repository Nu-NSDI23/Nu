#include <cstring>

#include "nu/utils/caladan.hpp"

namespace nu {

inline Counter::Counter() { reset(); }

inline void Counter::inc() {
  Caladan::PreemptGuard g;
  inc(g);
}

inline void Counter::inc(const Caladan::PreemptGuard &g) {
  cnts_[g.read_cpu()].c++;
}

inline void Counter::inc_unsafe() { cnts_[read_cpu()].c++; }

inline void Counter::dec() {
  Caladan::PreemptGuard g;
  dec(g);
}

inline void Counter::dec(const Caladan::PreemptGuard &g) {
  cnts_[g.read_cpu()].c--;
}

inline void Counter::dec_unsafe() { cnts_[read_cpu()].c--; }

inline int64_t Counter::get() const {
  int64_t sum = 0;
  for (auto &cnt : cnts_) {
    sum += cnt.c;
  }
  return sum;
}

inline void Counter::reset() { memset(cnts_, 0, sizeof(cnts_)); }

}  // namespace nu
