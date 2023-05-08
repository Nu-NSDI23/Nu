#pragma once

namespace nu {
template <typename T>
struct RuntimeDeleter {
  RuntimeDeleter() noexcept;
  RuntimeDeleter(const RuntimeDeleter &o) noexcept;
  RuntimeDeleter &operator=(const RuntimeDeleter &o) noexcept;
  RuntimeDeleter(RuntimeDeleter &&o) noexcept;
  RuntimeDeleter &operator=(RuntimeDeleter &&o) noexcept;
  void operator()(T *t) noexcept;
};
}  // namespace nu

#include "nu/impl/runtime_deleter.ipp"
