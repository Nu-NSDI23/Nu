#include <dlfcn.h>

#include "nu/commons.hpp"
#include "nu/runtime.hpp"
#include "nu/utils/caladan.hpp"
#include "nu/utils/slab.hpp"

using MallocFn = void *(*)(std::size_t);
using FreeFn = void (*)(void *);
using ReallocFn = void *(*)(void *, std::size_t);

static inline MallocFn get_real_malloc() {
  static MallocFn real_malloc;

  if (unlikely(!real_malloc)) {
    real_malloc = reinterpret_cast<MallocFn>(dlsym(RTLD_NEXT, "malloc"));
  }
  return real_malloc;
}

static inline FreeFn get_real_free() {
  static FreeFn real_free;

  if (unlikely(!real_free)) {
    real_free = reinterpret_cast<FreeFn>(dlsym(RTLD_NEXT, "free"));
  }
  return real_free;
}

static inline ReallocFn get_real_realloc() {
  static ReallocFn real_realloc;

  if (unlikely(!real_realloc)) {
    real_realloc = reinterpret_cast<ReallocFn>(dlsym(RTLD_NEXT, "realloc"));
  }
  return real_realloc;
}

static inline void *__new(std::size_t size) {
  nu::Caladan::PreemptGuard g;

  void *ptr;
  auto *slab = nu::Caladan::thread_self()
                   ? nu::get_runtime()->caladan()->thread_get_proclet_slab()
                   : nullptr;
  if (slab) {
    ptr = slab->allocate(size);
  } else if (auto *runtime_slab = nu::get_runtime()->runtime_slab()) {
    ptr = runtime_slab->allocate(size);
  } else {
    ptr = get_real_malloc()(size);
  }
  return ptr;
}

void *operator new(std::size_t size) throw() {
  auto *ptr = __new(size);
  BUG_ON(size && !ptr);
  return ptr;
}

void *operator new(std::size_t size,
                   const std::nothrow_t &nothrow_value) noexcept {
  return __new(size);
}

static inline void *__new_aligned(std::size_t size, std::align_val_t al) {
  size += sizeof(size_t);
  auto align = static_cast<std::size_t>(al);
  auto *ptr = __new(size + align - 1);
  auto ptr_addr = reinterpret_cast<uintptr_t>(ptr);
  auto aligned_ptr_addr =
      (ptr_addr + sizeof(size_t) + align - 1) & (~(align - 1));
  assert(reinterpret_cast<uintptr_t>(aligned_ptr_addr) % align == 0);
  auto *header = reinterpret_cast<void **>(aligned_ptr_addr) - 1;
  *header = ptr;
  return reinterpret_cast<void *>(aligned_ptr_addr);
}

void *operator new(std::size_t size, std::align_val_t al) {
  return __new_aligned(size, al);
}

void *operator new(std::size_t size, std::align_val_t al,
                   const std::nothrow_t &) {
  return __new_aligned(size, al);
}

static inline void __delete(void *ptr) {
  nu::Caladan::PreemptGuard g;

  auto ptr_addr = reinterpret_cast<uintptr_t>(ptr);
  if ((ptr_addr >= nu::kMinProcletHeapVAddr &&
       ptr_addr < nu::kMaxProcletHeapVAddr) ||
      (ptr_addr >= nu::kMinRuntimeHeapVaddr &&
       ptr_addr < nu::kMaxRuntimeHeapVaddr)) {
    nu::SlabAllocator::free(ptr);
  } else {
    get_real_free()(ptr);
  }
}

void operator delete(void *ptr) noexcept { __delete(ptr); }

void operator delete(void *ptr, std::align_val_t al) noexcept {
  auto *header = reinterpret_cast<void **>(ptr) - 1;
  __delete(*header);
}

void *malloc(std::size_t size) { return __new(size); }

void free(void *ptr) { __delete(ptr); }

void *realloc(void *ptr, std::size_t size) {
  nu::Caladan::PreemptGuard g;

  auto ptr_addr = reinterpret_cast<uintptr_t>(ptr);
  if ((ptr_addr >= nu::kMinProcletHeapVAddr &&
       ptr_addr < nu::kMaxProcletHeapVAddr) ||
      (ptr_addr >= nu::kMinRuntimeHeapVaddr &&
       ptr_addr < nu::kMaxRuntimeHeapVaddr)) {
    return nu::SlabAllocator::reallocate(ptr, size);
  } else {
    return get_real_realloc()(ptr, size);
  }
}
