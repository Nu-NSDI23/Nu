#include "nu/commons.hpp"

namespace nu {

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
inline DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::
    DistributedHashTable(const DistributedHashTable &o) {
  *this = o;
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
inline DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>
    &DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::operator=(
        const DistributedHashTable &o) {
  power_num_shards_ = o.power_num_shards_;
  num_shards_ = o.num_shards_;
  ref_cnter_ = o.ref_cnter_;
  shards_ = o.shards_;
  return *this;
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
inline DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::
    DistributedHashTable(DistributedHashTable &&o) {
  *this = std::move(o);
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>
    &DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::operator=(
        DistributedHashTable &&o) {
  power_num_shards_ = o.power_num_shards_;
  num_shards_ = o.num_shards_;
  ref_cnter_ = std::move(o.ref_cnter_);
  shards_ = std::move(o.shards_);
  return *this;
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
inline DistributedHashTable<K, V, Hash, KeyEqual,
                            NumBuckets>::DistributedHashTable()
    : power_num_shards_(0), num_shards_(0) {}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
inline uint32_t
DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::get_shard_idx(
    uint64_t key_hash) {
  return key_hash / (std::numeric_limits<uint64_t>::max() >> power_num_shards_);
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
template <typename K1>
inline uint32_t
DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::get_shard_idx(
    K1 &&k, uint32_t power_num_shards) {
  auto hash = Hash();
  auto key_hash = hash(std::forward<K1>(k));
  return key_hash / (std::numeric_limits<uint64_t>::max() >> power_num_shards);
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
inline ProcletID
DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::get_shard_proclet_id(
    uint32_t shard_id) {
  return shards_[shard_id].id_;
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
template <typename K1>
inline std::optional<V>
DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::get(K1 &&k) {
  auto hash = Hash();
  auto key_hash = hash(std::forward<K1>(k));
  auto shard_idx = get_shard_idx(key_hash);
  auto &shard = shards_[shard_idx];
  return shard.__run(&HashTableShard::template get_copy_with_hash<K>,
                     std::forward<K1>(k), key_hash);
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
template <typename K1>
inline std::optional<V>
DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::get(K1 &&k,
                                                            bool *is_local) {
  auto hash = Hash();
  auto key_hash = hash(std::forward<K1>(k));
  auto shard_idx = get_shard_idx(key_hash);
  auto &shard = shards_[shard_idx];
  return shard.__run_and_get_loc(
      is_local, &HashTableShard::template get_copy_with_hash<K>,
      std::forward<K1>(k), key_hash);
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
template <typename K1>
inline std::pair<std::optional<V>, uint32_t>
DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::get_with_ip(K1 &&k) {
  auto hash = Hash();
  auto key_hash = hash(std::forward<K1>(k));
  auto shard_idx = get_shard_idx(key_hash);
  auto &shard = shards_[shard_idx];
  return shard.__run(
      +[](HashTableShard &shard, K1 k, uint64_t key_hash) {
        return std::make_pair(shard.get_copy_with_hash(std::move(k), key_hash),
                              get_cfg_ip());
      },
      std::forward<K1>(k), key_hash);
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
template <typename K1, typename V1>
inline void DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::put(
    K1 &&k, V1 &&v) {
  auto hash = Hash();
  auto key_hash = hash(std::forward<K1>(k));
  auto shard_idx = get_shard_idx(key_hash);
  auto &shard = shards_[shard_idx];
  shard.__run(&HashTableShard::template put_with_hash<K, V>,
              std::forward<K1>(k), std::forward<V1>(v), key_hash);
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
template <typename K1>
inline bool DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::remove(
    K1 &&k) {
  auto hash = Hash();
  auto key_hash = hash(std::forward<K1>(k));
  auto shard_idx = get_shard_idx(key_hash);
  auto &shard = shards_[shard_idx];
  return shard.__run(&HashTableShard::template remove_with_hash<K>,
                     std::forward<K1>(k), key_hash);
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
template <typename K1, typename RetT, typename... A0s, typename... A1s>
inline RetT DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::apply(
    K1 &&k, RetT (*fn)(std::pair<const K, V> &, A0s...), A1s &&... args) {
  auto hash = Hash();
  auto key_hash = hash(std::forward<K1>(k));
  auto shard_idx = get_shard_idx(key_hash);
  auto &shard = shards_[shard_idx];
  return shard.__run(
      +[](HashTableShard &shard, K k, uint64_t key_hash,
          RetT (*fn)(std::pair<const K, V> &, A0s...), A0s... args) {
        return shard.apply_with_hash(std::forward<K1>(k), key_hash, fn,
                                     std::move(args)...);
      },
      std::forward<K1>(k), key_hash, fn, std::forward<A1s>(args)...);
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
template <typename K1>
inline Future<std::optional<V>>
DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::get_async(K1 &&k) {
  return nu::async([&, k] { return get(std::move(k)); });
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
template <typename K1, typename V1>
inline Future<void>
DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::put_async(K1 &&k,
                                                                  V1 &&v) {
  return nu::async([&, k, v] { return put(std::move(k), std::move(v)); });
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
template <typename K1>
inline Future<bool>
DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::remove_async(K1 &&k) {
  return nu::async([&, k] { return remove(std::move(k)); });
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
template <typename K1, typename RetT, typename... A0s, typename... A1s>
inline Future<RetT>
DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::apply_async(
    K1 &&k, RetT (*fn)(std::pair<const K, V> &, A0s...), A1s &&... args) {
  return nu::async([&, k, fn, ... args = std::forward<A1s>(args)]() mutable {
    return apply(std::move(k), fn, std::move(args)...);
  });
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
std::vector<std::pair<K, V>>
DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::get_all_pairs() {
  std::vector<std::pair<K, V>> vec;
  std::vector<Future<std::vector<std::pair<K, V>>>> futures;
  for (uint32_t i = 0; i < num_shards_; i++) {
    futures.emplace_back(shards_[i].__run_async(
        +[](HashTableShard &shard) { return shard.get_all_pairs(); }));
  }
  for (auto &future : futures) {
    auto &vec_shard = future.get();
    vec.insert(vec.end(), vec_shard.begin(), vec_shard.end());
  }
  return vec;
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
template <typename RetT, typename... A0s, typename... A1s>
RetT DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::associative_reduce(
    bool clear, RetT init_val,
    void (*reduce_fn)(RetT &, std::pair<const K, V> &, A0s...),
    void (*merge_fn)(RetT &, RetT &, A0s...), A1s &&... args) {
  RetT reduced_val(std::move(init_val));
  std::vector<Future<RetT>> futures;

  for (uint32_t i = 0; i < num_shards_; i++) {
    futures.emplace_back(shards_[i].__run_async(
        &HashTableShard::template associative_reduce<RetT>, clear, reduced_val,
        reduce_fn, std::forward<A1s>(args)...));
  }

  for (auto &future : futures) {
    merge_fn(reduced_val, future.get(), std::forward<A1s>(args)...);
  }

  return reduced_val;
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
template <typename RetT, typename... A0s, typename... A1s>
std::vector<RetT>
DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::associative_reduce(
    bool clear, RetT init_val,
    void (*reduce_fn)(RetT &, std::pair<const K, V> &, A0s...),
    A1s &&... args) {
  RetT reduced_val(std::move(init_val));
  std::vector<Future<RetT>> futures;

  for (uint32_t i = 0; i < num_shards_; i++) {
    futures.emplace_back(shards_[i].__run_async(
        &HashTableShard::template associative_reduce<RetT>, clear, reduced_val,
        reduce_fn, std::forward<A1s>(args)...));
  }

  std::vector<RetT> all_reduced_vals;
  all_reduced_vals.reserve(futures.size());
  for (auto &future : futures) {
    all_reduced_vals.emplace_back(std::move(future.get()));
  }

  return all_reduced_vals;
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
template <class Archive>
inline void DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>::serialize(
    Archive &ar) {
  ar(power_num_shards_);
  ar(num_shards_);
  ar(ref_cnter_);
  ar(shards_);
}

template <typename K, typename V, typename Hash, typename KeyEqual,
          uint64_t NumBuckets>
DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets> make_dis_hash_table(
    uint32_t power_num_shards, bool pinned) {
  using TableType = DistributedHashTable<K, V, Hash, KeyEqual, NumBuckets>;
  TableType table;
  table.power_num_shards_ = power_num_shards;
  table.num_shards_ = (1 << power_num_shards);
  table.ref_cnter_ = make_proclet<typename TableType::RefCnter>();
  table.shards_ = table.ref_cnter_.run(
      +[](TableType::RefCnter &self, uint32_t num_shards, bool pinned) {
        std::vector<WeakProclet<typename TableType::HashTableShard>>
            weak_shards;
        for (uint32_t i = 0; i < num_shards; i++) {
          self.shards.emplace_back(
              make_proclet<typename TableType::HashTableShard>(pinned));
          weak_shards.emplace_back(self.shards.back().get_weak());
        }
        return weak_shards;
      },
      table.num_shards_, pinned);
  return table;
}

}  // namespace nu
