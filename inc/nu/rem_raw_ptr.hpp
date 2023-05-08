#pragma once

#include "nu/rem_ptr.hpp"

namespace nu {

template <typename T>
class RemRawPtr : public RemPtr<T> {
 public:
  RemRawPtr();
  RemRawPtr(T *raw_ptr);
  RemRawPtr(const RemRawPtr &);
  RemRawPtr &operator=(const RemRawPtr &);
  RemRawPtr(RemRawPtr &&);
  RemRawPtr &operator=(RemRawPtr &&);

 private:
  template <typename U, typename... Args>
  friend RemRawPtr<U> make_rem_raw(Args &&... args);
};

template <typename T, typename... Args>
RemRawPtr<T> make_rem_raw(Args &&... args);

}  // namespace nu

#include "nu/impl/rem_raw_ptr.ipp"
