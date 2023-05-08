#include <functional>
#include <utility>

extern "C" {
#include <base/assert.h>
}

namespace nu {

template <typename K, typename Allocator>
template <typename K1>
inline void RCUHashSet<K, Allocator>::put(K1 &&k) {
  lock_.writer_lock();
  set_.emplace(std::forward<K1>(k));
  lock_.writer_unlock();
}

template <typename K, typename Allocator>
template <typename K1>
inline bool RCUHashSet<K, Allocator>::remove(K1 &&k) {
  lock_.writer_lock();
  auto ret = set_.erase(std::forward<K1>(k));
  lock_.writer_unlock();
  return ret;
}

template <typename K, typename Allocator>
template <typename K1>
inline bool RCUHashSet<K, Allocator>::contains(K1 &&k) {
  lock_.reader_lock();
  auto ret = set_.contains(std::forward<K1>(k));
  lock_.reader_unlock();
  return ret;
}

template <typename K, typename Allocator>
void RCUHashSet<K, Allocator>::for_each(
    const std::function<bool(const K &)> &fn) {
  lock_.reader_lock();
  for (const auto &k : set_) {
    if (!fn(k)) {
      lock_.reader_unlock();
      return;
    }
  }
  lock_.reader_unlock();
}

}  // namespace nu
