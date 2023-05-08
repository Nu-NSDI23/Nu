#include <net.h>
#include <runtime.h>
#include <thread.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>

#include "nu/runtime.hpp"
#include "nu/proclet.hpp"

using namespace rt;

constexpr static uint32_t kPingPongTimes = 400000;
constexpr uint32_t kServerIP = MAKE_IP_ADDR(18, 18, 1, 2);
constexpr uint32_t kClientIP = MAKE_IP_ADDR(18, 18, 1, 3);
constexpr uint32_t kPort = 8089;

class Server {
 public:
  void run(bool poll) {
    netaddr laddr = {.ip = 0, .port = kPort};
    auto *q = TcpQueue::Listen(laddr, 1);
    std::unique_ptr<TcpQueue> gc_q(q);
    BUG_ON(!q);
    TcpConn *c = q->Accept();
    std::unique_ptr<TcpConn> gc_c(c);
    for (uint32_t i = 0; i < kPingPongTimes; i++) {
      int num;
      BUG_ON(c->ReadFull(&num, sizeof(num), /* nt = */ false,
                         /* poll = */ poll) <= 0);
      num++;
      BUG_ON(c->WriteFull(&num, sizeof(num), /* nt = */ false,
                          /* poll = */ poll) < 0);
    }
    BUG_ON(c->Shutdown(SHUT_RDWR) != 0);
    q->Shutdown();
  }
};

class Client {
 public:
  uint64_t run(bool poll) {
    netaddr laddr = {.ip = 0, .port = 0};
    netaddr raddr = {.ip = kServerIP, .port = kPort};
    auto *c = TcpConn::Dial(laddr, raddr);
    std::unique_ptr<TcpConn> gc_c(c);
    BUG_ON(!c);
    int num = 0;
    auto start_us = microtime();
    for (uint32_t i = 0; i < kPingPongTimes; i++) {
      BUG_ON(c->WriteFull(&num, sizeof(num), /* nt = */ false,
                          /* poll = */ poll) < 0);
      int new_num;
      BUG_ON(c->ReadFull(&new_num, sizeof(new_num), /* nt = */ false,
                         /* poll = */ poll) <= 0);
      BUG_ON(num + 1 != new_num);
      num = new_num;
    }
    auto end_us = microtime();
    BUG_ON(c->Shutdown(SHUT_RDWR) != 0);
    return end_us - start_us;
  }
};

uint64_t run(bool poll) {
  auto server_proclet = nu::make_proclet<Server>(true, std::nullopt, kServerIP);
  auto future0 = server_proclet.run_async(&Server::run, poll);
  delay_us(100);

  auto client_proclet = nu::make_proclet<Client>(true, std::nullopt, kClientIP);
  auto future1 = client_proclet.run_async(&Client::run, poll);

  return future1.get();
}

int main(int argc, char **argv) {
  return nu::runtime_main_init(argc, argv, [](int, char **) {
    std::cout << run(false) << std::endl;
    std::cout << run(true) << std::endl;
    std::cout << "Passed" << std::endl;
  });
}
