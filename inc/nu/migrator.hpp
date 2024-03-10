#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <span>
#include <unordered_set>
#include <vector>

extern "C" {
#include <base/compiler.h>
#include <runtime/net.h>
#include <runtime/tcp.h>
}
#include <net.h>
#include <sync.h>

#include "nu/ctrl_client.hpp"
#include "nu/rpc_server.hpp"
#include "nu/utils/archive_pool.hpp"
#include "nu/utils/rpc.hpp"
#include "nu/utils/slab.hpp"

namespace nu {

class Mutex;
class CondVar;
class Time;
struct ProcletHeader;
class MigrationGuard;

enum MigratorTCPOp_t {
  kCopyProclet,
  kSkipProclet,
  kMigrate,
  kEnablePoll,
  kDisablePoll,
  kRegisterCallBack,
  kDeregisterCallBack,
};

struct RPCReqForward {
  RPCReqType rpc_type = kForward;
  RPCReturnCode rc;
  RPCReturner returner;
  ArchivePool<>::IASStream *gc_ia_sstream;
  uint64_t payload_len;
  uint8_t payload[0];
};

struct RPCReqMigrateThreadAndRetVal {
  RPCReqType rpc_type = kMigrateThreadAndRetVal;
  RPCReturnCode (*handler)(ProcletHeader *, void *, uint64_t, uint8_t *);
  ProcletHeader *dest_proclet_header;
  void *dest_ret_val_ptr;
  uint64_t payload_len;
  uint8_t payload[0];
} __attribute__((packed));

struct ProcletMigrationTask {
  ProcletHeader *header;
  uint64_t capacity;
  uint64_t size;
};

class MigratorConnManager;

class MigratorConn {
 public:
  MigratorConn();
  ~MigratorConn();
  MigratorConn(const MigratorConn &) = delete;
  MigratorConn &operator=(const MigratorConn &) = delete;
  MigratorConn(MigratorConn &&);
  MigratorConn &operator=(MigratorConn &&);
  rt::TcpConn *get_tcp_conn();
  void release();

 private:
  rt::TcpConn *tcp_conn_;
  uint32_t ip_;
  MigratorConnManager *manager_;
  friend class MigratorConnManager;

  MigratorConn(rt::TcpConn *tcp_conn, uint32_t ip,
               MigratorConnManager *manager);
};

class MigratorConnManager {
 public:
  ~MigratorConnManager();
  MigratorConn get(uint32_t ip);

 private:
  rt::Spin spin_;
  std::unordered_map<uint32_t, std::stack<rt::TcpConn *>> pool_map_;
  friend class MigratorConn;

  void put(uint32_t ip, rt::TcpConn *tcp_conn);
};

class Migrator {
 public:
  constexpr static uint32_t kTransmitProcletNumThreads = 3;
  constexpr static uint32_t kDefaultNumReservedConns = 8;
  constexpr static uint32_t kPort = 8002;
  constexpr static float kMigrationThrottleGBs = 0;
  constexpr static uint32_t kMigrationDelayUs = 0;

  static_assert(kTransmitProcletNumThreads > 1);

  Migrator();
  ~Migrator();
  uint32_t migrate(
      const std::vector<std::pair<ProcletMigrationTask, Resource>> &tasks);
  void reserve_conns(uint32_t dest_server_ip);
  void forward_to_original_server(RPCReturnCode rc, RPCReturner *returner,
                                  uint64_t payload_len, const void *payload,
                                  ArchivePool<>::IASStream *ia_sstream);
  void forward_to_client(RPCReqForward &req);
  template <typename RetT>
  static MigrationGuard migrate_thread_and_ret_val(
      RPCReturnBuffer &&ret_val_buf, ProcletID dest_id, RetT *dest_ret_val_ptr,
      std::move_only_function<void()> &&cleanup_fn);
  template <typename RetT>
  static RPCReturnCode load_thread_and_ret_val(
      ProcletHeader *dest_proclet_header, void *raw_dest_ret_val_ptr,
      uint64_t payload_len, uint8_t *payload);

