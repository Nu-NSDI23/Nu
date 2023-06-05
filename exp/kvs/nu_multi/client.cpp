#include <algorithm>
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

using namespace nu;

constexpr uint32_t kKeyLen = 20;
constexpr uint32_t kValLen = 2;
constexpr double kLoadFactor = 0.30;
constexpr uint32_t kPrintIntervalUS = 1000 * 1000;
constexpr uint32_t kNumProxies = 1;
constexpr uint32_t kProxyIps[] = {
    MAKE_IP_ADDR(18, 18, 1, 2),  MAKE_IP_ADDR(18, 18, 1, 3),
    MAKE_IP_ADDR(18, 18, 1, 4),  MAKE_IP_ADDR(18, 18, 1, 5),
    MAKE_IP_ADDR(18, 18, 1, 6),  MAKE_IP_ADDR(18, 18, 1, 7),
    MAKE_IP_ADDR(18, 18, 1, 8),  MAKE_IP_ADDR(18, 18, 1, 9),
    MAKE_IP_ADDR(18, 18, 1, 10), MAKE_IP_ADDR(18, 18, 1, 11),
    MAKE_IP_ADDR(18, 18, 1, 12), MAKE_IP_ADDR(18, 18, 1, 13),
    MAKE_IP_ADDR(18, 18, 1, 14), MAKE_IP_ADDR(18, 18, 1, 15),
    MAKE_IP_ADDR(18, 18, 1, 16)};
constexpr uint32_t kProxyPort = 10086;
constexpr uint32_t kNumThreads = 200;

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

void gen_reqs(std::vector<Req> *reqs) {
  std::cout << "Generate reqs..." << std::endl;

  std::vector<rt::Thread> threads;
  for (uint32_t i = 0; i < kNumThreads; i++) {
    threads.emplace_back([&, tid = i] {
      std::random_device rd;
      std::mt19937 mt(rd());
      std::uniform_int_distribution<int> dist('A', 'z');
      auto num_pairs = kNumPairs / kNumThreads;
      for (size_t j = 0; j < num_pairs; j++) {
        Key key;
        random_str(dist, mt, kKeyLen, key.data);
        auto shard_id = DSHashTable::get_shard_idx(
            key, DSHashTable::kDefaultPowerNumShards);
        reqs[tid].push_back({key, shard_id});
      }
    });
  }

  for (auto &thread : threads) {
    thread.Join();
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

void benchmark(std::vector<Req> *reqs) {
  std::cout << "Start benchmarking..." << std::endl;

  for (uint32_t i = 0; i < kNumThreads; i++) {
    rt::Thread([&, tid = i] {
      while (true) {
        for (const auto &req : reqs[tid]) {
          hashtable_get(tid, req);
          cnts[tid].cnt++;
        }
      }
    }).Detach();
  }

  uint64_t old_sum = 0;
  uint64_t old_us = microtime();
  while (true) {
    timer_sleep(kPrintIntervalUS);
    auto us = microtime();
    uint64_t sum = 0;
    for (uint32_t i = 0; i < kNumThreads; i++) {
      sum += ACCESS_ONCE(cnts[i].cnt);
    }
    std::cout << us - old_us << " " << sum - old_sum << std::endl;
    old_sum = sum;
    old_us = us;
  }
}

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
  std::vector<Req> reqs[kNumThreads];
  init_tcp();
  gen_reqs(reqs);
  benchmark(reqs);
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
