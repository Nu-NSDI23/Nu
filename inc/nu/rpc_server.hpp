#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "nu/utils/rpc.hpp"

namespace nu {

enum RPCReqEnum {
  // Migrator
  kReserveConns,
  kForward,
  kMigrateThreadAndRetVal,
  // Controller
  kRegisterNode,
  kAllocateProclet,
  kDestroyProclet,
  kResolveProclet,
  kAcquireMigrationDest,
  kAcquireNode,
  kReleaseNode,
  kUpdateLocation,
  kReportFreeResource,
  kDestroyLP,
  // Proclet server,
  kProcletCall,
  kGCStack,
  kShutdown
};

using RPCReqType = uint8_t;

class RPCServer {
 public:
  constexpr static uint32_t kPort = 12345;

  RPCServer();

 private:
  RPCServerListener listener_;
  friend class Migrator;

  void handler_fn(std::span<std::byte> args, RPCReturner *returner);
  void dec_ref_cnt();
};

}  // namespace nu
