#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <stack>
#include <string>
#include <string_view>
#include <type_traits>

#include "nu/commons.hpp"
#include "nu/utils/caladan.hpp"
#include "nu/utils/spin_lock.hpp"

namespace nu {

struct PtrHeader {
  uint64_t size : 56;
  uint64_t core_id : 8;
  SlabId_t slab_id;
};

// It's the constraint placed by GCC for enabling vectorization optimizations.
constexpr static uint32_t kAlignment = 16;
static_assert(sizeof(PtrHeader) % kAlignment == 0);

class SlabAllocator {
 public:
  constexpr static uint64_t kMaxSlabClassShift = 35;  // 32 GB.
  constexpr static uint64_t kMinSlabClassShift = 5;   // 32 B.
  constexpr static uint64_t kMaxNumCacheEntries = 32;
  constexpr static uint64_t kCacheSizeCutoff = 1024;
  static_assert((1 << kMinSlabClassShift) % kAlignment == 0);

  SlabAllocator();
  SlabAllocator(SlabId_t slab_id, void *buf, size_t len,
                bool aggressive_caching = false);
  ~SlabAllocator();
  void init(SlabId_t slab_id, void *buf, size_t len,
            bool aggressive_caching = false);
  void *allocate(size_t size);
  void *yield(size_t size);
  void *get_base() const;
  size_t get_cur_usage() const;
  size_t get_usage() const;
  size_t get_remaining() const;
  SlabId_t get_id();
  static SlabAllocator *get_slab_by_id();
  static void free(const void *ptr);
  static void *reallocate(const void *ptr, size_t size);
  static void register_slab_by_id(SlabAllocator *slab, SlabId_t slab_id);
  static void deregister_slab_by_id(SlabId_t slab_id);

 private:
  class FreePtrsLinkedList {
   public:
    void push(void *ptr);
    void *pop();
    uint64_t size();

   private:
    constexpr static uint32_t kBatchSize =
        ((1 << kMinSlabClassShift) + sizeof(PtrHeader)) / sizeof(void *);
    struct Batch {
      void *p[kBatchSize];
    };

    Batch *head_ = nullptr;
    uint64_t size_ = 0;
  };

  struct alignas(kCacheLineBytes) CoreCache {
    FreePtrsLinkedList lists[kMaxSlabClassShift];
  };

  struct alignas(kCacheLineBytes) TransferredCoreCache {
    SpinLock spin;
    FreePtrsLinkedList lists[kMaxSlabClassShift];
  };

  static SlabAllocator *slabs_[get_max_slab_id() + 1];
  SlabId_t slab_id_;
  bool aggressive_caching_;
  const uint8_t *start_;
  const uint8_t *end_;
  uint8_t *cur_;
  FreePtrsLinkedList slab_lists_[kMaxSlabClassShift];
  uint64_t global_free_bytes_;
  CoreCache cache_lists_[kNumCores];
  TransferredCoreCache transferred_caches_[kNumCores];
  SpinLock spin_;

  uint32_t get_slab_shift(uint64_t data_size);
  uint64_t get_slab_size(uint32_t slab_shift);
  void *__allocate(size_t size);
  static void __free(const void *ptr);
  void __do_free(const Caladan::PreemptGuard &g, PtrHeader *ptr,
                 uint32_t slab_shift);
  void free_to_cache_list(const Caladan::PreemptGuard &g, PtrHeader *hdr,
                          uint32_t slab_shift);
  void free_to_transferred_cache_list(PtrHeader *hdr, uint32_t slab_shift);
  void drain_transferred_cache(const Caladan::PreemptGuard &g,
                               uint32_t slab_shift);
};
}  // namespace nu

#include "nu/impl/slab.ipp"
