#include <asm/mman.h>
#include <sys/mman.h>

#include <cstdint>
#include <functional>
#include <memory>

extern "C" {
#include <base/assert.h>
#include <runtime/thread.h>
}

#include "nu/runtime.hpp"
#include "nu/proclet_mgr.hpp"

namespace nu {

uint8_t proclet_statuses[kMaxNumProclets];
SpinLock proclet_migration_spin[kMaxNumProclets];

ProcletManager::ProcletManager() {
  num_present_proclets_ = 0;
  for (uint64_t vaddr = kMinProcletHeapVAddr;
       vaddr + kMaxProcletHeapSize <= kMaxProcletHeapVAddr;
       vaddr += kMaxProcletHeapSize) {
    auto *proclet_base = reinterpret_cast<uint8_t *>(vaddr);
    auto mmap_addr =
        mmap(proclet_base, kMaxProcletHeapSize, PROT_READ | PROT_WRITE,
             MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE, -1, 0);
    BUG_ON(mmap_addr != proclet_base);
    auto rc = madvise(proclet_base, kMaxProcletHeapSize, MADV_DONTDUMP);
    BUG_ON(rc == -1);
  }
}

void ProcletManager::madvise_populate(void *proclet_base,
                                      uint64_t populate_len) {
  populate_len = ((populate_len - 1) / kPageSize + 1) * kPageSize;
  madvise(proclet_base, populate_len, MADV_POPULATE_WRITE);
}

void ProcletManager::cleanup(void *proclet_base, bool for_migration) {
  RuntimeSlabGuard guard;
  auto *proclet_header = reinterpret_cast<ProcletHeader *>(proclet_base);

  if (!for_migration) {
    while (unlikely(proclet_header->slab_ref_cnt.get())) {
      get_runtime()->caladan()->thread_yield();
    }
  }

  // Deregister its slab ID.
  std::destroy_at(&proclet_header->slab);

  bool defer = !for_migration;
  depopulate(proclet_base, proclet_header->heap_size(), defer);
}

void ProcletManager::depopulate(void *proclet_base, uint64_t size, bool defer) {
  size = ((size - 1) / kPageSize + 1) * kPageSize;

  if (defer) {
    // Try to keep the memory for future reuses.
    BUG_ON(madvise(proclet_base, size, MADV_FREE) != 0);
  } else {
    // Release mem ASAP.
    auto mmap_addr =
        mmap(proclet_base, size, PROT_READ | PROT_WRITE,
             MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE, -1, 0);
    BUG_ON(mmap_addr != proclet_base);
  }
}

void ProcletManager::setup(void *proclet_base, uint64_t capacity,
                           bool migratable, bool from_migration) {
  RuntimeSlabGuard guard;
  auto *proclet_header = reinterpret_cast<ProcletHeader *>(proclet_base);

  proclet_header->capacity = capacity;
  std::construct_at(&proclet_header->cpu_load);
  std::construct_at(&proclet_header->spin_lock);
  std::construct_at(&proclet_header->cond_var);
  std::construct_at(&proclet_header->blocked_syncer);
  std::construct_at(&proclet_header->time);
  proclet_header->migratable = migratable;

  if (!from_migration) {
    proclet_header->ref_cnt = 1;
    std::construct_at(&proclet_header->rcu_lock);
    std::construct_at(&proclet_header->slab_ref_cnt);
    auto slab_region_size = capacity - sizeof(ProcletHeader);
    std::construct_at(&proclet_header->slab, to_slab_id(proclet_header),
                      proclet_header + 1, slab_region_size);
  }
}

std::vector<void *> ProcletManager::get_all_proclets() {
  ScopedLock lock(&spin_);
  auto iter = present_proclets_.begin();
  for (auto *proclet_base : present_proclets_) {
    auto *proclet_header = reinterpret_cast<ProcletHeader *>(proclet_base);
    if (proclet_header->status() == kPresent) {
      *iter = proclet_base;
      iter++;
    }
  }
  present_proclets_.erase(iter, present_proclets_.end());
  return present_proclets_;
}

uint64_t ProcletManager::get_mem_usage() {
  uint64_t total_mem_usage = 0;
  auto proclets = get_all_proclets();
  for (auto *proclet_base : proclets) {
    auto *proclet_header = reinterpret_cast<ProcletHeader *>(proclet_base);
    auto &proclet_slab = proclet_header->slab;
    total_mem_usage += reinterpret_cast<uint8_t *>(proclet_slab.get_base()) -
                       reinterpret_cast<uint8_t *>(proclet_header) +
                       proclet_slab.get_usage();
  }

  return total_mem_usage;
}

}  // namespace nu
