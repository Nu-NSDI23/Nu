#include <chrono>
#include <net.h>
#include <runtime.h>
#include <thread.h>
#include <sync.h>
#include <queue>
#include <atomic>
#include <spanstream>
#include <memory>
#include <cereal/archives/binary.hpp>

#include "image.hpp"
#include "gpu.hpp"
#include "baseline_gpu.hpp"

using namespace imagenet;

constexpr static int kTCPListenBackLog = 16;

rt::Mutex mutex;
rt::CondVar condvar;
std::queue<Image> images;
std::vector<rt::Thread> gpu_ths;
bool done = false;

void tcp_server_fn(rt::TcpConn *conn) {
  while (true) {
    std::spanstream ss{std::span<char>()};
    cereal::BinaryInputArchive ia(ss);

    uint64_t size;
    BUG_ON(conn->ReadFull(&size, sizeof(size)) <= 0);
    auto buf = std::make_unique_for_overwrite<char[]>(size);

    BUG_ON(conn->ReadFull(buf.get(), size) <= 0);
    ss.span({buf.get(), size});

    Image image;
    ia >> image;
    ss.seekg(0);

    {
      rt::MutexGuard g(&mutex);
      images.emplace(std::move(image));
      condvar.Signal();
    }

    bool ack;
    BUG_ON(conn->WriteFull(&ack, sizeof(ack)) < 0);
  }
}

rt::TcpConn *start_tcp_server() {
  BUG_ON(get_cfg_ip() != kBaselineGPUIP);
  netaddr laddr{.ip = 0, .port = kBaselineGPUPort};
  auto *tcp_queue = rt::TcpQueue::Listen(laddr, kTCPListenBackLog);
  BUG_ON(!tcp_queue);
  auto *main_conn = tcp_queue->Accept();
  BUG_ON(!main_conn);

  rt::Spawn([tcp_queue] {
    rt::TcpConn *conn;
    while ((conn = tcp_queue->Accept())) {
      BUG_ON(!conn);
      rt::Spawn([conn] { tcp_server_fn(conn); });
    }
  });

  return main_conn;
}

void gpu_fn() {
  while (true) {
    Image image;

    {
      rt::MutexGuard g(&mutex);
      while (images.empty()) {
        if (unlikely(done)) {
	  return;
        }
        condvar.Wait(&mutex);
      }
      image = std::move(images.front());
      images.pop();
    }

    MockGPU<Image>::process(std::move(image));
  }
}

void start_gpus() {
  for (uint32_t i = 0; i < kNumGPUs; i++) {
    gpu_ths.emplace_back([] { gpu_fn(); });
  }
}

void wait_for_exit(rt::TcpConn *main_conn) {
  bool exit;
  BUG_ON(main_conn->ReadFull(&exit, sizeof(exit)) <= 0);
  {
    rt::MutexGuard g(&mutex);
    done = true;
    condvar.SignalAll();
  }
  for (auto &th : gpu_ths) {
    th.Join();
  }

  bool ack;
  BUG_ON(main_conn->WriteFull(&ack, sizeof(ack)) < 0);
}

void do_work() {
  auto *main_conn = start_tcp_server();
  start_gpus();
  wait_for_exit(main_conn);
}

int main(int argc, char **argv) {
  int ret;

  if (argc < 2) {
    std::cerr << "usage: [cfg_file]" << std::endl;
    return -EINVAL;
  }

  ret = rt::RuntimeInit(argv[1], [] { do_work(); });
  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }

  return 0;
}
