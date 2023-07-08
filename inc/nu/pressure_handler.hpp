#include <atomic>
#include <climits>
#include <cstddef>
#include <memory>
#include <set>

extern "C" {
#include <runtime/pressure.h>
}
#include <net.h>

#include "nu/migrator.hpp"

namespace nu {

using ResourcePressureInfo = struct resource_pressure_info;

struct AuxHandlerState {
  MigratorConn conn;
  std::vector<iovec> tcp_write_task;
  bool pause = false;
  bool task_pending = false;
  bool done = false;
};

struct Utility {
  Utility();
  Utility(ProcletHeader *proclet_header, uint64_t mem_size, float cpu_load);

  constexpr static uint32_t kFixedCostUs = 25;
  constexpr static uint32_t kNetBwGbps = 100;
  ProcletHeader *header;
  float mem_pressure_util;
  float cpu_pressure_util;
};

class PressureHandler {
 public:
  constexpr static uint32_t kNumAuxHandlers =
      Migrator::kTransmitProcletNumThreads - 1;
  constexpr static uint32_t kSortedProcletsUpdateIntervalMs = 200;
  constexpr static uint32_t kUpdateBudget = 200;
  constexpr static uint32_t kHandlerSleepUs = 100;
  constexpr static uint32_t kMinNumProcletsOnCPUPressure = 32;

  PressureHandler();
  ~PressureHandler();
  void wait_aux_tasks();
  void update_aux_handler_state(uint32_t handler_id, MigratorConn &&conn);
  void dispatch_aux_tcp_task(uint32_t handler_id,
                             std::vector<iovec> &&tcp_write_task);
  void dispatch_aux_pause_task(uint32_t handler_id);
  void mock_set_pressure();
  void mock_clear_pressure();
  bool has_cpu_pressure();
  bool has_mem_pressure();
  bool has_pressure();
  bool has_real_pressure();
  void set_handled();

 private:
  struct CmpMemUtil {
    bool operator()(const Utility &x, const Utility &y) const {
      return x.mem_pressure_util > y.mem_pressure_util;
    }
  };
  struct CmpCpuUtil {
    bool operator()(const Utility &x, const Utility &y) const {
      return x.cpu_pressure_util > y.cpu_pressure_util;
    }
  };
  std::shared_ptr<std::multiset<Utility, CmpMemUtil>>
      mem_pressure_sorted_proclets_;
  std::shared_ptr<std::multiset<Utility, CmpCpuUtil>>
      cpu_pressure_sorted_proclets_;
  rt::Thread update_th_;
  std::atomic<int> active_handlers_;
  AuxHandlerState aux_handler_states_[kNumAuxHandlers];
  bool mock_;
  bool done_;

  std::vector<std::pair<ProcletMigrationTask, Resource>> pick_tasks(
      uint32_t min_num_proclets, uint32_t min_mem_mbs);
  void update_sorted_proclets();
  void register_handlers();
  void pause_aux_handlers();
  void __main_handler();
  void __aux_handler(AuxHandlerState *state);
  static void main_handler(void *unused);
  static void aux_handler(void *args);
};

}  // namespace nu

#include "nu/impl/pressure_handler.ipp"
