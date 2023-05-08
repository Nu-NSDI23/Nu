#include "nu/rem_raw_ptr.hpp"
#include "nu/rem_shared_ptr.hpp"
#include "nu/rem_unique_ptr.hpp"
#include "nu/utils/promise.hpp"
#include "nu/utils/scoped_lock.hpp"

namespace nu {

template <typename T, typename... As>
inline RemRawPtr<T> DistributedMemPool::Heap::allocate_raw(As... args) {
  return RemRawPtr(new T(std::move(args)...));
}

template <typename T, typename... As>
inline RemUniquePtr<T> DistributedMemPool::Heap::allocate_unique(As... args) {
  return make_rem_unique<T>(std::move(args)...);
}

template <typename T, typename... As>
inline RemSharedPtr<T> DistributedMemPool::Heap::allocate_shared(As... args) {
  return make_rem_shared<T>(std::move(args)...);
}

template <typename T>
inline void DistributedMemPool::Heap::free_raw(T *raw_ptr) {
  delete raw_ptr;
}

inline bool DistributedMemPool::Heap::has_space_for(uint32_t size) {
  auto buf = new uint8_t[size];
  if (!buf) {
    return false;
  }
  delete[] buf;
  return true;
}

inline DistributedMemPool::Shard::Shard() {}

inline DistributedMemPool::Shard::Shard(Proclet<Heap> &&proclet)
    : proclet(std::move(proclet)) {}

inline DistributedMemPool::Shard::Shard(Shard &&o)
    : failed_alloc_size(o.failed_alloc_size), proclet(std::move(o.proclet)) {}

inline DistributedMemPool::Shard &DistributedMemPool::Shard::operator=(
    Shard &&o) {
  failed_alloc_size = o.failed_alloc_size;
  proclet = std::move(o.proclet);
  return *this;
}

inline DistributedMemPool::DistributedMemPool()
    : last_probing_us_(microtime()), probing_active_(false), done_(false) {}

inline DistributedMemPool::DistributedMemPool(DistributedMemPool &&o)
    : last_probing_us_(microtime()), probing_active_(false), done_(false) {
  *this = std::move(o);
}

inline DistributedMemPool &DistributedMemPool::operator=(
    DistributedMemPool &&o) {
  o.halt_probing();
  for (uint32_t i = 0; i < kNumCores; i++) {
    local_free_shards_[i] = std::move(o.local_free_shards_[i]);
  }
  global_free_shards_ = std::move(o.global_free_shards_);
  global_full_shards_ = std::move(o.global_full_shards_);
  last_probing_us_ = o.last_probing_us_;
  return *this;
}

inline DistributedMemPool::~DistributedMemPool() { halt_probing(); }

inline void DistributedMemPool::halt_probing() {
  {
    ScopedLock<Mutex> scope(&global_mutex_);
    done_ = true;
  }
  if (probing_thread_) {
    probing_thread_->join();
  }
}

template <typename T, typename... As>
inline RemRawPtr<T> DistributedMemPool::allocate_raw(As &&... args) {
  return __allocate<T>(&Heap::allocate_raw<T, std::decay_t<As>...>,
                       std::forward<As>(args)...);
}

template <typename T, typename... As>
inline Future<RemRawPtr<T>> DistributedMemPool::allocate_raw_async(
    As &&... args) {
  return nu::async([&, ... args = std::forward<As>(args)] {
    return allocate_raw(std::forward<As>(args)...);
  });
}

template <typename T>
inline void DistributedMemPool::free_raw(const RemRawPtr<T> &ptr) {
  auto &proclet = const_cast<WeakProclet<ErasedType> &>(ptr.proclet_);
  proclet.__run(
      +[](ErasedType &raw_obj, T *raw_ptr) {
        reinterpret_cast<Heap &>(raw_obj).free_raw(raw_ptr);
      },
      const_cast<RemRawPtr<T> &>(ptr).get());
  check_probing();
}

template <typename T>
inline Future<void> DistributedMemPool::free_raw_async(
    const RemRawPtr<T> &ptr) {
  return nu::async([&] { free_raw(ptr); });
}

inline void DistributedMemPool::check_probing() {
  auto cur_us = microtime();
  if (unlikely(cur_us >
               last_probing_us_ + kFullShardProbingIntervalMs * 1000)) {
    __check_probing(cur_us);
  }
}

template <class Archive>
void DistributedMemPool::save_move(Archive &ar) {
  halt_probing();
  for (auto &local_free_shard : local_free_shards_) {
    ar(std::move(local_free_shard));
  }
  ar(std::move(global_free_shards_), std::move(global_full_shards_));
}

template <class Archive>
void DistributedMemPool::load(Archive &ar) {
  for (auto &local_free_shard : local_free_shards_) {
    ar(local_free_shard);
  }
  ar(global_free_shards_, global_full_shards_);
  last_probing_us_ = microtime();
  probing_active_ = false;
  done_ = false;
}

template <typename T, typename... As>
inline RemUniquePtr<T> DistributedMemPool::allocate_unique(As &&... args) {
  return __allocate<T>(&Heap::allocate_unique<T, std::decay_t<As>...>,
                       std::forward<As>(args)...);
}

template <typename T, typename... As>
inline Future<RemUniquePtr<T>> DistributedMemPool::allocate_unique_async(
    As &&... args) {
  return nu::async([&, ... args = std::forward<As>(args)] {
    return allocate_unique(std::forward<As>(args)...);
  });
}

template <typename T, typename... As>
inline RemSharedPtr<T> DistributedMemPool::allocate_shared(As &&... args) {
  return __allocate<T>(&Heap::allocate_shared<T, std::decay_t<As>...>,
                       std::forward<As>(args)...);
}

template <typename T, typename... As>
inline Future<RemSharedPtr<T>> DistributedMemPool::allocate_shared_async(
    As &&... args) {
  return nu::async([&, ... args = std::forward<As>(args)] {
    return allocate_shared(std::forward<As>(args)...);
  });
}

template <typename T, typename AllocFn, typename... As>
auto DistributedMemPool::__allocate(AllocFn &&alloc_fn, As &&... args) {
retry:
  auto cpu = get_cpu();
  auto &free_shard_optional = local_free_shards_[cpu].shard;

  if (likely(free_shard_optional)) {
    auto &free_shard = *free_shard_optional;

    // The free shard has been marked as full.
    if (unlikely(ACCESS_ONCE(free_shard.failed_alloc_size))) {
      __handle_local_free_shard_full();
      goto retry;
    }

    auto id = free_shard.proclet.get_id();
    put_cpu();
    // Try to allocate.
    auto ptr = free_shard.proclet.__run(alloc_fn, std::forward<As>(args)...);
    // The shard turns out to be full, add a mark.
    if (unlikely(!ptr)) {
      // For the performance consideration, here we intentionally allow race
      // conditions which may cause the free shard to be marked as full. It's
      // fine since the mis-classification will soon be rectified by the probing
      // thread.
      if (free_shard.proclet.get_id() == id) {
        ACCESS_ONCE(free_shard.failed_alloc_size) = sizeof(T);
      }
      goto retry;
    }

    // Allocation succeeds, done.
    return ptr;
  } else {
    // The local cache is empty.
    __handle_no_local_free_shard();
    goto retry;
  }
}

}  // namespace nu
