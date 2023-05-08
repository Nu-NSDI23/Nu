#include "nu/runtime.hpp"

namespace nu {

template <typename T>
inline RuntimeDeleter<T>::RuntimeDeleter() noexcept {}

template <typename T>
inline RuntimeDeleter<T>::RuntimeDeleter(const RuntimeDeleter &o) noexcept {}

template <typename T>
inline RuntimeDeleter<T> &RuntimeDeleter<T>::operator=(
    const RuntimeDeleter &o) noexcept {
  return *this;
}

template <typename T>
inline RuntimeDeleter<T>::RuntimeDeleter(RuntimeDeleter &&o) noexcept {}

template <typename T>
inline RuntimeDeleter<T> &RuntimeDeleter<T>::operator=(
    RuntimeDeleter &&o) noexcept {
  return *this;
}

template <typename T>
inline void RuntimeDeleter<T>::operator()(T *t) noexcept {
  delete t;
}
}  // namespace nu
