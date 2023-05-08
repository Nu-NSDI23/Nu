#pragma once

#include <cstdint>
#include <list>
#include <set>
#include <stack>
#include <map>
#include <utility>

extern "C" {
#include <runtime/net.h>
#include <runtime/tcp.h>
}
#include <net.h>

#include "nu/commons.hpp"
#include "nu/rpc_client_mgr.hpp"
#include "nu/utils/cond_var.hpp"
#include "nu/utils/md5.hpp"
#include "nu/utils/mutex.hpp"

namespace nu {

// This is a logical node instead of a physical node.
struct NodeStatus {
  constexpr static float kEWMAWeight = 0.25;
  // Should be consistent with iokernel's IAS_PS_MEM_LOW_MB.
  constexpr static uint32_t kMemLowWaterMarkMBs = 1024;

  NodeStatus(bool isol);

  bool isol;
  bool acquired;
  Resource free_resource;
  CondVar cv;

  bool has_enough_cpu_resource(Resource resource) const;
  bool has_enough_mem_resource(Resource resource) const;
  bool has_enough_resource(Resource resource) const;
  void update_free_resource(Resource resource);
};

struct Node {
  NodeIP ip;
  NodeStatus status;
};

struct LPInfo {
  std::map<NodeIP, NodeStatus> node_statuses;
  std::map<NodeIP, NodeStatus>::iterator rr_iter;
  bool destroying;

  LPInfo();
};

struct ProcletHeapSegment {
  VAddrRange range;
  NodeIP prev_host;
};

class Controller {
 public:
  constexpr static bool kEnableBinaryVerification = true;

  Controller();
  ~Controller();
  std::optional<std::pair<lpid_t, VAddrRange>> register_node(NodeIP ip,
                                                             lpid_t lpid,
                                                             MD5Val md5,
                                                             bool isol);
  void destroy_lp(lpid_t lpid, NodeIP requestor_ip);
  std::optional<std::pair<ProcletID, NodeIP>> allocate_proclet(
      uint64_t capacity, lpid_t lpid, NodeIP ip_hint);
  void destroy_proclet(VAddrRange heap_segment);
  NodeIP resolve_proclet(ProcletID id);
  std::pair<NodeIP, Resource> acquire_migration_dest(lpid_t lpid,
                                                     NodeIP requestor_ip,
                                                     bool has_mem_pressure,
                                                     Resource resource);
  bool acquire_node(lpid_t lpid, NodeIP ip);
  void release_node(lpid_t lpid, NodeIP ip);
  void update_location(ProcletID id, NodeIP proclet_srv_ip);
  std::vector<std::pair<NodeIP, Resource>> report_free_resource(
      lpid_t lpid, NodeIP ip, Resource free_resource);

 private:
  constexpr static auto kNumProcletSegmentBuckets =
      bsr_64(kMaxProcletHeapSize) - bsr_64(kMinProcletHeapSize) + 1;
  std::stack<ProcletHeapSegment>
      free_proclet_heap_segments_[kNumProcletSegmentBuckets];
  std::stack<VAddrRange> free_stack_cluster_segments_;  // One segment per Node.
  std::set<lpid_t> free_lpids_;
  std::map<lpid_t, MD5Val> lpid_to_md5_;
  std::map<lpid_t, LPInfo> lpid_to_info_;
  std::map<ProcletID, NodeIP> proclet_id_to_ip_;
  bool done_;
  Mutex mutex_;

  NodeIP select_node_for_proclet(lpid_t lpid, NodeIP ip_hint,
                                 const ProcletHeapSegment &segment);
  bool update_node(std::set<Node>::iterator iter);
};
}  // namespace nu

#include "nu/impl/ctrl.ipp"
