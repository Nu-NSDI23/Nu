#include <iostream>
#include <memory>
#include <nu/commons.hpp>
#include <runtime.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>

#include "gen-cpp/ThriftBenchService.h"
#include "gen-cpp/thrift_bench_types.h"

extern "C" {
#include <runtime/timer.h>
}

using namespace thrift_bench;
using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;

constexpr uint32_t kPort = 10088;
constexpr uint32_t kNumThreads = 1000;
constexpr uint32_t kPrintIntervalUS = 1000 * 1000;
constexpr auto kIp = "18.18.1.2";

struct alignas(nu::kCacheLineBytes) AlignedCnt {
  uint32_t cnt;
};

AlignedCnt cnts_[kNumThreads];

void do_work() {
  std::vector<rt::Thread> threads;
  for (uint32_t i = 0; i < kNumThreads; i++) {
    threads.emplace_back([&, tid = i] {
      std::shared_ptr<TTransport> socket(new TSocket(kIp, kPort));
      std::shared_ptr<TTransport> transport(new TFramedTransport(socket));
      std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
      ThriftBenchServiceClient client(protocol);
      transport->open();

      while (true) {
        client.run();
        cnts_[tid].cnt++;
      }
    });
  }

  uint64_t old_sum = 0;
  uint64_t old_us = microtime();
  while (true) {
    timer_sleep(kPrintIntervalUS);
    auto us = microtime();
    uint64_t sum = 0;
    for (uint32_t i = 0; i < kNumThreads; i++) {
      sum += ACCESS_ONCE(cnts_[i].cnt);
    }
    std::cout << us - old_us << " " << sum - old_sum << " "
              << 1.0 * (sum - old_sum) / (us - old_us) << std::endl;
    old_sum = sum;
    old_us = us;
  }
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
