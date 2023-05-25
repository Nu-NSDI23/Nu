#pragma once

#include <cstdint>
#include <memory>
#include <sstream>

extern "C" {
#include <runtime/net.h>
}
#include <sync.h>

#include "nu/utils/archive_pool.hpp"
#include "nu/utils/counter.hpp"
#include "nu/utils/rpc.hpp"
#include "nu/utils/trace_logger.hpp"

namespace nu {

struct ProcletHeader;

class ProcletServer {
 public:
  ProcletServer();
  ~ProcletServer();
  netaddr get_addr() const;
  template <typename Cls>
  static void update_ref_cnt(ArchivePool<>::IASStream *ia_sstream,
                             RPCReturner *returner);
  template <typename Cls>
  static void update_ref_cnt_locally(MigrationGuard *callee_guard,
                                     ProcletHeader *caller_header,
                                     ProcletHeader *callee_header, int delta);
  template <typename Cls, typename... As>
  static void construct_proclet(ArchivePool<>::IASStream *ia_sstream,
                                RPCReturner *returner);
  template <typename Cls, typename... As>
  static void construct_proclet_locally(MigrationGuard &&caller_guard,
                                        void *base, uint64_t size, bool pinned,
                                        As &&... args);
  template <bool MigrEn, bool CPUMon, bool CPUSamp, typename Cls, typename RetT,
            typename FnPtr, typename... S1s>
  static void run_closure(ArchivePool<>::IASStream *ia_sstream,
                          RPCReturner *returner);
  template <bool MigrEn, bool CPUMon, bool CPUSamp, typename Cls, typename RetT,
            typename FnPtr, typename... Ss>
  static void run_closure_locally(MigrationGuard *callee_migration_guard,
                                  const ProcletSlabGuard &callee_slab_guard,
                                  RetT *caller_ptr,
                                  ProcletHeader *caller_header,
                                  ProcletHeader *callee_header, FnPtr fn_ptr,
                                  std::tuple<Ss...> &&states);

 private:
  using GenericHandler = void (*)(ArchivePool<>::IASStream *ia_sstream,
                                  RPCReturner *returner);

  TraceLogger trace_logger_;
  Counter ref_cnt_;
  friend class RPCServer;
  friend class Migrator;

  void dec_ref_cnt();
  static void forward(RPCReturnCode rc, RPCReturner *returner,
                      const void *payload, uint64_t payload_len);
  void parse_and_run_handler(std::span<std::byte> args, RPCReturner *returner);
  template <typename Cls, typename... As>
  static void __construct_proclet(MigrationGuard *callee_guard, Cls *obj,
                                  ArchivePool<>::IASStream *ia_sstream,
                                  RPCReturner returner);
  template <typename Cls>
  static void __update_ref_cnt(MigrationGuard *callee_guard, Cls *obj,
                               ArchivePool<>::IASStream *ia_sstream,
                               RPCReturner returner, int delta,
                               bool *destructed);
  template <bool MigrEn, bool CPUMon, bool CPUSamp, typename Cls, typename RetT,
            typename FnPtr, typename... S1s>
  static void __run_closure(MigrationGuard *callee_guard, Cls *obj,
                            ArchivePool<>::IASStream *ia_sstream,
                            RPCReturner returner);
};
}  // namespace nu

#include "nu/impl/proclet_server.ipp"
