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

#include "nu/proclet.hpp"
#include "nu/runtime.hpp"
#include "nu/utils/farmhash.hpp"
#include "nu/utils/sync_hash_map.hpp"

using namespace nu;

constexpr uint32_t kKeyLen = 20;
constexpr uint32_t kValLen = 2;
constexpr double kLoadFactor = 0.30;
constexpr uint32_t kProxyPort = 10086;
constexpr uint64_t kNumBuckets = 32768 * (1 << 13);

struct Key {
  char data[kKeyLen];

  bool operator==(const Key &o) const {
    return __builtin_memcmp(data, o.data, kKeyLen) == 0;
  }
};

struct Val {
  char data[kValLen];
};

constexpr static auto kFarmHashKeytoU64 = [](const Key &key) {
  return util::Hash64(key.data, kKeyLen);
};

struct Req {
  Key key;
};

struct Resp {
  bool found;
  Val val;
};

std::unique_ptr<SyncHashMap<kNumBuckets, Key, Val, decltype(kFarmHashKeytoU64)>>
    hashtable;

void random_str(auto &dist, auto &mt, uint32_t len, char *buf) {
  for (uint32_t i = 0; i < len; i++) {
    buf[i] = dist(mt);
  }
}

void init() {
  hashtable.reset(
      new SyncHashMap<kNumBuckets, Key, Val, decltype(kFarmHashKeytoU64)>());
  std::vector<rt::Thread> threads;
  constexpr uint32_t kNumThreads = 400;
  for (uint32_t i = 0; i < kNumThreads; i++) {
    threads.emplace_back([&, tid = i] {
      std::random_device rd;
      std::mt19937 mt(rd());
      std::uniform_int_distribution<int> dist('A', 'z');
      auto num_pairs = kNumBuckets * kLoadFactor / kNumThreads;
      for (size_t j = 0; j < num_pairs; j++) {
        Key key;
        Val val;
        random_str(dist, mt, kKeyLen, key.data);
        random_str(dist, mt, kValLen, val.data);
        hashtable->put(key, val);
      }
    });
  }
  for (auto &thread : threads) {
    thread.Join();
  }
}

void handle(rt::TcpConn *c) {
  while (true) {
    Req req;
    BUG_ON(c->ReadFull(&req, sizeof(req)) <= 0);
    Resp resp;
    auto *v = hashtable->get(req.key);
    if (v) {
      resp.found = true;
      resp.val = *v;
    } else {
      resp.found = false;
    }
    BUG_ON(c->WriteFull(&resp, sizeof(resp)) < 0);
  }
}

void run_loop() {
  netaddr laddr = {.ip = 0, .port = kProxyPort};
  auto *queue = rt::TcpQueue::Listen(laddr, 128);
  rt::TcpConn *c;
  while ((c = queue->Accept())) {
    rt::Thread([&, c] { handle(c); }).Detach();
  }
}

void do_work() {
  std::cout << "start initing..." << std::endl;
  init();
  std::cout << "finish initing..." << std::endl;
  run_loop();
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
