#include <nu/utils/scoped_lock.hpp>
#include <utility>

namespace nu {

template <class Impl, class Synchronized>
template <typename RetT, typename F>
inline RetT GeneralContainerBase<Impl, Synchronized>::synchronized(
    F &&f) const {
  if constexpr (Synchronized::value) {
    ScopedLock<Mutex> guard(const_cast<Mutex *>(&mutex_));
    return f();
  } else {
    return f();
  }
}

template <class Impl, class Synchronized>
inline bool GeneralContainerBase<Impl, Synchronized>::insert_batch_if(
    std::function<bool(std::size_t)> cond,
    std::vector<DataEntry> &reqs) requires InsertAble<Impl> {
  return synchronized<std::size_t>([&] {
    if (cond(impl_.size())) {
      for (auto &req : reqs) {
        if constexpr (HasVal<Impl>) {
          impl_.insert(std::move(req.first), std::move(req.second));
        } else {
          impl_.insert(std::move(req));
        }
      }
      return true;
    } else {
      return false;
    }
  });
}

template <class Impl, class Synchronized>
inline std::pair<bool, bool>
GeneralContainerBase<Impl, Synchronized>::push_back_batch_if(
    std::function<bool(std::size_t)> cond,
    std::vector<Val> &reqs) requires PushBackAble<Impl> {
  return synchronized<std::pair<bool, bool>>([&] {
    auto prev_size = impl_.size();
    if (cond(prev_size)) {
      impl_.push_back_batch(std::move(reqs));
      return std::make_pair(true, !prev_size);
    } else {
      return std::make_pair(false, false);
    }
  });
}

}  // namespace nu
