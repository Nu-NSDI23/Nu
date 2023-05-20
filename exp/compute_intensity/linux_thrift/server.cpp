#include <iostream>
#include <memory>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>

extern "C" {
#include <base/time.h>
}

#include "gen-cpp/ThriftBenchService.h"
#include "gen-cpp/thrift_bench_types.h"

using namespace thrift_bench;
using apache::thrift::protocol::TBinaryProtocolFactory;
using apache::thrift::server::TThreadedServer;
using apache::thrift::transport::TFramedTransportFactory;
using apache::thrift::transport::TServerSocket;

constexpr uint32_t kPort = 10088;
constexpr uint32_t kDelayNs =  10000;

void delay_ns(uint64_t ns) {
  auto start_tsc = rdtsc();
  uint64_t cycles = ns * cycles_per_us / 1000.0;
  auto end_tsc = start_tsc + cycles;
  while (rdtsc() < end_tsc)
    ;
}

namespace thrift_bench {
class ThriftBenchService : public ThriftBenchServiceIf {
public:
  void run() override { delay_ns(kDelayNs); }
};
} // namespace thrift_bench

void do_work() {
  auto thrift_bench_service_handler = std::make_shared<ThriftBenchService>();
  std::shared_ptr<TServerSocket> server_socket =
      std::make_shared<TServerSocket>("0.0.0.0", kPort);
  TThreadedServer server(std::make_shared<ThriftBenchServiceProcessor>(
                             std::move(thrift_bench_service_handler)),
                         server_socket,
                         std::make_shared<TFramedTransportFactory>(),
                         std::make_shared<TBinaryProtocolFactory>());
  std::cout << "Starting the ThriftBackEndServer..." << std::endl;
  server.serve();
}

int main(int argc, char **argv) {
  do_work();

  return 0;
}
