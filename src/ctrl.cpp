#include <cereal/archives/binary.hpp>
#include <cstdint>
#include <limits>

extern "C" {
#include <base/assert.h>
#include <base/compiler.h>
#include <net/ip.h>
#include <runtime/timer.h>
}
#include <thread.h>

#include "nu/commons.hpp"
#include "nu/ctrl.hpp"
#include "nu/ctrl_server.hpp"
#include "nu/migrator.hpp"
#include "nu/proclet_server.hpp"
#include "nu/rpc_client_mgr.hpp"
#include "nu/runtime.hpp"
#include "nu/utils/future.hpp"
#include "nu/utils/scoped_lock.hpp"

namespace nu {

constexpr NodeIP get_proclet_segment_bucket_id(uint64_t capacity) {
  auto bsr_capacity = bsr_64(capacity);
  auto bsr_min = bsr_64(kMinProcletHeapSize);
  BUG_ON(bsr_capacity < bsr_min);
  return bsr_capacity - bsr_min;
}

LPInfo::LPInfo() : rr_iter(node_statuses.end()), destroying(false) {}

Controller::Controller() {
  for (lpid_t lpid = 1; lpid < std::numeric_limits<lpid_t>::max(); lpid++) {
    free_lpids_.insert(lpid);
  }

  auto &highest_bucket =
      free_proclet_heap_segments_[kNumProcletSegmentBuckets - 1];
  for (uint64_t start_addr = kMinProcletHeapVAddr;
       start_addr + kMaxProcletHeapSize <= kMaxProcletHeapVAddr;
       start_addr += kMaxProcletHeapSize) {
    VAddrRange range = {.start = start_addr,
                        .end = start_addr + kMaxProcletHeapSize};
    highest_bucket.push({range, 0});
  }

  for (uint64_t start_addr = kMinStackClusterVAddr;
       start_addr + kStackClusterSize <= kMaxStackClusterVAddr;
       start_addr += kStackClusterSize) {
    VAddrRange range = {.start = start_addr,
                        .end = start_addr + kStackClusterSize};
    free_stack_cluster_segments_.push(range);
  }

  done_ = false;
}

Controller::~Controller() {
  done_ = true;
  barrier();
}

std::optional<std::pair<lpid_t, VAddrRange>> Controller::register_node(
    NodeIP ip, lpid_t lpid, MD5Val md5, bool isol) {
  ScopedLock lock(&mutex_);

  if (lpid) {
    auto info_iter = lpid_to_info_.find(lpid);
    if (info_iter != lpid_to_info_.end()) {
      if (unlikely(info_iter->second.destroying)) {
        return std::nullopt;
      }
    }

    auto lpid_iter = free_lpids_.find(lpid);
    if (lpid_iter == free_lpids_.end()) {
      if constexpr (kEnableBinaryVerification) {
        if (unlikely(lpid_to_md5_[lpid] != md5)) {
          return std::nullopt;
        }
      }
    } else {
      free_lpids_.erase(lpid_iter);
      lpid_to_md5_[lpid] = md5;
    }
  } else {
    if (unlikely(free_lpids_.empty())) {
      return std::nullopt;
    }
    auto begin_lpid_iter = free_lpids_.begin();
    lpid = *begin_lpid_iter;
    free_lpids_.erase(begin_lpid_iter);
    lpid_to_md5_[lpid] = md5;
  }

  if (unlikely(free_stack_cluster_segments_.empty())) {
    free_lpids_.insert(lpid);
    return std::nullopt;
  }

  auto stack_cluster = free_stack_cluster_segments_.top();
  free_stack_cluster_segments_.pop();

  auto &node_statuses = lpid_to_info_[lpid].node_statuses;
  for (const auto &[existing_node_ip, _] : node_statuses) {
    auto *client = get_runtime()->rpc_client_mgr()->get_by_ip(existing_node_ip);
    RPCReqReserveConns req;
    RPCReturnBuffer return_buf;
    req.dest_server_ip = ip;
    BUG_ON(client->Call(to_span(req), &return_buf) != kOk);
  }

  auto *client = get_runtime()->rpc_client_mgr()->get_by_ip(ip);
  for (const auto &[existing_node_ip, _] : node_statuses) {
    RPCReqReserveConns req;
    RPCReturnBuffer return_buf;
    req.dest_server_ip = existing_node_ip;
    BUG_ON(client->Call(to_span(req), &return_buf) != kOk);
  }

  auto [iter, success] = node_statuses.try_emplace(ip, isol);
  BUG_ON(!success);
  return std::make_pair(lpid, stack_cluster);
}

void Controller::destroy_lp(lpid_t lpid, NodeIP requestor_ip) {
  std::vector<Future<void>> futures;
  std::map<lpid_t, LPInfo>::iterator info_iter;

  {
    ScopedLock lock(&mutex_);

    BUG_ON(free_lpids_.count(lpid));
    BUG_ON(!lpid_to_md5_.erase(lpid));

    info_iter = lpid_to_info_.find(lpid);
    BUG_ON(info_iter->second.destroying);
    info_iter->second.destroying = true;

    for (auto &[ip, status] : info_iter->second.node_statuses) {
      while (unlikely(status.acquired)) {
        status.cv.wait(&mutex_);
      }

      if (ip != requestor_ip) {
        futures.emplace_back(async([ip] {
          RPCReqShutdown req;
          RPCReturnBuffer return_buf;
          RPCReturnCode rc;
          auto *client = get_runtime()->rpc_client_mgr()->get_by_ip(ip);
          rc = client->Call(to_span(req), &return_buf);
          BUG_ON(rc != kOk);
        }));
      }
    }
  }

  futures.clear();

  {
    ScopedLock lock(&mutex_);

    BUG_ON(!free_lpids_.emplace(lpid).second);
    for (const auto &[ip, _] : info_iter->second.node_statuses) {
      get_runtime()->rpc_client_mgr()->remove_by_ip(ip);
    }
    lpid_to_info_.erase(info_iter);
  }
}

std::optional<std::pair<ProcletID, NodeIP>> Controller::allocate_proclet(
    uint64_t capacity, lpid_t lpid, NodeIP ip_hint) {
  ScopedLock lock(&mutex_);

  auto &bucket =
      free_proclet_heap_segments_[get_proclet_segment_bucket_id(capacity)];
  if (unlikely(bucket.empty())) {
    auto &highest_bucket =
        free_proclet_heap_segments_[kNumProcletSegmentBuckets - 1];
    if (unlikely(highest_bucket.empty())) {
      return std::nullopt;
    }
    auto max_segment = highest_bucket.top();
    highest_bucket.pop();
    for (auto start_addr = max_segment.range.start;
         start_addr < max_segment.range.end; start_addr += capacity) {
      VAddrRange range = {.start = start_addr, .end = start_addr + capacity};
      bucket.push({range, max_segment.prev_host});
    }
  }

  auto segment = bucket.top();
  bucket.pop();
  auto start_addr = segment.range.start;
  auto id = start_addr;
  auto node_ip = select_node_for_proclet(lpid, ip_hint, segment);
  if (unlikely(!node_ip)) {
    return std::nullopt;
  }
  auto [iter, _] = proclet_id_to_ip_.try_emplace(id);
  iter->second = node_ip;
  return std::make_pair(id, node_ip);
}

void Controller::destroy_proclet(VAddrRange proclet_segment) {
  ScopedLock lock(&mutex_);

  auto capacity = proclet_segment.end - proclet_segment.start;
  auto &bucket =
      free_proclet_heap_segments_[get_proclet_segment_bucket_id(capacity)];
  auto proclet_id = proclet_segment.start;
  auto iter = proclet_id_to_ip_.find(proclet_id);
  if (unlikely(iter == proclet_id_to_ip_.end())) {
    WARN();
    return;
  }
  bucket.push({proclet_segment, iter->second});
  proclet_id_to_ip_.erase(iter);
}

NodeIP Controller::resolve_proclet(ProcletID id) {
  ScopedLock lock(&mutex_);

  auto iter = proclet_id_to_ip_.find(id);
  return iter != proclet_id_to_ip_.end() ? iter->second : 0;
}

NodeIP Controller::select_node_for_proclet(lpid_t lpid, NodeIP ip_hint,
                                           const ProcletHeapSegment &segment) {
  auto &[node_statuses, rr_iter, _] = lpid_to_info_[lpid];
  BUG_ON(node_statuses.empty());

  if (ip_hint) {
    auto iter = node_statuses.find(ip_hint);
    if (unlikely(iter == node_statuses.end())) {
      return 0;
    }
    return ip_hint;
  }

  if (segment.prev_host) {
    return segment.prev_host;
  }

  // TODO: adopt a more sophisticated mechanism once we've added more fields.
  NodeIP ip;
  do {
    if (unlikely(rr_iter == node_statuses.end())) {
      rr_iter = node_statuses.begin();
    }
    ip = rr_iter->first;
  } while (rr_iter++->second.isol);

  return ip;
}

std::pair<NodeIP, Resource> Controller::acquire_migration_dest(
    lpid_t lpid, NodeIP requestor_ip, bool has_mem_pressure,
    Resource resource) {
  ScopedLock lock(&mutex_);

  auto &[node_statuses, rr_iter, destroying] = lpid_to_info_[lpid];
  if (unlikely(destroying)) {
    return std::make_pair(0, Resource{});
  }

  auto initial_rr_iter = rr_iter;
  BUG_ON(node_statuses.empty());

  auto search_fn = [&](auto filter_fn) {
    do {
      if (unlikely(rr_iter == node_statuses.end())) {
        rr_iter = node_statuses.begin();
      }
      if (rr_iter->first != requestor_ip && !rr_iter->second.isol &&
          !rr_iter->second.acquired && filter_fn(rr_iter->second)) {
        return true;
      }
    } while (++rr_iter != initial_rr_iter);
    return false;
  };

  // Round 1: search for any candidate node that has enough resource.
  if (search_fn([&](const auto &node) {
        return node.has_enough_resource(resource);
      })) {
    goto found;
  }

  // Round 2: if it's memory pressure, search for any candidate that has enough
  // memory.
  if (has_mem_pressure) {
    if (search_fn([&](const auto &node) {
          return node.has_enough_mem_resource(resource);
        })) {
      goto found;
    }
  }

  // Oof, no candidate found.
  return std::make_pair(0, Resource{});

found:
  // Found a candidate.
  rr_iter->second.acquired = true;
  auto pair = std::pair(rr_iter->first, rr_iter->second.free_resource);
  rr_iter++;
  return pair;
}

bool Controller::acquire_node(lpid_t lpid, NodeIP ip) {
  ScopedLock lock(&mutex_);

  auto &node_statuses = lpid_to_info_[lpid].node_statuses;
  auto iter = node_statuses.find(ip);
  if (unlikely(iter == node_statuses.end() || iter->second.acquired)) {
    return false;
  }
  iter->second.acquired = true;
  return true;
}

void Controller::release_node(lpid_t lpid, NodeIP ip) {
  ScopedLock lock(&mutex_);

  auto &node_statuses = lpid_to_info_[lpid].node_statuses;
  auto iter = node_statuses.find(ip);
  BUG_ON(iter == node_statuses.end());
  BUG_ON(!iter->second.acquired);
  iter->second.acquired = false;
  iter->second.cv.signal();
}

void Controller::update_location(ProcletID id, NodeIP proclet_srv_ip) {
  ScopedLock lock(&mutex_);

  auto iter = proclet_id_to_ip_.find(id);
  BUG_ON(iter == proclet_id_to_ip_.end());
  iter->second = proclet_srv_ip;
}

std::vector<std::pair<NodeIP, Resource>> Controller::report_free_resource(
    lpid_t lpid, NodeIP ip, Resource free_resource) {
  std::vector<std::pair<NodeIP, Resource>> global_free_resources;

  ScopedLock lock(&mutex_);

  auto lp_info_iter = lpid_to_info_.find(lpid);
  if (unlikely(lp_info_iter == lpid_to_info_.end())) {
    return global_free_resources;
  }

  auto &node_statuses = lp_info_iter->second.node_statuses;
  auto iter = node_statuses.find(ip);
  if (unlikely(iter == node_statuses.end())) {
    return global_free_resources;
  }

  iter->second.update_free_resource(free_resource);

  for (auto &[ip, status] : node_statuses) {
    if (!status.isol) {
      global_free_resources.emplace_back(ip, status.free_resource);
    }
  }

  return global_free_resources;
}

void NodeStatus::update_free_resource(Resource resource) {
  ewma(kEWMAWeight, &free_resource.cores, resource.cores);
  ewma(kEWMAWeight, &free_resource.mem_mbs, resource.mem_mbs);
}

}  // namespace nu
