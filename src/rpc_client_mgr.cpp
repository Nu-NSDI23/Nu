#include "nu/rpc_client_mgr.hpp"
#include "nu/ctrl_client.hpp"

namespace nu {

NodeID RPCClientMgr::get_node_id_by_node_ip(NodeIP ip) {
  NodeID id;
  rt::ScopedLock<rt::Mutex> guard(&mutex_);
  if (node_ip_to_node_id_map_.find(ip) == node_ip_to_node_id_map_.end()) {
    id = node_ip_to_node_id_map_[ip] = next_node_id_++;
    BUG_ON(!next_node_id_);  // Overflow.
  } else {
    id = node_ip_to_node_id_map_[ip];
  }
  return id;
}

RPCClientMgr::RPCClientMgr(uint16_t port) : port_(port), next_node_id_(0) {}

RPCClient *RPCClientMgr::get_client(NodeInfo info) {
  auto &client = rpc_clients_[info.id];
  if (unlikely(!client)) {
    rt::ScopedLock<rt::Mutex> guard(&mutex_);
    if (likely(!client)) {
      client = RPCClient::Dial(netaddr(info.ip, port_));
    }
  }

  return client.get();
}

void RPCClientMgr::remove_client(NodeInfo info) {
  auto &client = rpc_clients_[info.id];
  BUG_ON(!client);
  client.reset();
}

RPCClient *RPCClientMgr::get_by_ip(NodeIP ip) {
  NodeInfo info;
  info.ip = ip;
  info.id = get_node_id_by_node_ip(ip);
  return get_client(info);
}

void RPCClientMgr::remove_by_ip(NodeIP ip) {
  NodeInfo info;
  info.ip = ip;
  info.id = get_node_id_by_node_ip(ip);
  remove_client(info);
}

RPCClientMgr::NodeInfo RPCClientMgr::get_info(ProcletID proclet_id) {
retry:
  auto slab_id = to_slab_id(proclet_id);
  auto info = rem_id_to_node_info_[slab_id];

  if (unlikely(!info.raw)) {
    rt::MutexGuard g(&node_info_mutexes_[slab_id]);
    auto &info_ref = rem_id_to_node_info_[slab_id];
    if (!info_ref.raw) {
      auto ip = get_runtime()->controller_client()->resolve_proclet(proclet_id);
      BUG_ON(!ip);
      NodeInfo info;
      info.ip = ip;
      info.id = get_node_id_by_node_ip(ip);
      info_ref = info;
    }
    goto retry;
  }

  return info;
}

RPCClient *RPCClientMgr::get_by_proclet_id(ProcletID proclet_id) {
  return get_client(get_info(proclet_id));
}

NodeIP RPCClientMgr::get_ip_by_proclet_id(ProcletID proclet_id) {
  return get_info(proclet_id).ip;
}

void RPCClientMgr::invalidate_cache(ProcletID proclet_id, RPCClient *old_client) {
  auto slab_id = to_slab_id(proclet_id);
  auto &info_ref = rem_id_to_node_info_[slab_id];
  if (info_ref.raw) {
    if (info_ref.ip != old_client->GetAddr().ip) {
      return;
    } else {
      info_ref.raw = 0;
    }
  }
}

void RPCClientMgr::update_cache(ProcletID proclet_id, NodeIP ip) {
  auto slab_id = to_slab_id(proclet_id);
  rt::MutexGuard g(&node_info_mutexes_[slab_id]);

  auto &info = rem_id_to_node_info_[slab_id];
  info.ip = ip;
  info.id = get_node_id_by_node_ip(ip);
}

}  // namespace nu
