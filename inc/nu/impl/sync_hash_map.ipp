#include "nu/cereal.hpp"

namespace nu {

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
inline SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator,
                   Lock>::SyncHashMap(const SyncHashMap &o) noexcept
    : SyncHashMap() {
  SyncHashMap::operator=(o);
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
inline SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator, Lock>
    &SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator, Lock>::operator=(
        const SyncHashMap &o) noexcept {
  Allocator allocator;
  BucketNodeAllocator bucket_node_allocator;

  for (size_t i = 0; i < NBuckets; i++) {
    auto *bucket_node = &bucket_heads_[i].node;
    decltype(bucket_node) prev_bucket_node;
    auto *o_bucket_node = &o.bucket_heads_[i].node;
    if (o_bucket_node->pair) {
      do {
        bucket_node->key_hash = o_bucket_node->key_hash;

        auto *pair = reinterpret_cast<std::pair<K, V> *>(bucket_node->pair);
        auto *o_pair = reinterpret_cast<Pair *>(o_bucket_node->pair);
        if (pair) {
          *pair = *o_pair;
        } else {
          bucket_node->pair = allocator.allocate(1);
          new (bucket_node->pair) Pair(*o_pair);
        }

        if (!bucket_node->next && o_bucket_node->next) {
          bucket_node->next = bucket_node_allocator.allocate(1);
          new (bucket_node->next) BucketNode();
        }

        prev_bucket_node = bucket_node;
        bucket_node = bucket_node->next;
        o_bucket_node = o_bucket_node->next;
      } while (o_bucket_node);

      if (bucket_node) {
        prev_bucket_node->next = nullptr;
        do {
          auto *pair = reinterpret_cast<Pair *>(bucket_node->pair);
          allocator.deallocate(pair, 1);
          prev_bucket_node = bucket_node;
          bucket_node = bucket_node->next;
          bucket_node_allocator.deallocate(prev_bucket_node, 1);
        } while (bucket_node);
      }
    }
  }

  return *this;
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
inline SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator,
                   Lock>::SyncHashMap(SyncHashMap &&o) noexcept
    : bucket_heads_(o.bucket_heads_) {
  o.bucket_heads_ = nullptr;
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
inline SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator, Lock>
    &SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator, Lock>::operator=(
        SyncHashMap &&o) noexcept {
  bucket_heads_ = o.bucket_heads_;
  o.bucket_heads_ = nullptr;
  return *this;
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
inline SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator,
                   Lock>::SyncHashMap() {
  BucketHeadAllocator bucket_head_allocator;
  bucket_heads_ = bucket_head_allocator.allocate(NBuckets);
  for (size_t i = 0; i < NBuckets; i++) {
    auto &bucket_head = bucket_heads_[i];
    new (&bucket_head) BucketHead();
    bucket_head.node.pair = bucket_head.node.next = nullptr;
  }
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
inline SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator,
                   Lock>::~SyncHashMap() {
  if (bucket_heads_) {
    BucketHeadAllocator bucket_head_allocator;
    for (size_t i = 0; i < NBuckets; i++) {
      auto &bucket_head = bucket_heads_[i];
      std::destroy_at(&bucket_head);
    }
    bucket_head_allocator.deallocate(bucket_heads_, NBuckets);
  }
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
template <typename K1>
inline V *SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator, Lock>::get(
    K1 &&k) {
  auto hasher = Hash();
  auto key_hash = hasher(k);
  return get_with_hash(std::forward<K1>(k), key_hash);
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
template <typename K1>
std::optional<V>
SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator, Lock>::get_copy(K1 &&k) {
  auto hasher = Hash();
  auto key_hash = hasher(k);
  return get_copy_with_hash(std::forward<K1>(k), key_hash);
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
template <typename K1>
V *SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator, Lock>::get_with_hash(
    K1 &&k, uint64_t key_hash) {
  auto equaler = KeyEqual();
  auto bucket_idx = key_hash % NBuckets;
  auto &bucket_head = bucket_heads_[bucket_idx];
  auto *bucket_node = &bucket_head.node;
  auto &lock = bucket_head.lock;
  lock.lock();

  while (bucket_node && bucket_node->pair) {
    if (key_hash == bucket_node->key_hash) {
      auto *pair = reinterpret_cast<Pair *>(bucket_node->pair);
      if (equaler(k, pair->first)) {
        auto ret = &pair->second;
        lock.unlock();
        return ret;
      }
    }
    bucket_node = bucket_node->next;
  }
  lock.unlock();
  return nullptr;
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
template <typename K1>
std::optional<V> SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator,
                             Lock>::get_copy_with_hash(K1 &&k,
                                                       uint64_t key_hash) {
  auto equaler = KeyEqual();
  auto bucket_idx = key_hash % NBuckets;
  auto &bucket_head = bucket_heads_[bucket_idx];
  auto *bucket_node = &bucket_head.node;
  auto &lock = bucket_head.lock;
  lock.lock();

  while (bucket_node && bucket_node->pair) {
    if (key_hash == bucket_node->key_hash) {
      auto *pair = reinterpret_cast<Pair *>(bucket_node->pair);
      if (equaler(k, pair->first)) {
        auto ret = std::make_optional(pair->second);
        lock.unlock();
        return ret;
      }
    }
    bucket_node = bucket_node->next;
  }
  lock.unlock();
  return std::nullopt;
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
template <typename K1, typename V1>
inline void SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator, Lock>::put(
    K1 k, V1 v) {
  auto hasher = Hash();
  auto key_hash = hasher(k);
  put_with_hash(std::move(k), std::move(v), key_hash);
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
template <typename K1, typename V1>
void SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator,
                 Lock>::put_with_hash(K1 k, V1 v, uint64_t key_hash) {
  auto equaler = KeyEqual();
  auto bucket_idx = key_hash % NBuckets;
  auto &bucket_head = bucket_heads_[bucket_idx];
  auto *bucket_node = &bucket_head.node;
  auto &lock = bucket_head.lock;
  BucketNode **prev_next = nullptr;
  lock.lock();

  while (bucket_node && bucket_node->pair) {
    if (key_hash == bucket_node->key_hash) {
      auto *pair = reinterpret_cast<Pair *>(bucket_node->pair);
      if (equaler(k, pair->first)) {
        pair->second = std::forward<V1>(v);
        lock.unlock();
        return;
      }
    }
    prev_next = &bucket_node->next;
    bucket_node = bucket_node->next;
  }

  auto allocator = Allocator();
  auto *pair = allocator.allocate(1);
  new (pair) Pair(k, v);

  if (!prev_next) {
    bucket_node->key_hash = key_hash;
    bucket_node->pair = pair;
  } else {
    BucketNodeAllocator bucket_node_allocator;
    auto *new_bucket_node = bucket_node_allocator.allocate(1);
    new (new_bucket_node) BucketNode();
    new_bucket_node->key_hash = key_hash;
    new_bucket_node->pair = pair;
    new_bucket_node->next = nullptr;
    *prev_next = new_bucket_node;
  }
  lock.unlock();
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
template <typename K1, typename... Args>
inline bool SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator,
                        Lock>::try_emplace(K1 k, Args... args) {
  auto hasher = Hash();
  auto key_hash = hasher(k);
  return try_emplace_with_hash(k, key_hash, std::move(args)...);
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
template <typename K1, typename... Args>
bool SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator,
                 Lock>::try_emplace_with_hash(K1 k, uint64_t key_hash,
                                              Args... args) {
  auto equaler = KeyEqual();
  auto bucket_idx = key_hash % NBuckets;
  auto &bucket_head = bucket_heads_[bucket_idx];
  auto *bucket_node = &bucket_head.node;
  auto &lock = bucket_head.lock;
  BucketNode **prev_next = nullptr;
  lock.lock();

  while (bucket_node && bucket_node->pair) {
    if (key_hash == bucket_node->key_hash) {
      auto *pair = reinterpret_cast<Pair *>(bucket_node->pair);
      if (equaler(k, pair->first)) {
        lock.unlock();
        return false;
      }
    }
    prev_next = &bucket_node->next;
    bucket_node = bucket_node->next;
  }

  auto allocator = Allocator();
  auto *pair = allocator.allocate(1);
  new (pair) Pair(k, V1(args)...);

  if (!prev_next) {
    bucket_node->key_hash = key_hash;
    bucket_node->pair = pair;
  } else {
    BucketNodeAllocator bucket_node_allocator;
    auto *new_bucket_node = bucket_node_allocator.allocate(1);
    new (new_bucket_node) BucketNode();
    new_bucket_node->key_hash = key_hash;
    new_bucket_node->pair = pair;
    new_bucket_node->next = nullptr;
    *prev_next = new_bucket_node;
  }
  lock.unlock();
  return true;
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
template <typename K1>
inline bool
SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator, Lock>::remove(K1 &&k) {
  auto hasher = Hash();
  auto key_hash = hasher(k);
  return remove_with_hash(std::forward<K1>(k), key_hash);
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
template <typename K1>
bool SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator,
                 Lock>::remove_with_hash(K1 &&k, uint64_t key_hash) {
  auto equaler = KeyEqual();
  auto bucket_idx = key_hash % NBuckets;
  auto &bucket_head = bucket_heads_[bucket_idx];
  auto *bucket_node = &bucket_head.node;
  auto &lock = bucket_head.lock;
  BucketNode **prev_next = nullptr;
  BucketNodeAllocator bucket_node_allocator;
  lock.lock();

  while (bucket_node && bucket_node->pair) {
    if (key_hash == bucket_node->key_hash) {
      auto *pair = reinterpret_cast<Pair *>(bucket_node->pair);
      if (equaler(k, pair->first)) {
        if (!prev_next) {
          if (!bucket_node->next) {
            bucket_node->pair = nullptr;
          } else {
            auto *next = bucket_node->next;
            *bucket_node = *next;
            bucket_node_allocator.deallocate(next, 1);
          }
        } else {
          *prev_next = bucket_node->next;
          bucket_node_allocator.deallocate(bucket_node, 1);
        }
        lock.unlock();
        auto allocator = Allocator();
        std::destroy_at(pair);
        allocator.deallocate(pair, 1);
        return true;
      }
    }
    prev_next = &bucket_node->next;
    bucket_node = bucket_node->next;
  }
  lock.unlock();
  return false;
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
template <typename K1, typename RetT, typename... A0s, typename... A1s>
inline RetT SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator, Lock>::apply(
    K1 &&k, RetT (*fn)(std::pair<const K, V> &, A0s...), A1s &&...args) {
  auto hasher = Hash();
  auto key_hash = hasher(k);
  return apply_with_hash(std::forward<K1>(k), key_hash, fn,
                         std::forward<A1s>(args)...);
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
template <typename K1, typename RetT, typename... A0s, typename... A1s>
RetT SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator,
                 Lock>::apply_with_hash(K1 &&k, uint64_t key_hash,
                                        RetT (*fn)(std::pair<const K, V> &,
                                                   A0s...),
                                        A1s &&...args) {
  auto equaler = KeyEqual();
  auto bucket_idx = key_hash % NBuckets;
  auto &bucket_head = bucket_heads_[bucket_idx];
  auto *bucket_node = &bucket_head.node;
  auto &lock = bucket_head.lock;
  BucketNode **prev_next = nullptr;
  Pair *pair;
  Allocator allocator;

  lock.lock();
  while (bucket_node && bucket_node->pair) {
    if (key_hash == bucket_node->key_hash) {
      pair = reinterpret_cast<Pair *>(bucket_node->pair);
      if (equaler(k, pair->first)) {
        goto apply_fn;
      }
    }
    prev_next = &bucket_node->next;
    bucket_node = bucket_node->next;
  }

  pair = allocator.allocate(1);
  new (pair) Pair(std::forward<K1>(k), V());

  if (!prev_next) {
    bucket_node->key_hash = key_hash;
    bucket_node->pair = pair;
  } else {
    BucketNodeAllocator bucket_node_allocator;
    auto *new_bucket_node = bucket_node_allocator.allocate(1);
    new (new_bucket_node) BucketNode();
    new_bucket_node->key_hash = key_hash;
    new_bucket_node->pair = pair;
    new_bucket_node->next = nullptr;
    *prev_next = new_bucket_node;
  }

apply_fn:
  if constexpr (!std::is_same<RetT, void>::value) {
    auto ret = fn(*pair, std::forward<A1s>(args)...);
    lock.unlock();
    return ret;
  } else {
    fn(*pair, std::forward<A1s>(args)...);
    lock.unlock();
  }
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
template <typename RetT, typename... A0s, typename... A1s>
RetT SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator,
                 Lock>::associative_reduce(bool clear, RetT init_val,
                                           void (*reduce_fn)(
                                               RetT &, std::pair<const K, V> &,
                                               A0s...),
                                           A1s &&...args) {
  Allocator allocator;
  BucketNodeAllocator bucket_node_allocator;

  RetT reduced_val(std::move(init_val));
  for (size_t i = 0; i < NBuckets; i++) {
    auto &bucket_head = bucket_heads_[i];
    auto *bucket_node = &bucket_head.node;
    auto &lock = bucket_head.lock;
    if (bucket_head.node.pair) {
      lock.lock();
      bool head = true;
      while (bucket_node && bucket_node->pair) {
        auto *pair = reinterpret_cast<Pair *>(bucket_node->pair);
        reduce_fn(reduced_val, *pair, std::forward<A1s>(args)...);
        auto *next = bucket_node->next;

        if (clear) {
          std::destroy_at(pair);
          allocator.deallocate(pair, 1);
          if (head) {
            head = false;
            bucket_node->pair = bucket_node->next = nullptr;
          } else {
            bucket_node_allocator.deallocate(bucket_node, 1);
          }
        }

        bucket_node = next;
      }
      lock.unlock();
    }
  }
  return reduced_val;
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
inline std::vector<std::pair<K, V>>
SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator, Lock>::get_all_pairs() {
  return associative_reduce(
      /* clear = */ false, /* init_val = */ std::vector<std::pair<K, V>>(),
      /* reduce_fn = */
      +[](std::vector<std::pair<K, V>> &reduced_val,
          std::pair<const K, V> &pair) { reduced_val.push_back(pair); });
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
template <typename K1>
std::optional<V> SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator,
                             Lock>::get_and_remove(K1 &&k) {
  auto hasher = Hash();
  auto key_hash = hasher(k);
  auto equaler = KeyEqual();
  auto bucket_idx = key_hash % NBuckets;
  auto &bucket_head = bucket_heads_[bucket_idx];
  auto *bucket_node = &bucket_head.node;
  auto &lock = bucket_head.lock;
  BucketNode **prev_next = nullptr;
  BucketNodeAllocator bucket_node_allocator;
  lock.lock();

  while (bucket_node && bucket_node->pair) {
    if (key_hash == bucket_node->key_hash) {
      auto *pair = reinterpret_cast<Pair *>(bucket_node->pair);
      if (equaler(k, pair->first)) {
        if (!prev_next) {
          if (!bucket_node->next) {
            bucket_node->pair = nullptr;
          } else {
            auto *next = bucket_node->next;
            *bucket_node = *next;
            bucket_node_allocator.deallocate(next, 1);
          }
        } else {
          *prev_next = bucket_node->next;
          bucket_node_allocator.deallocate(bucket_node, 1);
        }
        lock.unlock();
        auto allocator = Allocator();
        auto ret = std::make_optional(std::move(pair->second));
        std::destroy_at(pair);
        allocator.deallocate(pair, 1);
        return ret;
      }
    }
    prev_next = &bucket_node->next;
    bucket_node = bucket_node->next;
  }
  lock.unlock();
  return std::nullopt;
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
inline std::vector<std::pair<uint64_t, K>>
SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator,
            Lock>::get_all_hashes_and_keys() {
  std::vector<std::pair<uint64_t, K>> hashes_and_keys;

  for (size_t i = 0; i < NBuckets; i++) {
    auto *bucket_node = &bucket_heads_[i].node;
    if (bucket_node->pair) {
      do {
        hashes_and_keys.emplace_back(
            bucket_node->key_hash,
            reinterpret_cast<Pair *>(bucket_node->pair)->first);
        bucket_node = bucket_node->next;
      } while (bucket_node);
    }
  }
  return hashes_and_keys;
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
template <class Archive>
inline void SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator, Lock>::save(
    Archive &ar) const {
  std::vector<BucketNode *> nodes;

  for (size_t i = 0; i < NBuckets; i++) {
    auto *bucket_node = &bucket_heads_[i].node;
    nodes.clear();
    if (bucket_node->pair) {
      do {
        nodes.push_back(bucket_node);
        bucket_node = bucket_node->next;
      } while (bucket_node);
    }
    ar(nodes.size());
    for (auto *node : nodes) {
      auto *pair = reinterpret_cast<Pair *>(node->pair);
      ar(node->key_hash, pair->first, pair->second);
    }
  }
}

template <size_t NBuckets, typename K, typename V, typename Hash,
          typename KeyEqual, typename Allocator, typename Lock>
template <class Archive>
inline void SyncHashMap<NBuckets, K, V, Hash, KeyEqual, Allocator, Lock>::load(
    Archive &ar) {
  Allocator allocator;
  BucketNodeAllocator bucket_node_allocator;

  for (size_t i = 0; i < NBuckets; i++) {
    size_t num_nodes;
    ar(num_nodes);
    auto *bucket_node = &bucket_heads_[i].node;
    for (size_t j = 0; j < num_nodes; j++) {
      bucket_node->pair = allocator.allocate(1);
      new (bucket_node->pair) Pair();
      auto *pair = reinterpret_cast<std::pair<K, V> *>(bucket_node->pair);
      ar(bucket_node->key_hash, pair->first, pair->second);
      if (j != num_nodes - 1) {
        bucket_node->next = bucket_node_allocator.allocate(1);
        new (bucket_node->next) BucketNode();
        bucket_node = bucket_node->next;
      }
    }
  }
}

}  // namespace nu
