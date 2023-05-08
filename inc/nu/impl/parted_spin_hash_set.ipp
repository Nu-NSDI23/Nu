#include <functional>
#include <utility>

extern "C" {
#include <base/assert.h>
}

#include "nu/utils/scoped_lock.hpp"

namespace nu {

template <typename K, typename Allocator, size_t NPartitions>
template <typename K1>
inline size_t SpinlockHashSet<K, Allocator, NPartitions>::partitioner(K1 &&k) {
  return std::hash<K>{}(std::forward<K1>(k)) % NPartitions;
}

template <typename K, typename Allocator, size_t NPartitions>
template <typename K1>
inline void SpinlockHashSet<K, Allocator, NPartitions>::put(K1 &&k) {
  auto idx = partitioner(std::forward<K1>(k));
  ScopedLock<Spin> lock(&spins_[idx].spin);
  sets_[idx].emplace(std::forward<K1>(k));
}

template <typename K, typename Allocator, size_t NPartitions>
template <typename K1>
inline bool SpinlockHashSet<K, Allocator, NPartitions>::remove(K1 &&k) {
  auto idx = partitioner(std::forward<K1>(k));
  ScopedLock<Spin> lock(&spins_[idx].spin);
  return sets_[idx].erase(std::forward<K1>(k));
}

template <typename K, typename Allocator, size_t NPartitions>
template <typename K1>
inline bool SpinlockHashSet<K, Allocator, NPartitions>::contains(K1 &&k) {
  auto idx = partitioner(std::forward<K1>(k));
  ScopedLock<Spin> lock(&spins_[idx].spin);
  return sets_[idx].contains(std::forward<K1>(k));
}

template <typename K, typename Allocator, size_t NPartitions>
std::vector<K, Allocator>
SpinlockHashSet<K, Allocator, NPartitions>::all_keys() {
  std::vector<K, Allocator> keys;
  for (size_t i = 0; i < NPartitions; i++) {
    ScopedLock<Spin> lock(&spins_[i].spin);
    for (const auto &k : sets_[i]) {
      keys.push_back(k);
    }
  }
  return keys;
}

template <typename K, typename Allocator, size_t NPartitions>
void SpinlockHashSet<K, Allocator, NPartitions>::for_each(
    const std::function<bool(const K &)> &fn) {
  for (size_t i = 0; i < NPartitions; i++) {
    ScopedLock<Spin> lock(&spins_[i].spin);
    for (const auto &k : sets_[i]) {
      if (!fn(k)) {
        return;
      }
    }
  }
}

}  // namespace nu
