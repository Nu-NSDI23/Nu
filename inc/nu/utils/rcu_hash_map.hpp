#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

#include "nu/utils/cond_var.hpp"
#include "nu/utils/read_skewed_lock.hpp"

namespace nu {

template <typename K, typename V,
          typename Allocator = std::allocator<std::pair<const K, V>>>
class RCUHashMap {
 public:
  constexpr static uint32_t kReaderWaitFastPathMaxUs = 20;

  template <typename K1>
  V *get(K1 &&k);
  template <typename K1, typename V1>
  void put(K1 &&k, V1 &&v);
  template <typename K1, typename V1>
  void put_if_not_exists(K1 &&k, V1 &&v);
  template <typename K1, typename... Args>
  void emplace_if_not_exists(K1 &&k, Args &&... args);
  template <typename K1, typename V1, typename V2>
  bool update_if_equals(K1 &&k, V1 &&old_v, V2 &&new_v);
  template <typename K1>
  bool remove(K1 &&k);
  template <typename K1, typename V1>
  bool remove_if_equals(K1 &&k, V1 &&v);
  void for_each(const std::function<bool(const std::pair<const K, V> &)> &fn);
  template <typename K1, typename RetT>
  RetT apply(K1 &&k, const std::function<RetT(std::pair<const K, V> *)> &f);

 private:
  using Hash = std::hash<K>;
  using KeyEqual = std::equal_to<K>;

  ReadSkewedLock lock_;
  std::unordered_map<K, V, Hash, KeyEqual, Allocator> map_;
};
}  // namespace nu

#include "nu/impl/rcu_hash_map.ipp"
