#include <iostream>
#include <limits>
#include <type_traits>

#include <sync.h>
#include <thread.h>

#include "nu/commons.hpp"
#include "nu/ctrl_client.hpp"
#include "nu/runtime.hpp"
#include "nu/migrator.hpp"
#include "nu/pressure_handler.hpp"
#include "nu/utils/caladan.hpp"

constexpr static bool kEnableLogging = false;

namespace nu {

PressureHandler::PressureHandler()
    : active_handlers_{0}, mock_(false), done_(false) {
  register_handlers();

  update_th_ = rt::Thread([&] {
    while (!rt::access_once(done_)) {
      timer_sleep_hp(kSortedProcletsUpdateIntervalMs * kOneMilliSecond);
      update_sorted_proclets();
    }
  });
}

PressureHandler::~PressureHandler() {
  done_ = true;
  remove_all_resource_pressure_handlers();
  mb();
  update_th_.Join();
  while (
      unlikely(rt::access_once(resource_pressure_info->status) == HANDLING)) {
    rt::Yield();
  }
}

Utility::Utility() {}

Utility::Utility(ProcletHeader *proclet_header, uint64_t mem_size,
                 float cpu_load) {
  header = proclet_header;
  auto time = kFixedCostUs + (mem_size / (kNetBwGbps / 8.0f) / 1000.0f);

  cpu_pressure_util = cpu_load / time;
  mem_pressure_util = mem_size / time;
}

void PressureHandler::update_sorted_proclets() {
  CPULoad::flush_all();

  auto new_mem_pressure_sorted_proclets =
      std::make_shared<decltype(mem_pressure_sorted_proclets_)::element_type>();
  auto new_cpu_pressure_sorted_proclets =
      std::make_shared<decltype(cpu_pressure_sorted_proclets_)::element_type>();
  auto all_proclets = get_runtime()->proclet_manager()->get_all_proclets();
  for (auto *proclet_base : all_proclets) {
    auto *proclet_header = reinterpret_cast<ProcletHeader *>(proclet_base);
    auto optional_info = get_runtime()->proclet_manager()->get_proclet_info(
        proclet_header, std::function([](const ProcletHeader *header) {
          return std::make_tuple(header->migratable, header->total_mem_size(),
                                 header->cpu_load.get_load());
        }));

    if (likely(optional_info)) {
      auto [migratable, mem_size, cpu_load] = *optional_info;
      if (migratable) {
        Utility u(proclet_header, mem_size, cpu_load);
        new_cpu_pressure_sorted_proclets->insert(u);
        new_mem_pressure_sorted_proclets->insert(u);
      }
    }
  }

  {
    Caladan::PreemptGuard g;

    std::atomic_exchange(&cpu_pressure_sorted_proclets_,
                         new_cpu_pressure_sorted_proclets);
    std::atomic_exchange(&mem_pressure_sorted_proclets_,
                         new_mem_pressure_sorted_proclets);
  }
}

void PressureHandler::register_handlers() {
  resource_pressure_closure closures[kNumAuxHandlers + 1];
  closures[0] = {main_handler, nullptr};
  for (uint32_t i = 1; i < kNumAuxHandlers + 1; i++) {
    closures[i] = {aux_handler, &aux_handler_states_[i - 1]};
  }
  create_resource_pressure_handlers(closures, kNumAuxHandlers + 1);
}

void PressureHandler::main_handler(void *unused) {
  get_runtime()->pressure_handler()->__main_handler();
}

void PressureHandler::__main_handler() {
  active_handlers_ += kNumAuxHandlers + 1;

  auto node_guard = get_runtime()->controller_client()->acquire_node();
  if (unlikely(!node_guard)) {
    goto done;
  }

  while (has_pressure()) {
    if constexpr (kEnableLogging) {
      std::cout << "Detect pressure = { .mem_mbs = "
                << rt::RuntimeToReleaseMemMbs()
                << ", .cpu_pressure = " << rt::RuntimeCpuPressure() << " }."
                << std::endl;
    }

    auto min_num_proclets =
        has_cpu_pressure() ? kMinNumProcletsOnCPUPressure : 0;
    auto min_mem_mbs = rt::RuntimeToReleaseMemMbs();
    auto picked_tasks = pick_tasks(min_num_proclets, min_mem_mbs);
    if (likely(!picked_tasks.empty())) {
      auto num_migrated = get_runtime()->migrator()->migrate(picked_tasks);
      if constexpr (kEnableLogging) {
        std::cout << "Migrate " << num_migrated << " proclets." << std::endl;
      }
      if (unlikely(num_migrated != picked_tasks.size())) {
        break;
      }
    } else {
      if (mock_)  {
        mock_clear_pressure();
      }
      break;
    }
  }

done:
  pause_aux_handlers();
  if (--active_handlers_ == 0) {
    set_handled();
  }
}

void PressureHandler::pause_aux_handlers() {
  // Pause aux pressure handlers.
  for (uint32_t i = 0; i < PressureHandler::kNumAuxHandlers; i++) {
    rt::access_once(aux_handler_states_[i].done) = true;
  }
}

void PressureHandler::wait_aux_tasks() {
  for (uint32_t i = 0; i < kNumAuxHandlers; i++) {
    while (rt::access_once(aux_handler_states_[i].task_pending)) {
      get_runtime()->caladan()->unblock_and_relax();
    }
  }
}

void PressureHandler::update_aux_handler_state(uint32_t handler_id,
                                               MigratorConn &&conn) {
  auto &state = aux_handler_states_[handler_id];
  while (rt::access_once(state.task_pending)) {
    get_runtime()->caladan()->unblock_and_relax();
  }
  barrier();
  state.conn = std::move(conn);
}

void PressureHandler::dispatch_aux_tcp_task(
    uint32_t handler_id, std::vector<iovec> &&tcp_write_task) {
  auto &state = aux_handler_states_[handler_id];
  while (rt::access_once(state.task_pending)) {
    get_runtime()->caladan()->unblock_and_relax();
  }
  state.tcp_write_task = std::move(tcp_write_task);
  store_release(&state.task_pending, true);
}

void PressureHandler::dispatch_aux_pause_task(uint32_t handler_id) {
  auto &state = aux_handler_states_[handler_id];
  while (rt::access_once(state.task_pending)) {
    get_runtime()->caladan()->unblock_and_relax();
  }
  state.pause = true;
  store_release(&state.task_pending, true);
}

void PressureHandler::__aux_handler(AuxHandlerState *state) {
  // Serve tasks.
  while (!rt::access_once(state->done)) {
    if (unlikely(load_acquire(&state->task_pending))) {
      if (state->pause) {
        pause_migrating_ths_aux();
        store_release(&state->pause, false);
      } else {
        auto *c = state->conn.get_tcp_conn();
        BUG_ON(c->WritevFull(std::span<const iovec>(state->tcp_write_task),
                             /* nt = */ true,
                             /* poll = */ true) < 0);
      }
      store_release(&state->task_pending, false);
    }
    get_runtime()->caladan()->unblock_and_relax();
  }
  state->conn.release();
  // Prepare for the next start.
  store_release(&state->done, false);

  if (--active_handlers_ == 0) {
    set_handled();
  }
}

void PressureHandler::aux_handler(void *args) {
  get_runtime()->pressure_handler()->__aux_handler(
      reinterpret_cast<AuxHandlerState *>(args));
}

void PressureHandler::set_handled() {
  // Tell iokernel that the pressure has been handled.
  auto &pressure = *resource_pressure_info;
  pressure.mock = false;
  store_release(&pressure.status, HANDLED);
}

std::vector<std::pair<ProcletMigrationTask, Resource>>
PressureHandler::pick_tasks(uint32_t min_num_proclets, uint32_t min_mem_mbs) {
  bool done = false;
  uint32_t total_mem_mbs = 0;
  std::vector<std::pair<ProcletMigrationTask, Resource>> picked_tasks;
  std::set<ProcletHeader *> dedupper;

  auto pick_fn = [&](ProcletHeader *header) {
    auto optional = get_runtime()->proclet_manager()->get_proclet_info(
        header, std::function([&](const ProcletHeader *header) {
          return std::make_tuple(header->migratable, header->capacity,
                                 header->heap_size(), header->total_mem_size(),
                                 header->cpu_load.get_load());
        }));
    if (likely(optional)) {
      auto &[migratable, capacity, heap_size, mem_size, cpu_load] = *optional;
      if (likely(migratable && !dedupper.contains(header))) {
        dedupper.insert(header);
        ProcletMigrationTask task(header, capacity, heap_size);
        auto mem_mbs = mem_size / static_cast<float>(kOneMB);
        Resource resource(cpu_load, mem_mbs);
        picked_tasks.emplace_back(std::move(task), std::move(resource));
        total_mem_mbs += mem_mbs;
        done = ((total_mem_mbs >= min_mem_mbs) &&
                (picked_tasks.size() >= min_num_proclets));
      }
    }

    return optional.has_value();
  };

  auto traverse_fn = [&]<typename T>(T &&sorted_proclets) {
    if (sorted_proclets) {
      auto iter = sorted_proclets->begin();
      while (iter != sorted_proclets->end() && !done) {
        if (pick_fn(iter->header)) {
          ++iter;
        } else {
          iter = sorted_proclets->erase(iter);
        }
      }
    }
  };

  bool cpu_pressure = min_num_proclets;
  assert_preempt_disabled();
  if (cpu_pressure) {
    traverse_fn(std::atomic_load(&cpu_pressure_sorted_proclets_));
  } else {
    traverse_fn(std::atomic_load(&mem_pressure_sorted_proclets_));
  }

  if (unlikely(!done)) {
    CPULoad::flush_all();
    auto all_proclets = get_runtime()->proclet_manager()->get_all_proclets();
    auto iter = all_proclets.begin();
    while (iter != all_proclets.end() && !done) {
      auto *header = reinterpret_cast<ProcletHeader *>(*(iter++));
      pick_fn(header);
    }
  }

  return picked_tasks;
}

void PressureHandler::mock_set_pressure() {
  mock_ = true;
  store_release(&resource_pressure_info->mock, true);
}

void PressureHandler::mock_clear_pressure() {
  mock_ = false;
  store_release(&resource_pressure_info->mock, false);
}

}  // namespace nu
