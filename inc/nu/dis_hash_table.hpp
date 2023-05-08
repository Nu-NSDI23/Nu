#pragma once

#include <memory>
#include <utility>
#include <vector>

extern "C" {
#include <runtime/net.h>
}

#include "nu/proclet.hpp"
#include "nu/utils/mutex.hpp"
#include "nu/utils/spin_lock.hpp"
#include "nu/utils/sync_hash_map.hpp"

namespace nu {

template <typename K, typename V, typename Hash = std::hash<K>,
          typename KeyEqual = std::equal_to<K>, uint64_t NumBuckets = 32768>
class DistributedHashTable {
 public:
  constexpr static uint32_t kDefaultPowerNumShards = 13;
  constexpr static uint64_t kNumBucketsPerShard = NumBuckets;

  using HashTableShard =
      SyncHashMap<NumBuckets, K, V, Hash, std::equal_to<K>,
                  std::allocator<std::pair<const K, V>>, Mutex>;

  DistributedHashTable(const DistributedHashTable &);
  DistributedHashTable &operator=(const DistributedHashTable &);
  DistributedHashTable(DistributedHashTable &&);
  DistributedHashTable &operator=(DistributedHashTable &&);
  DistributedHashTable();
  template <typename K1>
  std::optional<V> get(K1 &&k);
  template <typename K1>
  std::optional<V> get(K1 &&k, bool *is_local);
  template <typename K1, typename V1>
  void put(K1 &&k, V1 &&v);
  template <typename K1>
  bool remove(K1 &&k);
  template <typename K1, typename RetT, typename... A0s, typename... A1s>
  RetT apply(K1 &&k, RetT (*fn)(std::pair<const K, V> &, A0s...),
             A1s &&... args);
  template <typename K1>
  Future<std::optional<V>> get_async(K1 &&k);
  template <typename K1, typename V1>
  Future<void> put_async(K1 &&k, V1 &&v);
  template <typename K1>
  Future<bool> remove_async(K1 &&k);
  template <typename K1, typename RetT, typename... A0s, typename... A1s>
  Future<RetT> apply_async(K1 &&k, RetT (*fn)(std::pair<const K, V> &, A0s...),
                           A1s &&... args);
  template <typename RetT, typename... A0s, typename... A1s>
  RetT associative_reduce(
      bool clear, RetT init_val,
      void (*reduce_fn)(RetT &, std::pair<const K, V> &, A0s...),
      void (*merge_fn)(RetT &result, RetT &partition, A0s...), A1s &&... args);
  template <typename RetT, typename... A0s, typename... A1s>
  std::vector<RetT> associative_reduce(
      bool clear, RetT init_val,
      void (*reduce_fn)(RetT &, std::pair<const K, V> &, A0s...),
      A1s &&... args);
  std::vector<std::pair<K, V>> get_all_pairs();
  template <typename K1>
  static uint32_t get_shard_idx(K1 &&k, uint32_t power_num_shards);
  ProcletID get_shard_proclet_id(uint32_t shard_id);

  template <class Archive>
  void serialize(Archive &ar);

  // For debugging and performance analysis.
  template <typename K1>
  std::pair<std::optional<V>, uint32_t> get_with_ip(K1 &&k);

 private:
  struct RefCnter {
    std::vector<Proclet<HashTableShard>> shards;
  };

  friend class Test;
  uint32_t power_num_shards_;
  uint32_t num_shards_;
  Proclet<RefCnter> ref_cnter_;
  std::vector<WeakProclet<HashTableShard>> shards_;

  uint32_t get_shard_idx(uint64_t key_hash);
  template <typename X, typename Y, typename H, typename Eq, uint64_t N>
  friend DistributedHashTable<X, Y, H, Eq, N> make_dis_hash_table(
      uint32_t power_num_shards, bool pinned);
};

template <typename K, typename V, typename Hash = std::hash<K>,
          typename KeyEqual = std::equal_to<K>, uint64_t NumBuckets = 32768>
DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets> make_dis_hash_table(
    uint32_t power_num_shards = DistributedHashTable<
        K, V, Hash, KeyEqual, NumBuckets>::kDefaultPowerNumShards,
    bool pinned = false);

}  // namespace nu

#include "nu/impl/dis_hash_table.ipp"
