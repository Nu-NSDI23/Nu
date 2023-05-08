#include "nu/commons.hpp"
#include "nu/runtime.hpp"
#include "nu/utils/caladan.hpp"

namespace nu {

template <typename T>
template <class Archive>
inline void RemPtr<T>::save(Archive &ar) const {
  ar(proclet_, raw_ptr_);
}

template <typename T>
template <class Archive>
inline void RemPtr<T>::save_move(Archive &ar) {
  ar(proclet_, raw_ptr_);
}

template <typename T>
template <class Archive>
inline void RemPtr<T>::load(Archive &ar) {
  ar(proclet_, raw_ptr_);
}

template <typename T>
inline RemPtr<T>::RemPtr() noexcept {}

template <typename T>
inline RemPtr<T>::RemPtr(const RemPtr<T> &o) noexcept
    : proclet_(o.proclet_), raw_ptr_(o.raw_ptr_) {}

template <typename T>
inline RemPtr<T> &RemPtr<T>::operator=(const RemPtr<T> &o) noexcept {
  proclet_ = o.proclet_;
  raw_ptr_ = o.raw_ptr_;
  return *this;
}

template <typename T>
inline RemPtr<T>::RemPtr(RemPtr<T> &&o) noexcept
    : proclet_(std::move(o.proclet_)), raw_ptr_(o.raw_ptr_) {}

template <typename T>
inline RemPtr<T> &RemPtr<T>::operator=(RemPtr<T> &&o) noexcept {
  proclet_ = std::move(o.proclet_);
  raw_ptr_ = o.raw_ptr_;
  return *this;
}

template <typename T>
inline RemPtr<T>::RemPtr(T *raw_ptr) : raw_ptr_(raw_ptr) {
  Caladan::PreemptGuard g;
  proclet_.id_ = to_proclet_id(get_runtime()->get_current_proclet_header());
}

template <typename T>
inline RemPtr<T>::operator bool() const {
  return raw_ptr_;
}

template <typename T>
inline T *RemPtr<T>::get() {
  return raw_ptr_;
}

template <typename T>
inline T RemPtr<T>::operator*() {
  return proclet_.__run(
      +[](ErasedType &, T *raw_ptr) { return *raw_ptr; }, raw_ptr_);
}

template <typename T>
template <typename RetT, typename... S0s, typename... S1s>
inline Future<RetT> RemPtr<T>::run_async(RetT (*fn)(T &, S0s...),
                                         S1s &&... states) {
  auto raw_ptr_addr = reinterpret_cast<uintptr_t>(raw_ptr_);
  return proclet_.__run_async(
      +[](ErasedType &, uintptr_t raw_ptr_addr, RetT (*fn)(T &, S0s...),
          S1s &&... states) {
        auto *raw_ptr = reinterpret_cast<T *>(raw_ptr_addr);
        return fn(*raw_ptr, std::forward<S1s>(states)...);
      },
      raw_ptr_addr, fn, std::forward<S1s>(states)...);
}

template <typename T>
template <typename RetT, typename... S0s, typename... S1s>
inline RetT RemPtr<T>::run(RetT (*fn)(T &, S0s...), S1s &&... states) {
  auto raw_ptr_addr = reinterpret_cast<uintptr_t>(raw_ptr_);
  return proclet_.__run(
      +[](ErasedType &, uintptr_t raw_ptr_addr, RetT (*fn)(T &, S0s...),
          S0s... states) {
        auto *raw_ptr = reinterpret_cast<T *>(raw_ptr_addr);
        return fn(*raw_ptr, std::move(states)...);
      },
      raw_ptr_addr, fn, std::forward<S1s>(states)...);
}

}  // namespace nu
