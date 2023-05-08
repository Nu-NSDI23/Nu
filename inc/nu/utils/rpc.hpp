#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <queue>
#include <span>
#include <vector>
#include <climits>

#include <net.h>
#include <sync.h>
#include <thread.h>

#include "nu/commons.hpp"
#include "nu/utils/counter.hpp"

namespace nu {

// RPCReturnBuffer manages a return data buffer and its lifetime.
class RPCReturnBuffer {
 public:
  RPCReturnBuffer() {}
  RPCReturnBuffer(std::span<const std::byte> buf,
                  std::move_only_function<void()> deleter_fn = {})
      : buf_(buf), deleter_fn_(std::move(deleter_fn)) {}
  ~RPCReturnBuffer() {
    if (deleter_fn_) deleter_fn_();
  }

  // disable copy
  RPCReturnBuffer(const RPCReturnBuffer &) = delete;
  RPCReturnBuffer &operator=(const RPCReturnBuffer &) = delete;

  // support move
  RPCReturnBuffer(RPCReturnBuffer &&rbuf)
      : buf_(rbuf.buf_), deleter_fn_(std::move(rbuf.deleter_fn_)) {
    rbuf.buf_ = std::span<const std::byte>();
  }
  RPCReturnBuffer &operator=(RPCReturnBuffer &&rbuf) {
    if (deleter_fn_) deleter_fn_();
    buf_ = rbuf.buf_;
    deleter_fn_ = std::move(rbuf.deleter_fn_);
    rbuf.buf_ = std::span<const std::byte>();
    return *this;
  }

  explicit operator bool() const { return !buf_.empty(); }

  // replaces the return data buffer.
  void Reset(std::span<const std::byte> buf = {},
             std::move_only_function<void()> deleter_fn = nullptr) {
    if (deleter_fn_) deleter_fn_();
    buf_ = buf;
    deleter_fn_ = std::move(deleter_fn);
  }

  // Gets the return the immutable data buffer.
  std::span<const std::byte> get_buf() const { return buf_; }

  // Gets the return the mutable data buffer.
  std::span<std::byte> get_mut_buf() const {
    return std::span(const_cast<std::byte *>(buf_.data()), buf_.size());
  }

 private:
  std::span<const std::byte> buf_;
  std::move_only_function<void()> deleter_fn_;
};

enum RPCReturnCode { kErrWrongClient = -2, kErrTimeout = -1, kOk = 0 };

class RPCReturner {
 public:
  RPCReturner() {}
  RPCReturner(void *rpc_server, std::size_t completion_data);
  void Return(RPCReturnCode rc, std::span<const std::byte> buf,
              std::move_only_function<void()> deleter_fn = nullptr);
  void Return(RPCReturnCode rc);

 private:
  void *rpc_server_;
  std::size_t completion_data_;
};

// A function handler for each RPC request, invoked concurrently.
using RPCHandler = std::move_only_function<void(std::span<std::byte> args,
                                                RPCReturner *rpc_returner)>;
// A callback for each RPC request, invoked when the response data is ready.
using RPCCallback = std::move_only_function<void(ssize_t len, rt::TcpConn *c)>;

namespace rpc_internal {

class RPCServerWorker;

// RPCCompletion manages the completion of an inflight request.
class RPCCompletion {
 public:
  RPCCompletion(RPCReturnBuffer *return_buf)
      : return_buf_(return_buf), poll_(!preempt_enabled()) {
    if (!poll_) {
      w_.Arm();
    }
  }
  RPCCompletion(RPCCallback &&callback)
      : callback_(std::move(callback)), poll_(!preempt_enabled()) {
    w_.Arm();
  }
  ~RPCCompletion() {}

  // Complete the request by invoking the callback and waking up the blocking
  // thread.
  void Done(ssize_t len, rt::TcpConn *c);

  RPCReturnCode get_return_code() const {
    Poll();
    return rc_;
  }

 private:
  void Poll() const;

  RPCReturnCode rc_;
  RPCReturnBuffer *return_buf_;
  RPCCallback callback_;
  rt::ThreadWaker w_;
  bool poll_;
};

// RPCFlow encapsulates one of the TCP connections used by an RPCClient.
class RPCFlow {
 public:
  constexpr static bool kEnableAdaptiveBatching = true;
  constexpr static uint64_t kReqBatchSize = 4;
  constexpr static uint64_t kBatchTimeoutUs = 5;

  RPCFlow(std::unique_ptr<rt::TcpConn> c)
      : close_(false),
        c_(std::move(c)),
        sent_count_(0),
        recv_count_(0),
        credits_(std::numeric_limits<decltype(credits_)>::max()) {}
  ~RPCFlow();

  // A factory to create new flows with CPU affinity.
  static std::unique_ptr<RPCFlow> New(unsigned int cpu_affinity, netaddr raddr);

  // Make an RPC call over this flow.
  void Call(std::span<const std::byte> src, RPCCompletion *c);

  // Disable move and copy.
  RPCFlow(const RPCFlow &) = delete;
  RPCFlow &operator=(const RPCFlow &) = delete;

 private:
  // State for managing inflight requests.
  struct req_ctx {
    std::span<const std::byte> payload;
    RPCCompletion *completion;
  };

  // Internal worker threads for sending and receiving.
  void SendWorker();
  void ReceiveWorker();
  bool EnoughBatching();

  rt::Thread sender_, receiver_;
  rt::Spin lock_;
  bool close_;
  rt::ThreadWaker wake_sender_;
  std::unique_ptr<rt::TcpConn> c_;
  unsigned int sent_count_;
  unsigned int recv_count_;
  unsigned int credits_;
  std::queue<req_ctx> reqs_;
  uint64_t last_sent_us_;
};

}  // namespace rpc_internal

class RPCClient {
 public:
  ~RPCClient(){};

  // Creates an RPC Client and establishes the underlying TCP connections.
  static std::unique_ptr<RPCClient> Dial(netaddr raddr);

  // Calls an RPC method, the RPC layer allocates a return buffer and stores
  // response into it.
  RPCReturnCode Call(std::span<const std::byte> args, RPCReturnBuffer *buf);

  // Calls an RPC method, the RPC layer invokes the callback when the response
  // is ready on the TCP connection.
  RPCReturnCode Call(std::span<const std::byte> args, RPCCallback &&callback);

  netaddr GetAddr() { return raddr_; }

  // disable move and copy.
  RPCClient(const RPCClient &) = delete;
  RPCClient &operator=(const RPCClient &) = delete;

 private:
  using RPCCompletion = rpc_internal::RPCCompletion;
  using RPCFlow = rpc_internal::RPCFlow;

  RPCClient(std::vector<std::unique_ptr<RPCFlow>> flows, netaddr raddr)
      : flows_(std::move(flows)), raddr_(raddr) {}

  // an array of per-kthread RPC flows.
  std::vector<std::unique_ptr<RPCFlow>> flows_;
  netaddr raddr_;
};

// RPCServerListener initializes and runs the RPC server.
class RPCServerListener {
 public:
  RPCServerListener(uint16_t port, RPCHandler &&handler);
  ~RPCServerListener();
  void dec_ref_cnt() { counter_.dec(); }

 private:
  RPCHandler handler_;
  std::unique_ptr<rt::TcpQueue> q_;
  rt::Thread listener_;
  std::vector<std::unique_ptr<rpc_internal::RPCServerWorker>> workers_;
  Counter counter_;
};

}  // namespace nu

#include "nu/impl/rpc.ipp"
