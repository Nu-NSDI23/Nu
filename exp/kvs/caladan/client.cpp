#include <algorithm>
#include <atomic>
#include <cereal/types/optional.hpp>
#include <cereal/types/string.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <utility>
#include <vector>

#include <runtime.h>

#include "nu/dis_hash_table.hpp"
#include "nu/proclet.hpp"
#include "nu/runtime.hpp"
#include "nu/utils/farmhash.hpp"
#include "nu/utils/rcu_hash_map.hpp"
#include "nu/utils/perf.hpp"

using namespace nu;

constexpr uint32_t kKeyLen = 20;
constexpr uint32_t kValLen = 2;
constexpr double kLoadFactor = 0.20;
constexpr uint32_t kPrintIntervalUS = 1000 * 1000;
constexpr uint32_t kProxyIp = MAKE_IP_ADDR(18, 18, 1, 2);
constexpr uint32_t kProxyPort = 10086;
constexpr uint32_t kNumThreads = 1000;
constexpr double kTargetMops = 5;
constexpr uint32_t kWarmupUs = 1 * kOneSecond;
constexpr uint32_t kDurationUs = 15 * kOneSecond;

struct MemcachedPerfThreadState : nu::PerfThreadState {
  MemcachedPerfThreadState(uint32_t _tid)
      : tid(_tid), rd(), gen(rd()), dist_char('A', 'z') {}

  uint32_t tid;
  std::random_device rd;
  std::mt19937 gen;
  std::uniform_int_distribution<char> dist_char;
};

rt::TcpConn *conns[kNumThreads];

struct alignas(kCacheLineBytes) AlignedCnt {
  uint32_t cnt;
};

AlignedCnt cnts[kNumThreads];

struct Key {
  char data[kKeyLen];
};

struct Val {
  char data[kValLen];
};

struct Req {
  Key key;
};

struct PerfReq: nu::PerfRequest {
  Req req;
};

struct Resp {
  bool found;
  Val val;
};

namespace nu {

class Test {
public:
  constexpr static auto kFarmHashKeytoU64 = [](const Key &key) {
    return util::Hash64(key.data, kKeyLen);
  };
  using DSHashTable =
      DistributedHashTable<Key, Val, decltype(kFarmHashKeytoU64)>;
  constexpr static size_t kNumPairs =
      (1 << DSHashTable::kDefaultPowerNumShards) *
      DSHashTable::kNumBucketsPerShard * kLoadFactor;
};
} // namespace nu

void random_str(auto &dist, auto &mt, uint32_t len, char *buf) {
  for (uint32_t i = 0; i < len; i++) {
    buf[i] = dist(mt);
  }
}

void hashtable_get(uint32_t tid, const Req &req) {
  BUG_ON(conns[tid]->WriteFull(&req, sizeof(req)) < 0);
  Resp resp;
  BUG_ON(conns[tid]->ReadFull(&resp, sizeof(resp)) <= 0);
}

class MemcachedPerfAdapter : public nu::PerfAdapter {
public:
  std::unique_ptr<nu::PerfThreadState> create_thread_state() {
    static std::atomic<uint32_t> num_threads = 0;
    uint32_t tid = num_threads++;
    return std::make_unique<MemcachedPerfThreadState>(tid);
  }

  std::unique_ptr<nu::PerfRequest> gen_req(nu::PerfThreadState *perf_state) {
    auto *state = reinterpret_cast<MemcachedPerfThreadState *>(perf_state);

    auto perf_req = std::make_unique<PerfReq>();
    random_str(state->dist_char, state->gen, kKeyLen, perf_req->req.key.data);

    return perf_req;
  }

  bool serve_req(nu::PerfThreadState *perf_state,
                 const nu::PerfRequest *perf_req) {
    auto *state = reinterpret_cast<MemcachedPerfThreadState *>(perf_state);
    auto *req = reinterpret_cast<const PerfReq *>(perf_req);
    hashtable_get(state->tid, req->req);

    return true;
  }
};

void init_tcp() {
  netaddr raddr = {.ip = kProxyIp, .port = kProxyPort};
  for (uint32_t j = 0; j < kNumThreads; j++) {
    conns[j] = rt::TcpConn::DialAffinity(j % rt::RuntimeMaxCores(), raddr);
    delay_us(1000);
    BUG_ON(!conns[j]);
  }
}

void do_work() {
  init_tcp();

  MemcachedPerfAdapter memcached_perf_adapter;
  nu::Perf perf(memcached_perf_adapter);
  perf.run(kNumThreads, kTargetMops, kDurationUs, kWarmupUs,
           50 * nu::kOneMilliSecond);
  std::cout << "real_mops, avg_lat, 50th_lat, 90th_lat, 95th_lat, 99th_lat, "
               "99.9th_lat"
            << std::endl;
  std::cout << perf.get_real_mops() << " " << perf.get_average_lat() << " "
            << perf.get_nth_lat(50) << " " << perf.get_nth_lat(90) << " "
            << perf.get_nth_lat(95) << " " << perf.get_nth_lat(99) << " "
            << perf.get_nth_lat(99.9) << std::endl;
}

int main(int argc, char **argv) {
  int ret;

  if (argc < 2) {
    std::cerr << "usage: [cfg_file]" << std::endl;
    return -EINVAL;
  }

  ret = rt::RuntimeInit(std::string(argv[1]), [] { do_work(); });

  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }

  return 0;
}
