extern "C" {
#include <base/stddef.h>
}

namespace nu {

template <typename Allocator>
ArchivePool<Allocator>::ArchivePool(uint32_t per_core_cache_size)
    : ia_pool_(
          [] {
            IAAllocator allocator;
            auto *ia_sstream = allocator.allocate(1);
            new (ia_sstream) IASStream();
            return ia_sstream;
          },
          [](IASStream *ia_sstream) {
            IAAllocator allocator;
            ia_sstream->~IASStream();
            allocator.deallocate(ia_sstream, 1);
          },
          per_core_cache_size),
      oa_pool_(
          [] {
            OAAllocator allocator;
            auto *oa_sstream = allocator.allocate(1);
            new (oa_sstream) OASStream();
            return oa_sstream;
          },
          [](OASStream *oa_sstream) {
            OAAllocator allocator;
            oa_sstream->~OASStream();
            allocator.deallocate(oa_sstream, 1);
          },
          per_core_cache_size) {}

template <typename Allocator>
inline ArchivePool<Allocator>::IASStream *
ArchivePool<Allocator>::get_ia_sstream() {
  return ia_pool_.get();
}

template <typename Allocator>
inline ArchivePool<Allocator>::OASStream *
ArchivePool<Allocator>::get_oa_sstream() {
  return oa_pool_.get();
}

template <typename Allocator>
inline void ArchivePool<Allocator>::put_ia_sstream(IASStream *ia_sstream) {
  ia_sstream->ss.seekg(0);
  return ia_pool_.put(ia_sstream);
}

template <typename Allocator>
inline void ArchivePool<Allocator>::put_oa_sstream(OASStream *oa_sstream) {
  if (unlikely(oa_sstream->ss.tellp() >= kOAStreamMaxBufSize)) {
    oa_sstream->ss.str(String(kOAStreamPreallocBufSize, '\0'));
  }
  oa_sstream->ss.seekp(0);
  return oa_pool_.put(oa_sstream);
}

}  // namespace nu
