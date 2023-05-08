#include <limits>
#include <type_traits>

#include "nu/runtime.hpp"

namespace nu {

template <typename T>
inline RuntimeAllocator<T>::RuntimeAllocator() noexcept {}

template <typename T>
template <typename U>
inline RuntimeAllocator<T>::RuntimeAllocator(
    const RuntimeAllocator<U> &o) noexcept {}

template <typename T>
template <typename U>
inline RuntimeAllocator<T> &RuntimeAllocator<T>::operator=(
    const RuntimeAllocator<U> &o) noexcept {
  return *this;
}

template <typename T>
template <typename U>
inline RuntimeAllocator<T>::RuntimeAllocator(RuntimeAllocator<U> &&o) noexcept {
}

template <typename T>
template <typename U>
inline RuntimeAllocator<T> &RuntimeAllocator<T>::operator=(
    RuntimeAllocator<U> &&o) noexcept {
  return *this;
}

template <typename T>
inline T *RuntimeAllocator<T>::allocate(std::size_t n) {
  return reinterpret_cast<T *>(
      get_runtime()->runtime_slab()->allocate(n * sizeof(T)));
}

template <typename T>
inline void RuntimeAllocator<T>::deallocate(value_type *p,
                                            std::size_t n) noexcept {
  get_runtime()->runtime_slab()->free(p);
}

template <typename T>
inline T *RuntimeAllocator<T>::allocate(std::size_t n, const_void_pointer) {
  return allocate(n);
}

template <typename T>
template <typename U, typename... Args>
inline void RuntimeAllocator<T>::construct(U *p, Args &&... args) {
  ::new (p) U(std::forward<Args>(args)...);
}

template <typename T>
template <typename U>
inline void RuntimeAllocator<T>::destroy(U *p) noexcept {
  p->~U();
}

template <typename T>
inline std::size_t RuntimeAllocator<T>::max_size() const noexcept {
  return std::numeric_limits<size_type>::max();
}

template <class T>
inline bool operator==(const RuntimeAllocator<T> &x,
                       const RuntimeAllocator<T> &y) noexcept {
  return true;
}

template <class T>
inline bool operator!=(const RuntimeAllocator<T> &x,
                       const RuntimeAllocator<T> &y) noexcept {
  return !(x == y);
}

}  // namespace nu
