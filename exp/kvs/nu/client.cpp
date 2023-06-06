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
constexpr double kLoadFactor = 0.30;
constexpr uint32_t kPrintIntervalUS = 1000 * 1000;
constexpr uint32_t kNumProxies = 1;
constexpr uint32_t kProxyIps[] = {MAKE_IP_ADDR(18, 18, 1, 3)};
constexpr uint32_t kProxyPort = 10086;
constexpr static netaddr kClientAddrs[] = {
    {.ip = MAKE_IP_ADDR(18, 18, 1, 4), .port = 9000},
    {.ip = MAKE_IP_ADDR(18, 18, 1, 5), .port = 9000},
};
constexpr uint32_t kNumThreads = 200;
constexpr double kTargetMops = 1;
constexpr uint32_t kWarmupUs = 1 * kOneSecond;
constexpr uint32_t kDurationUs = 15 * kOneSecond;

struct MemcachedPerfThreadState : nu::PerfThreadState {
  MemcachedPerfThreadState(uint32_t _tid)
      : tid(_tid), rd(), gen(rd()), dist_char('A', 'z'),
        dist_proxy_id(0, kNumProxies - 1) {}

  uint32_t tid;
  std::random_device rd;
  std::mt19937 gen;
  std::uniform_int_distribution<char> dist_char;
  std::uniform_int_distribution<int> dist_proxy_id;
};

rt::TcpConn *conns[kNumProxies][kNumThreads];

RCUHashMap<uint32_t, uint32_t> shard_id_to_proxy_id_map_;

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
  uint32_t shard_id;
};

struct PerfReq: nu::PerfRequest {
  Req req;
};

struct Resp {
  uint32_t latest_shard_ip;
  bool found;
  Val val;
};

constexpr static auto kFarmHashKeytoU64 = [](const Key &key) {
  return util::Hash64(key.data, kKeyLen);
};

using DSHashTable = DistributedHashTable<Key, Val, decltype(kFarmHashKeytoU64)>;

constexpr static size_t kNumPairs = (1 << DSHashTable::kDefaultPowerNumShards) *
                                    DSHashTable::kNumBucketsPerShard *
                                    kLoadFactor;

void random_str(auto &dist, auto &mt, uint32_t len, char *buf) {
  for (uint32_t i = 0; i < len; i++) {
    buf[i] = dist(mt);
  }
}

void hashtable_get(uint32_t tid, const Req &req) {
  auto *proxy_id_ptr = shard_id_to_proxy_id_map_.get(req.shard_id);
  auto proxy_id = (!proxy_id_ptr) ? 0 : *proxy_id_ptr;
  BUG_ON(conns[proxy_id][tid]->WriteFull(&req, sizeof(req)) < 0);
  Resp resp;
  BUG_ON(conns[proxy_id][tid]->ReadFull(&resp, sizeof(resp)) <= 0);
  if (resp.latest_shard_ip) {
    auto proxy_ip_ptr = std::find(std::begin(kProxyIps), std::end(kProxyIps),
                                  resp.latest_shard_ip);
    BUG_ON(proxy_ip_ptr == std::end(kProxyIps));
    uint32_t proxy_id = proxy_ip_ptr - std::begin(kProxyIps);
    shard_id_to_proxy_id_map_.put(req.shard_id, proxy_id);
  }
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
    perf_req->req.shard_id = DSHashTable::get_shard_idx(
        perf_req->req.key, DSHashTable::kDefaultPowerNumShards);

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
  for (uint32_t i = 0; i < kNumProxies; i++) {
    netaddr raddr = {.ip = kProxyIps[i], .port = kProxyPort};
    for (uint32_t j = 0; j < kNumThreads; j++) {
      conns[i][j] =
          rt::TcpConn::DialAffinity(j % rt::RuntimeMaxCores(), raddr);
      delay_us(1000);
      BUG_ON(!conns[i][j]);
    }
  }
}

void do_work() {
  init_tcp();

  MemcachedPerfAdapter memcached_perf_adapter;
  nu::Perf perf(memcached_perf_adapter);
  perf.run_multi_clients(std::span(kClientAddrs), kNumThreads,
                         kTargetMops / std::size(kClientAddrs), kDurationUs,
                         kWarmupUs, 50 * nu::kOneMilliSecond);
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
