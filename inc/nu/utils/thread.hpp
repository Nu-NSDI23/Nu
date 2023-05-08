#pragma once

#include <memory>
#include <functional>

#include "nu/utils/cond_var.hpp"
#include "nu/utils/spin_lock.hpp"

namespace nu {

struct ProcletHeader;

struct join_data {
  template <typename F>
  join_data(F &&f) : done(false), func(std::move(f)), header(nullptr) {}
  template <typename F>
  join_data(F &&f, ProcletHeader *hdr)
      : done(false), func(std::move(f)), header(hdr) {}

  bool done;
  SpinLock lock;
  CondVar cv;
  std::move_only_function<void()> func;
  ProcletHeader *header;
};

class Thread {
 public:
  template <typename F>
  Thread(F &&f, bool copy_rcu_ctxs = true);
  Thread();
  ~Thread();
  Thread(const Thread &) = delete;
  Thread &operator=(const Thread &) = delete;
  Thread(Thread &&t);
  Thread &operator=(Thread &&t);
  bool joinable();
  void join();
  void detach();
  uint64_t get_id();
  static uint64_t get_current_id();

 private:
  uint64_t id_;
  join_data *join_data_;
  friend class Migrator;

  template <typename F>
  void create_in_proclet_env(F &&f, ProcletHeader *header, bool copy_rcu_ctxs);
  template <typename F>
  void create_in_runtime_env(F &&f);
  static void trampoline_in_runtime_env(void *args);
  static void trampoline_in_proclet_env(void *args);
};

}  // namespace nu

#include "nu/impl/thread.ipp"
