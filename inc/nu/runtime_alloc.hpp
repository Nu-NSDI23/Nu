#pragma once

#include <sstream>

namespace nu {

template <typename T>
class RuntimeAllocator {
 public:
  using value_type = T;

  template <typename U>
  struct rebind {
    typedef RuntimeAllocator<U> other;
  };
  using pointer = value_type *;
  using const_pointer =
      typename std::pointer_traits<pointer>::template rebind<value_type const>;
  using void_pointer =
      typename std::pointer_traits<pointer>::template rebind<void>;
  using const_void_pointer =
      typename std::pointer_traits<pointer>::template rebind<const void>;

  using difference_type =
      typename std::pointer_traits<pointer>::difference_type;
  using size_type = std::make_unsigned_t<difference_type>;

  RuntimeAllocator() noexcept;
  template <typename U>
  RuntimeAllocator(const RuntimeAllocator<U> &o) noexcept;
  template <typename U>
  RuntimeAllocator &operator=(const RuntimeAllocator<U> &o) noexcept;
  template <typename U>
  RuntimeAllocator(RuntimeAllocator<U> &&o) noexcept;
  template <typename U>
  RuntimeAllocator &operator=(RuntimeAllocator<U> &&o) noexcept;
  value_type *allocate(std::size_t n);
  void deallocate(value_type *p, std::size_t n) noexcept;
  value_type *allocate(std::size_t n, const_void_pointer);
  template <typename U, typename... Args>
  void construct(U *p, Args &&... args);
  template <typename U>
  void destroy(U *p) noexcept;
  std::size_t max_size() const noexcept;
};

using RuntimeStringStream =
    std::basic_stringstream<char, std::char_traits<char>,
                            RuntimeAllocator<char>>;

template <class T>
bool operator==(const RuntimeAllocator<T> &x,
                const RuntimeAllocator<T> &y) noexcept;

template <class T, class U>
bool operator!=(const RuntimeAllocator<T> &x,
                const RuntimeAllocator<T> &y) noexcept;

}  // namespace nu

#include "nu/impl/runtime_alloc.ipp"
