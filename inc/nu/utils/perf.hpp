#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>
#include <optional>

extern "C" {
#include <runtime/net.h>
}
#include <thread.h>

#include "nu/commons.hpp"

namespace nu {

struct PerfRequest {
  virtual ~PerfRequest() = default;
};

struct PerfRequestWithTime {
  uint64_t start_us;
  std::unique_ptr<PerfRequest> req;
};

struct Trace {
  uint64_t absl_start_us;
  uint64_t start_us;
  uint64_t duration_us;
};

struct PerfThreadState {
  virtual ~PerfThreadState() = default;
};

class PerfAdapter {
 public:
  virtual std::unique_ptr<PerfThreadState> create_thread_state() = 0;
  virtual std::unique_ptr<PerfRequest> gen_req(PerfThreadState *state) = 0;
  virtual bool serve_req(PerfThreadState *state, const PerfRequest *req) = 0;
};

// Closed-loop, possion arrival.
class Perf {
 public:
  Perf(PerfAdapter &adapter);
  void reset();
  void run(uint32_t num_threads, double target_mops, uint64_t duration_us,
           uint64_t warmup_us = 0, uint64_t miss_ddl_thresh_us = 500);
  void run_multi_clients(std::span<const netaddr> client_addrs,
                         uint32_t num_threads, double target_mops,
                         uint64_t duration_us, uint64_t warmup_us = 0,
                         uint64_t miss_ddl_thresh_us = 500);
  uint64_t get_average_lat();
  uint64_t get_nth_lat(double nth);
  std::vector<Trace> get_timeseries_nth_lats(uint64_t interval_us, double nth);
  double get_real_mops() const;
  const std::vector<Trace> &get_traces() const;

 private:
  enum TraceFormat { kUnsorted, kSortedByDuration, kSortedByStart };

  PerfAdapter &adapter_;
  std::vector<Trace> traces_;
  TraceFormat trace_format_;
  double real_mops_;
  friend class Test;

  void tcp_barrier(std::span<const netaddr> participant_addrs);
  void create_thread_states(
      std::vector<std::unique_ptr<PerfThreadState>> *thread_states,
      uint32_t num_threads);
  void gen_reqs(
      std::vector<PerfRequestWithTime> *all_reqs,
      const std::vector<std::unique_ptr<PerfThreadState>> &thread_states,
      uint32_t num_threads, double target_mops, uint64_t duration_us);
  std::vector<Trace> benchmark(
      std::vector<PerfRequestWithTime> *all_reqs,
      const std::vector<std::unique_ptr<PerfThreadState>> &thread_states,
      uint32_t num_threads, std::optional<uint64_t> miss_ddl_thresh_us);
};

}  // namespace nu
