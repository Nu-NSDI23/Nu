#pragma once

#include <sync.h>

#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "nu/utils/spin_lock.hpp"

namespace nu {

template <size_t NBuckets, typename K, typename V, typename Hash = std::hash<K>,
          typename KeyEqual = std::equal_to<K>,
          typename Allocator = std::allocator<std::pair<const K, V>>,
          typename Lock = SpinLock>
class SyncHashMap {
 public:
  SyncHashMap();
  ~SyncHashMap();
  SyncHashMap(const SyncHashMap &) noexcept;
  SyncHashMap &operator=(const SyncHashMap &) noexcept;
  SyncHashMap(SyncHashMap &&) noexcept;
  SyncHashMap &operator=(SyncHashMap &&) noexcept;
  template <typename K1>
  V *get(K1 &&k);
  template <typename K1>
  V *get_with_hash(K1 &&k, uint64_t key_hash);
  template <typename K1>
  std::optional<V> get_copy(K1 &&k);
  template <typename K1>
  std::optional<V> get_copy_with_hash(K1 &&k, uint64_t key_hash);
  template <typename K1, typename V1>
  void put(K1 k, V1 v);
  template <typename K1, typename V1>
  void put_with_hash(K1 k, V1 v, uint64_t key_hash);
  template <typename K1, typename... Args>
  bool try_emplace(K1 k, Args... args);
  template <typename K1, typename... Args>
  bool try_emplace_with_hash(K1 k, uint64_t key_hash, Args... args);
  template <typename K1>
  bool remove(K1 &&k);
  template <typename K1>
  bool remove_with_hash(K1 &&k, uint64_t key_hash);
  template <typename K1, typename RetT, typename... A0s, typename... A1s>
  RetT apply(K1 &&k, RetT (*fn)(std::pair<const K, V> &, A0s...),
             A1s &&... args);
  template <typename K1, typename RetT, typename... A0s, typename... A1s>
  RetT apply_with_hash(K1 &&k, uint64_t key_hash,
                       RetT (*fn)(std::pair<const K, V> &, A0s...),
                       A1s &&... args);
  template <typename RetT, typename... A0s, typename... A1s>
  RetT associative_reduce(bool clear, RetT init_val,
                          void (*reduce_fn)(RetT &, std::pair<const K, V> &,
                                            A0s...),
                          A1s &&...args);
  template <typename K1>
  std::optional<V> get_and_remove(K1 &&k);
  std::vector<std::pair<K, V>> get_all_pairs();
  std::vector<std::pair<uint64_t, K>> get_all_hashes_and_keys();
  template <class Archive>
  void save(Archive &ar) const;
  template <class Archive>
  void load(Archive &ar);

 private:
  struct BucketNode {
    uint64_t key_hash;
    void *pair;
    BucketNode *next;
  };
  struct BucketHead {
    BucketNode node;
    Lock lock;
  };
  using Pair = std::pair<const K, V>;
  using BucketNodeAllocator =
      std::allocator_traits<Allocator>::template rebind_alloc<BucketNode>;
  using BucketHeadAllocator =
      std::allocator_traits<Allocator>::template rebind_alloc<BucketHead>;

  BucketHead *bucket_heads_;

  template <typename K1>
  V *__get_with_hash(K1 &&k, uint64_t key_hash);
};
}  // namespace nu

#include "nu/impl/sync_hash_map.ipp"
