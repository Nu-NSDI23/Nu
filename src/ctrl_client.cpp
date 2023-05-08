extern "C" {
#include <net/ip.h>
#include <runtime/net.h>
}

#include "nu/ctrl_client.hpp"
#include "nu/ctrl_server.hpp"
#include "nu/migrator.hpp"
#include "nu/proclet_server.hpp"
#include "nu/runtime.hpp"
#include "nu/utils/caladan.hpp"

namespace nu {

ControllerClient::ControllerClient(NodeIP ctrl_server_ip, Runtime::Mode mode,
                                   lpid_t lpid, bool isol)
    : lpid_(lpid),
      rpc_client_(get_runtime()->rpc_client_mgr()->get_by_ip(ctrl_server_ip)) {
  netaddr laddr{.ip = 0, .port = 0};
  netaddr raddr{.ip = ctrl_server_ip, .port = ControllerServer::kPort};
  tcp_conn_.reset(rt::TcpConn::Dial(laddr, raddr));
  BUG_ON(!tcp_conn_);

  auto md5 = get_self_md5();
  auto optional = register_node(get_cfg_ip(), md5, isol);
  BUG_ON(!optional);
  BUG_ON(lpid_ && lpid_ != optional->first);
  std::tie(lpid_, stack_cluster_) = *optional;
  std::cout << "running with lpid = " << lpid_ << std::endl;
}

std::optional<std::pair<lpid_t, VAddrRange>> ControllerClient::register_node(
    NodeIP ip, MD5Val md5, bool isol) {
  RPCReqRegisterNode req;
  req.ip = ip;
  req.lpid = lpid_;
  req.md5 = md5;
  req.isol = isol;
  RPCReturnBuffer return_buf;
  auto rc = rpc_client_->Call(to_span(req), &return_buf);
  BUG_ON(rc != kOk);
  auto &resp = from_span<RPCRespRegisterNode>(return_buf.get_buf());
  if (resp.empty) {
    return std::nullopt;
  } else {
    auto lpid = resp.lpid;
    auto stack_cluster = resp.stack_cluster;
    return std::make_pair(lpid, stack_cluster);
  }
}

std::optional<std::pair<ProcletID, NodeIP>> ControllerClient::allocate_proclet(
    uint64_t capacity, NodeIP ip_hint) {
  RPCReqAllocateProclet req;
  req.capacity = capacity;
  req.lpid = lpid_;
  req.ip_hint = ip_hint;
  RPCReturnBuffer return_buf;
  BUG_ON(rpc_client_->Call(to_span(req), &return_buf) != kOk);
  auto &resp = from_span<RPCRespAllocateProclet>(return_buf.get_buf());
  if (resp.empty) {
    return std::nullopt;
  } else {
    auto id = resp.id;
    auto server_ip = resp.server_ip;
    return std::make_pair(id, server_ip);
  }
}

void ControllerClient::destroy_proclet(VAddrRange heap_segment) {
  RPCReqDestroyProclet req;
  req.heap_segment = heap_segment;
  RPCReturnBuffer return_buf;
  BUG_ON(rpc_client_->Call(to_span(req), &return_buf) != kOk);
}

NodeIP ControllerClient::resolve_proclet(ProcletID id) {
  RPCReqResolveProclet req;
  req.id = id;
  RPCReturnBuffer return_buf;
  BUG_ON(rpc_client_->Call(to_span(req), &return_buf) != kOk);
  auto &resp = from_span<RPCRespResolveProclet>(return_buf.get_buf());
  return resp.ip;
}

std::pair<NodeGuard, Resource> ControllerClient::acquire_migration_dest(
    bool has_mem_pressure, Resource resource) {
  rt::SpinGuard g(&spin_);

  RPCReqAcquireMigrationDest req;
  req.lpid = lpid_;
  req.src_ip = get_cfg_ip();
  req.has_mem_pressure = has_mem_pressure;
  req.resource = resource;
  BUG_ON(tcp_conn_->WriteFull(&req, sizeof(req), /* nt = */ false,
                              /* poll = */ true) != sizeof(req));

  RPCRespAcquireMigrationDest resp;
  BUG_ON(tcp_conn_->ReadFull(&resp, sizeof(resp), /* nt = */ false,
                             /* poll = */ true) != sizeof(resp));
  auto ip = resp.ip;
  resource = resp.resource;
  return std::pair<NodeGuard, Resource>(std::piecewise_construct,
                                        std::make_tuple(this, ip),
                                        std::make_tuple(resource));
}

NodeGuard ControllerClient::acquire_node() {
  rt::SpinGuard g(&spin_);

  RPCReqAcquireNode req;
  req.lpid = lpid_;
  req.ip = get_cfg_ip();
  BUG_ON(tcp_conn_->WriteFull(&req, sizeof(req), /* nt = */ false,
                              /* poll = */ true) != sizeof(req));

  RPCRespAcquireNode resp;
  BUG_ON(tcp_conn_->ReadFull(&resp, sizeof(resp), /* nt = */ false,
                             /* poll = */ true) != sizeof(resp));
  return NodeGuard(this, resp.succeed ? req.ip : 0);
}

void ControllerClient::update_location(ProcletID id, NodeIP proclet_srv_ip) {
  rt::SpinGuard g(&spin_);

  RPCReqUpdateLocation req;
  req.id = id;
  req.proclet_srv_ip = proclet_srv_ip;
  BUG_ON(tcp_conn_->WriteFull(&req, sizeof(req), /* nt = */ false,
                              /* poll = */ true) != sizeof(req));
}

VAddrRange ControllerClient::get_stack_cluster() const {
  return stack_cluster_;
}

std::vector<std::pair<NodeIP, Resource>> ControllerClient::report_free_resource(
    Resource resource) {
  rt::SpinGuard g(&spin_);

  RPCReqReportFreeResource req;
  req.lpid = lpid_;
  req.ip = get_cfg_ip();
  req.resource = resource;
  BUG_ON(tcp_conn_->WriteFull(&req, sizeof(req), /* nt = */ false,
                              /* poll = */ true) != sizeof(req));
  std::size_t num_nodes;
  BUG_ON(tcp_conn_->ReadFull(&num_nodes, sizeof(num_nodes), /* nt = */ false,
                             /* poll = */ true) != sizeof(num_nodes));
  std::vector<std::pair<NodeIP, Resource>> global_free_resources;
  global_free_resources.resize(num_nodes);
  ssize_t size_bytes = std::span(global_free_resources).size_bytes();
  BUG_ON(tcp_conn_->ReadFull(global_free_resources.data(), size_bytes,
                             /* nt = */ false,
                             /* poll = */ true) != size_bytes);
  return global_free_resources;
}

void ControllerClient::release_node(NodeIP ip) {
  rt::SpinGuard g(&spin_);

  RPCReqReleaseNode req;
  req.lpid = lpid_;
  req.ip = ip;
  BUG_ON(tcp_conn_->WriteFull(&req, sizeof(req), /* nt = */ false,
                              /* poll = */ true) != sizeof(req));
}

void ControllerClient::destroy_lp() {
  RPCReqDestroyLP req;
  req.lpid = lpid_;
  req.ip = get_runtime()->caladan()->get_ip();
  RPCReturnBuffer return_buf;
  auto rc = rpc_client_->Call(to_span(req), &return_buf);
  BUG_ON(rc != kOk);
}

NodeGuard::NodeGuard(ControllerClient *client, NodeIP ip)
    : client_(client), ip_(ip) {}

NodeGuard::~NodeGuard() {
  if (ip_) {
    client_->release_node(ip_);
  }
}

NodeGuard::operator bool() const { return ip_; }

NodeIP NodeGuard::get_ip() const { return ip_; }

}  // namespace nu
