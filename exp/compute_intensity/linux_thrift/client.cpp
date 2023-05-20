#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>
#include <unistd.h>

#include "gen-cpp/ThriftBenchService.h"
#include "gen-cpp/thrift_bench_types.h"

using namespace thrift_bench;
using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;

constexpr uint32_t kPort = 10088;
constexpr uint32_t kNumThreads = NCORES - 2;
constexpr auto kIp =  "10.10.2.2";

struct alignas(64) AlignedCnt {
  volatile uint32_t cnt;
};

AlignedCnt cnts_[kNumThreads];

uint64_t microtime() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void do_work() {
  std::vector<std::thread> threads;
  for (uint32_t i = 0; i < kNumThreads; i++) {
    threads.emplace_back([&, tid = i] {
      std::shared_ptr<TTransport> socket(new TSocket(kIp, kPort));
      std::shared_ptr<TTransport> transport(new TFramedTransport(socket));
      std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
      ThriftBenchServiceClient client(protocol);
      transport->open();

      while (true) {
        client.run();
        cnts_[tid].cnt = cnts_[tid].cnt + 1;
      }
    });
  }

  uint64_t old_sum = 0;
  uint64_t old_us = microtime();
  while (true) {
    sleep(1);
    auto us = microtime();
    uint64_t sum = 0;
    for (uint32_t i = 0; i < kNumThreads; i++) {
      sum += cnts_[i].cnt;
    }
    std::cout << us - old_us << " " << sum - old_sum << " "
              << 1.0 * (sum - old_sum) / (us - old_us) << std::endl;
    old_sum = sum;
    old_us = us;
  }
}

int main(int argc, char **argv) {
  do_work();

  return 0;
}
