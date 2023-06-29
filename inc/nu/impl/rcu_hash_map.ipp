#include <functional>
#include <type_traits>
#include <utility>

extern "C" {
#include <base/assert.h>
}

namespace nu {

template <typename K, typename V, typename Allocator>
template <typename K1>
inline V *RCUHashMap<K, V, Allocator>::get(K1 &&k) {
  lock_.reader_lock();
  auto iter = map_.find(std::forward<K1>(k));
  V *ret;
  if (iter == map_.end()) {
    ret = nullptr;
  } else {
    ret = &iter->second;
  }
  lock_.reader_unlock();
  return ret;
}

template <typename K, typename V, typename Allocator>
template <typename K1, typename V1>
inline void RCUHashMap<K, V, Allocator>::put(K1 &&k, V1 &&v) {
  lock_.writer_lock();
  auto p = map_.try_emplace(std::forward<K1>(k), std::forward<V1>(v));
  if (!p.second) {
    p.first->second = std::forward<V1>(v);
  }
  lock_.writer_unlock();
}

template <typename K, typename V, typename Allocator>
template <typename K1, typename V1>
inline void RCUHashMap<K, V, Allocator>::put_if_not_exists(K1 &&k, V1 &&v) {
  lock_.writer_lock();
  map_.try_emplace(std::forward<K1>(k), std::forward<V1>(v));
  lock_.writer_unlock();
}

template <typename K, typename V, typename Allocator>
template <typename K1, typename... Args>
inline void RCUHashMap<K, V, Allocator>::emplace_if_not_exists(
    K1 &&k, Args &&... args) {
  lock_.writer_lock();
  map_.try_emplace(std::forward<K1>(k), std::forward<Args>(args)...);
  lock_.writer_unlock();
}

template <typename K, typename V, typename Allocator>
template <typename K1, typename V1, typename V2>
bool RCUHashMap<K, V, Allocator>::update_if_equals(K1 &&k, V1 &&old_v,
                                                   V2 &&new_v) {
  bool updated = false;
  lock_.writer_lock();
  auto iter = map_.find(std::forward<K1>(k));
  if (iter != map_.end() && iter->second == std::forward<V1>(old_v)) {
    iter->second = std::forward<V2>(new_v);
    updated = true;
  }
  lock_.writer_unlock();
  return updated;
}

template <typename K, typename V, typename Allocator>
template <typename K1>
inline bool RCUHashMap<K, V, Allocator>::remove(K1 &&k) {
  lock_.writer_lock();
  auto ret = map_.erase(std::forward<K1>(k));
  lock_.writer_unlock();
  return ret;
}

template <typename K, typename V, typename Allocator>
template <typename K1, typename V1>
bool RCUHashMap<K, V, Allocator>::remove_if_equals(K1 &&k, V1 &&v) {
  bool removed = false;
  lock_.writer_lock();
  auto iter = map_.find(std::forward<K1>(k));
  if (iter != map_.end() && iter->second == std::forward<V1>(v)) {
    map_.erase(iter);
    removed = true;
  }
  lock_.writer_unlock();
  return removed;
}

template <typename K, typename V, typename Allocator>
void RCUHashMap<K, V, Allocator>::for_each(
    const std::function<bool(const std::pair<const K, V> &)> &fn) {
  lock_.reader_lock();
  for (const auto &p : map_) {
    if (!fn(p)) {
      lock_.reader_unlock();
      return;
    }
  }
  lock_.reader_unlock();
}

template <typename K, typename V, typename Allocator>
template <typename K1, typename RetT>
RetT RCUHashMap<K, V, Allocator>::apply(
    K1 &&k, const std::function<RetT(std::pair<const K, V> *)> &f) {
  lock_.reader_lock();
  auto iter = map_.find(std::forward<K1>(k));
  std::pair<const K, V> *pair_ptr = (iter == map_.end()) ? nullptr : &(*iter);
  if constexpr (std::is_same<RetT, void>::value) {
    f(pair_ptr);
    lock_.reader_unlock();
  } else {
    auto ret = f(pair_ptr);
    lock_.reader_unlock();
    return ret;
  }
}

}  // namespace nu
