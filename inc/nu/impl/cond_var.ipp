#include "nu/utils/caladan.hpp"

namespace nu {

inline CondVar::CondVar() { Caladan::condvar_init(&cv_); }

inline CondVar::~CondVar() {}

inline list_head *CondVar::get_waiters() { return &cv_.waiters; }

}  // namespace nu
