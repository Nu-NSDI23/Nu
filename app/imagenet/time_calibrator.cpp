extern "C" {
#include <asm/ops.h>
}

#include <iostream>
#include <net.h>
#include <runtime.h>

constexpr uint32_t kServerIP = MAKE_IP_ADDR(18, 18, 1, 11);
constexpr uint32_t kServerPort = 8888;
constexpr uint32_t kWarmupTimes = 1000;
constexpr uint32_t kMeasureTimes = 10000;

int64_t do_measurement(rt::TcpConn *conn) {
  bool stop = false;
  uint64_t client_mid_tsc;

  barrier();
  auto server_start_tsc = rdtsc();
  barrier();

  BUG_ON(conn->WriteFull(&stop, sizeof(stop)) < 0);
  BUG_ON(conn->ReadFull(&client_mid_tsc, sizeof(client_mid_tsc)) <= 0);

  barrier();
  auto server_end_tsc = rdtsc();
  barrier();

  auto rtt = server_end_tsc - server_start_tsc;
  auto single_trip = rtt / 2;
  auto client_start_tsc = client_mid_tsc - single_trip;
  return client_start_tsc - server_start_tsc;
}

void run_server() {
  netaddr laddr{.ip = 0, .port = kServerPort};
  auto *tcp_queue = rt::TcpQueue::Listen(laddr, 1);
  BUG_ON(!tcp_queue);
  auto *conn = tcp_queue->Accept();
  BUG_ON(!conn);
  for (uint32_t i = 0; i < kWarmupTimes; i++) {
    do_measurement(conn);
  }
  int64_t sum = 0;
  for (uint32_t i = 0; i < kMeasureTimes; i++) {
    sum += do_measurement(conn);
  }
  sum /= kMeasureTimes;
  std::cout << "The client tsc is " << sum
            << " cycles faster than the server's." << std::endl;
  bool stop = true;
  BUG_ON(conn->WriteFull(&stop, sizeof(stop)) < 0);
}

void run_client() {
  netaddr laddr{.ip = 0, .port = 0};
  netaddr raddr{.ip = kServerIP, .port = kServerPort};
  auto *conn = rt::TcpConn::Dial(laddr, raddr);
  BUG_ON(!conn);

  bool stop = false;;
  uint64_t tsc;
  while (!stop) {
    BUG_ON(conn->ReadFull(&stop, sizeof(stop)) <= 0);

    barrier();
    tsc = rdtsc();
    barrier();

    BUG_ON(conn->WriteFull(&tsc, sizeof(tsc)) < 0);
  }
}

int main(int argc, char **argv) {
  int ret;

  if (argc < 2) {
    std::cerr << "usage: [cfg_file] S/C" << std::endl;
    return -EINVAL;
  }

  ret = rt::RuntimeInit(argv[1], [] {
    if (get_cfg_ip() == kServerIP) {
      run_server();
    } else {
      run_client();
    }
  });

  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }

  return 0;
}