 private:
  constexpr static uint32_t kTCPListenBackLog = 64;
  std::unique_ptr<rt::TcpQueue> tcp_queue_;
  MigratorConnManager migrator_conn_mgr_;
  std::set<rt::TcpConn *> callback_conns_;
  bool callback_triggered_;
  std::unordered_set<uint32_t> delayed_srv_ips_;
  rt::Thread th_;

  void run_background_loop();
  void handle_copy_proclet(rt::TcpConn *c);
  void handle_load(rt::TcpConn *c);
  void handle_register_callback(rt::TcpConn *c);
  void handle_deregister_callback(rt::TcpConn *c);
  VAddrRange load_stack_cluster_mmap_task(rt::TcpConn *c);
  void transmit(rt::TcpConn *c, ProcletHeader *proclet_header,
                struct list_head *head);
  void update_proclet_location(rt::TcpConn *c, ProcletHeader *proclet_header);
  void transmit_stack_cluster_mmap_task(rt::TcpConn *c);
  void transmit_proclet(rt::TcpConn *c, ProcletHeader *proclet_header);
  void transmit_proclet_migration_tasks(
      rt::TcpConn *c, bool has_mem_pressure,
      const std::vector<ProcletMigrationTask> &tasks);
  void transmit_mutexes(rt::TcpConn *c, std::vector<Mutex *> mutexes);
  void transmit_condvars(rt::TcpConn *c, std::vector<CondVar *> condvars);
  void transmit_time(rt::TcpConn *c, Time *time);
  void transmit_threads(rt::TcpConn *c, const std::vector<thread_t *> &threads);
  void transmit_one_thread(rt::TcpConn *c, thread_t *thread);
  bool try_mark_proclet_migrating(ProcletHeader *proclet_header);
  void load(rt::TcpConn *c);
  bool load_proclet(rt::TcpConn *c, ProcletHeader *proclet_header,
                    uint64_t capacity);
  std::pair<bool, std::vector<ProcletMigrationTask>>
  load_proclet_migration_tasks(rt::TcpConn *c);
  void populate_proclets(std::vector<ProcletMigrationTask> &tasks);
  void depopulate_proclet(ProcletHeader *proclet_header);
  void load_mutexes(rt::TcpConn *c, ProcletHeader *proclet_header);
  void load_condvars(rt::TcpConn *c, ProcletHeader *proclet_header);
  void load_time_and_mark_proclet_present(rt::TcpConn *c,
                                          ProcletHeader *proclet_header);
  void load_threads(rt::TcpConn *c, ProcletHeader *proclet_header);
  thread_t *load_one_thread(rt::TcpConn *c, ProcletHeader *proclet_header);
  void aux_handlers_enable_polling(uint32_t dest_ip);
  void aux_handlers_disable_polling();
  void callback();
  uint32_t __migrate(const NodeGuard &dest_guard, bool mem_pressure,
                     const std::vector<ProcletMigrationTask> &tasks);
  void pause_migrating_threads(ProcletHeader *proclet_header);
  void post_migration_cleanup(ProcletHeader *proclet_header);
  template <typename RetT>
  static void snapshot_thread_and_ret_val(std::unique_ptr<std::byte[]> *req_buf,
                                          uint64_t *req_buf_len,
                                          RPCReturnBuffer &&ret_val_buf,
                                          ProcletID dest_id,
                                          RetT *dest_ret_val_ptr);
  static void switch_stack_and_transmit(std::unique_ptr<std::byte[]> *req_buf,
                                        uint64_t req_buf_len, ProcletID dest_id,
                                        uint8_t *proclet_stack);
  static void transmit_thread_and_ret_val(std::unique_ptr<std::byte[]> *req_buf,
                                          uint64_t req_buf_len,
                                          ProcletID dest_id,
                                          uint8_t *proclet_stack);
};

}  // namespace nu

#include "nu/impl/migrator.ipp"
