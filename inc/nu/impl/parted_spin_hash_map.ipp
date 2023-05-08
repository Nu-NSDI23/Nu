#include <functional>
#include <utility>

extern "C" {
#include <base/assert.h>
}

#include "nu/utils/scoped_lock.hpp"

namespace nu {

template <typename K, typename V, typename Allocator, size_t NPartitions>
template <typename K1>
inline size_t ThreadSafeHashMap<K, V, Allocator, NPartitions>::partitioner(
    K1 &&k) {
  return std::hash<K>{}(std::forward<K1>(k)) % NPartitions;
}

template <typename K, typename V, typename Allocator, size_t NPartitions>
template <typename K1>
inline V &ThreadSafeHashMap<K, V, Allocator, NPartitions>::get(K1 &&k) {
  auto idx = partitioner(std::forward<K1>(k));
  ScopedLock<Spin> lock(&spins_[idx].spin);
  return maps_[idx].find(std::forward<K1>(k))->second;
}

template <typename K, typename V, typename Allocator, size_t NPartitions>
template <typename K1, typename V1>
inline void ThreadSafeHashMap<K, V, Allocator, NPartitions>::put(K1 &&k,
                                                                 V1 &&v) {
  auto idx = partitioner(std::forward<K1>(k));
  ScopedLock<Spin> lock(&spins_[idx].spin);
  maps_[idx].emplace(std::forward<K1>(k), std::forward<V1>(v));
}

template <typename K, typename V, typename Allocator, size_t NPartitions>
template <typename K1, typename... Args>
inline V &ThreadSafeHashMap<K, V, Allocator, NPartitions>::get_or_emplace(
    K1 &&k, Args &&... args) {
  auto idx = partitioner(std::forward<K1>(k));
  ScopedLock<Spin> lock(&spins_[idx].spin);
  auto iter = maps_[idx].find(std::forward<K1>(k));
  if (iter == maps_[idx].end()) {
    iter = maps_[idx].try_emplace(k, std::forward<Args>(args)...).first;
  }
  return iter->second;
}

template <typename K, typename V, typename Allocator, size_t NPartitions>
template <typename K1>
inline bool ThreadSafeHashMap<K, V, Allocator, NPartitions>::remove(K1 &&k) {
  auto idx = partitioner(std::forward<K1>(k));
  ScopedLock<Spin> lock(&spins_[idx].spin);
  return maps_[idx].erase(std::forward<K1>(k));
}

template <typename K, typename V, typename Allocator, size_t NPartitions>
template <typename K1>
inline bool ThreadSafeHashMap<K, V, Allocator, NPartitions>::contains(K1 &&k) {
  auto idx = partitioner(std::forward<K1>(k));
  ScopedLock<Spin> lock(&spins_[idx].spin);
  return maps_[idx].contains(std::forward<K1>(k));
}

template <typename K, typename V, typename Allocator, size_t NPartitions>
template <typename K1>
bool ThreadSafeHashMap<K, V, Allocator, NPartitions>::try_get_and_remove(K1 &&k,
                                                                         V *v) {
  auto idx = partitioner(std::forward<K1>(k));
  ScopedLock<Spin> lock(&spins_[idx].spin);
  auto iter = maps_[idx].find(std::forward<K1>(k));
  if (iter == maps_[idx].end()) {
    return false;
  }
  *v = std::move(iter->second);
  maps_[idx].erase(iter);
  return true;
}

}  // namespace nu
