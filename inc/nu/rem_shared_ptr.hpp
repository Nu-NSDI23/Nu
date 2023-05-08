#pragma once

#include <memory>

#include "nu/rem_ptr.hpp"

namespace nu {

template <typename T>
class RemSharedPtr : public RemPtr<T> {
 public:
  RemSharedPtr() noexcept;
  RemSharedPtr(std::shared_ptr<T> &&shared_ptr) noexcept;
  ~RemSharedPtr() noexcept;
  RemSharedPtr(const RemSharedPtr &) noexcept;
  RemSharedPtr &operator=(const RemSharedPtr &) noexcept;
  RemSharedPtr(RemSharedPtr &&) noexcept;
  RemSharedPtr &operator=(RemSharedPtr &&) noexcept;
  void reset();
  Future<void> reset_async();

  template <class Archive>
  void save(Archive &ar) const;
  template <class Archive>
  void save_move(Archive &ar);
  template <class Archive>
  void load(Archive &ar);

 private:
  std::shared_ptr<T> *shared_ptr_ = nullptr;

  RemSharedPtr(std::shared_ptr<T> *shared_ptr);

  template <typename U, typename... Args>
  friend RemSharedPtr<U> make_rem_shared(Args &&... args);
};

template <typename T, typename... Args>
RemSharedPtr<T> make_rem_shared(Args &&... args);

}  // namespace nu

#include "nu/impl/rem_shared_ptr.ipp"
