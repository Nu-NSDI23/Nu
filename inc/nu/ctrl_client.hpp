#pragma once

#include <functional>
#include <utility>

extern "C" {
#include <net/ip.h>
#include <runtime/net.h>
}
#include <net.h>
#include <sync.h>

#include <memory>

#include "nu/commons.hpp"
#include "nu/ctrl_server.hpp"
#include "nu/rpc_server.hpp"
#include "nu/runtime.hpp"
#include "nu/utils/rpc.hpp"

namespace nu {

class ControllerClient;

class NodeGuard {
 public:
  NodeGuard(ControllerClient *client, NodeIP ip);
  NodeGuard(const NodeGuard &) = delete;
  NodeGuard(NodeGuard &&) = delete;
  NodeGuard &operator=(const NodeGuard &) = delete;
  NodeGuard &operator=(NodeGuard &&) = delete;
  ~NodeGuard();
  operator bool() const;
  NodeIP get_ip() const;

 private:
  ControllerClient *client_;
  NodeIP ip_;
};

class ControllerClient {
 public:
  ControllerClient(NodeIP ctrl_server_ip, Runtime::Mode mode, lpid_t lpid,
                   bool isol);
  std::optional<std::pair<lpid_t, VAddrRange>> register_node(NodeIP ip,
                                                             MD5Val md5,
                                                             bool isol);
  std::optional<std::pair<ProcletID, NodeIP>> allocate_proclet(
      uint64_t capacity, NodeIP ip_hint);
  void destroy_proclet(VAddrRange heap_segment);
  NodeIP resolve_proclet(ProcletID id);
  NodeGuard acquire_node();
  std::pair<NodeGuard, Resource> acquire_migration_dest(bool has_mem_pressure,
                                                        Resource resource);
  void update_location(ProcletID id, NodeIP proclet_srv_ip);
  VAddrRange get_stack_cluster() const;
  std::vector<std::pair<NodeIP, Resource>> report_free_resource(
      Resource resource);
  void destroy_lp();

 private:
  lpid_t lpid_;
  VAddrRange stack_cluster_;
  RPCClient *rpc_client_;
  std::unique_ptr<rt::TcpConn> tcp_conn_;
  rt::Spin spin_;
  friend class NodeGuard;

  void release_node(NodeIP ip);
};
}  // namespace nu
