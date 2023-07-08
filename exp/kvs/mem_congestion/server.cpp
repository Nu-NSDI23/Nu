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
#include "nu/utils/thread.hpp"

constexpr uint32_t kKeyLen = 20;
constexpr uint32_t kValLen = 2;
constexpr double kLoadFactor = 0.30;
constexpr uint32_t kNumProxies = 6;
constexpr uint32_t kProxyPort = 10086;
constexpr uint32_t kPowerNumShards = 16;

struct Key {
  char data[kKeyLen];

  bool operator==(const Key &o) const {
    return __builtin_memcmp(data, o.data, kKeyLen) == 0;
  }

  template <class Archive> void serialize(Archive &ar) {
    ar(cereal::binary_data(data, sizeof(data)));
  }
};

struct Val {
  char data[kValLen];

  template <class Archive> void serialize(Archive &ar) {
    ar(cereal::binary_data(data, sizeof(data)));
  }
};

struct Req {
  Key key;
  uint32_t shard_id;
};

struct Resp {
  int latest_shard_ip;
  bool found;
  Val val;
};

constexpr static auto kFarmHashKeytoU64 = [](const Key &key) {
  return util::Hash64(key.data, kKeyLen);
};

using DSHashTable =
    nu::DistributedHashTable<Key, Val, decltype(kFarmHashKeytoU64)>;

constexpr static size_t kNumPairs =
    (1 << kPowerNumShards) * DSHashTable::kNumBucketsPerShard * kLoadFactor;

void random_str(auto &dist, auto &mt, uint32_t len, char *buf) {
  for (uint32_t i = 0; i < len; i++) {
    buf[i] = dist(mt);
  }
}

void init(DSHashTable *hash_table) {
  std::vector<nu::Thread> threads;
  constexpr uint32_t kNumThreads = 400;
  for (uint32_t i = 0; i < kNumThreads; i++) {
    threads.emplace_back([&, tid = i] {
      std::random_device rd;
      std::mt19937 mt(rd());
      std::uniform_int_distribution<int> dist('A', 'z');
      auto num_pairs = kNumPairs / kNumThreads;
      for (size_t j = 0; j < num_pairs; j++) {
        Key key;
        Val val;
        random_str(dist, mt, kKeyLen, key.data);
        random_str(dist, mt, kValLen, val.data);
        hash_table->put(key, val);
      }
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }
}

class Proxy {
 public:
  Proxy(DSHashTable hash_table) : hash_table_(std::move(hash_table)) {}

  void run_loop() {
    netaddr laddr = {.ip = 0, .port = kProxyPort};
    auto *queue = rt::TcpQueue::Listen(laddr, 128);
    rt::TcpConn *c;
    while ((c = queue->Accept())) {
      nu::Thread([&, c] { handle(c); }).detach();
    }
  }

  void handle(rt::TcpConn *c) {
    while (true) {
      Req req;
      BUG_ON(c->ReadFull(&req, sizeof(req)) <= 0);
      Resp resp;
      bool is_local;
      auto optional_v = hash_table_.get(req.key, &is_local);
      resp.found = optional_v.has_value();
      if (resp.found) {
        resp.val = *optional_v;
      }
      auto id = hash_table_.get_shard_proclet_id(req.shard_id);
      if (is_local) {
        resp.latest_shard_ip = 0;
      } else {
        resp.latest_shard_ip =
            nu::get_runtime()->rpc_client_mgr()->get_ip_by_proclet_id(id);
      }
      BUG_ON(c->WriteFull(&resp, sizeof(resp)) < 0);
    }
  }

 private:
  DSHashTable hash_table_;
};

void do_work() {
  DSHashTable hash_table =
      nu::make_dis_hash_table<Key, Val, decltype(kFarmHashKeytoU64)>(
          kPowerNumShards);
  init(&hash_table);
  std::cout << "finish initing..." << std::endl;

  std::vector<nu::Future<void>> futures;
  nu::Proclet<Proxy> proxies[kNumProxies];
  for (uint32_t i = 0; i < kNumProxies; i++) {
    proxies[i] =
        nu::make_proclet<Proxy>(std::forward_as_tuple(hash_table), true);
    futures.emplace_back(proxies[i].run_async(&Proxy::run_loop));
  }
  futures.front().get();
}

int main(int argc, char **argv) {
  return nu::runtime_main_init(argc, argv, [](int, char **) { do_work(); });
}
