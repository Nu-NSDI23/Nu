#pragma once

#include <sync.h>

#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

#include "nu/utils/spin_lock.hpp"

namespace nu {

template <typename K, typename V,
          typename Allocator = std::allocator<std::pair<const K, V>>,
          size_t NPartitions = 29>
class PartitionedSpinHashMap {
 public:
  template <typename K1>
  V &get(K1 &&k);
  template <typename K1, typename V1>
  void put(K1 &&k, V1 &&v);
  template <typename K1, typename... Args>
  V &get_or_emplace(K1 &&k, Args &&... args);
  template <typename K1>
  bool remove(K1 &&k);
  template <typename K1>
  bool contains(K1 &&k);
  template <typename K1>
  bool try_get_and_remove(K1 &&k, V *v);

 private:
  using Hash = std::hash<K>;
  using KeyEqual = std::equal_to<K>;

  struct alignas(kCacheLineBytes) AlignedSpin {
    Spin spin;
  };

  std::unordered_map<K, V, Hash, KeyEqual, Allocator> maps_[NPartitions];
  AlignedSpin spins_[NPartitions];

  template <typename K1>
  size_t partitioner(K1 &&k);
};
}  // namespace nu

#include "nu/impl/parted_spin_hash_map.ipp"
