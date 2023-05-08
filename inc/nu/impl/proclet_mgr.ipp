#include <sys/mman.h>

#include "nu/runtime.hpp"
#include "nu/utils/scoped_lock.hpp"
#include "nu/utils/time.hpp"

namespace nu {

inline uint64_t ProcletHeader::global_idx() const {
  return (reinterpret_cast<uint64_t>(this) - kMinProcletHeapVAddr) /
         kMinProcletHeapSize;
}

inline uint64_t ProcletHeader::heap_size() const {
  return reinterpret_cast<uint64_t>(slab.get_base()) + slab.get_usage() -
         reinterpret_cast<uint64_t>(this);
}

inline uint64_t ProcletHeader::stack_size() const {
  return thread_cnt.get() * kStackSize;
}

inline uint64_t ProcletHeader::total_mem_size() const {
  return heap_size() + stack_size();
}

inline uint8_t &ProcletHeader::status() {
  return proclet_statuses[global_idx()];
}

inline uint8_t ProcletHeader::status() const {
  return proclet_statuses[global_idx()];
}

inline SpinLock &ProcletHeader::migration_spin() {
  return proclet_migration_spin[global_idx()];
}

inline VAddrRange ProcletHeader::range() const {
  auto start_addr = reinterpret_cast<uint64_t>(this);
  auto end_addr = start_addr + capacity;
  return VAddrRange{start_addr, end_addr};
}

inline void ProcletManager::wait_until(ProcletHeader *proclet_header,
                                       ProcletStatus status) {
  proclet_header->spin_lock.lock();
  while (Caladan::access_once(proclet_header->status()) != status) {
    proclet_header->cond_var.wait(&proclet_header->spin_lock);
  }
  proclet_header->spin_lock.unlock();
}

inline void ProcletManager::insert(void *proclet_base) {
  ScopedLock lock(&spin_);
  reinterpret_cast<ProcletHeader *>(proclet_base)->status() = kPresent;
  num_present_proclets_++;
  present_proclets_.push_back(proclet_base);
}

inline bool ProcletManager::remove_for_migration(void *proclet_base) {
  return __remove(proclet_base, kMigrating);
}

inline bool ProcletManager::remove_for_destruction(void *proclet_base) {
  return __remove(proclet_base, kDestructing);
}

inline bool ProcletManager::__remove(void *proclet_base,
                                     ProcletStatus new_status) {
  ScopedLock lock(&spin_);
  auto *proclet_header = reinterpret_cast<ProcletHeader *>(proclet_base);
  auto &status = proclet_header->status();
  if (status == kPresent) {
    num_present_proclets_--;
    status = new_status;
    return true;
  } else {
    return false;
  }
}

inline uint32_t ProcletManager::get_num_present_proclets() {
  return num_present_proclets_;
}

template <typename RetT>
inline std::optional<RetT> ProcletManager::get_proclet_info(
    const ProcletHeader *header, std::function<RetT(const ProcletHeader *)> f) {
  ScopedLock lock(&spin_);
  if (header->status() != kPresent) {
    return std::nullopt;
  }
  return f(header);
}

}  // namespace nu
