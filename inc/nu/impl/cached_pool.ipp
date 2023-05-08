#include <memory>

#include "nu/utils/scoped_lock.hpp"

namespace nu {

template <typename T, typename Allocator>
inline CachedPool<T, Allocator>::CachedPool(
    const std::function<T *(void)> &new_fn,
    const std::function<void(T *)> &delete_fn, uint32_t per_core_cache_size)
    : new_fn_(new_fn),
      delete_fn_(delete_fn),
      per_core_cache_size_(per_core_cache_size) {
  init(per_core_cache_size);
}

template <typename T, typename Allocator>
inline CachedPool<T, Allocator>::CachedPool(
    std::function<T *(void)> &&new_fn, std::function<void(T *)> &&delete_fn,
    uint32_t per_core_cache_size)
    : new_fn_(std::move(new_fn)),
      delete_fn_(std::move(delete_fn)),
      per_core_cache_size_(per_core_cache_size) {
  init(per_core_cache_size);
}

template <typename T, typename Allocator>
inline void CachedPool<T, Allocator>::init(uint32_t per_core_cache_size) {
  RebindAlloc alloc;
  for (uint32_t i = 0; i < kNumCores; i++) {
    locals_[i].num = 0;
    locals_[i].items = alloc.allocate(per_core_cache_size + 1);
  }
}

template <typename T, typename Allocator>
CachedPool<T, Allocator>::~CachedPool() {
  RebindAlloc alloc;
  for (size_t i = 0; i < kNumCores; i++) {
    auto &local = locals_[i];
    for (uint32_t j = 0; j < local.num; j++) {
      delete_fn_(local.items[j]);
    }
    alloc.deallocate(local.items, per_core_cache_size_ + 1);
  }
  while (!global_.empty()) {
    auto *item = global_.top();
    global_.pop();
    delete_fn_(item);
  }
}

template <typename T, typename Allocator>
T *CachedPool<T, Allocator>::get_slow_path(LocalCache *local) {
  {
    ScopedLock<SpinLock> lock(&global_spin_);
    while (!global_.empty() && local->num < per_core_cache_size_) {
      local->items[local->num++] = global_.top();
      global_.pop();
    }
  }
  while (local->num < per_core_cache_size_) {
    local->items[local->num++] = new_fn_();
  }
  return local->items[--local->num];
}

template <typename T, typename Allocator>
inline T *CachedPool<T, Allocator>::get() {
  int cpu = get_cpu();
  auto &local = locals_[cpu];
  T *item;

  if (likely(local.num)) {
    item = local.items[--local.num];
  } else {
    item = get_slow_path(&local);
  }
  put_cpu();

  return item;
}

template <typename T, typename Allocator>
void CachedPool<T, Allocator>::put_slow_path(LocalCache *local) {
  ScopedLock<SpinLock> lock(&global_spin_);
  while (local->num > per_core_cache_size_ / 2 && local->num > 1) {
    global_.push(local->items[--local->num]);
  }
}

template <typename T, typename Allocator>
inline void CachedPool<T, Allocator>::put(T *item) {
  int cpu = get_cpu();
  auto &local = locals_[cpu];
  local.items[local.num++] = item;

  if (unlikely(local.num > per_core_cache_size_)) {
    put_slow_path(&local);
  }
  put_cpu();
}

template <typename T, typename Allocator>
void CachedPool<T, Allocator>::reserve(uint32_t num) {
  auto items = std::make_unique<T *[]>(num);
  for (uint32_t i = 0; i < num; i++) {
    items[i] = new_fn_();
  }

  ScopedLock<SpinLock> lock(&global_spin_);
  for (uint32_t i = 0; i < num; i++) {
    global_.push(items[i]);
  }
}

}  // namespace nu
