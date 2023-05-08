#include <algorithm>

#include "nu/utils/slab.hpp"
#include "nu/utils/scoped_lock.hpp"

namespace nu {

SlabAllocator *SlabAllocator::slabs_[get_max_slab_id() + 1];

void *SlabAllocator::FreePtrsLinkedList::pop() {
  size_--;
  BUG_ON(!head_);
  for (uint32_t i = kBatchSize - 1; i > 0; i--) {
    if (head_->p[i]) {
      auto ret = head_->p[i];
      head_->p[i] = nullptr;
      return ret;
    }
  }
  auto ret = head_;
  head_ = reinterpret_cast<Batch *>(head_->p[0]);
  return ret;
}

void SlabAllocator::FreePtrsLinkedList::push(void *ptr) {
  size_++;
  if (unlikely(!head_)) {
    head_ = reinterpret_cast<Batch *>(ptr);
    std::fill(std::begin(head_->p), std::end(head_->p), nullptr);
    return;
  }

  for (uint32_t i = 1; i < kBatchSize; i++) {
    if (!head_->p[i]) {
      head_->p[i] = ptr;
      return;
    }
  }
  auto old_head = head_;
  head_ = reinterpret_cast<Batch *>(ptr);
  head_->p[0] = old_head;
  std::fill(std::begin(head_->p) + 1, std::end(head_->p), nullptr);
}

// TODO: should be dynamic.
inline uint32_t get_max_num_cache_entries(bool aggressive_caching,
                                          uint32_t slab_shift) {
  switch (slab_shift) {
    case 4:  // 32 B
      return 64;
    case 5:  // 64 B
      return 64;
    case 6:  // 128 B
      return 32;
    case 7:  // 256 B
      return 32;
    case 8:  // 512 B
      return 16;
    case 9:  // 1024 B
      return 8;
    case 10:  // 2048 B
      return 4;
    case 11:  // 4096 B
      return 2;
    case 12:  // 8192 B
      return 1;
    default:
      return aggressive_caching && slab_shift <= 20;  // 2 MiB
  }
}

inline void SlabAllocator::drain_transferred_cache(
    const Caladan::PreemptGuard &g, uint32_t slab_shift) {
  auto &transferred_cache = transferred_caches_[g.read_cpu()];
  auto &list = transferred_cache.lists[slab_shift];

  if (list.size()) {
    ScopedLock l(&transferred_cache.spin);

    while (list.size()) {
      auto *hdr = reinterpret_cast<PtrHeader *>(list.pop());
      free_to_cache_list(g, hdr, slab_shift);
    }
  }
}

void *SlabAllocator::__allocate(size_t size) {
  void *ret = nullptr;
  int cpu;
  auto slab_shift = get_slab_shift(size);

  if (likely(slab_shift < kMaxSlabClassShift)) {
    Caladan::PreemptGuard g;

    drain_transferred_cache(g, slab_shift);
    cpu = g.read_cpu();
    auto &cache_list = cache_lists_[cpu].lists[slab_shift];
    if (likely(cache_list.size())) {
      ret = cache_list.pop();
    }

    if (unlikely(!ret)) {
      ScopedLock lock(&spin_);
      auto &slab_list = slab_lists_[slab_shift];
      auto max_num_cache_entries =
          std::max(static_cast<uint32_t>(1),
                   get_max_num_cache_entries(aggressive_caching_, slab_shift));
      while (slab_list.size() && cache_list.size() < max_num_cache_entries) {
        cache_list.push(slab_list.pop());
        global_free_bytes_ -= get_slab_size(slab_shift);
      }

      auto remaining = max_num_cache_entries - cache_list.size();
      if (remaining) {
        auto slab_size = get_slab_size(slab_shift);
        remaining = std::min(remaining, (end_ - cur_) / slab_size);
        cur_ += slab_size * remaining;
        auto tmp = cur_;
        for (uint32_t i = 0; i < remaining; i++) {
          tmp -= slab_size;
          cache_list.push(tmp);
        }
      }

      if (likely(cache_list.size())) {
        ret = cache_list.pop();
      }
    }
  }

  if (ret) {
    auto *hdr = reinterpret_cast<PtrHeader *>(ret);
    hdr->size = size;
    hdr->core_id = cpu;
    hdr->slab_id = slab_id_;
    auto addr = reinterpret_cast<uintptr_t>(ret);
    addr += sizeof(PtrHeader);
    assert(addr % kAlignment == 0);
    ret = reinterpret_cast<uint8_t *>(addr);
  }

  return ret;
}

void SlabAllocator::__free(const void *_ptr) {
  auto ptr = const_cast<void *>(_ptr);
  auto *hdr = reinterpret_cast<PtrHeader *>(reinterpret_cast<uintptr_t>(ptr) -
                                            sizeof(PtrHeader));
  auto *slab = slabs_[hdr->slab_id];
  assert(reinterpret_cast<const uint8_t *>(_ptr) >= slab->start_);
  assert(reinterpret_cast<const uint8_t *>(_ptr) < slab->cur_);

  auto size = hdr->size;
  auto slab_shift = slab->get_slab_shift(size);

  if (likely(slab_shift < slab->kMaxSlabClassShift)) {
    Caladan::PreemptGuard g;

    slab->__do_free(g, hdr, slab_shift);
  }
}

void *SlabAllocator::reallocate(const void *_ptr, size_t new_size) {
  auto *ptr = const_cast<void *>(_ptr);
  auto *hdr = reinterpret_cast<PtrHeader *>(reinterpret_cast<uintptr_t>(ptr) -
                                            sizeof(PtrHeader));
  auto *slab = slabs_[hdr->slab_id];
  assert(reinterpret_cast<const uint8_t *>(_ptr) >= slab->start_);
  assert(reinterpret_cast<const uint8_t *>(_ptr) < slab->cur_);

  auto size = hdr->size;
  auto slab_shift = slab->get_slab_shift(size);

  BUG_ON(slab_shift >= slab->kMaxSlabClassShift);

  auto *new_ptr = slab->allocate(new_size);
  if (unlikely(!new_ptr)) {
    return nullptr;
  }
  memcpy(new_ptr, ptr, std::min(size, new_size));

  {
    Caladan::PreemptGuard g;
    slab->__do_free(g, hdr, slab_shift);
  }

  return new_ptr;
}

inline void SlabAllocator::__do_free(const Caladan::PreemptGuard &g,
                                     PtrHeader *hdr, uint32_t slab_shift) {
  drain_transferred_cache(g, slab_shift);

  if (likely(g.read_cpu() == hdr->core_id)) {
    free_to_cache_list(g, hdr, slab_shift);
  } else {
    free_to_transferred_cache_list(hdr, slab_shift);
  }
}

void SlabAllocator::free_to_cache_list(const Caladan::PreemptGuard &g,
                                       PtrHeader *hdr, uint32_t slab_shift) {
  auto max_num_cache_entries =
      get_max_num_cache_entries(aggressive_caching_, slab_shift);
  auto &cache_list = cache_lists_[g.read_cpu()].lists[slab_shift];
  cache_list.push(hdr);

  if (unlikely(cache_list.size() > max_num_cache_entries)) {
    auto &slab_list = slab_lists_[slab_shift];
    ScopedLock lock(&spin_);

    while (cache_list.size() > max_num_cache_entries / 2) {
      slab_list.push(cache_list.pop());
      global_free_bytes_ += get_slab_size(slab_shift);
    }
  }
}

void SlabAllocator::free_to_transferred_cache_list(PtrHeader *hdr,
                                                   uint32_t slab_shift) {
  auto max_num_cache_entries =
      get_max_num_cache_entries(aggressive_caching_, slab_shift);
  auto &transferred_cache = transferred_caches_[hdr->core_id];
  auto &transferred_cache_list = transferred_cache.lists[slab_shift];
  auto &cache_list = cache_lists_[hdr->core_id].lists[slab_shift];

  ScopedLock lock(&transferred_cache.spin);
  transferred_cache.lists[slab_shift].push(hdr);

  auto total_num = transferred_cache_list.size() + cache_list.size();
  if (unlikely(total_num > max_num_cache_entries)) {
    auto num_to_turn_in = std::min(transferred_cache_list.size(),
                                   total_num - max_num_cache_entries / 2);
    auto &slab_list = slab_lists_[slab_shift];
    ScopedLock lock(&spin_);

    while (num_to_turn_in--) {
      slab_list.push(transferred_cache_list.pop());
      global_free_bytes_ += get_slab_size(slab_shift);
    }
  }
}

void *SlabAllocator::yield(size_t size) {
  ScopedLock lock(&spin_);
  size = (((size - 1) / kAlignment) + 1) * kAlignment;
  if (unlikely(cur_ + size > end_)) {
    return nullptr;
  }
  auto ret = cur_;
  cur_ += size;
  return ret;
}

}  // namespace nu
