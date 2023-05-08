#pragma once

#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>

#include <sync.h>

#include "nu/runtime_alloc.hpp"
#include "nu/utils/rcu_hash_map.hpp"
#include "nu/utils/rpc.hpp"

namespace nu {

using NodeID = uint16_t;

class RPCClientMgr {
 public:
  RPCClientMgr(uint16_t port);
  RPCClient *get_by_proclet_id(ProcletID proclet_id);
  RPCClient *get_by_ip(NodeIP ip);
  NodeIP get_ip_by_proclet_id(ProcletID proclet_id);
  void remove_by_ip(NodeIP ip);
  void update_cache(ProcletID proclet_id, NodeIP ip);
  void invalidate_cache(ProcletID proclet_id, RPCClient *old_client);

 private:
  union NodeInfo {  // Supports atomic assignment.
    struct {
      NodeIP ip;
      NodeID id;
    };
    uint64_t raw = 0;

    NodeInfo &operator=(const NodeInfo &o) {
      raw = o.raw;
      return *this;
    }
  };
  static_assert(sizeof(NodeInfo) == sizeof(NodeInfo::raw));

  uint16_t port_;
  NodeInfo rem_id_to_node_info_[get_max_slab_id() + 1];
  rt::Mutex node_info_mutexes_[get_max_slab_id() + 1];
  std::unordered_map<NodeIP, NodeID> node_ip_to_node_id_map_;
  NodeID next_node_id_;
  std::unique_ptr<RPCClient>
      rpc_clients_[std::numeric_limits<NodeID>::max() + 1];
  rt::Mutex mutex_;

  NodeID get_node_id_by_node_ip(NodeIP ip);
  RPCClient *get_client(NodeInfo info);
  void remove_client(NodeInfo info);
  NodeInfo get_info(ProcletID proclet_id);
};
}  // namespace nu
