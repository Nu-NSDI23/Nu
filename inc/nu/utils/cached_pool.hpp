#pragma once

#include <memory>
#include <stack>

#include "nu/commons.hpp"
#include "nu/utils/spin_lock.hpp"

namespace nu {
template <typename T, typename Allocator = std::allocator<T>>
class CachedPool {
 public:
  CachedPool(const std::function<T *(void)> &new_fn,
             const std::function<void(T *)> &delete_fn,
             uint32_t per_core_cache_size);
  CachedPool(std::function<T *(void)> &&new_fn,
             std::function<void(T *)> &&delete_fn,
             uint32_t per_core_cache_size);
  ~CachedPool();
  T *get();
  void put(T *item);
  void reserve(uint32_t num);

 private:
  using RebindAlloc =
      std::allocator_traits<Allocator>::template rebind_alloc<T *>;
  using ItemStack = std::stack<T *, std::vector<T *, RebindAlloc>>;

  struct alignas(kCacheLineBytes) LocalCache {
    uint32_t num;
    T **items;
  };

  std::function<T *(void)> new_fn_;
  std::function<void(T *)> delete_fn_;
  uint32_t per_core_cache_size_;
  LocalCache locals_[kNumCores];
  ItemStack global_;
  SpinLock global_spin_;

  void init(uint32_t per_core_cache_size);
  T *get_slow_path(LocalCache *local);
  void put_slow_path(LocalCache *local);
};
}  // namespace nu

#include "nu/impl/cached_pool.ipp"
