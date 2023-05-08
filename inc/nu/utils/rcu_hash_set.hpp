#pragma once

#include <sync.h>

#include <functional>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

#include "nu/utils/cond_var.hpp"
#include "nu/utils/read_skewed_lock.hpp"

namespace nu {

template <typename K, typename Allocator = std::allocator<K>>
class RCUHashSet {
 public:
  constexpr static uint32_t kReaderWaitFastPathMaxUs = 20;

  template <typename K1>
  void put(K1 &&k);
  template <typename K1>
  bool remove(K1 &&k);
  template <typename K1>
  bool contains(K1 &&k);
  void for_each(const std::function<bool(const K &)> &fn);

 private:
  using Hash = std::hash<K>;
  using KeyEqual = std::equal_to<K>;

  ReadSkewedLock lock_;
  std::unordered_set<K, Hash, KeyEqual, Allocator> set_;
};
}  // namespace nu

#include "nu/impl/rcu_hash_set.ipp"
