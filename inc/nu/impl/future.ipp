#pragma once

#include "nu/utils/promise.hpp"

namespace nu {

template <typename T, typename Deleter>
inline Future<T, Deleter>::Future() {}

template <typename T, typename Deleter>
inline Future<T, Deleter>::Future(Promise<T> *promise)
    : promise_(promise, Deleter()) {}

template <typename T, typename Deleter>
inline Future<T, Deleter>::Future(Future<T, Deleter> &&o) {
  if (promise_) {
    get();
  }
  promise_ = std::move(o.promise_);
}

template <typename T, typename Deleter>
inline Future<T, Deleter> &Future<T, Deleter>::operator=(
    Future<T, Deleter> &&o) {
  if (promise_) {
    get();
  }
  promise_ = std::move(o.promise_);
  return *this;
}

template <typename Deleter>
inline Future<void, Deleter>::Future() {}

template <typename Deleter>
inline Future<void, Deleter>::Future(Promise<void> *promise)
    : promise_(promise, Deleter()) {}

template <typename Deleter>
inline Future<void, Deleter>::Future(Future<void, Deleter> &&o) {
  if (promise_) {
    get();
  }
  promise_ = std::move(o.promise_);
}

template <typename Deleter>
inline Future<void, Deleter> &Future<void, Deleter>::operator=(
    Future<void, Deleter> &&o) {
  if (promise_) {
    get();
  }
  promise_ = std::move(o.promise_);
  return *this;
}

template <typename T, typename Deleter>
inline Future<T, Deleter>::~Future() {
  if (promise_) {
    get();
  }
}

template <typename Deleter>
inline Future<void, Deleter>::~Future() {
  if (promise_) {
    get();
  }
}

template <typename T, typename Deleter>
inline Future<T, Deleter>::operator bool() const {
  return promise_.get();
}

template <typename Deleter>
inline Future<void, Deleter>::operator bool() const {
  return promise_.get();
}

template <typename T, typename Deleter>
inline bool Future<T, Deleter>::is_ready() {
  return Caladan::access_once(promise_->ready_);
}

template <typename Deleter>
inline bool Future<void, Deleter>::is_ready() {
  return Caladan::access_once(promise_->ready_);
}

template <typename T, typename Deleter>
T &Future<T, Deleter>::get_slow_path() {
  promise_->spin_.lock();
  while (!is_ready()) {
    promise_->cv_.wait(&promise_->spin_);
  }
  promise_->spin_.unlock();
  return promise_->t_;
}

template <typename T, typename Deleter>
inline T &Future<T, Deleter>::get() {
  if (is_ready()) {
    return promise_->t_;
  }

  return get_slow_path();
}

template <typename T, typename Deleter>
inline T &Future<T, Deleter>::get_sync() {
  while (!is_ready())
    ;
  return promise_->t_;
}

template <typename Deleter>
void Future<void, Deleter>::get_slow_path() {
  promise_->spin_.lock();
  while (!is_ready()) {
    promise_->cv_.wait(&promise_->spin_);
  }
  promise_->spin_.unlock();
}

template <typename Deleter>
inline void Future<void, Deleter>::get() {
  if (is_ready()) {
    return;
  }

  get_slow_path();
}

template <typename Deleter>
inline void Future<void, Deleter>::get_sync() {
  while (!is_ready())
    ;
}

template <typename F, typename Allocator>
inline Future<std::invoke_result_t<std::decay_t<F>>> async(F &&f) {
  return Promise<std::invoke_result_t<std::decay_t<F>>>::create(
             std::forward<F>(f))
      ->get_future();
}

}  // namespace nu
