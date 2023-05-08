#pragma once

#include <functional>
#include <memory>

#include "nu/utils/cond_var.hpp"
#include "nu/utils/spin_lock.hpp"

namespace nu {

template <typename T, typename Deleter>
class Future;

template <typename T>
class Promise {
 public:
  Promise(const Promise &) = delete;
  Promise &operator=(const Promise &) = delete;
  ~Promise();
  template <typename Deleter = std::default_delete<Promise>>
  Future<T, Deleter> get_future();
  template <typename F, typename Allocator = std::allocator<Promise>>
  static Promise *create(F &&f);

 private:
  bool futurized_;
  bool ready_;
  SpinLock spin_;
  CondVar cv_;
  T t_;
  template <typename U, typename Deleter>
  friend class Future;

  Promise();
  void set_ready();
  T *data();
};

template <>
class Promise<void> {
 public:
  Promise(const Promise &) = delete;
  Promise &operator=(const Promise &) = delete;
  ~Promise();
  template <typename Deleter = std::default_delete<Promise>>
  Future<void, Deleter> get_future();
  template <typename F, typename Allocator = std::allocator<Promise>>
  static Promise *create(F &&f);

 private:
  bool futurized_;
  bool ready_;
  SpinLock spin_;
  CondVar cv_;
  template <typename U, typename Deleter>
  friend class Future;

  Promise();
  void set_ready();
};
}  // namespace nu

#include "nu/impl/promise.ipp"
