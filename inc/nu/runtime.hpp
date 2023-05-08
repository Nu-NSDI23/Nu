#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

extern "C" {
#include <runtime/net.h>
}

#include "nu/commons.hpp"
#include "nu/rpc_server.hpp"
#include "nu/stack_manager.hpp"
#include "nu/utils/archive_pool.hpp"
#include "nu/utils/caladan.hpp"

namespace nu {

struct ProcletHeader;
class ProcletServer;
class ProcletManager;
class ControllerClient;
class ControllerServer;
class RPCClientMgr;
class Migrator;
class RPCServer;
class PressureHandler;
class ResourceReporter;
template <typename T>
class WeakProclet;
class MigrationGuard;
class RPCReturner;
class SlabAllocator;
class Caladan;

struct RPCReqReserveConns {
  RPCReqType rpc_type = kReserveConns;
  uint32_t dest_server_ip;
} __attribute__((packed));

struct RPCReqShutdown {
  RPCReqType rpc_type = kShutdown;
};

class Runtime {
 public:
  enum Mode { kMainServer, kServer, kController };

  ~Runtime();
  SlabAllocator *runtime_slab();
  StackManager *stack_manager();
  ArchivePool<> *archive_pool();
  ProcletManager *proclet_manager();
  PressureHandler *pressure_handler();
  ControllerClient *controller_client();
  RPCClientMgr *rpc_client_mgr();
  RPCServer *rpc_server();
  Migrator *migrator();
  ControllerServer *controller_server();
  ProcletServer *proclet_server();
  ResourceReporter *resource_reporter();
  Caladan *caladan();
  void reserve_conns(uint32_t ip);
  void init_base();
  void init_runtime_heap();
  void init_as_controller();
  void init_as_server(uint32_t remote_ctrl_ip, lpid_t lpid, bool isol);
  template <typename Cls, typename... A0s, typename... A1s>
  bool run_within_proclet_env(void *proclet_base, void (*fn)(A0s...),
                              A1s &&... args);
  SlabAllocator *switch_slab(SlabAllocator *slab);
  SlabAllocator *switch_to_runtime_slab();
  void *switch_stack(void *new_rsp);
  void switch_to_runtime_stack();
  VAddrRange get_proclet_stack_range(thread_t *thread);
  SlabAllocator *get_current_proclet_slab();
  ProcletHeader *get_current_proclet_header();
  template <typename T>
  T *get_current_root_obj();
  template <typename T>
  T *get_root_obj(ProcletID id);
  template <typename T>
  WeakProclet<T> get_current_weak_proclet();
  template <typename T>
  static WeakProclet<T> to_weak_proclet(T *root_obj);
  template <typename T>
  static ProcletHeader *to_proclet_header(T *root_obj);
  // Detach the current thread from the current proclet.
  void detach(const MigrationGuard &g);
  // Attach the current thread to the specified proclet and disable migration.
  // Return std::nullopt if failed, or MigrationGuard if succeeded.
  // No migration will happen during its invocation.
  std::optional<MigrationGuard> attach_and_disable_migration(
      ProcletHeader *proclet_header);
  // Reattach the current thread from an old proclet (with migration disabled)
  // into a new proclet. Return std::nullopt if failed, or MigrationGuard of the
  // new proclet if succeeded. No migration will happen during its invocation.
  std::optional<MigrationGuard> reattach_and_disable_migration(
      ProcletHeader *new_header, const MigrationGuard &old_guard);
  void send_rpc_resp_ok(ArchivePool<>::OASStream *oa_sstream,
                        ArchivePool<>::IASStream *ia_sstream,
                        RPCReturner *returner);
  void send_rpc_resp_wrong_client(RPCReturner *returner);
  void shutdown(RPCReturner *returner);

 private:
  SlabAllocator *runtime_slab_;
  ControllerServer *controller_server_;
  RPCClientMgr *rpc_client_mgr_;
  Caladan *caladan_;
  ArchivePool<> *archive_pool_;
  ProcletServer *proclet_server_;
  RPCServer *rpc_server_;
  Migrator *migrator_;
  ControllerClient *controller_client_;
  ProcletManager *proclet_manager_;
  PressureHandler *pressure_handler_;
  ResourceReporter *resource_reporter_;
  StackManager *stack_manager_;

  friend int runtime_main_init(int, char **, std::function<void(int, char **)>);
  friend int ctrl_main(int, char **);

  Runtime();
  Runtime(uint32_t remote_ctrl_ip, Mode mode, lpid_t lpid, bool isol);
  template <typename Cls, typename... A0s, typename... A1s>
  bool __run_within_proclet_env(void *proclet_base, void (*fn)(A0s...),
                                       A1s &&... args);
  std::optional<MigrationGuard> __reattach_and_disable_migration(
      ProcletHeader *proclet_header);
  void destroy();
  void destroy_base();
};

class RuntimeSlabGuard {
 public:
  RuntimeSlabGuard();
  ~RuntimeSlabGuard();
  RuntimeSlabGuard(const RuntimeSlabGuard &) = delete;
  RuntimeSlabGuard &operator=(const RuntimeSlabGuard &) = delete;
  RuntimeSlabGuard(RuntimeSlabGuard &&) = delete;
  RuntimeSlabGuard &operator=(RuntimeSlabGuard &&) = delete;

 private:
  SlabAllocator *original_slab_;
};

class ProcletSlabGuard {
 public:
  ProcletSlabGuard(SlabAllocator *slab);
  ~ProcletSlabGuard();
  ProcletSlabGuard(const ProcletSlabGuard &) = delete;
  ProcletSlabGuard &operator=(const ProcletSlabGuard &) = delete;
  ProcletSlabGuard &operator=(ProcletSlabGuard &&) = delete;

 private:
  SlabAllocator *original_slab_;
};

// Once constructed, it guarantees that the current proclet won't be migrated.
// However, migration might happen during its construction.
class MigrationGuard {
 public:
  MigrationGuard();
  ~MigrationGuard();
  MigrationGuard(const MigrationGuard &) = delete;
  MigrationGuard &operator=(const MigrationGuard &) = delete;
  MigrationGuard(MigrationGuard &&);
  MigrationGuard &operator=(MigrationGuard &&);
  ProcletHeader *header() const;
  template <typename F>
  auto enable_for(F &&f);
  void reset();
  void release();

 private:
  ProcletHeader *header_;
  friend class Runtime;

  MigrationGuard(ProcletHeader *header);
};

int runtime_main_init(int argc, char **argv,
                      std::function<void(int argc, char **argv)> main_func);
Runtime *get_runtime();

}  // namespace nu

#include "nu/impl/runtime.ipp"
