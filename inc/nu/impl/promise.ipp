#pragma once

extern "C" {
#include <base/assert.h>
#include <base/compiler.h>
}
#include <sync.h>

#include "nu/utils/future.hpp"
#include "nu/utils/thread.hpp"

namespace nu {

template <typename T>
inline Promise<T>::Promise() : futurized_(false), ready_(false) {}

inline Promise<void>::Promise() : futurized_(false), ready_(false) {}

template <typename T>
inline Promise<T>::~Promise() {
  spin_.lock();
  spin_.unlock();
}

inline Promise<void>::~Promise() {
  spin_.lock();
  spin_.unlock();
}

template <typename T>
template <typename Deleter>
inline Future<T, Deleter> Promise<T>::get_future() {
  BUG_ON(futurized_);
  futurized_ = true;
  return Future<T, Deleter>(this);
}

template <typename Deleter>
inline Future<void, Deleter> Promise<void>::get_future() {
  BUG_ON(futurized_);
  futurized_ = true;
  return Future<void, Deleter>(this);
}

template <typename T>
inline void Promise<T>::set_ready() {
  spin_.lock();
  ready_ = true;
  cv_.signal_all();
  spin_.unlock();
}

inline void Promise<void>::set_ready() {
  spin_.lock();
  ready_ = true;
  cv_.signal_all();
  spin_.unlock();
}

template <typename T>
inline T *Promise<T>::data() {
  return &t_;
}

template <typename T>
template <typename F, typename Allocator>
inline Promise<T> *Promise<T>::create(F &&f) {
  Allocator allocator;
  auto *promise = allocator.allocate(1);
  new (promise) Promise<T>();
  Thread([promise, f = std::forward<F>(f)]() mutable {
    *promise->data() = f();
    promise->set_ready();
  }).detach();
  return promise;
}

template <typename F, typename Allocator>
inline Promise<void> *Promise<void>::create(F &&f) {
  Allocator allocator;
  auto *promise = allocator.allocate(1);
  new (promise) Promise<void>();
  Thread([promise, f = std::forward<F>(f)]() mutable {
    f();
    promise->set_ready();
  }).detach();
  return promise;
}

}  // namespace nu
