#pragma once

#include <cstddef>
#include <memory>
#include <spanstream>
#include <sstream>
#include <utility>

#include "nu/utils/cached_pool.hpp"

namespace nu {

template <typename Allocator = std::allocator<std::byte>>
class ArchivePool {
 public:
  constexpr static uint32_t kOAStreamPreallocBufSize = 128 - 1;
  constexpr static uint32_t kOAStreamMaxBufSize = 8192 - 1;

  using CharAllocator =
      std::allocator_traits<Allocator>::template rebind_alloc<char>;
  using StringStream =
      std::basic_stringstream<char, std::char_traits<char>, CharAllocator>;
  using String = std::basic_string<char, std::char_traits<char>, CharAllocator>;

  struct IASStream {
    std::spanstream ss;
    cereal::BinaryInputArchive ia;
    IASStream() : ss{std::span<char>()}, ia(ss) {}
  };

  struct OASStream {
    StringStream ss;
    cereal::BinaryOutputArchive oa;
    OASStream() : ss(String(kOAStreamPreallocBufSize, '\0')), oa(ss) {}
  };

  ArchivePool(uint32_t per_core_cache_size = 64);
  IASStream *get_ia_sstream();
  void put_ia_sstream(IASStream *ia_sstream);
  OASStream *get_oa_sstream();
  void put_oa_sstream(OASStream *oa_sstream);

 private:
  using IAAllocator =
      std::allocator_traits<Allocator>::template rebind_alloc<IASStream>;
  using OAAllocator =
      std::allocator_traits<Allocator>::template rebind_alloc<OASStream>;

  CachedPool<IASStream, IAAllocator> ia_pool_;
  CachedPool<OASStream, OAAllocator> oa_pool_;
};

}  // namespace nu

#include "nu/impl/archive_pool.ipp"
