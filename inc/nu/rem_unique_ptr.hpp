#pragma once

#include <memory>

#include "nu/rem_ptr.hpp"

namespace nu {

template <typename T>
class RemUniquePtr : public RemPtr<T> {
 public:
  RemUniquePtr() noexcept;
  RemUniquePtr(std::unique_ptr<T> &&unique_ptr) noexcept;
  ~RemUniquePtr() noexcept;
  RemUniquePtr(const RemUniquePtr &) = delete;
  RemUniquePtr &operator=(const RemUniquePtr &) = delete;
  RemUniquePtr(RemUniquePtr &&) noexcept;
  RemUniquePtr &operator=(RemUniquePtr &&) noexcept;
  void release();
  void reset();
  Future<void> reset_async();

  template <class Archive>
  void save(Archive &ar) = delete;
  template <class Archive>
  void save_move(Archive &ar);

 private:
  RemUniquePtr(T *raw_ptr);

  template <typename U, typename... Args>
  friend RemUniquePtr<U> make_rem_unique(Args &&... args);
};

template <typename T, typename... Args>
RemUniquePtr<T> make_rem_unique(Args &&... args);

}  // namespace nu

#include "nu/impl/rem_unique_ptr.ipp"
