#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <vector>
#include <unordered_map>

extern "C" {
#include <runtime/net.h>
#include <runtime/thread.h>
}
#include <sync.h>

#include "nu/commons.hpp"
#include "nu/utils/blocked_syncer.hpp"
#include "nu/utils/cond_var.hpp"
#include "nu/utils/counter.hpp"
#include "nu/utils/cpu_load.hpp"
#include "nu/utils/mutex.hpp"
#include "nu/utils/rcu_lock.hpp"
#include "nu/utils/slab.hpp"
#include "nu/utils/spin_lock.hpp"
#include "nu/utils/time.hpp"

namespace nu {

enum ProcletStatus {
  kAbsent = 0,
  kPopulating,
  kDepopulating,
  kCleaning,
  kMigrating,
  kPresent,
  kDestructing,
};

// Proclet statuses are stored out of band so that they are always accessible
// even if the proclets are not present locally.
extern uint8_t proclet_statuses[kMaxNumProclets];
extern SpinLock proclet_migration_spin[kMaxNumProclets];

struct ProcletHeader {
  ~ProcletHeader() = default;

  // Used for monitoring cpu load.
  CPULoad cpu_load;

  // Used for monitoring total amount of local calls, estimate total bytes for performance
  Counter local_call_cnt;

  // stores amount and total data size of outgoing remote calls to every NodeIP (machine)
  // Synchronized using spin_lock
  std::unordered_map<NodeIP, std::pair<uint32_t, uint64_t>> remote_call_map;

  // Max heap size.
  uint64_t populate_size;
  uint64_t capacity;

  // For synchronization.
  SpinLock spin_lock;
  CondVar cond_var;

  // Used for monitoring active threads count.
  Counter thread_cnt;

  // Migration related.
  std::atomic<int8_t> pending_load_cnt;
  BlockedSyncer blocked_syncer;
  bool migratable;

  // Logical timer.
  Time time;

  //--- Fields below will be automatically copied during migration. ---/
  uint8_t copy_start[0];

  // For disabling migration.
  RCULock rcu_lock;

  // Ref cnt related.
  int ref_cnt;

  // Heap mem allocator. Must be the last field.
  Counter slab_ref_cnt;
  SlabAllocator slab;

  uint64_t global_idx() const;
  uint64_t total_mem_size() const;
  uint64_t heap_size() const;
  uint64_t stack_size() const;
  uint8_t &status();
  uint8_t status() const;
  SpinLock &migration_spin();
  VAddrRange range() const;
};

class ProcletManager {
 public:
  ProcletManager();

  static void setup(void *proclet_base, uint64_t capacity, bool migratable,
                    bool from_migration);
  void cleanup(void *proclet_base, bool for_migration);
  static void madvise_populate(void *proclet_base, uint64_t populate_len);
  static void depopulate(void *proclet_base, uint64_t size, bool defer);
  static void wait_until(ProcletHeader *proclet_header, ProcletStatus status);
  void insert(void *proclet_base);
  bool remove_for_migration(void *proclet_base);
  bool remove_for_destruction(void *proclet_base);
  std::vector<void *> get_all_proclets();
  uint64_t get_mem_usage();
  uint32_t get_num_present_proclets();
  template <typename RetT>
  std::optional<RetT> get_proclet_info(
      const ProcletHeader *header,
      std::function<RetT(const ProcletHeader *)> f);

 private:
  std::vector<void *> present_proclets_;
  uint32_t num_present_proclets_;
  SpinLock spin_;
  friend class Test;

  bool __remove(void *proclet_base, ProcletStatus new_status);
};

}  // namespace nu

#include "nu/impl/proclet_mgr.ipp"
