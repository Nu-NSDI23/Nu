#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>

#include <sync.h>

#include "nu/runtime.hpp"
#include "nu/utils/slab.hpp"

using namespace nu;

constexpr static uint64_t kBufSize = (4ULL << 30);
constexpr static uint64_t kMinSlabClassSize =
    (1ULL << SlabAllocator::kMinSlabClassShift);
constexpr static uint64_t kMaxSlabClassSize = (1ULL << 27);
uint16_t slab_id = kRuntimeSlabId + 1;

static_assert(kBufSize >= kMaxSlabClassSize);

bool run_with_size(uint64_t obj_size, uint64_t class_size) {
  rt::Preempt p;
  rt::PreemptGuard g(&p);

  class_size += sizeof(PtrHeader);
  auto *buf = new uint8_t[kBufSize];
  std::unique_ptr<uint8_t[]> buf_gc(buf);

  auto slab = std::make_unique<SlabAllocator>(slab_id++, buf, kBufSize);
  uint64_t count = kBufSize / class_size;
  if (slab->get_base() != buf) {
    return false;
  }

  for (uint64_t i = 0; i < count; i++) {
    auto ptr = slab->allocate(obj_size);
    if (ptr != buf + i * class_size + sizeof(PtrHeader)) {
      return false;
    }
  }
  if (slab->allocate(obj_size) != nullptr) {
    return false;
  }

  slab->free(buf + sizeof(PtrHeader));
  if (slab->allocate(obj_size) != buf + sizeof(PtrHeader)) {
    return false;
  }

  return true;
}

bool run_min_size() { return run_with_size(1, kMinSlabClassSize); }

bool run_mid_size() {
  return run_with_size(110, 128) & run_with_size(200, 256);
}

bool run_max_size() {
  return run_with_size(kMaxSlabClassSize - sizeof(PtrHeader),
                       kMaxSlabClassSize);
}

bool run_more_than_buf_size() {
  rt::Preempt p;
  rt::PreemptGuard g(&p);

  auto *buf = new uint8_t[kBufSize];
  std::unique_ptr<uint8_t[]> buf_gc(buf);

  auto slab = std::make_unique<SlabAllocator>(slab_id++, buf, kBufSize);
  if (slab->allocate(kBufSize - sizeof(PtrHeader) + 1) != nullptr) {
    return false;
  }
  return true;
}

bool run() {
  return run_min_size() & run_mid_size() & run_max_size() &
         run_more_than_buf_size();
}

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) {
    if (run()) {
      std::cout << "Passed" << std::endl;
    } else {
      std::cout << "Failed" << std::endl;
    }
  });
}
