extern "C" {
#include <runtime/preempt.h>
#include <runtime/timer.h>
}

namespace nu {

template <typename Fn>
inline std::pair<uint64_t, uint64_t> TraceLogger::add_trace(Fn &&fn) {
  auto t0 = rdtsc();
  fn();
  auto t1 = rdtsc();

  auto duration_tsc = t1 - t0;
  add_trace(duration_tsc);
  return std::make_pair(t0, t1);
}

inline void TraceLogger::add_trace(uint64_t duration_tsc) {
  int cpu_id = get_cpu();
  auto bucket_id = std::min(
      static_cast<uint32_t>(duration_tsc / cycles_per_us / kBucketIntervalUs),
      kNumBuckets - 1);
  ACCESS_ONCE(aligned_cnts_[cpu_id][bucket_id].cnt) =
      aligned_cnts_[cpu_id][bucket_id].cnt + 1;
  put_cpu();
}

}  // namespace nu
