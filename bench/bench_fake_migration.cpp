#include <sys/mman.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numeric>
#include <vector>

extern "C" {
#include <net/ip.h>
#include <runtime/net.h>
#include <runtime/runtime.h>
#include <runtime/tcp.h>
#include <runtime/timer.h>
}
#include <net.h>
#include <runtime.h>
#include <sync.h>
#include <thread.h>

#include "nu/commons.hpp"

using namespace nu;

constexpr netaddr server_addr = {.ip = MAKE_IP_ADDR(18, 18, 1, 4),
                                 .port = 8080};
constexpr uint32_t kBufSize = 6 << 20;
constexpr uint32_t kNumBufs = 100;
bool server_mode;

using IDType = uint64_t;

void server_fn(rt::TcpConn *c) {
  bool mmapped[kNumBufs];
  memset(mmapped, false, sizeof(mmapped));

  auto *buf = reinterpret_cast<uint8_t *>(0x80000000);
  rt::Thread([&, buf]() mutable {
    for (uint32_t i = 0; i < kNumBufs; i++) {
      auto *addr =
          mmap(buf, kBufSize, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_FIXED, -1, 0);
      BUG_ON(buf != addr);
      ACCESS_ONCE(mmapped[i]) = true;
      buf += kBufSize;
    }
  }).Detach();

  for (uint32_t i = 0; i < kNumBufs; i++) {
    auto t0 = rdtsc();
    while (!ACCESS_ONCE(mmapped[i]))
      ;
    auto t1 = rdtsc();
    BUG_ON(c->ReadFull(buf, kBufSize) < 0);
    buf += kBufSize;
    auto t2 = rdtsc();
    preempt_disable();
    std::cout << t1 - t0 << " " << t2 - t1 << std::endl;
    preempt_enable();
  }
}

void do_server() {
  auto *q = rt::TcpQueue::Listen(server_addr, 100);
  BUG_ON(!q);

  rt::TcpConn *c;
  while ((c = q->Accept())) {
    rt::Thread([&, c] { server_fn(c); }).Detach();
  }
}

void do_client() {
  netaddr local_addr = {.ip = MAKE_IP_ADDR(0, 0, 0, 0), .port = 0};
  auto c = rt::TcpConn::Dial(local_addr, server_addr);
  BUG_ON(!c);

  std::unique_ptr<uint8_t[]> buf(new uint8_t[kBufSize]);

  for (uint32_t i = 0; i < kNumBufs; i++) {
    auto t0 = rdtsc();
    BUG_ON(c->WriteFull(buf.get(), kBufSize) < 0);
    auto t1 = rdtsc();
    preempt_disable();
    std::cout << t1 - t0 << std::endl;
    preempt_enable();
  }
}

int main(int argc, char **argv) {
  int ret;
  std::string mode_str;

  if (argc < 3) {
    goto wrong_args;
  }

  mode_str = std::string(argv[2]);
  if (mode_str == "SRV") {
    server_mode = true;
  } else if (mode_str == "CLT") {
    server_mode = false;
  } else {
    goto wrong_args;
  }

  ret = rt::RuntimeInit(std::string(argv[1]), [] {
    std::cout << "Running " << __FILE__ "..." << std::endl;

    if (server_mode) {
      do_server();
    } else {
      do_client();
    }
  });

  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }

  return 0;

wrong_args:
  std::cerr << "usage: [cfg_file] CLT/SRV" << std::endl;
  return -EINVAL;
}
