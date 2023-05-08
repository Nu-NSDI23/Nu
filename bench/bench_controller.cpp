#include <thread.h>

#include <atomic>
#include <functional>
#include <iostream>
#include <random>
#include <vector>

#include "nu/ctrl_client.hpp"
#include "nu/runtime.hpp"
#include "nu/utils/perf.hpp"

using namespace nu;

constexpr uint32_t kNumThreads = 18;
constexpr uint32_t kTargetMops = 2;
constexpr uint64_t kPerfDurationUs = 10 * kOneSecond;
constexpr uint32_t kNumProclets = 65566;

namespace nu {

struct PerfResolveObjThreadState : PerfThreadState {
  PerfResolveObjThreadState()
      : rd(), gen(rd()), dist_proclet_num(1, kNumProclets) {}

  std::random_device rd;
  std::mt19937 gen;
  std::uniform_int_distribution<> dist_proclet_num;
};

struct PerfResolveObjReq : PerfRequest {
  PerfResolveObjReq(uint32_t num) : proclet_num(num) {}

  uint32_t proclet_num;
};

class PerfResolveObjAdapter : public PerfAdapter {
 public:
  PerfResolveObjAdapter(ControllerClient *client) : client_(client) {}

  std::unique_ptr<PerfThreadState> create_thread_state() {
    return std::make_unique<PerfResolveObjThreadState>();
  }

  std::unique_ptr<PerfRequest> gen_req(PerfThreadState *perf_state) {
    auto *state = reinterpret_cast<PerfResolveObjThreadState *>(perf_state);
    auto proclet_num = (state->dist_proclet_num)(state->gen);
    return std::make_unique<PerfResolveObjReq>(proclet_num);
  }

  bool serve_req(PerfThreadState *perf_state, const PerfRequest *perf_req) {
    auto *req = reinterpret_cast<const PerfResolveObjReq *>(perf_req);
    ProcletID proclet_id =
        kMaxProcletHeapVAddr - req->proclet_num * kMaxProcletHeapSize;
    auto ip = client_->resolve_proclet(proclet_id);
    BUG_ON(!ip);
    return true;
  }

 private:
  ControllerClient *client_;
};

class PerfAcquireMigrationDestAdapter : public PerfAdapter {
 public:
  PerfAcquireMigrationDestAdapter(ControllerClient *client) : client_(client) {}

  std::unique_ptr<PerfThreadState> create_thread_state() {
    return std::make_unique<PerfThreadState>();
  }

  std::unique_ptr<PerfRequest> gen_req(PerfThreadState *state) {
    return std::make_unique<PerfRequest>();
  }

  bool serve_req(PerfThreadState *state, const PerfRequest *req) {
    Resource resource{0, 0};
    auto [dest_guard, _] = client_->acquire_migration_dest(false, resource);
    BUG_ON(!dest_guard);
    return true;
  }

 private:
  ControllerClient *client_;
};

struct PerfUpdateLocationThreadState : PerfThreadState {
  PerfUpdateLocationThreadState()
      : rd(), gen(rd()), dist_proclet_num(1, kNumProclets) {}

  std::random_device rd;
  std::mt19937 gen;
  std::uniform_int_distribution<> dist_proclet_num;
};

struct PerfUpdateLocationReq : PerfRequest {
  PerfUpdateLocationReq(uint32_t num) : proclet_num(num) {}

  uint32_t proclet_num;
};

class PerfUpdateLocationAdapter : public PerfAdapter {
 public:
  PerfUpdateLocationAdapter(ControllerClient *client) : client_(client) {}

  std::unique_ptr<PerfThreadState> create_thread_state() {
    return std::make_unique<PerfUpdateLocationThreadState>();
  }

  std::unique_ptr<PerfRequest> gen_req(PerfThreadState *perf_state) {
    auto *state = reinterpret_cast<PerfUpdateLocationThreadState *>(perf_state);
    auto proclet_num = (state->dist_proclet_num)(state->gen);
    return std::make_unique<PerfUpdateLocationReq>(proclet_num);
  }

  bool serve_req(PerfThreadState *perf_state, const PerfRequest *perf_req) {
    auto *req = reinterpret_cast<const PerfUpdateLocationReq *>(perf_req);
    ProcletID proclet_id =
        kMaxProcletHeapVAddr - req->proclet_num * kMaxProcletHeapSize;
    client_->update_location(proclet_id, get_cfg_ip());
    return true;
  }

 private:
  ControllerClient *client_;
};

class Test {
 public:
  void run() {
    {
      rt::Preempt p;
      rt::PreemptGuard g(&p);
      PerfResolveObjAdapter perf_resolve_obj_adapter(
          get_runtime()->controller_client());
      Perf perf(perf_resolve_obj_adapter);
      perf.run(kNumThreads, kTargetMops, kPerfDurationUs);
      std::cout << "resolve_obj() mops = " << perf.get_real_mops() << std::endl;
    }

    {
      rt::Preempt p;
      rt::PreemptGuard g(&p);
      PerfAcquireMigrationDestAdapter perf_acquire_migration_dest_adapter(
          get_runtime()->controller_client());
      Perf perf(perf_acquire_migration_dest_adapter);
      perf.run(kNumThreads, kTargetMops, kPerfDurationUs);
      std::cout << "acquire_migration_obj() mops = " << perf.get_real_mops()
                << std::endl;
    }

    {
      rt::Preempt p;
      rt::PreemptGuard g(&p);
      PerfUpdateLocationAdapter perf_update_location_adapter(
          get_runtime()->controller_client());
      Perf perf(perf_update_location_adapter);
      perf.run(kNumThreads, kTargetMops, kPerfDurationUs);
      std::cout << "update_location() mops = " << perf.get_real_mops()
                << std::endl;
    }
  }

 private:
};

}  // namespace nu

int main(int argc, char **argv) {
  return runtime_main_init(argc, argv, [](int, char **) {
    Test test;
    test.run();
  });
}
