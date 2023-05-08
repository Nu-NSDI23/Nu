#include <cstring>
#include <iostream>

namespace nu {

inline SlabAllocator::SlabAllocator() : slab_id_(0) {}

inline SlabAllocator::SlabAllocator(SlabId_t slab_id, void *buf, uint64_t len,
                                    bool aggressive_caching) {
  init(slab_id, buf, len, aggressive_caching);
}

inline SlabAllocator::~SlabAllocator() {
  if (slab_id_) {
    deregister_slab_by_id(slab_id_);
  }
}

inline void SlabAllocator::init(SlabId_t slab_id, void *buf, uint64_t len,
                                bool aggressive_caching) {
  register_slab_by_id(this, slab_id);
  slab_id_ = slab_id;
  aggressive_caching_ = aggressive_caching;
  start_ = reinterpret_cast<const uint8_t *>(buf);
  end_ = start_ + len;
  cur_ = const_cast<uint8_t *>(start_);
  global_free_bytes_ = 0;
}

inline void *SlabAllocator::allocate(size_t size) {
  if (unlikely(!size)) {
    return nullptr;
  }
  return __allocate(size);
}

inline void SlabAllocator::free(const void *ptr) {
  if (unlikely(!ptr)) {
    return;
  }
  __free(ptr);
}

inline uint32_t SlabAllocator::get_slab_shift(uint64_t data_size) {
  return data_size <= (1ULL << kMinSlabClassShift) ? kMinSlabClassShift - 1
                                                   : bsr_64(data_size - 1);
}

inline uint64_t SlabAllocator::get_slab_size(uint32_t slab_shift) {
  return (1ULL << (slab_shift + 1)) + sizeof(PtrHeader);
}

inline void *SlabAllocator::get_base() const {
  return const_cast<uint8_t *>(start_);
}

inline size_t SlabAllocator::get_cur_usage() const {
  auto total_usage = get_usage();
  auto global_free_bytes =
      static_cast<std::size_t>(rt::access_once(global_free_bytes_));
  BUG_ON(total_usage < global_free_bytes);
  return total_usage - global_free_bytes;
}

inline size_t SlabAllocator::get_usage() const {
  return rt::access_once(cur_) - start_;
}

inline size_t SlabAllocator::get_remaining() const {
  return end_ - start_ - get_usage();
}

inline SlabId_t SlabAllocator::get_id() { return slab_id_; }

inline void SlabAllocator::register_slab_by_id(SlabAllocator *slab,
                                               SlabId_t slab_id) {
  BUG_ON(slabs_[slab_id]);
  slabs_[slab_id] = slab;
}

inline void SlabAllocator::deregister_slab_by_id(SlabId_t slab_id) {
  BUG_ON(!slabs_[slab_id]);
  slabs_[slab_id] = nullptr;
}

inline uint64_t SlabAllocator::FreePtrsLinkedList::size() { return size_; }

}  // namespace nu
