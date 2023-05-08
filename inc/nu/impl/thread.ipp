#include "nu/runtime.hpp"
#include "nu/utils/caladan.hpp"

namespace nu {

extern void trampoline_in_proclet_env(void *arg);
extern void trampoline_in_runtime_env(void *arg);

inline Thread::Thread() : join_data_(nullptr) {}

inline Thread::~Thread() { BUG_ON(join_data_); }

inline Thread::Thread(Thread &&t) { *this = std::move(t); }

inline Thread &Thread::operator=(Thread &&t) {
  join_data_ = t.join_data_;
  t.join_data_ = nullptr;
  return *this;
}

template <typename F>
inline Thread::Thread(F &&f, bool copy_rcu_ctxs) {
  ProcletHeader *proclet_header;
  {
    Caladan::PreemptGuard g;

    proclet_header = get_runtime()->get_current_proclet_header();
  }

  if (proclet_header) {
    create_in_proclet_env(f, proclet_header, copy_rcu_ctxs);
  } else {
    create_in_runtime_env(f);
  }
}

template <typename F>
void Thread::create_in_proclet_env(F &&f, ProcletHeader *header,
                                   bool copy_rcu_ctxs) {
  Caladan::PreemptGuard g;

  auto *proclet_stack = get_runtime()->stack_manager()->get();
  auto proclet_stack_addr = reinterpret_cast<uint64_t>(proclet_stack);
  assert(proclet_stack_addr % kStackAlignment == 0);
  id_ = proclet_stack_addr;
  join_data_ = new join_data(std::forward<F>(f), header);
  BUG_ON(!join_data_);
  auto *th = get_runtime()->caladan()->thread_nu_create_with_args(
      proclet_stack, kStackSize, trampoline_in_proclet_env, join_data_,
      copy_rcu_ctxs);
  BUG_ON(!th);
  get_runtime()->caladan()->thread_ready(th);
}

template <typename F>
inline void Thread::create_in_runtime_env(F &&f) {
  auto *th = get_runtime()->caladan()->thread_create_with_buf(
      trampoline_in_runtime_env, reinterpret_cast<void **>(&join_data_),
      sizeof(*join_data_));
  id_ = get_runtime()->caladan()->get_thread_id(th);
  BUG_ON(!th);
  new (join_data_) join_data(std::forward<F>(f));
  get_runtime()->caladan()->thread_ready(th);
}

inline bool Thread::joinable() { return join_data_; }

inline uint64_t Thread::get_id() {
  return id_;
}

inline uint64_t Thread::get_current_id() {
  auto *proclet_header = get_runtime()->get_current_proclet_header();

  if (proclet_header) {
    return get_runtime()->get_proclet_stack_range(Caladan::thread_self()).end;
  } else {
    return get_runtime()->caladan()->get_current_thread_id();
  }
}

}  // namespace nu
