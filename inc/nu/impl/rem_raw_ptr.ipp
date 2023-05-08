#include "nu/commons.hpp"
#include "nu/runtime.hpp"

namespace nu {

template <typename T>
inline RemRawPtr<T>::RemRawPtr() {}

template <typename T>
inline RemRawPtr<T>::RemRawPtr(T *raw_ptr) : RemPtr<T>(raw_ptr) {}

template <typename T>
inline RemRawPtr<T>::RemRawPtr(const RemRawPtr<T> &o) : RemPtr<T>(o) {}

template <typename T>
inline RemRawPtr<T> &RemRawPtr<T>::operator=(const RemRawPtr<T> &o) {
  RemPtr<T>::operator=(o);
  return *this;
}

template <typename T>
inline RemRawPtr<T>::RemRawPtr(RemRawPtr<T> &&o) : RemPtr<T>(std::move(o)) {}

template <typename T>
inline RemRawPtr<T> &RemRawPtr<T>::operator=(RemRawPtr<T> &&o) {
  RemPtr<T>::operator=(std::move(o));
  return *this;
}

template <typename T, typename... Args>
inline RemRawPtr<T> make_rem_raw(Args &&... args) {
  return RemRawPtr<T>(new T(std::forward<Args>(args)...));
}

}  // namespace nu
