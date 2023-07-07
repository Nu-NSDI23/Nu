extern "C" {
#include <runtime/timer.h>
}

#include <net.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <random>
#include <vector>

#include "nu/utils/perf.hpp"

namespace nu {

Perf::Perf(PerfAdapter &adapter)
    : adapter_(adapter), trace_format_(kUnsorted), real_mops_(0) {}

void Perf::reset() {
  traces_.clear();
  trace_format_ = kUnsorted;
  real_mops_ = 0;
}

void Perf::gen_reqs(
    std::vector<PerfRequestWithTime> *all_reqs,
    const std::vector<std::unique_ptr<PerfThreadState>> &thread_states,
    uint32_t num_threads, double target_mops, uint64_t duration_us) {
  std::vector<rt::Thread> threads;

  for (uint32_t i = 0; i < num_threads; i++) {
    threads.emplace_back(
        [&, &reqs = all_reqs[i], thread_state = thread_states[i].get()] {
          std::random_device rd;
          std::mt19937 gen(rd());
          std::exponential_distribution<double> d(target_mops / num_threads);
          uint64_t cur_us = 0;

          while (cur_us < duration_us) {
            auto interval = std::max(1l, std::lround(d(gen)));
            PerfRequestWithTime req_with_time;
            req_with_time.start_us = cur_us;
            req_with_time.req = adapter_.gen_req(thread_state);
            reqs.emplace_back(std::move(req_with_time));
            cur_us += interval;
          }
        });
  }

  for (auto &thread : threads) {
    thread.Join();
  }
}

std::vector<Trace> Perf::benchmark(
    std::vector<PerfRequestWithTime> *all_reqs,
    const std::vector<std::unique_ptr<PerfThreadState>> &thread_states,
    uint32_t num_threads, std::optional<uint64_t> miss_ddl_thresh_us) {
  std::vector<rt::Thread> threads;
  std::vector<Trace> all_traces[num_threads];

  for (uint32_t i = 0; i < num_threads; i++) {
    all_traces[i].reserve(all_reqs[i].size());
  }

  for (uint32_t i = 0; i < num_threads; i++) {
    threads.emplace_back([&, &reqs = all_reqs[i], &traces = all_traces[i],
                          thread_state = thread_states[i].get()] {
      auto start_us = microtime();

      for (const auto &req : reqs) {
        auto relative_us = microtime() - start_us;
        if (req.start_us > relative_us) {
          timer_sleep(req.start_us - relative_us);
        } else if (miss_ddl_thresh_us &&
                   req.start_us + *miss_ddl_thresh_us < relative_us) {
          continue;
        }
        Trace trace;
        trace.absl_start_us = microtime();
        trace.start_us = trace.absl_start_us - start_us;
        bool ok = adapter_.serve_req(thread_state, req.req.get());
        trace.duration_us = microtime() - start_us - trace.start_us;
        if (ok) {
          traces.push_back(trace);
        }
      }
    });
  }

  for (auto &thread : threads) {
    thread.Join();
  }

  std::vector<Trace> gathered_traces;
  for (uint32_t i = 0; i < num_threads; i++) {
    gathered_traces.insert(gathered_traces.end(), all_traces[i].begin(),
                           all_traces[i].end());
  }
  return gathered_traces;
}

void Perf::create_thread_states(
    std::vector<std::unique_ptr<PerfThreadState>> *thread_states,
    uint32_t num_threads) {
  for (uint32_t i = 0; i < num_threads; i++) {
    thread_states->emplace_back(adapter_.create_thread_state());
  }
}

void Perf::run(uint32_t num_threads, double target_mops, uint64_t duration_us,
               uint64_t warmup_us, uint64_t miss_ddl_thresh_us) {
  run_multi_clients(std::span<const netaddr>(), num_threads, target_mops,
                    duration_us, warmup_us, miss_ddl_thresh_us);
}

void Perf::run_multi_clients(std::span<const netaddr> client_addrs,
                             uint32_t num_threads, double target_mops,
                             uint64_t duration_us, uint64_t warmup_us,
                             uint64_t miss_ddl_thresh_us) {
  std::vector<std::unique_ptr<PerfThreadState>> thread_states;
  create_thread_states(&thread_states, num_threads);
  std::vector<PerfRequestWithTime> all_warmup_reqs[num_threads];
  std::vector<PerfRequestWithTime> all_perf_reqs[num_threads];
  gen_reqs(all_warmup_reqs, thread_states, num_threads, target_mops, warmup_us);
  gen_reqs(all_perf_reqs, thread_states, num_threads, target_mops, duration_us);
  benchmark(all_warmup_reqs, thread_states, num_threads, std::nullopt);
  tcp_barrier(client_addrs);
  traces_ = move(
      benchmark(all_perf_reqs, thread_states, num_threads, miss_ddl_thresh_us));
  auto real_duration_us =
      std::accumulate(traces_.begin(), traces_.end(), static_cast<uint64_t>(0),
                      [](uint64_t ret, Trace t) {
                        return std::max(ret, t.start_us + t.duration_us);
                      });
  real_mops_ = static_cast<double>(traces_.size()) / real_duration_us;
}

void Perf::tcp_barrier(std::span<const netaddr> participant_addrs) {
  if (participant_addrs.empty()) {
    return;
  }

  auto sink_addr = participant_addrs.front();
  auto num_workers = participant_addrs.size() - 1;
  bool dummy;

  if (get_cfg_ip() == sink_addr.ip) {
    if (num_workers) {
      auto *q = rt::TcpQueue::Listen(sink_addr, num_workers);
      BUG_ON(!q);
      auto q_gc = std::unique_ptr<rt::TcpQueue>(q);

      std::vector<rt::TcpConn *> conns;
      while (num_workers) {
        auto *c = q->Accept();
        BUG_ON(!c);
        conns.push_back(c);
        num_workers--;
      }

      for (auto *c : conns) {
        auto c_gc = std::unique_ptr<rt::TcpConn>(c);
        BUG_ON(c->WriteFull(&dummy, sizeof(dummy)) != sizeof(dummy));
        BUG_ON(c->Shutdown(SHUT_RDWR) != 0);
      }
      q->Shutdown();
    }
  } else {
    std::optional<netaddr> matched_addr;
    for (auto addr : participant_addrs) {
      if (get_cfg_ip() == addr.ip) {
        matched_addr = addr;
      }
    }
    BUG_ON(!matched_addr);
    auto *c = rt::TcpConn::Dial(*matched_addr, sink_addr);
    auto c_gc = std::unique_ptr<rt::TcpConn>(c);
    BUG_ON(!c);
    BUG_ON(c->ReadFull(&dummy, sizeof(dummy)) != sizeof(dummy));
  }
}

uint64_t Perf::get_average_lat() {
  if (trace_format_ != kSortedByDuration) {
    std::sort(traces_.begin(), traces_.end(),
              [](const Trace &x, const Trace &y) {
                return x.duration_us < y.duration_us;
              });
    trace_format_ = kSortedByDuration;
  }

  auto sum = std::accumulate(
      std::next(traces_.begin()), traces_.end(), 0ULL,
      [](uint64_t sum, const Trace &t) { return sum + t.duration_us; });
  return sum / traces_.size();
}

uint64_t Perf::get_nth_lat(double nth) {
  if (trace_format_ != kSortedByDuration) {
    std::sort(traces_.begin(), traces_.end(),
              [](const Trace &x, const Trace &y) {
                return x.duration_us < y.duration_us;
              });
    trace_format_ = kSortedByDuration;
  }

  size_t idx = nth / 100.0 * traces_.size();
  return traces_[idx].duration_us;
}

std::vector<Trace> Perf::get_timeseries_nth_lats(uint64_t interval_us,
                                                 double nth) {
  std::vector<Trace> timeseries;
  if (trace_format_ != kSortedByStart) {
    std::sort(
        traces_.begin(), traces_.end(),
        [](const Trace &x, const Trace &y) { return x.start_us < y.start_us; });
    trace_format_ = kSortedByStart;
  }

  auto cur_win_us = traces_.front().start_us;
  auto absl_cur_win_us = traces_.front().absl_start_us;
  std::vector<uint64_t> win_durations;
  for (auto &trace : traces_) {
    if (cur_win_us + interval_us < trace.start_us) {
      std::sort(win_durations.begin(), win_durations.end());
      if (win_durations.size() >= 100) {
        size_t idx = nth / 100.0 * win_durations.size();
        timeseries.emplace_back(absl_cur_win_us, cur_win_us,
                                win_durations[idx]);
      }
      cur_win_us += interval_us;
      absl_cur_win_us += interval_us;
      win_durations.clear();
    }
    win_durations.push_back(trace.duration_us);
  }

  return timeseries;
}

double Perf::get_real_mops() const { return real_mops_; }

const std::vector<Trace> &Perf::get_traces() const { return traces_; }

}  // namespace nu
